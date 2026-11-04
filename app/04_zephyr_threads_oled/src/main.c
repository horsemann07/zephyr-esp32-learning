
/* ============================================================================
 * File        : main.c
 * Description : Zephyr RTOS multithreading with SSD1306 OLED
 * Board       : Heltec WiFi LoRa 32 V2
 * Zephyr      : 4.4.0
 * ============================================================================
 *
 * Description:
 *   Demonstrates three Zephyr threads using statically allocated stacks.
 *
 *   Thread A:
 *     - Increments counter A every 500 ms.
 *
 *   Thread B:
 *     - Increments counter B every 1000 ms.
 *
 *   OLED Thread:
 *     - Reads both counters.
 *     - Updates the OLED every 200 ms.
 *
 *   Both counters use Zephyr atomic operations to allow safe
 *   communication between threads without a mutex.
 *
 *   Only the OLED thread writes to the display.
 *
 * Initialization Sequence:
 *   1. Recover I2C bus.
 *   2. Initialize deferred SSD1306 driver.
 *   3. Initialize Character Framebuffer (CFB).
 *   4. Create three Zephyr threads.
 *   5. Allow Zephyr scheduler to run them.
 *
 * ========================================================================== */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>

#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/display.h>
#include <zephyr/display/cfb.h>

#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>

#include <errno.h>
#include <stdint.h>

/* -------------------------------------------------------------------------- */
/* Application Configuration                                                  */
/* -------------------------------------------------------------------------- */

#define I2C_NODE               DT_NODELABEL(i2c0)
#define OLED_NODE              DT_CHOSEN(zephyr_display)

/* Thread stack sizes in bytes. */
#define THREAD_A_STACK_SIZE    1024
#define THREAD_B_STACK_SIZE    1024
#define THREAD_OLED_STACK_SIZE 2048

/* Thread priorities: lower number = higher priority. */
#define THREAD_A_PRIORITY      5
#define THREAD_B_PRIORITY      5
#define THREAD_OLED_PRIORITY   6

/* Thread execution intervals. */
#define THREAD_A_INTERVAL_MS   500
#define THREAD_B_INTERVAL_MS   1000
#define OLED_REFRESH_MS        200

/* -------------------------------------------------------------------------- */
/* Compile-Time Devicetree Validation                                        */
/* -------------------------------------------------------------------------- */

#if !DT_NODE_EXISTS(I2C_NODE)
    #error "I2C0 Devicetree node not found"
#endif

#if !DT_NODE_HAS_STATUS(I2C_NODE, okay)
    #error "I2C0 Devicetree node is disabled"
#endif

#if !DT_HAS_CHOSEN(zephyr_display)
    #error "No display selected in Devicetree"
#endif

#if !DT_NODE_HAS_STATUS(OLED_NODE, okay)
    #error "OLED Devicetree node is disabled"
#endif

#if !DT_PROP(OLED_NODE, zephyr_deferred_init)
    #error "OLED must use zephyr,deferred-init"
#endif

/* -------------------------------------------------------------------------- */
/* Device References                                                          */
/* -------------------------------------------------------------------------- */

static const struct device* const i2c_dev = DEVICE_DT_GET(I2C_NODE);

static const struct device* const oled_dev = DEVICE_DT_GET(OLED_NODE);

/* -------------------------------------------------------------------------- */
/* Shared Atomic Counters                                                     */
/* -------------------------------------------------------------------------- */

/*
 * Atomic variables can be safely accessed by multiple threads
 * using Zephyr atomic APIs.
 */
static atomic_t counter_a = ATOMIC_INIT(0);
static atomic_t counter_b = ATOMIC_INIT(0);

/* -------------------------------------------------------------------------- */
/* Static Thread Stacks                                                       */
/* -------------------------------------------------------------------------- */

/*
 * These macros allocate each thread's stack at build time.
 *
 * No runtime stack allocation is required.
 */
