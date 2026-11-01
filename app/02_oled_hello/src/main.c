
/* ============================================================================
 * File        : main.c
 * Description : SSD1306 OLED display with I2C bus recovery
 * Board       : Heltec WiFi LoRa 32 V2 (ESP32)
 * Date        : 2026-11-01
 * ============================================================================
 *
 * Description:
 *   Initializes the onboard SSD1306 OLED using Zephyr's display
 *   driver and Character Framebuffer (CFB) subsystem.
 *
 *   The Heltec ESP32 I2C bus was previously observed in a busy
 *   state during SSD1306 driver initialization.
 *
 *   This application uses deferred device initialization so that
 *   the I2C bus can be recovered before the SSD1306 driver starts.
 *
 * Hardware:
 *   GPIO4  : OLED I2C SDA
 *   GPIO15 : OLED I2C SCL
 *   GPIO16 : OLED reset
 *   GPIO21 : Vext power control
 *   I2C    : SSD1306 address 0x3C
 *   OLED   : 128 x 64 monochrome display
 *
 * Behavior:
 *   1. Verify that the I2C controller is ready.
 *   2. Recover the I2C bus.
 *   3. Initialize the deferred SSD1306 driver.
 *   4. Verify that the OLED device is ready.
 *   5. Configure the monochrome pixel format.
 *   6. Initialize the Character Framebuffer.
 *   7. Clear the display.
 *   8. Render "Hello Zephyr!" on the OLED.
 *
 * Notes:
 *   The Heltec board initialization already enables Vext power
 *   on GPIO21 and releases OLED reset on GPIO16.
 *
 *   The ssd1306_128x64 shield must be enabled.
 *
 *   The SSD1306 node must use zephyr,deferred-init.
 *
 * Change Log (newest first):
 * Date          Description
 * ------------  -------------------------------------------------------------
 * 2026-11-01    Added deferred SSD1306 initialization
 * 2026-11-01    Added I2C bus recovery before display initialization
 * 2026-11-01    Added CFB text rendering
 * ========================================================================== */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/display.h>
#include <zephyr/display/cfb.h>
#include <zephyr/sys/printk.h>

#include <errno.h>
#include <stdint.h>

/* -------------------------------------------------------------------------- */
/* Application Configuration                                                  */
/* -------------------------------------------------------------------------- */

/* OLED text position in pixels. */
#define OLED_TEXT_X                  (0)
#define OLED_TEXT_Y                  (0)

/* Short delay after display initialization. */
#define OLED_STARTUP_DELAY_MS        (100U)

/*
 * Select the ESP32 I2C controller defined in the board Devicetree.
 */
#define I2C_NODE                     DT_NODELABEL(i2c0)

/*
 * Select the display declared by the SSD1306 shield.
 */
#define OLED_NODE                    DT_CHOSEN(zephyr_display)

/* -------------------------------------------------------------------------- */
/* Compile-Time Validation                                                   */
/* -------------------------------------------------------------------------- */

#if !DT_NODE_EXISTS(I2C_NODE)
    #error "I2C0 Devicetree node not found"
#endif

#if !DT_NODE_HAS_STATUS(I2C_NODE, okay)
    #error "I2C0 Devicetree node is disabled"
#endif

#if !DT_HAS_CHOSEN(zephyr_display)
    #error "No OLED display selected in Devicetree"
#endif

#if !DT_NODE_HAS_STATUS(OLED_NODE, okay)
    #error "OLED Devicetree node is disabled"
#endif

#if !DT_PROP(OLED_NODE, zephyr_deferred_init)
    #error "OLED must have zephyr,deferred-init in app.overlay"
#endif

/* -------------------------------------------------------------------------- */
/* Device References                                                          */
/* -------------------------------------------------------------------------- */

/*
 * I2C controller device.
 *
 * This device initializes normally during Zephyr startup.
 */
static const struct device *const i2c_dev =
    DEVICE_DT_GET(I2C_NODE);

/*
 * SSD1306 display device.
 *
 * Its initialization is deferred until device_init() is called.
 */
