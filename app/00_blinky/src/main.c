
/* ============================================================================
 * File        : main.c
 * Description : LED blinking application using Zephyr RTOS GPIO driver
 * Platform    : Zephyr RTOS / Supported development board
 * Language    : C
 * ============================================================================
 *
 * DESCRIPTION:
 *
 * This application demonstrates basic GPIO output control using Zephyr RTOS.
 *
 * The program:
 *
 *   1. Obtains the LED GPIO configuration from the DeviceTree.
 *   2. Checks whether the associated GPIO controller is ready.
 *   3. Configures the LED GPIO pin as an output.
 *   4. Toggles the LED state continuously.
 *   5. Waits 500 milliseconds between each toggle.
 *   6. Prints a debug message after each successful toggle.
 *
 * HARDWARE REQUIREMENTS:
 *
 *   - A Zephyr-supported board.
 *   - A GPIO-controlled LED.
 *   - A valid "led0" alias in the board DeviceTree.
 *
 * EXECUTION FLOW:
 *
 *   System Boot
 *        |
 *        v
 *   Zephyr Kernel Initialization
 *        |
 *        v
 *   main()
 *        |
 *        v
 *   Get LED Configuration from DeviceTree
 *        |
 *        v
 *   Check GPIO Controller Ready
 *        |
 *        v
 *   Configure LED as Output (Initially OFF)
 *        |
 *        v
 *   +-------------------------------+
 *   | Toggle LED                    |
 *   | Print Status                  |
 *   | Sleep for 500 milliseconds    |
 *   +-------------------------------+
 *        |
 *        +-------- Repeat Forever
 *
 * NOTE:
 *
 * The application uses Zephyr's GPIO abstraction instead of directly
 * accessing MCU GPIO registers.
 *
 * This allows the same source file to work with different boards,
 * provided each board defines a suitable "led0" DeviceTree alias.
 *
 * ========================================================================== */

/* -------------------------------------------------------------------------- */
/* Header Files                                                               */
/* -------------------------------------------------------------------------- */

/*
 * Provides Zephyr kernel interfaces.
 *
 * Used here for:
 *
 *   - k_sleep()
 *   - K_MSEC()
 *
 * k_sleep() suspends the current thread for a specified duration,
 * allowing the scheduler to execute other ready threads.
 */
#include <zephyr/kernel.h>

/*
 * Provides Zephyr's generic GPIO driver interfaces.
 *
 * Used here for:
 *
 *   - struct gpio_dt_spec
 *   - GPIO_DT_SPEC_GET()
 *   - gpio_is_ready_dt()
 *   - gpio_pin_configure_dt()
 *   - gpio_pin_toggle_dt()
 *   - GPIO_OUTPUT_INACTIVE
 *
 * These interfaces are independent of the underlying MCU GPIO driver.
 */
#include <zephyr/drivers/gpio.h>

/*
 * Provides standard negative error codes such as ENODEV.
 */
#include <errno.h>

/*
 * Provides printk().
 *
 * printk() is Zephyr's low-level diagnostic printing interface.
 * It is useful for simple debugging and early boot messages.
 */
#include <zephyr/sys/printk.h>

/* -------------------------------------------------------------------------- */
/* Application Configuration                                                  */
/* -------------------------------------------------------------------------- */

/*
 * LED blinking interval in milliseconds.
 *
 * A value of 500 means the LED changes state every 500 ms.
 *
 * Full ON/OFF cycle:
 *
 *     500 ms ON + 500 ms OFF = 1000 ms
 *
 * Therefore, one complete blinking cycle takes approximately
 * one second, excluding execution and scheduling overhead.
 */
#define LED_BLINK_INTERVAL_MS (500U)

/*
 * Obtain the DeviceTree node referenced by the "led0" alias.
 *
 * DT_ALIAS(led0):
 *
 *   - Is evaluated during compilation.
 *   - Searches the generated DeviceTree configuration.
 *   - Resolves the hardware node assigned to the led0 alias.
 *
 * Example board DeviceTree:
 *
 *   aliases {
 *       led0 = &user_led;
 *   };
 *
 * This macro does not initialize the LED or access the hardware.
 * It only identifies the corresponding DeviceTree node.
 */
#define LED_NODE DT_ALIAS(led0)

/* -------------------------------------------------------------------------- */
/* Compile-Time DeviceTree Validation                                         */
/* -------------------------------------------------------------------------- */

/*
 * Verify that the board defines an enabled led0 node.
 *
 * DT_NODE_EXISTS() checks whether the alias resolves to a node.
 *
 * DT_NODE_HAS_STATUS(..., okay) checks whether the selected node
 * is enabled in DeviceTree.
 *
 * If either check fails, compilation stops with a useful error.
 *
 * This is preferable to discovering an invalid DeviceTree
 * configuration only after the firmware has started.
 */
#if !DT_NODE_EXISTS(LED_NODE)
    #error "DeviceTree alias led0 is not defined for this board"
#endif

#if !DT_NODE_HAS_STATUS(LED_NODE, okay)
    #error "DeviceTree led0 node is not enabled"
#endif

/* -------------------------------------------------------------------------- */
/* GPIO Device Specification                                                  */
/* -------------------------------------------------------------------------- */

