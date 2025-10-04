#!/usr/bin/env bash
set -euo pipefail

STATE_FILE="/var/run/dummy-ifaces-${USER}.list"

usage() {
  cat <<'USAGE'
Usage:
  create-dummy.sh IFACE [IFACE ...]
  create-dummy.sh --prefix PREFIX --count N

Examples:
  sudo ./create-dummy.sh ethd0 ethd1 ethd2
  sudo ./create-dummy.sh --prefix ethd --count 4   # creates ethd0..ethd3
Notes:
  - Requires root (sudo).
  - Records created interfaces in /var/run/dummy-ifaces-$USER.list for teardown.
USAGE
}

require_root() {
  if [[ $EUID -ne 0 ]]; then
    echo "ERROR: Please run as root (sudo)." >&2
    exit 1
  fi
}

iface_exists() {
  ip link show dev "$1" &>/dev/null
}

create_iface() {
  local ifc="$1"
  if iface_exists "$ifc"; then
    echo "[skip] $ifc already exists"
  else
    ip link add "$ifc" type dummy
    echo "[ok]   created $ifc"
  fi
  ip link set "$ifc" up
  echo "[ok]   set $ifc up"
  
  # Enable promiscuous mode
  ip link set "$ifc" promisc on
  echo "[ok]   set $ifc promisc"
}

# -------- main --------
require_root

if [[ $# -eq 0 ]]; then
  usage; exit 1
fi

declare -a IFACES=()
if [[ "$1" == "--prefix" ]]; then
  [[ $# -eq 4 && "$3" == "--count" ]] || { usage; exit 1; }
  PREFIX="$2"
  COUNT="$4"
  [[ "$COUNT" =~ ^[0-9]+$ && "$COUNT" -gt 0 ]] || { echo "ERROR: --count must be > 0" >&2; exit 1; }
  for ((i=0; i<COUNT; i++)); do
    IFACES+=("${PREFIX}${i}")
  done
else
  IFACES=("$@")
fi

# ensure state file exists and is writable
install -m 644 /dev/null "$STATE_FILE" 2>/dev/null || true
touch "$STATE_FILE"

for ifc in "${IFACES[@]}"; do
  create_iface "$ifc"
done

# Record (append unique)
TMP=$(mktemp)
sort -u <(printf "%s\n" "${IFACES[@]}") "$STATE_FILE" >"$TMP"
install -m 644 "$TMP" "$STATE_FILE"
rm -f "$TMP"

echo
echo "[info] Tracked interfaces written to: $STATE_FILE"
