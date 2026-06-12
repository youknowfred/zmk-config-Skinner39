if (CONFIG_RAW_HID)
  zephyr_library()

  zephyr_library_sources(src/events.c)

  if (CONFIG_RAW_HID_USB)
    zephyr_library_sources(src/usb_hid.c)
  endif()

  if (CONFIG_RAW_HID_BLE)
    zephyr_library_sources(src/hog.c)
  endif()

  zephyr_include_directories(include)
  zephyr_include_directories(${APPLICATION_SOURCE_DIR}/include)
endif()
