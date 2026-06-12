#!/bin/bash
# Strings-probe the P0-6 BLE-first Glance artifacts BEFORE any flash.
# (The de-block-first rule: strings split across 512-byte UF2 block payloads —
#  this caught two bad logging builds on 2026-06-11. Never `strings` a raw UF2.)
#
# Usage:
#   gh run download <run-id> -R youknowfred/zmk-config-Skinner39 -D /tmp/glance-ble
#   tools/glance-ble-probe.sh /tmp/glance-ble
#
# The two genuine discriminators (validated against a real pre-raw-HID image):
#   desc  = raw-HID report-descriptor bytes 06 60 FF 09 61 A1 01 (usage-page
#           0xFF60, usage 0x61, collection). In rodata iff a transport TU is
#           compiled in (the descriptor lives in raw_hid.h, included only by
#           usb_hid.c / hog.c — events.c does not include it).
#   HID_1 = the second USB HID instance's device name (Zephyr device table +
#           usb_hid.c's device_get_binding target). Present iff the USB
#           transport shape (USB_HID_DEVICE_COUNT=2) is in the image.
# So: desc present + HID_1 absent  ⟹  the descriptor's only consumer is the
# GATT path — BLE transport in, USB transport out.
#
# NOTE: "HID Over GATT Send Work" is NOT a usable marker — ZMK core's own
# app/src/hog.c uses the identical work-queue name (zzeneg copied it), so it
# appears in every BLE build. (Caught by probing a pre-raw-HID image.)
#
# Widget-in vs nowidget is NOT strings-distinguishable (the widget adds no
# rodata literal of its own) — trust the artifact NAME for that axis; the two
# images' sizes will differ slightly.
set -euo pipefail

dir="${1:?usage: glance-ble-probe.sh <downloaded-artifacts-dir>}"
here="$(cd "$(dirname "$0")" && pwd)"
fail=0

probe() { # probe <uf2> <want: ble|none>
    local uf2="$1" want="$2" payload
    payload="$(mktemp)"
    python3 "$here/uf2_payload.py" "$uf2" > "$payload" 2>/dev/null

    local desc=absent hid1=absent hid0=absent
    if python3 - "$payload" <<'EOF'
import sys
sys.exit(0 if bytes.fromhex("0660FF0961A101") in open(sys.argv[1],"rb").read() else 1)
EOF
    then desc=present; fi
    strings "$payload" | grep -qx "HID_1" && hid1=present
    strings "$payload" | grep -qx "HID_0" && hid0=present
    rm -f "$payload"

    local verdict=PASS
    case "$want" in
        ble)  [ "$desc" = present ] && [ "$hid1" = absent ] || verdict=FAIL ;;
        none) [ "$desc" = absent ] && [ "$hid1" = absent ] || verdict=FAIL ;;
    esac
    [ "$verdict" = FAIL ] && fail=1
    printf '%s  %s\n    desc-0xFF60=%s  HID_1=%s  HID_0=%s  (expected: %s)\n' \
        "$verdict" "$(basename "$uf2")" "$desc" "$hid1" "$hid0" "$want"
}

found_any=0
while IFS= read -r -d '' uf2; do
    found_any=1
    case "$uf2" in
        *GLANCE_BLE*) probe "$uf2" ble ;;
        *settings_reset*|*CLEAR_BONDS*|*USB_LOGGING*|*right*) ;; # out of scope
        *keysmith_glance*left*|*left*keysmith_glance*) probe "$uf2" none ;;
    esac
done < <(find "$dir" -name '*.uf2' -print0)

if [ "$found_any" = 0 ]; then echo "no .uf2 under $dir"; exit 2; fi
if [ "$fail" = 1 ]; then echo "PROBE FAILED — do not flash"; exit 1; fi
echo "all probes pass — artifacts match their declared transports"