/*
 * Describe the LED GPIO using information generated from DeviceTree.
 *
 * GPIO_DT_SPEC_GET(LED_NODE, gpios) obtains:
 *
 *   1. GPIO controller device pointer.
 *   2. GPIO pin number.
 *   3. DeviceTree GPIO flags.
 *
 * For example, a DeviceTree entry might look like:
 *
 *   user_led: led_0 {
 *       gpios = <&gpio0 25 GPIO_ACTIVE_HIGH>;
 *   };
 *
 * In that example:
 *
 *   &gpio0            -> GPIO controller
 *   25                -> GPIO pin number
 *   GPIO_ACTIVE_HIGH  -> LED active polarity
 *
 * This is an example only. Your board may use a different pin,
 * GPIO controller or polarity.
 *
 * The "static const" declaration means:
 *
 *   static:
 *       The variable is visible only inside this source file.
 *
 *   const:
 *       The application cannot modify the GPIO specification.
 *
 * The structure itself is prepared using compile-time information.
 * Hardware setup happens later inside main().
 */
static const struct gpio_dt_spec led =
    GPIO_DT_SPEC_GET(LED_NODE, gpios);

/* ============================================================================
 * MAIN APPLICATION
 * ========================================================================== */

/**
 * @brief Zephyr LED blinking application entry point.
 *
 * @details
 * On the usual Zephyr multithreaded configuration, main() executes
 * in the Zephyr main thread after kernel and device initialization.
 *
 * The function validates the LED GPIO controller, configures the
 * LED pin as an output and continuously toggles its logical state.
 *
 * @return 0 on normal termination.
 *
 * @note Under successful operation, this function runs forever.
 */
int main(void)
{
    int status;

    /*
     * STEP 1: Check whether the GPIO controller is initialized.
     *
     * gpio_is_ready_dt():
     *
     *   - Reads the GPIO controller referenced by led.port.
     *   - Checks whether the device is ready for use.
     *   - Returns true if the controller is ready.
     *   - Returns false if the controller is unavailable.
     *
     * This does not check whether the LED itself is working.
     *
     * It checks whether the associated GPIO controller driver
     * has initialized successfully.
     */
    if(!gpio_is_ready_dt(&led))
    {
        printk("ERROR: LED GPIO controller is not ready\n");

        /*
         * Terminate with a negative error code.
         *
         * ENODEV means the required device is unavailable.
         */
        return -ENODEV;
    }

    /*
     * STEP 2: Configure the LED GPIO pin.
     *
     * gpio_pin_configure_dt():
     *
     *   - Selects the GPIO controller from led.port.
     *   - Selects the pin from led.pin.
     *   - Applies the DeviceTree GPIO flags.
     *   - Configures the pin as an output.
     *
     * GPIO_OUTPUT_INACTIVE:
     *
     *   - Configures the pin as an output.
     *   - Sets the initial LOGICAL state to inactive.
     *
     * For an active-high LED, inactive normally means LOW.
     *
     * For an active-low LED, inactive normally means HIGH.
     *
     * Zephyr accounts for the DeviceTree polarity flags.
     *
     * This allows the application to use logical ON/OFF states
     * without hardcoding the electrical LED polarity.
     *
     * Return value:
     *
     *    0 : Configuration successful.
     *   <0 : GPIO driver reported an error.
     */
    status = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);

    if(status < 0)
    {
        printk("ERROR: LED GPIO configuration failed (%d)\n",
               status);

        return status;
    }

    /*
     * STEP 3: Report successful initialization.
     *
     * printk() sends diagnostic output through Zephyr's
     * configured console backend.
     *
     * The output may appear on a UART terminal, RTT console,
     * or another supported console transport.
     *
     * printk() is separate from the Zephyr logging subsystem.
     */
    printk("Zephyr LED blinking application started\n");

    printk("LED toggle interval: %u ms\n",
           (unsigned int)LED_BLINK_INTERVAL_MS);

    /*
     * STEP 4: Execute the LED blinking loop.
     *
     * while(1) means the loop runs indefinitely.
     *
     * The main thread continues running until the system
     * is reset, powered off, or otherwise stopped.
     */
    while(1)
    {
        /*
         * STEP 4.1: Toggle the LED.
         *
         * gpio_pin_toggle_dt():
         *
         *   - Accesses the LED GPIO controller.
         *   - Toggles the physical pin output state.
         *   - Reports success or a negative driver error.
         *
         * Example:
         *
         *   Before call: LED OFF
         *   After call : LED ON
         *
         *   Next call  : LED OFF
         *
         * It toggles the output once per loop iteration.
         *
         * No software-maintained LED state variable is needed.
         */
        status = gpio_pin_toggle_dt(&led);

        if(status < 0)
        {
            printk("ERROR: LED toggle failed (%d)\n", status);

            /*
             * Stop the demonstration if the GPIO operation fails.
             */
            return status;
        }

        /*
         * STEP 4.2: Print the status.
         *
         * This message confirms that a GPIO toggle operation
         * was successfully requested.
         *
         * It does not independently verify the LED's
         * physical light output.
         */
        printk("LED toggled!\n");

        /*
         * STEP 4.3: Sleep for 500 milliseconds.
         *
         * K_MSEC():
         *
         *   Converts the specified millisecond duration into
         *   Zephyr's kernel timeout representation.
         *
         * k_sleep():
         *
         *   Suspends the current thread for the requested period.
         *
         *   While the main thread is sleeping, Zephyr can run
         *   other ready threads.
         *
         *   The CPU may also enter an idle or low-power state,
         *   depending on system configuration.
         *
         * IMPORTANT:
         *
         *   k_sleep() is not the same as a CPU busy-wait loop.
         *
         *   It does not intentionally keep the CPU executing
         *   instructions for the entire delay interval.
         *
         *   Scheduling may cause a small timing variation.
         */
        k_sleep(K_MSEC(LED_BLINK_INTERVAL_MS));
    }

    /*
     * This return statement is not reached during normal
     * blinking operation because the loop is infinite.
     */
    return 0;
}
