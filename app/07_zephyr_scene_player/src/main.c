/* SPDX-License-Identifier: Apache-2.0 */
/* Example 07: six-scene animation player on Heltec WiFi LoRa 32 V2. */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/display.h>
#include <zephyr/display/cfb.h>
#include <zephyr/sys/printk.h>
#include <errno.h>
#include "app_events.h"

#define I2C_NODE DT_NODELABEL(i2c0)
#define OLED_NODE DT_CHOSEN(zephyr_display)

#if !DT_HAS_CHOSEN(zephyr_display)
#error "Select the ssd1306_128x64 shield to provide zephyr,display"
#endif
#if !DT_NODE_HAS_PROP(OLED_NODE, zephyr_deferred_init)
#error "OLED must be deferred in app.overlay so I2C can be recovered first"
#endif

static const struct device *const i2c_dev = DEVICE_DT_GET(I2C_NODE);
static const struct device *const oled_dev = DEVICE_DT_GET(OLED_NODE);

/* Input = producer; display = consumer. Both share one statically allocated queue. */
K_MSGQ_DEFINE(app_event_queue, sizeof(struct app_event), 16, 4);

#define INPUT_STACK_SIZE 2048
#define DISPLAY_STACK_SIZE 3072
#define INPUT_PRIORITY 5
#define DISPLAY_PRIORITY 5

K_THREAD_STACK_DEFINE(input_stack, INPUT_STACK_SIZE);
K_THREAD_STACK_DEFINE(display_stack, DISPLAY_STACK_SIZE);
static struct k_thread input_thread_data;
static struct k_thread display_thread_data;

static int init_oled(void)
{
    if (!device_is_ready(i2c_dev)) { return -ENODEV; }

    /* Crucial for the Heltec V2: recovery must precede deferred SSD1306 init. */
    printk("OLED: recovering I2C bus...\n");
    int rc = i2c_recover_bus(i2c_dev);
    if (rc < 0) { return rc; }

    rc = device_init(oled_dev);
    if (rc < 0 || !device_is_ready(oled_dev)) {
        return rc < 0 ? rc : -ENODEV;
    }
    rc = display_set_pixel_format(oled_dev, PIXEL_FORMAT_MONO01);
    if (rc < 0) { return rc; }
    rc = cfb_framebuffer_init(oled_dev);
    if (rc < 0) { return rc; }
    rc = cfb_framebuffer_clear(oled_dev, false);
    if (rc < 0) { return rc; }
    rc = display_blanking_off(oled_dev);
    if (rc < 0) { return rc; }
    printk("OLED: ready\n");
    return 0;
}

int main(void)
{
    printk("\n=================================\n");
    printk(" Example 07: OLED Scene Player\n");
    printk("=================================\n");

    int rc = init_oled();
    if (rc < 0) {
        printk("ERROR: OLED initialization: %d\n", rc);
        return rc;
    }
    rc = input_thread_init();
    if (rc < 0) {
        printk("ERROR: joystick initialization: %d\n", rc);
        return rc;
    }

    /* Display starts first and renders the help screen. */
    k_thread_create(&display_thread_data, display_stack,
                    K_THREAD_STACK_SIZEOF(display_stack), display_thread_entry,
                    NULL, NULL, NULL, DISPLAY_PRIORITY, 0, K_NO_WAIT);
    k_thread_create(&input_thread_data, input_stack,
                    K_THREAD_STACK_SIZEOF(input_stack), input_thread_entry,
                    NULL, NULL, NULL, INPUT_PRIORITY, 0, K_NO_WAIT);

    printk("APP: two threads running; PRG=open/back, SW=play/pause\n");
    return 0;
}