static const struct device *const oled_dev =
    DEVICE_DT_GET(OLED_NODE);

/* -------------------------------------------------------------------------- */
/* I2C Bus Initialization and Recovery                                        */
/* -------------------------------------------------------------------------- */

/**
 * @brief Verify and recover the ESP32 I2C bus.
 *
 * The ESP32 I2C controller must already be initialized.
 *
 * Bus recovery is performed before initializing the SSD1306
 * display driver.
 *
 * @return 0 on success, otherwise a negative error code.
 */
static int oled_i2c_prepare(void)
{
    int status;

    /*
     * STEP 1: Verify that the I2C controller is ready.
     */
    if (!device_is_ready(i2c_dev))
    {
        printk("ERROR: I2C controller is not ready\n");
        return -ENODEV;
    }

    printk("I2C controller: %s\n", i2c_dev->name);

    /*
     * STEP 2: Attempt to recover the I2C bus.
     *
     * Our diagnostic example previously showed:
     *
     *   Before recovery: SDA=0, SCL=1
     *   After recovery : SDA=1, SCL=1
     *
     * The recovery API attempts to release a stuck bus and
     * restore a usable controller state.
     */
    printk("Attempting I2C bus recovery...\n");

    status = i2c_recover_bus(i2c_dev);

    if (status < 0)
    {
        printk("ERROR: I2C bus recovery failed: %d\n", status);
        return status;
    }

    printk("I2C bus recovery successful\n");

    return 0;
}

/* -------------------------------------------------------------------------- */
/* SSD1306 Device Initialization                                              */
/* -------------------------------------------------------------------------- */

/**
 * @brief Initialize the deferred SSD1306 display driver.
 *
 * The SSD1306 device is marked with zephyr,deferred-init
 * in Devicetree.
 *
 * Therefore, its driver is not initialized automatically
 * during the normal Zephyr boot sequence.
 *
 * @return 0 on success, otherwise a negative error code.
 */
static int oled_device_initialize(void)
{
    int status;

    /*
     * STEP 1: Start the SSD1306 driver initialization.
     *
     * This sends the OLED controller configuration commands
     * through the ESP32 I2C driver.
     */
    printk("Initializing SSD1306 driver...\n");

    status = device_init(oled_dev);

    if (status < 0)
    {
        printk("ERROR: SSD1306 device initialization failed: %d\n",
               status);
        return status;
    }

    /*
     * STEP 2: Verify that the device is ready.
     */
    if (!device_is_ready(oled_dev))
    {
        printk("ERROR: SSD1306 device is not ready\n");
        return -ENODEV;
    }

    printk("SSD1306 driver initialized successfully\n");
    printk("OLED device: %s\n", oled_dev->name);

    return 0;
}

/* -------------------------------------------------------------------------- */
/* Character Framebuffer Initialization                                       */
/* -------------------------------------------------------------------------- */

/**
 * @brief Initialize the OLED character framebuffer.
 *
 * Configures the pixel format, initializes CFB, clears the
 * screen, and enables visible OLED output.
 *
 * @return 0 on success, otherwise a negative error code.
 */
static int oled_framebuffer_initialize(void)
{
    int status;

    /*
     * STEP 1: Configure monochrome pixel format.
     *
     * PIXEL_FORMAT_MONO01:
     *   0 = Black
     *   1 = White
     */
    status = display_set_pixel_format(oled_dev,
                                      PIXEL_FORMAT_MONO01);

    if (status < 0)
    {
        printk("ERROR: Pixel format configuration failed: %d\n",
               status);
        return status;
    }

    /*
     * STEP 2: Initialize the Character Framebuffer.
     *
     * CFB allocates framebuffer memory and prepares the
     * configured fonts for text rendering.
     */
    status = cfb_framebuffer_init(oled_dev);

    if (status < 0)
    {
        printk("ERROR: CFB initialization failed: %d\n", status);
        return status;
    }

    /*
     * STEP 3: Clear the framebuffer.
     *
     * The false argument means the clear is performed
     * in memory without immediately transferring pixels.
     */
    status = cfb_framebuffer_clear(oled_dev, false);

    if (status < 0)
    {
        printk("ERROR: Framebuffer clear failed: %d\n", status);
        return status;
    }

    /*
     * STEP 4: Enable the OLED display output.
     *
     * SSD1306 initialization may leave the screen blanked.
     */
    status = display_blanking_off(oled_dev);

    if (status < 0)
    {
        printk("ERROR: OLED display enable failed: %d\n", status);
        return status;
    }

    printk("OLED framebuffer initialized successfully\n");

    return 0;
}

