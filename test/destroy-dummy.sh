#!/usr/bin/env bash
set -euo pipefail

STATE_FILE="/var/run/dummy-ifaces-${USER}.list"

usage() {
    cat <<'USAGE'
Usage: destroy-dummy.sh [IFACE ...]
       destroy-dummy.sh --all        # delete all recorded in /var/run/dummy-ifaces-$USER.list
       destroy-dummy.sh --state FILE # use alternate state file

Examples:
    sudo ./destroy-dummy.sh ethd0 ethd1
    sudo ./destroy-dummy.sh --all
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

delete_iface() {
    local ifc="$1"
    if iface_exists "$ifc"; then
        ip link delete "$ifc" type dummy &>/dev/null || ip link delete "$ifc" &>/dev/null
        echo "[ok] deleted $ifc"
    else
        echo "[skip] $ifc does not exist"
    fi
}

# -------- main --------

require_root

if [[ $# -eq 0 ]]; then
    usage
    exit 1
fi

declare -a IFACES=()
ALT_STATE="$STATE_FILE"

case "${1:-}" in
    --all)
        if [[ -f "$ALT_STATE" ]]; then
            mapfile -t IFACES < <(grep -v '^\s*$' "$ALT_STATE" || true)
        else
            echo "Nothing to delete: state file not found ($ALT_STATE)"
            exit 0
        fi
        ;;
    --state)
        [[ $# -ge 2 ]] || { usage; exit 1; }
        ALT_STATE="$2"
        shift 2
        if [[ $# -gt 0 ]]; then
            IFACES=("$@")
        elif [[ -f "$ALT_STATE" ]]; then
            mapfile -t IFACES < <(grep -v '^\s*$' "$ALT_STATE" || true)
        else
            echo "ERROR: state file not found: $ALT_STATE" >&2
            exit 1
        fi
        ;;
    *)
        IFACES=("$@")
        ;;
esac

if [[ ${#IFACES[@]} -eq 0 ]]; then
    echo "Nothing to delete."
    exit 0
fi

for ifc in "${IFACES[@]}"; do
    delete_iface "$ifc"
done

# prune from state file if using the default/alt file and it exists
if [[ -f "$ALT_STATE" ]]; then
    TMP=$(mktemp)
    grep -vxF -f <(printf "%s\n" "${IFACES[@]}") "$ALT_STATE" >"$TMP" || true
    install -m 644 "$TMP" "$ALT_STATE"
    rm -f "$TMP"
fi
