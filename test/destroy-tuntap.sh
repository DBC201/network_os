#!/usr/bin/env bash
set -euo pipefail

STATE_FILE="/var/run/tuntap-ifaces-${USER}.list"
OVERRIDE_MODE=""   # optional: --mode tap|tun when names are given

usage() {
  cat <<'USAGE'
Usage:
  destroy-tuntap.sh [--mode tap|tun] IFACE [IFACE ...]
  destroy-tuntap.sh --all
  destroy-tuntap.sh --state FILE [--all | IFACE ...]
Options:
  --mode tap|tun       Use this if the state file lacks mode for given IFACEs.
Examples:
  sudo ./destroy-tuntap.sh tap0 tap1
  sudo ./destroy-tuntap.sh --mode tun tun0
  sudo ./destroy-tuntap.sh --all
Notes:
  - Requires root (sudo).
  - Reads / updates /var/run/tuntap-ifaces-$USER.list (format: "<name> <mode>").
  - For backward-compat lines with only "<name>", you must pass --mode or it will try both.
USAGE
}

require_root() {
  if [[ $EUID -ne 0 ]]; then
    echo "ERROR: Please run as root (sudo)." >&2
    exit 1
  fi
}

iface_exists() {
  ip link show dev "$1" &>/null
}

delete_iface() {
  local ifc="$1"
  local mode_hint="${2:-}"

  # If we have a mode, delete with that. If not, try both.
  if [[ -n "$mode_hint" ]]; then
    if ip tuntap del dev "$ifc" mode "$mode_hint" 2>/dev/null; then
      echo "[ok]   deleted $ifc (mode=$mode_hint)"
      return 0
    fi
  else
    if ip tuntap del dev "$ifc" mode tap 2>/dev/null; then
      echo "[ok]   deleted $ifc (mode=tap)"
      return 0
    fi
    if ip tuntap del dev "$ifc" mode tun 2>/dev/null; then
      echo "[ok]   deleted $ifc (mode=tun)"
      return 0
    fi
  fi

  echo "[skip] could not delete $ifc (not found or wrong mode)"
  return 0
}

# -------- main --------
require_root
modprobe tun 2>/dev/null || true

if [[ $# -eq 0 ]]; then
  usage; exit 1
fi

ALT_STATE="$STATE_FILE"
DO_ALL=0
declare -a NAMES=()

while [[ $# -gt 0 ]]; do
  case "$1" in
    --all) DO_ALL=1; shift ;;
    --state)
      [[ $# -ge 2 ]] || { usage; exit 1; }
      ALT_STATE="$2"; shift 2
      ;;
    --mode)
      [[ $# -ge 2 ]] || { usage; exit 1; }
      OVERRIDE_MODE="$2"; shift 2
      [[ "$OVERRIDE_MODE" == "tap" || "$OVERRIDE_MODE" == "tun" ]] || { echo "ERROR: --mode must be tap or tun" >&2; exit 1; }
      ;;
    -*)
      usage; exit 1
      ;;
    *)
      NAMES+=("$1"); shift
      ;;
  esac
done

declare -a TO_DELETE=()    # entries like "name mode"

if [[ $DO_ALL -eq 1 ]]; then
  if [[ -f "$ALT_STATE" ]]; then
    # Read lines as: name [mode]
    while read -r nm md rest; do
      [[ -z "$nm" ]] && continue
      if [[ -n "$md" ]]; then
        TO_DELETE+=("$nm $md")
      else
        TO_DELETE+=("$nm ${OVERRIDE_MODE:-}")   # may be empty; delete_iface() will try both
      fi
    done < "$ALT_STATE"
  else
    echo "[info] No state file $ALT_STATE; nothing to do."
    exit 0
  fi
else
  if [[ ${#NAMES[@]} -eq 0 ]]; then usage; exit 1; fi

  # Build map from state to get modes if present
  declare -A STATE_MODE=()
  if [[ -f "$ALT_STATE" ]]; then
    while read -r nm md rest; do
      [[ -z "$nm" ]] && continue
      [[ -n "$md" ]] && STATE_MODE["$nm"]="$md"
    done < "$ALT_STATE"
  fi

  for nm in "${NAMES[@]}"; do
    md="${STATE_MODE[$nm]:-${OVERRIDE_MODE:-}}"
    TO_DELETE+=("$nm $md")
  done
fi

# Delete
for entry in "${TO_DELETE[@]}"; do
  nm="${entry%% *}"
  md="${entry#* }"
  [[ "$nm" == "$md" ]] && md=""   # handle case where entry lacked a mode
  delete_iface "$nm" "$md"
done

# Update state: remove deleted names
if [[ -f "$ALT_STATE" ]]; then
  TMP=$(mktemp)
  awk -v dels="$(printf "%s\n" "${TO_DELETE[@]}" | awk '{print $1}' | sort -u | tr '\n' ' ')" '
    BEGIN {
      n=split(dels, a, " ");
      for (i=1;i<=n;i++) if (length(a[i])) D[a[i]]=1
    }
    {
      nm=$1
      if (!(nm in D)) print $0
    }
  ' "$ALT_STATE" > "$TMP" || true
  install -m 644 "$TMP" "$ALT_STATE"
  rm -f "$TMP"
fi

echo "[info] Done."
