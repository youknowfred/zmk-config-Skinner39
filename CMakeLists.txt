# Repo-root Zephyr-module CMake (mounted via zephyr/module.yml `build.cmake: .`,
# which rides into every build through build-user-config.yml's ZMK_EXTRA_MODULES).
# Keep this file a thin index; everything it pulls in must be config-gated so the
# non-Glance targets (RIGHT, settings_reset, bond-clear) compile nothing extra.

# Vendored raw-HID transport module (upstream zzeneg/zmk-raw-hid @ e25e325a +
# the RAW_HID_USB / RAW_HID_BLE transport split — see vendor/zmk-raw-hid/VENDORED.md).
# Inert unless CONFIG_RAW_HID=y (its own guard).
add_subdirectory(vendor/zmk-raw-hid)

# Keysmith status-report channel (TB-7): device->host layer/battery frames on
# the raw-HID link. Inert unless CONFIG_KEYSMITH_STATUS_REPORTS=y (its guard).
add_subdirectory(src)
