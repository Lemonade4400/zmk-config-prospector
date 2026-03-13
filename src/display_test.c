/*
 * Display Hardware Diagnostic Test
 * Tests each display pin individually to identify solder issues.
 * Results are logged via USB CDC ACM serial console (printk).
 *
 * Pin mapping (Seeeduino XIAO BLE -> WaveShare 1.69" ST7789V2):
 *   SPI SCK:   P1.13 (xiao D8)
 *   SPI MOSI:  P1.15 (xiao D10)
 *   SPI CS:    P1.14 (xiao D9) - active low
 *   DC:        P1.12 (xiao D7) - active low (0=cmd, 1=data)
 *   RST:       P0.03 (xiao D3) - active low
 *   Backlight: P1.11 (xiao D6) - inverted (low=on for this HW)
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>

/* GPIO port devices */
#define GPIO0_DEV DEVICE_DT_GET(DT_NODELABEL(gpio0))
#define GPIO1_DEV DEVICE_DT_GET(DT_NODELABEL(gpio1))

/* Pin definitions (nRF52840 GPIO numbers) */
#define PIN_BL      11  /* P1.11 - Backlight */
#define PIN_SCK     13  /* P1.13 - SPI Clock */
#define PIN_MOSI    15  /* P1.15 - SPI MOSI */
#define PIN_CS      14  /* P1.14 - SPI CS */
#define PIN_DC      12  /* P1.12 - Data/Command */
#define PIN_RST      3  /* P0.03 - Reset */

/* ST7789V commands */
#define ST7789_SWRESET  0x01
#define ST7789_SLPOUT   0x11
#define ST7789_NORON    0x13
#define ST7789_INVON    0x21
#define ST7789_DISPON   0x29
#define ST7789_CASET    0x2A
#define ST7789_RASET    0x2B
#define ST7789_RAMWR    0x2C
#define ST7789_MADCTL   0x36
#define ST7789_COLMOD   0x3A
#define ST7789_RDDID    0x04  /* Read Display ID */

/* Helpers */
static const struct device *gpio0;
static const struct device *gpio1;

static void banner(const char *msg) {
    printk("\n========================================\n");
    printk("  %s\n", msg);
    printk("========================================\n");
}

static int configure_pin_output(const struct device *port, int pin, const char *name) {
    int ret = gpio_pin_configure(port, pin, GPIO_OUTPUT);
    if (ret) {
        printk("[FAIL] Cannot configure %s (port %s pin %d): err %d\n",
               name, port->name, pin, ret);
    } else {
        printk("[ OK ] Configured %s (port %s pin %d) as output\n",
               name, port->name, pin);
    }
    return ret;
}

/* ---- Bit-bang SPI for raw diagnostics ---- */

static void bb_spi_byte(uint8_t byte) {
    for (int i = 7; i >= 0; i--) {
        /* Set MOSI */
        gpio_pin_set(gpio1, PIN_MOSI, (byte >> i) & 1);
        k_busy_wait(1);
        /* Clock high */
        gpio_pin_set(gpio1, PIN_SCK, 1);
        k_busy_wait(1);
        /* Clock low */
        gpio_pin_set(gpio1, PIN_SCK, 0);
        k_busy_wait(1);
    }
}

static void send_cmd(uint8_t cmd) {
    gpio_pin_set(gpio1, PIN_DC, 0);   /* Command mode */
    gpio_pin_set(gpio1, PIN_CS, 0);   /* CS active */
    k_busy_wait(1);
    bb_spi_byte(cmd);
    gpio_pin_set(gpio1, PIN_CS, 1);   /* CS inactive */
    k_busy_wait(1);
}

static void send_data(uint8_t data) {
    gpio_pin_set(gpio1, PIN_DC, 1);   /* Data mode */
    gpio_pin_set(gpio1, PIN_CS, 0);   /* CS active */
    k_busy_wait(1);
    bb_spi_byte(data);
    gpio_pin_set(gpio1, PIN_CS, 1);   /* CS inactive */
    k_busy_wait(1);
}

