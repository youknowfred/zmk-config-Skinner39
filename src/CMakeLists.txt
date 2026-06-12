# Keysmith firmware-side sources (config-gated; nothing compiles unless the
# target opts in via cmake-args — see build.yaml's GLANCE_BLE_STATUS images).
if(CONFIG_KEYSMITH_STATUS_REPORTS)
  zephyr_library_named(keysmith_status_reports)
  zephyr_library_sources(keysmith_status_report.c)
endif()
