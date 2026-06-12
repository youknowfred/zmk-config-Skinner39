# Vendored module provenance

- **Upstream:** https://github.com/zzeneg/zmk-raw-hid @ `e25e325adc90fccb60cc08393b8c2b0ba6d647c7`
  (upstream HEAD, "pin zmk v0.3" — the exact revision west.yml pinned 2026-06-09 → 2026-06-12).
- **Vendored:** 2026-06-12, replacing the west.yml project entirely (P0-6 BLE-first pivot).
  Dropped from upstream: `.github/`, `build.yaml`, `config/west.yml`, `zephyr/module.yml`
  (we mount via the repo-root module), and `boards/shields/raw_hid_adapter/` (the
  nvhid adapter shield — keysmith doc 03 §7 gotcha #1: it redefines `nice_view: ls0xx@0`
  and re-chooses the display; pulling it = duplicate-node devicetree error).
- **Local delta vs upstream (the whole point):** `Kconfig` + `CMakeLists.txt` split the
  two transports — `RAW_HID_USB` (src/usb_hid.c, legacy hid_ops API, needs
  `USB_HID_DEVICE_COUNT=2`) and `RAW_HID_BLE` (src/hog.c, HID-over-GATT, zero USB
  delta). Both default y, preserving upstream behavior; the Skinner39 LEFT builds pass
  `-DCONFIG_RAW_HID=y -DCONFIG_RAW_HID_USB=n` because the legacy Zephyr USB stack on
  this board dies under the CDC+2×HID composite (boot crash with the widget, ~60 s
  wedge without it, plus the 2026-06-11 CDC-flood kill — see `build.yaml` comments and
  `docs/2026-06-10-split-ble-root-cause-and-fix.md` §5).
- `src/*.c` and `include/raw_hid/*.h` are byte-identical to upstream.
