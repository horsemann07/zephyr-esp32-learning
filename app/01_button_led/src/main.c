
/* ============================================================================
 * File        : main.c
 * Description : Button-controlled LED with software debouncing
 * Board       : Heltec WiFi LoRa 32 V2 (ESP32)
 * Date        : 2026-11-01
 * ============================================================================
 *
 * Description:
 *   Reads the PRG button using GPIO polling and controls the onboard LED.
 *
 *   A button state change is accepted only after it remains stable
 *   for at least 30 milliseconds.
 *
 * Hardware:
 *   GPIO25 : Onboard LED (active HIGH)
 *   GPIO0  : PRG button (active LOW)
 *
 * Behavior:
 *   Button pressed  -> LED ON
 *   Button released -> LED OFF
 *
 * Change Log (newest first):
 * Date          Description
 * ------------  -------------------------------------------------------------
 * 2026-11-01    Added 30 ms software debounce using Zephyr uptime
 * 2026-11-01    Added button polling and LED control
 * ========================================================================== */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>

#include <errno.h>
#include <stdint.h>

/* -------------------------------------------------------------------------- */
/* Application Configuration                                                  */
/* -------------------------------------------------------------------------- */

/*
 * Read the button every 5 ms.
 *
 * A shorter polling interval helps detect button transitions
 * while still allowing the Zephyr scheduler to run other threads.
 */
#define BUTTON_POLL_INTERVAL_MS  (5U)

/*
 * Minimum time for which a new button state must remain stable
 * before the application accepts that state.
 */
#define BUTTON_DEBOUNCE_MS       (50U)

/*
 * Access the existing board DeviceTree node labels.
 *
 * No DT_ALIAS() is used.
 */
#define LED_NODE                DT_NODELABEL(led0)
#define BUTTON_NODE             DT_NODELABEL(button0)

/* -------------------------------------------------------------------------- */
/* Compile-Time Validation                                                   */
/* -------------------------------------------------------------------------- */

#if !DT_NODE_EXISTS(LED_NODE) || !DT_NODE_EXISTS(BUTTON_NODE)
    #error "LED or button DeviceTree node not found"
#endif

#if !DT_NODE_HAS_STATUS(LED_NODE, okay) || \
    !DT_NODE_HAS_STATUS(BUTTON_NODE, okay)
    #error "LED or button DeviceTree node is disabled"
#endif

/* -------------------------------------------------------------------------- */
/* GPIO Specifications                                                        */
/* -------------------------------------------------------------------------- */

static const struct gpio_dt_spec led =
    GPIO_DT_SPEC_GET(LED_NODE, gpios);

static const struct gpio_dt_spec button =
    GPIO_DT_SPEC_GET(BUTTON_NODE, gpios);

/* -------------------------------------------------------------------------- */
/* Main Application                                                           */
/* -------------------------------------------------------------------------- */

int main(void)
{
    int status;
    int raw_state;
    int last_raw_state;
    int stable_state;

    int64_t last_change_time;

    /* Verify GPIO controllers. */
    if(!gpio_is_ready_dt(&led) || !gpio_is_ready_dt(&button))
    {
        printk("ERROR: GPIO controller not ready\n");
        return -ENODEV;
    }

    /* Configure LED initially OFF. */
    status = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);

    if(status < 0)
    {
        printk("ERROR: LED configuration failed: %d\n", status);
        return status;
    }

    /* Configure the PRG button as input. */
    status = gpio_pin_configure_dt(&button, GPIO_INPUT);

    if(status < 0)
    {
        printk("ERROR: Button configuration failed: %d\n", status);
        return status;
    }

    /*
     * Read initial button state.
     *
     * gpio_pin_get_dt() returns the logical state:
     *   1 = pressed
     *   0 = released
     */
    raw_state = gpio_pin_get_dt(&button);

    if(raw_state < 0)
    {
        printk("ERROR: Initial button read failed: %d\n", raw_state);
        return raw_state;
    }

    /*
     * last_raw_state:
     *   Latest sampled electrical button state.
     *
     * stable_state:
     *   Last accepted debounced button state.
     *
     * last_change_time:
     *   Timestamp when the raw button state last changed.
     *
     * The initial sample is used as the starting state.
     */
    last_raw_state = raw_state;
    stable_state = raw_state;
    last_change_time = k_uptime_get();

    /* Make LED match the initial button state. */
    status = gpio_pin_set_dt(&led, stable_state);

    if(status < 0)
    {
        printk("ERROR: Initial LED update failed: %d\n", status);
        return status;
    }

    printk("Button LED debounce example started\n");
    printk("Debounce period: %u ms\n",
           (unsigned int)BUTTON_DEBOUNCE_MS);

    while(1)
    {
        /*
         * STEP 1: Read the current button state.
         */
        raw_state = gpio_pin_get_dt(&button);

        if(raw_state < 0)
        {
            printk("ERROR: Button read failed: %d\n", raw_state);
            return raw_state;
        }

        /*
         * STEP 2: Detect a new raw transition.
         *
         * Mechanical contacts may bounce between 0 and 1.
         *
         * Every observed raw transition restarts the debounce timer.
         */
        if(raw_state != last_raw_state)
        {
            last_raw_state = raw_state;
            last_change_time = k_uptime_get();
        }

        /*
         * STEP 3: Accept the state only when:
         *
         *   1. It differs from the previous stable state.
         *   2. No raw transition has been observed for 30 ms.
         *
         * This prevents short bouncing transitions from
         * immediately changing the LED output.
         */
        if((raw_state != stable_state) &&
           ((k_uptime_get() - last_change_time) >= BUTTON_DEBOUNCE_MS))
        {
            /*
             * Update the LED before committing the new stable state.
             * This ensures a failed GPIO write can be retried.
             */
            status = gpio_pin_set_dt(&led, raw_state);

            if(status < 0)
            {
                printk("ERROR: LED update failed: %d\n", status);
                return status;
            }

            stable_state = raw_state;

            /*
             * Print only accepted (debounced) transitions.
             */
            if(stable_state != 0)
            {
                printk("Button PRESSED -> LED ON\n");
            }
            else
            {
                printk("Button RELEASED -> LED OFF\n");
            }
        }

        /*
         * STEP 4: Sleep for 5 ms.
         *
         * k_sleep() suspends the current thread.
         * This is not a CPU busy-wait.
         */
        k_sleep(K_MSEC(BUTTON_POLL_INTERVAL_MS));
    }

    return 0;
}