/* -------------------------------------------------------------------------- */
/* OLED Text Rendering                                                       */
/* -------------------------------------------------------------------------- */

/**
 * @brief Render a welcome message on the OLED.
 *
 * CFB draws text into a framebuffer in memory.
 * Finalizing the framebuffer transfers the pixels to the
 * SSD1306 through the Zephyr display driver.
 *
 * @return 0 on success, otherwise a negative error code.
 */
static int oled_show_welcome_message(void)
{
    int status;

    /*
     * STEP 1: Clear previous framebuffer contents.
     */
    status = cfb_framebuffer_clear(oled_dev, false);

    if (status < 0)
    {
        printk("ERROR: Framebuffer clear failed: %d\n", status);
        return status;
    }

    /*
     * STEP 2: Draw the welcome message.
     */
    status = cfb_print(oled_dev,
                       "Hello Zephyr!",
                       OLED_TEXT_X,
                       OLED_TEXT_Y);

    if (status < 0)
    {
        printk("ERROR: OLED text drawing failed: %d\n", status);
        return status;
    }

    /*
     * STEP 3: Transfer framebuffer pixels to the OLED.
     *
     * This operation sends display data over I2C.
     */
    status = cfb_framebuffer_finalize(oled_dev);

    if (status < 0)
    {
        printk("ERROR: OLED framebuffer transfer failed: %d\n",
               status);
        return status;
    }

    printk("Message displayed: Hello Zephyr!\n");

    return 0;
}

/* -------------------------------------------------------------------------- */
/* Main Application                                                           */
/* -------------------------------------------------------------------------- */

/**
 * @brief Main application entry point.
 *
 * Controls the initialization sequence:
 *
 *   I2C recovery
 *       |
 *       v
 *   SSD1306 driver initialization
 *       |
 *       v
 *   CFB framebuffer initialization
 *       |
 *       v
 *   OLED text rendering
 *
 * @return 0 on success, otherwise a negative error code.
 */
int main(void)
{
    int status;

    printk("========================================\n");
    printk(" Zephyr OLED Hello Example\n");
    printk(" Board: Heltec WiFi LoRa 32 V2\n");
    printk("========================================\n");

    /*
     * STEP 1: Prepare and recover the I2C controller.
     */
    status = oled_i2c_prepare();

    if (status < 0)
    {
        printk("ERROR: I2C preparation failed: %d\n", status);
        return status;
    }

    /*
     * STEP 2: Initialize the deferred SSD1306 device.
     */
    status = oled_device_initialize();

    if (status < 0)
    {
        printk("ERROR: OLED device initialization failed: %d\n",
               status);
        return status;
    }

    /*
     * STEP 3: Initialize the character framebuffer.
     */
    status = oled_framebuffer_initialize();

    if (status < 0)
    {
        printk("ERROR: OLED framebuffer initialization failed: %d\n",
               status);
        return status;
    }

    /*
     * STEP 4: Allow a short settling period.
     */
    k_sleep(K_MSEC(OLED_STARTUP_DELAY_MS));

    /*
     * STEP 5: Display the welcome message.
     */
    status = oled_show_welcome_message();

    if (status < 0)
    {
        printk("ERROR: OLED text rendering failed: %d\n", status);
        return status;
    }

    printk("========================================\n");
    printk(" OLED example completed successfully\n");
    printk("========================================\n");

    /*
     * The OLED retains its image until it is overwritten,
     * reset, or powered off.
     */
    return 0;
}