static void send_data_buf(const uint8_t *buf, size_t len) {
    gpio_pin_set(gpio1, PIN_DC, 1);   /* Data mode */
    gpio_pin_set(gpio1, PIN_CS, 0);   /* CS active */
    k_busy_wait(1);
    for (size_t i = 0; i < len; i++) {
        bb_spi_byte(buf[i]);
    }
    gpio_pin_set(gpio1, PIN_CS, 1);   /* CS inactive */
    k_busy_wait(1);
}

/* ---- Test functions ---- */

static void test_backlight(void) {
    banner("TEST 1: Backlight (P1.11)");

    /*
     * The backlight on this HW is ACTIVE LOW (nordic,invert in PWM config).
     * GPIO_ACTIVE_HIGH is defined in the DTS GPIO node, but the actual
     * hardware has an inverter or P-FET, so:
     *   GPIO LOW  = backlight ON
     *   GPIO HIGH = backlight OFF
     *
     * We test BOTH polarities so you can see which one lights up.
     */

    configure_pin_output(gpio1, PIN_BL, "BACKLIGHT");

    printk("\n[INFO] Setting backlight pin LOW (should be ON if inverted HW)...\n");
    gpio_pin_set_raw(gpio1, PIN_BL, 0);
    printk("[INFO] >>> Look at the display — is the backlight ON now? <<<\n");
    printk("[INFO] Waiting 3 seconds...\n");
    k_msleep(3000);

    printk("\n[INFO] Setting backlight pin HIGH (should be OFF if inverted HW)...\n");
    gpio_pin_set_raw(gpio1, PIN_BL, 1);
    printk("[INFO] >>> Look at the display — is the backlight OFF now? <<<\n");
    printk("[INFO] Waiting 3 seconds...\n");
    k_msleep(3000);

    printk("\n[INFO] Setting backlight pin LOW again (ON)...\n");
    gpio_pin_set_raw(gpio1, PIN_BL, 0);
    printk("[INFO] Waiting 2 seconds...\n");
    k_msleep(2000);

    printk("\n[INFO] Now TOGGLING backlight 5 times (1 sec on, 1 sec off)...\n");
    for (int i = 0; i < 5; i++) {
        gpio_pin_set_raw(gpio1, PIN_BL, 0);  /* ON */
        printk("  Blink %d: ON\n", i + 1);
        k_msleep(1000);
        gpio_pin_set_raw(gpio1, PIN_BL, 1);  /* OFF */
        printk("  Blink %d: OFF\n", i + 1);
        k_msleep(1000);
    }

    /* Leave backlight ON for subsequent tests */
    gpio_pin_set_raw(gpio1, PIN_BL, 0);
    printk("\n[RESULT] Backlight test complete. Did you see blinking?\n");
    printk("  YES = backlight solder joint OK\n");
    printk("  NO  = check solder on pin D6 (P1.11) and VCC/GND\n");
}

static void test_reset(void) {
    banner("TEST 2: Reset Pin (P0.03)");

    configure_pin_output(gpio0, PIN_RST, "RESET");

    printk("\n[INFO] Asserting RESET (pin LOW)...\n");
    gpio_pin_set_raw(gpio0, PIN_RST, 0);
    k_msleep(100);

    printk("[INFO] Releasing RESET (pin HIGH)...\n");
    gpio_pin_set_raw(gpio0, PIN_RST, 1);
    k_msleep(200);

    printk("[RESULT] Reset pin toggled. If HW is connected, display should have reset.\n");
    printk("  (No visual feedback expected yet — display needs SPI init after reset)\n");
}

