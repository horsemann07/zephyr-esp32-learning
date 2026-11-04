
/* ============================================================================
 * File        : main.c
 * Description : System initialization and static thread creation
 * Application : 06_emoji_notification_center
 * Board       : Heltec WiFi LoRa 32 V2 (ESP32)
 * Zephyr      : 4.4.0
 * ============================================================================
 *
 * Responsibilities:
 *   1. Recover the I2C bus before initializing the OLED driver.
 *   2. Initialize the deferred SSD1306 display device.
 *   3. Initialize the Character Framebuffer (CFB).
 *   4. Configure the joystick ADC channels and push buttons.
 *   5. Create input, display and notification threads with STATIC stacks.
 *
 * Thread communication:
 *   Input thread -----------+
 *                           +--> app_event_queue --> Display thread --> OLED
 *   Notification thread ----+
 *
 * The display thread is the only thread that draws or refreshes the OLED.
 *
 * UI behavior:
 *   At boot       : Help / joystick controls screen
 *   Onboard PRG   : Toggle help screen <-> emoji browser
 *   Joystick UP   : Previous category
 *   Joystick DOWN : Next category
 *   Joystick LEFT : Previous emoji
 *   Joystick RIGHT: Next emoji
 *   Joystick SW   : Select / animate current emoji
 * ============================================================================ */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/display.h>
#include <zephyr/display/cfb.h>
#include <zephyr/sys/printk.h>

#include <errno.h>

#include "app_events.h"

/* -------------------------------------------------------------------------- */
/* Devicetree nodes                                                           */
/* -------------------------------------------------------------------------- */

#define I2C_NODE  DT_NODELABEL(i2c0)
#define OLED_NODE DT_CHOSEN(zephyr_display)

#if !DT_NODE_EXISTS(I2C_NODE)
    #error "Missing i2c0 Devicetree node"
#endif

#if !DT_NODE_HAS_STATUS(I2C_NODE, okay)
    #error "I2C controller must be enabled"
#endif

#if !DT_HAS_CHOSEN(zephyr_display)
    #error "SSD1306 shield must set chosen zephyr,display"
#endif

#if !DT_NODE_HAS_STATUS(OLED_NODE, okay)
    #error "OLED display must be enabled"
#endif

#if !DT_NODE_HAS_PROP(OLED_NODE, zephyr_deferred_init)
    #error "OLED must have zephyr,deferred-init in app.overlay"
#endif

static const struct device* const i2c_dev  = DEVICE_DT_GET(I2C_NODE);
static const struct device* const oled_dev = DEVICE_DT_GET(OLED_NODE);

/* -------------------------------------------------------------------------- */
/* Shared application message queue                                           */
/* -------------------------------------------------------------------------- */

/*
 * The queue is defined ONCE here. Other .c files access it through
 * 'extern struct k_msgq app_event_queue;' in app_events.h.
 *
 * Each queue slot stores a COPY of struct app_event.
 */
K_MSGQ_DEFINE(app_event_queue, sizeof(struct app_event), 16, 4);

/* -------------------------------------------------------------------------- */
/* Static thread stacks and thread control blocks                             */
/* -------------------------------------------------------------------------- */

#define INPUT_THREAD_STACK_SIZE   2048
#define DISPLAY_THREAD_STACK_SIZE 3072
#define NOTIFY_THREAD_STACK_SIZE  1024

/* Lower numeric value means higher priority for preemptible threads. */
#define INPUT_THREAD_PRIORITY     5
#define DISPLAY_THREAD_PRIORITY   5
#define NOTIFY_THREAD_PRIORITY    6

K_THREAD_STACK_DEFINE(input_thread_stack, INPUT_THREAD_STACK_SIZE);
K_THREAD_STACK_DEFINE(display_thread_stack, DISPLAY_THREAD_STACK_SIZE);
K_THREAD_STACK_DEFINE(notify_thread_stack, NOTIFY_THREAD_STACK_SIZE);

static struct k_thread input_thread_data;
static struct k_thread display_thread_data;
static struct k_thread notify_thread_data;

/* -------------------------------------------------------------------------- */
/* OLED hardware initialization                                               */
/* -------------------------------------------------------------------------- */

/**
 * @brief Recover I2C, initialize deferred SSD1306 and prepare CFB.
 *
 * Our board previously returned -EBUSY during normal SSD1306 startup.
 * Delaying OLED initialization until after i2c_recover_bus() fixes that.
 *
 * @return 0 when the OLED is ready, otherwise a negative error code.
 */
