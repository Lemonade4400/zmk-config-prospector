if(CONFIG_DISPLAY_HW_TEST)
  zephyr_library_named(display_hw_test)
  zephyr_library_sources(src/display_test.c)
endif()