static void test_spi_init_display(void) {
    banner("TEST 3: SPI Display Init (bit-bang)");

    printk("[INFO] Configuring SPI pins as GPIO outputs for bit-bang...\n");
    configure_pin_output(gpio1, PIN_SCK,  "SCK");
    configure_pin_output(gpio1, PIN_MOSI, "MOSI");
    configure_pin_output(gpio1, PIN_CS,   "CS");
    configure_pin_output(gpio1, PIN_DC,   "DC");

    /* Idle state */
    gpio_pin_set(gpio1, PIN_SCK, 0);
    gpio_pin_set(gpio1, PIN_CS, 1);   /* CS inactive (high) */
    gpio_pin_set(gpio1, PIN_DC, 1);   /* Data mode default */

    printk("\n[INFO] Sending Software Reset (0x01)...\n");
    send_cmd(ST7789_SWRESET);
    k_msleep(150);  /* Wait for reset */

    printk("[INFO] Sending Sleep Out (0x11)...\n");
    send_cmd(ST7789_SLPOUT);
    k_msleep(120);  /* Wait for sleep out */

    printk("[INFO] Setting color mode to RGB565 (0x3A, 0x05)...\n");
    send_cmd(ST7789_COLMOD);
    send_data(0x05);  /* RGB565 */
    k_msleep(10);

    printk("[INFO] Setting MADCTL (0x36, 0x00)...\n");
    send_cmd(ST7789_MADCTL);
    send_data(0x00);
    k_msleep(10);

    printk("[INFO] Sending Inversion On (0x21)...\n");
    send_cmd(ST7789_INVON);
    k_msleep(10);

    printk("[INFO] Sending Normal Display Mode On (0x13)...\n");
    send_cmd(ST7789_NORON);
    k_msleep(10);

    printk("[INFO] Sending Display ON (0x29)...\n");
    send_cmd(ST7789_DISPON);
    k_msleep(100);

    printk("[RESULT] Display init sequence sent via bit-bang SPI.\n");
    printk("  If the display is wired correctly, the screen should now be\n");
    printk("  active (might show random/noise content or solid color).\n");
}

static void test_fill_red(void) {
    banner("TEST 4: Fill Screen RED");

    printk("[INFO] Setting column address (0-239)...\n");
    send_cmd(ST7789_CASET);
    uint8_t ca[] = {0x00, 0x00, 0x00, 239};
    send_data_buf(ca, 4);

    printk("[INFO] Setting row address (20-299, with y-offset=20)...\n");
    send_cmd(ST7789_RASET);
    uint8_t ra[] = {0x00, 20, (280 + 20 - 1) >> 8, (280 + 20 - 1) & 0xFF};
    send_data_buf(ra, 4);

    printk("[INFO] Writing RED pixels (240x280 = 67200 pixels)...\n");
    send_cmd(ST7789_RAMWR);

    /* RED in RGB565 = 0xF800 (MSB first: 0xF8, 0x00) */
    /* But with LV_COLOR_16_SWAP, it's byte-swapped: 0x00, 0xF8 */
    /* For this raw test, we send native RGB565: 0xF800 */
    gpio_pin_set(gpio1, PIN_DC, 1);   /* Data mode */
    gpio_pin_set(gpio1, PIN_CS, 0);   /* CS active */
    k_busy_wait(1);

    int total_pixels = 240 * 280;
    for (int i = 0; i < total_pixels; i++) {
        bb_spi_byte(0xF8);  /* Red high byte */
        bb_spi_byte(0x00);  /* Red low byte */
        /* Print progress every ~10% */
        if (i % 6720 == 0) {
            printk("  Progress: %d%%\n", (i * 100) / total_pixels);
        }
    }

    gpio_pin_set(gpio1, PIN_CS, 1);   /* CS inactive */

    printk("\n[RESULT] RED fill complete.\n");
    printk("  If you see a RED screen = ALL pins working correctly!\n");
    printk("  If screen is still black:\n");
    printk("    - Backlight blinked in Test 1? If NO: check D6/VCC/GND solder\n");
    printk("    - Backlight blinked but no red: check D8(SCK), D10(MOSI),\n");
    printk("      D9(CS), D7(DC), D3(RST) solder joints\n");
}

static void test_fill_blue(void) {
    banner("TEST 5: Fill Screen BLUE (verify not stuck)");

    send_cmd(ST7789_CASET);
    uint8_t ca[] = {0x00, 0x00, 0x00, 239};
    send_data_buf(ca, 4);

    send_cmd(ST7789_RASET);
    uint8_t ra[] = {0x00, 20, (280 + 20 - 1) >> 8, (280 + 20 - 1) & 0xFF};
    send_data_buf(ra, 4);

    printk("[INFO] Writing BLUE pixels...\n");
    send_cmd(ST7789_RAMWR);

    gpio_pin_set(gpio1, PIN_DC, 1);
    gpio_pin_set(gpio1, PIN_CS, 0);
    k_busy_wait(1);

    /* BLUE in RGB565 = 0x001F */
    int total_pixels = 240 * 280;
    for (int i = 0; i < total_pixels; i++) {
        bb_spi_byte(0x00);
        bb_spi_byte(0x1F);
        if (i % 6720 == 0) {
            printk("  Progress: %d%%\n", (i * 100) / total_pixels);
        }
    }

    gpio_pin_set(gpio1, PIN_CS, 1);

    printk("\n[RESULT] BLUE fill complete.\n");
    printk("  RED then BLUE = SPI fully working, display is fine!\n");
    printk("  Only RED visible = display might be stuck (try power cycle)\n");
    printk("  Neither visible = SPI or control pins have solder issues\n");
}