static int oled_initialize(void)
{
    int status;

    /* STEP 1: I2C controller must already be ready. */
    if(!device_is_ready(i2c_dev))
    {
        printk("ERROR: I2C controller not ready\n");
        return -ENODEV;
    }

    /* STEP 2: Recover the I2C bus before SSD1306 initialization. */
    printk("OLED: recovering I2C bus...\n");
    status = i2c_recover_bus(i2c_dev);

    if(status < 0)
    {
        printk("ERROR: I2C recovery failed: %d\n", status);
        return status;
    }

    /* STEP 3: Start the OLED device marked deferred in Devicetree. */
    printk("OLED: initializing deferred SSD1306...\n");
    status = device_init(oled_dev);

    if(status < 0)
    {
        printk("ERROR: SSD1306 device_init failed: %d\n", status);
        return status;
    }

    if(!device_is_ready(oled_dev))
    {
        printk("ERROR: SSD1306 driver not ready\n");
        return -ENODEV;
    }

    /* STEP 4: Configure the monochrome OLED pixel format. */
    status = display_set_pixel_format(oled_dev, PIXEL_FORMAT_MONO01);

    if(status < 0)
    {
        printk("ERROR: OLED pixel format failed: %d\n", status);
        return status;
    }

    /* STEP 5: Initialize the CFB framebuffer. */
    status = cfb_framebuffer_init(oled_dev);

    if(status < 0)
    {
        printk("ERROR: CFB initialization failed: %d\n", status);
        return status;
    }

    /* STEP 6: Clear the in-memory framebuffer. */
    status = cfb_framebuffer_clear(oled_dev, false);

    if(status < 0)
    {
        printk("ERROR: CFB clear failed: %d\n", status);
        return status;
    }

    /* STEP 7: Turn on the physical OLED output. */
    status = display_blanking_off(oled_dev);

    if(status < 0)
    {
        printk("ERROR: OLED blanking-off failed: %d\n", status);
        return status;
    }

    printk("OLED: SSD1306 and CFB ready\n");
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Application main                                                           */
/* -------------------------------------------------------------------------- */

/**
 * @brief Initialize hardware and create all three Zephyr threads.
 *
 * @return 0 on successful startup or a negative error code.
 */
int main(void)
{
    int status;

    printk("\n========================================\n");
    printk(" Example 06: Emoji Notification Center\n");
    printk(" Zephyr RTOS / Heltec WiFi LoRa 32 V2\n");
    printk("========================================\n");

    /*
     * STEP 1: Initialize the OLED before any worker thread
     * can start drawing.
     */
    status = oled_initialize();

    if(status < 0)
    {
        printk("ERROR: OLED initialization failed: %d\n", status);
        return status;
    }

    /*
     * STEP 2: Configure joystick X/Y ADC channels,
     * joystick SW and onboard PRG button.
     */
    status = input_thread_init();

    if(status < 0)
    {
        printk("ERROR: Input initialization failed: %d\n", status);
        return status;
    }

    /*
     * STEP 3: Create the display thread first.
     *
     * This thread owns the OLED and initially displays
     * the joystick help / controls screen.
     */
    (void)k_thread_create(&display_thread_data, display_thread_stack,
                          K_THREAD_STACK_SIZEOF(display_thread_stack), display_thread_entry,
                          NULL, NULL, NULL, DISPLAY_THREAD_PRIORITY, 0, K_NO_WAIT);

    /*
     * STEP 4: Create the input thread.
     *
     * This thread reads joystick movement and button presses.
     * All user interactions are sent through app_event_queue.
     */
    (void)k_thread_create(&input_thread_data, input_thread_stack,
                          K_THREAD_STACK_SIZEOF(input_thread_stack), input_thread_entry,
                          NULL, NULL, NULL, INPUT_THREAD_PRIORITY, 0, K_NO_WAIT);

    /*
     * STEP 5: Create the notification thread.
     *
     * Generates simulated notifications and sends them
     * to the same application message queue.
     */
    (void)k_thread_create(&notify_thread_data, notify_thread_stack,
                          K_THREAD_STACK_SIZEOF(notify_thread_stack), notification_thread_entry,
                          NULL, NULL, NULL, NOTIFY_THREAD_PRIORITY, 0, K_NO_WAIT);

    printk("APP: three static threads created\n");
    printk("APP: PRG toggles help/browser\n");
    printk("APP: joystick controls emojis\n");

    /*
     * main() may return here.
     *
     * The three worker threads continue running under
     * the Zephyr scheduler.
     */
    return 0;
}