K_THREAD_STACK_DEFINE(thread_a_stack, THREAD_A_STACK_SIZE);
K_THREAD_STACK_DEFINE(thread_b_stack, THREAD_B_STACK_SIZE);
K_THREAD_STACK_DEFINE(thread_oled_stack, THREAD_OLED_STACK_SIZE);

/* -------------------------------------------------------------------------- */
/* Thread Control Blocks                                                      */
/* -------------------------------------------------------------------------- */

/*
 * Zephyr stores thread state and scheduling information
 * in struct k_thread.
 */
static struct k_thread thread_a_data;
static struct k_thread thread_b_data;
static struct k_thread thread_oled_data;

/* -------------------------------------------------------------------------- */
/* OLED Hardware Initialization                                               */
/* -------------------------------------------------------------------------- */

/**
 * @brief Recover the I2C bus before initializing the OLED.
 *
 * @return 0 on success, otherwise a negative error code.
 */
static int oled_i2c_prepare(void)
{
    int status;

    if(!device_is_ready(i2c_dev))
    {
        printk("ERROR: I2C controller not ready\n");
        return -ENODEV;
    }

    printk("Recovering I2C bus...\n");

    status = i2c_recover_bus(i2c_dev);

    if(status < 0)
    {
        printk("ERROR: I2C recovery failed: %d\n", status);
        return status;
    }

    printk("I2C recovery successful\n");

    return 0;
}

/**
 * @brief Initialize the deferred SSD1306 device.
 *
 * @return 0 on success, otherwise a negative error code.
 */
static int oled_device_initialize(void)
{
    int status;

    printk("Initializing SSD1306...\n");

    status = device_init(oled_dev);

    if(status < 0)
    {
        printk("ERROR: SSD1306 initialization failed: %d\n", status);
        return status;
    }

    if(!device_is_ready(oled_dev))
    {
        printk("ERROR: OLED device not ready\n");
        return -ENODEV;
    }

    printk("SSD1306 ready\n");

    return 0;
}

/**
 * @brief Initialize the OLED character framebuffer.
 *
 * @return 0 on success, otherwise a negative error code.
 */
static int oled_framebuffer_initialize(void)
{
    int status;

    status = display_set_pixel_format(oled_dev, PIXEL_FORMAT_MONO01);

    if(status < 0)
    {
        return status;
    }

    status = cfb_framebuffer_init(oled_dev);

    if(status < 0)
    {
        return status;
    }

    status = cfb_framebuffer_clear(oled_dev, false);

    if(status < 0)
    {
        return status;
    }

    status = display_blanking_off(oled_dev);

    if(status < 0)
    {
        return status;
    }

    printk("OLED framebuffer ready\n");

    return 0;
}

/* -------------------------------------------------------------------------- */
/* Thread A                                                                   */
/* -------------------------------------------------------------------------- */

/**
 * @brief Increment counter A every 500 milliseconds.
 */
static void thread_a_entry(void* arg1, void* arg2, void* arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    printk("Thread A started\n");

    while(true)
    {
        /* Atomically increment counter A. */
        atomic_inc(&counter_a);

        /* Sleep and allow other threads to execute. */
        k_sleep(K_MSEC(THREAD_A_INTERVAL_MS));
    }
}

/* -------------------------------------------------------------------------- */
/* Thread B                                                                   */
/* -------------------------------------------------------------------------- */

/**
 * @brief Increment counter B every 1000 milliseconds.
 */
static void thread_b_entry(void* arg1, void* arg2, void* arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    printk("Thread B started\n");

    while(true)
    {
        /* Atomically increment counter B. */
        atomic_inc(&counter_b);

        /* Sleep and allow other threads to execute. */
        k_sleep(K_MSEC(THREAD_B_INTERVAL_MS));
    }
}

/* -------------------------------------------------------------------------- */
/* OLED Display Thread                                                        */
/* -------------------------------------------------------------------------- */

