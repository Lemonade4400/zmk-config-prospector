/*
 * GPIO Backlight Test
 *
 * Bypasses PWM entirely. Drives P1.11 as a raw GPIO output.
 * Toggles between HIGH and LOW every 2 seconds so we can see
 * which polarity (active-high or active-low) turns on the backlight.
 *
 * If the backlight blinks: pin P1.11 IS connected to the backlight.
 * If nothing happens: either wrong pin or hardware issue (cold solder joint).
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(gpio_bl_test, LOG_LEVEL_INF);

/* P1.11 = gpio1 pin 11 */
static const struct device *gpio1_dev = DEVICE_DT_GET(DT_NODELABEL(gpio1));

#define BACKLIGHT_PIN 11

static struct k_timer blink_timer;
static bool pin_state = false;

static void blink_cb(struct k_timer *timer) {
    pin_state = !pin_state;
    int val = pin_state ? 1 : 0;
    gpio_pin_set(gpio1_dev, BACKLIGHT_PIN, val);
    LOG_INF("GPIO P1.11 = %d (%s)", val, val ? "HIGH" : "LOW");
}

static int gpio_backlight_test_init(void) {
    LOG_INF("=== GPIO BACKLIGHT TEST ===");
    LOG_INF("Testing P1.11 as raw GPIO (no PWM)");

    if (!device_is_ready(gpio1_dev)) {
        LOG_ERR("GPIO1 device not ready!");
        return -ENODEV;
    }

    /* Configure P1.11 as output, start LOW (active-low = ON) */
    int ret = gpio_pin_configure(gpio1_dev, BACKLIGHT_PIN, GPIO_OUTPUT_LOW);
    if (ret < 0) {
        LOG_ERR("Failed to configure P1.11: %d", ret);
        return ret;
    }

    LOG_INF("P1.11 configured as GPIO output, initial state: LOW");
    LOG_INF("If backlight is active-low, it should be ON now");
    LOG_INF("Will toggle every 2 seconds to test both polarities");

    /* Start blinking after 3 seconds, toggle every 2 seconds */
    k_timer_init(&blink_timer, blink_cb, NULL);
    k_timer_start(&blink_timer, K_SECONDS(3), K_SECONDS(2));

    return 0;
}

/* Run at priority 49 = BEFORE backlight_init.c (priority 50) */
SYS_INIT(gpio_backlight_test_init, APPLICATION, 49);
