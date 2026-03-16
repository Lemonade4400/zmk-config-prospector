if(CONFIG_GPIO_BACKLIGHT_TEST)
  zephyr_library()
  zephyr_library_sources(src/gpio_backlight_test.c)
endif()