/**
 * @brief Read both counters and update the OLED.
 *
 * Only this thread accesses the OLED framebuffer.
 */
static void thread_oled_entry(void* arg1, void* arg2, void* arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    char line_a[32];
    char line_b[32];

    atomic_val_t value_a;
    atomic_val_t value_b;

    int status;

    printk("OLED thread started\n");

    while(true)
    {
        /*
         * STEP 1: Read shared counters atomically.
         */
        value_a = atomic_get(&counter_a);
        value_b = atomic_get(&counter_b);

        /*
         * STEP 2: Format counter values.
         */
        snprintk(line_a, sizeof(line_a), "A: %ld", (long)value_a);

        snprintk(line_b, sizeof(line_b), "B: %ld", (long)value_b);

        /*
         * STEP 3: Clear the previous framebuffer.
         */
        status = cfb_framebuffer_clear(oled_dev, false);

        if(status < 0)
        {
            printk("OLED clear failed: %d\n", status);
            goto next_refresh;
        }

        /*
         * STEP 4: Draw text into framebuffer memory.
         */
        status = cfb_print(oled_dev, "ZEPHYR TASKS", 0, 0);

        if(status < 0)
        {
            goto next_refresh;
        }

        status = cfb_print(oled_dev, line_a, 0, 20);

        if(status < 0)
        {
            goto next_refresh;
        }

        status = cfb_print(oled_dev, line_b, 0, 36);

        if(status < 0)
        {
            goto next_refresh;
        }

        /*
         * STEP 5: Transfer framebuffer to SSD1306.
         */
        status = cfb_framebuffer_finalize(oled_dev);

        if(status < 0)
        {
            printk("OLED update failed: %d\n", status);
        }

    next_refresh:
        /*
         * STEP 6: Refresh every 200 milliseconds.
         */
        k_sleep(K_MSEC(OLED_REFRESH_MS));
    }
}

/* -------------------------------------------------------------------------- */
/* Main Application                                                           */
/* -------------------------------------------------------------------------- */

/**
 * @brief Initialize hardware and start all three threads.
 *
 * @return 0 on success, otherwise a negative error code.
 */
int main(void)
{
    int status;

    printk("\n========================================\n");
    printk(" Lesson 04: Zephyr Multithreading\n");
    printk("========================================\n");

    /*
     * STEP 1: Recover the I2C bus.
     */
    status = oled_i2c_prepare();

    if(status < 0)
    {
        return status;
    }

    /*
     * STEP 2: Initialize the SSD1306 driver.
     */
    status = oled_device_initialize();

    if(status < 0)
    {
        return status;
    }

    /*
     * STEP 3: Initialize the OLED framebuffer.
     */
    status = oled_framebuffer_initialize();

    if(status < 0)
    {
        return status;
    }

    /*
     * STEP 4: Create Thread A.
     */
    k_thread_create(&thread_a_data, thread_a_stack,
                    K_THREAD_STACK_SIZEOF(thread_a_stack), thread_a_entry, NULL,
                    NULL, NULL, THREAD_A_PRIORITY, 0, K_NO_WAIT);

    /*
     * STEP 5: Create Thread B.
     */
    k_thread_create(&thread_b_data, thread_b_stack,
                    K_THREAD_STACK_SIZEOF(thread_b_stack), thread_b_entry, NULL,
                    NULL, NULL, THREAD_B_PRIORITY, 0, K_NO_WAIT);

    /*
     * STEP 6: Create the OLED display thread.
     */
    k_thread_create(&thread_oled_data, thread_oled_stack,
                    K_THREAD_STACK_SIZEOF(thread_oled_stack), thread_oled_entry,
                    NULL, NULL, NULL, THREAD_OLED_PRIORITY, 0, K_NO_WAIT);

    printk("All three threads created successfully\n");

    /*
     * main() can return here.
     *
     * The three worker threads continue running under
     * Zephyr's scheduler.
     */
    return 0;
}