/* ---- Main test entry via SYS_INIT ---- */

static void display_test_thread(void *p1, void *p2, void *p3) {
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    /* Wait for USB CDC ACM to be ready */
    printk("\n\n");
    banner("DISPLAY HARDWARE DIAGNOSTIC TEST");
    printk("Waiting 6 seconds for USB serial to be ready...\n");
    printk("(Connect serial terminal NOW if not already connected)\n");
    k_msleep(6000);

    banner("STARTING DIAGNOSTICS");
    printk("Pin mapping:\n");
    printk("  Backlight: P1.11 (D6)\n");
    printk("  SPI SCK:   P1.13 (D8)\n");
    printk("  SPI MOSI:  P1.15 (D10)\n");
    printk("  SPI CS:    P1.14 (D9)\n");
    printk("  DC:        P1.12 (D7)\n");
    printk("  RST:       P0.03 (D3)\n");
    printk("\n");

    /* Check GPIO devices */
    gpio0 = GPIO0_DEV;
    gpio1 = GPIO1_DEV;

    if (!device_is_ready(gpio0)) {
        printk("[FATAL] GPIO0 device not ready!\n");
        return;
    }
    if (!device_is_ready(gpio1)) {
        printk("[FATAL] GPIO1 device not ready!\n");
        return;
    }
    printk("[ OK ] GPIO0 and GPIO1 devices ready\n");

    /* Run tests sequentially */
    test_backlight();

    printk("\n--- Pausing 2 seconds before next test ---\n");
    k_msleep(2000);

    test_reset();

    printk("\n--- Pausing 1 second before SPI test ---\n");
    k_msleep(1000);

    test_spi_init_display();

    printk("\n--- Pausing 2 seconds — look at screen ---\n");
    k_msleep(2000);

    test_fill_red();

    printk("\n--- RED displayed for 5 seconds ---\n");
    k_msleep(5000);

    test_fill_blue();

    banner("ALL TESTS COMPLETE");
    printk("\nSummary:\n");
    printk("  Test 1 (Backlight): Did it blink? Check D6/VCC/GND\n");
    printk("  Test 2 (Reset):     No direct visual feedback\n");
    printk("  Test 3 (SPI Init):  Screen should wake up\n");
    printk("  Test 4 (Red Fill):  Screen should be RED\n");
    printk("  Test 5 (Blue Fill): Screen should be BLUE\n");
    printk("\nIf ALL tests pass but normal firmware doesn't show anything,\n");
    printk("the problem is in the Zephyr display driver config, not hardware.\n");
    printk("\nIf NO visual output at all, most likely a solder issue on the\n");
    printk("display flex cable connector or VCC/GND power rails.\n");
    printk("\nTest will now loop: alternating RED/BLUE every 3 seconds.\n");

    while (1) {
        test_fill_red();
        k_msleep(3000);
        test_fill_blue();
        k_msleep(3000);
    }
}

K_THREAD_STACK_DEFINE(disp_test_stack, 2048);
static struct k_thread disp_test_thread_data;

static int display_test_init(void) {
    k_thread_create(&disp_test_thread_data, disp_test_stack,
                     K_THREAD_STACK_SIZEOF(disp_test_stack),
                     display_test_thread,
                     NULL, NULL, NULL,
                     K_PRIO_PREEMPT(10), 0, K_NO_WAIT);
    return 0;
}

/* Run at APPLICATION priority 99 — after everything else */
SYS_INIT(display_test_init, APPLICATION, 99);
