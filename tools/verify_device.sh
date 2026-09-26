#!/usr/bin/env bash
#
# ESPX device bring-up / verification helper.
#
#   tools/verify_device.sh info      - chip + flash size + MAC
#   tools/verify_device.sh flash     - build, flash, open monitor
#   tools/verify_device.sh flashonly - build + flash (no monitor)
#   tools/verify_device.sh erase     - erase entire flash (factory reset)
#   tools/verify_device.sh monitor   - open serial monitor
#   tools/verify_device.sh listen    - 30s log capture (no interactive monitor)
#
# Port override:  ESPX_PORT=/dev/cu.usbserial-XXXX tools/verify_device.sh flash
#
set -euo pipefail

PORT="${ESPX_PORT:-/dev/cu.usbserial-A5069RR4}"
BAUD="${ESPX_BAUD:-460800}"
MONBAUD="${ESPX_MONBAUD:-115200}"

cd "$(dirname "$0")/.."

need_idf() {
    if ! command -v idf.py >/dev/null 2>&1; then
        echo "ESP-IDF environment not active." >&2
        echo "Run:  . <idf-path>/export.sh" >&2
        exit 1
    fi
}

case "${1:-info}" in
    info)
        need_idf
        python3 -m esptool --port "$PORT" --baud "$BAUD" flash_id
        ;;

    build)
        need_idf
        idf.py build
        ;;

    flash)
        need_idf
        idf.py -p "$PORT" -b "$BAUD" flash monitor
        ;;

    flashonly)
        need_idf
        idf.py -p "$PORT" -b "$BAUD" flash
        ;;

    erase)
        need_idf
        echo "This erases the whole flash (NVS, provisioning, devices)."
        read -r -p "Continue? [y/N] " a
        [[ "$a" == "y" || "$a" == "Y" ]] || exit 1
        idf.py -p "$PORT" erase-flash
        ;;

    monitor)
        need_idf
        idf.py -p "$PORT" monitor
        ;;

    listen)
        need_idf
        SECS="${2:-30}"
        OUT="dist/boot-$(date +%Y%m%d-%H%M%S).log"
        mkdir -p dist
        echo "Capturing ${SECS}s of boot log to $OUT ..."
        # Reset the chip by toggling DTR/RTS, then read the port.
        python3 - "$PORT" "$MONBAUD" "$SECS" "$OUT" <<'PY'
import sys, time, serial
port, baud, secs, out = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
s = serial.Serial(port, baud, timeout=0.2)
s.setDTR(False); s.setRTS(True);  time.sleep(0.15)   # EN low  -> reset
s.setRTS(False);                  time.sleep(0.05)   # EN high -> run
deadline = time.time() + secs
data = bytearray()
with open(out, "wb") as f:
    while time.time() < deadline:
        chunk = s.read(4096)
        if chunk:
            f.write(chunk); f.flush(); data += chunk
s.close()
sys.stderr.write(f"captured {len(data)} bytes\n")
PY
        echo "--- log ($OUT) ---"
        sed 's/\r$//' "$OUT" | tail -80
        ;;

    *)
        sed -n '3,14p' "$0" | sed 's/^# \{0,1\}//'
        exit 1
        ;;
esac
