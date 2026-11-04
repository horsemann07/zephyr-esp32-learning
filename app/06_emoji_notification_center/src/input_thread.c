
/* ============================================================================
 * File        : input_thread.c
 * Description : Joystick and PRG input handling using Zephyr ADC/GPIO
 * Application : 06_emoji_notification_center
 * Board       : Heltec WiFi LoRa 32 V2
 * Zephyr      : 4.4.0
 * ============================================================================
 *
 * Hardware:
 *   GPIO36 : Joystick VRx (ADC1 channel 0)
 *   GPIO39 : Joystick VRy (ADC1 channel 3)
 *   GPIO13 : Joystick SW, active LOW
 *   GPIO0  : Onboard PRG button, active LOW
 *
 * Features:
 *   1. Read analog joystick X and Y.
 *   2. Convert analog readings to direction events.
 *   3. Debounce joystick SW and onboard PRG.
 *   4. Publish events using Zephyr k_msgq.
 *   5. Prevent repeated navigation while holding joystick.
 *
 * ========================================================================== */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>

#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>

#include <zephyr/sys/printk.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include "app_events.h"

/* -------------------------------------------------------------------------- */
/* Configuration                                                              */
/* -------------------------------------------------------------------------- */

#define INPUT_POLL_INTERVAL_MS  50

#define BUTTON_DEBOUNCE_SAMPLES 3

/*
 * Joystick ADC thresholds for 12-bit readings.
 *
 * These are starting values. Adjust them after observing
 * the actual neutral and extreme ADC readings.
 */
#define JOY_LOW_THRESHOLD       1100
#define JOY_HIGH_THRESHOLD      3200

/*
 * A wider neutral release region prevents accidental
 * repeated navigation due to ADC noise.
 */
#define JOY_CENTER_MIN          1500
#define JOY_CENTER_MAX          2700

/* Joystick SW is connected to GPIO13. */
#define JOYSTICK_SW_PIN         13

/* -------------------------------------------------------------------------- */
/* Devicetree References                                                      */
/* -------------------------------------------------------------------------- */

#define ADC_USER_NODE           DT_PATH(zephyr_user)
#define PRG_NODE                DT_ALIAS(sw0)
#define GPIO_NODE               DT_NODELABEL(gpio0)

/*
 * The overlay must provide:
 *
 * zephyr,user {
 *     io-channels = <&adc0 0>, <&adc0 3>;
 * };
 *
 * IMPORTANT:
 * Zephyr names the ESP32's ADC1 hardware controller "adc0".
 */
#if !DT_NODE_EXISTS(ADC_USER_NODE)
    #error "Missing zephyr,user ADC configuration in app.overlay"
#endif

#if !DT_NODE_EXISTS(PRG_NODE)
    #error "Missing onboard PRG button alias sw0"
#endif

static const struct adc_dt_spec joystick_x = ADC_DT_SPEC_GET_BY_IDX(ADC_USER_NODE, 0);

static const struct adc_dt_spec joystick_y = ADC_DT_SPEC_GET_BY_IDX(ADC_USER_NODE, 1);

/* Onboard PRG button from the board definition. */
static const struct gpio_dt_spec prg_button = GPIO_DT_SPEC_GET(PRG_NODE, gpios);

/* GPIO controller for joystick SW. */
static const struct device* const gpio_dev = DEVICE_DT_GET(GPIO_NODE);

/* -------------------------------------------------------------------------- */
/* Debounce State                                                             */
/* -------------------------------------------------------------------------- */

struct button_state
{
    bool last_sample;
    bool stable_pressed;
    uint8_t consecutive_samples;
};

/* Each physical button maintains its own debounce state. */
static struct button_state prg_state;
static struct button_state joystick_sw_state;

/* -------------------------------------------------------------------------- */
/* Queue Event Publisher                                                      */
/* -------------------------------------------------------------------------- */

/**
 * @brief Send one event without blocking the input thread.
 */
static void input_publish_event(enum app_event_type type, enum app_event_source source)
{
    struct app_event event = {
        .type = type, .source = source, .value = 0, .timestamp_ms = k_uptime_get_32()
    };

    /*
     * K_NO_WAIT ensures the input thread does not stop
     * if the display thread is currently busy.
     */
    int status = k_msgq_put(&app_event_queue, &event, K_NO_WAIT);

    if(status < 0)
    {
        printk("INPUT: Event dropped, queue full: %d\n", status);
    }
}

/* -------------------------------------------------------------------------- */
/* Analog Input                                                               */
/* -------------------------------------------------------------------------- */

/**
 * @brief Read one joystick ADC channel.
 *
 * @param channel ADC channel specification.
 * @param value   Output raw ADC sample.
 *
 * @return 0 on success, negative error code on failure.
 */
static int input_read_axis(const struct adc_dt_spec* channel, int16_t* value)
{
    int16_t sample = 0;

    struct adc_sequence sequence = { .buffer = &sample, .buffer_size = sizeof(sample) };

    int status = adc_sequence_init_dt(channel, &sequence);

    if(status < 0)
    {
        return status;
    }

    status = adc_read_dt(channel, &sequence);

    if(status < 0)
    {
        return status;
    }

    *value = sample;

    return 0;
}

/* -------------------------------------------------------------------------- */
/* Button Debouncing                                                          */
/* -------------------------------------------------------------------------- */

/**
 * @brief Detect a debounced button press.
 *
 * Three consecutive identical samples are required
 * before accepting a change.
 *
 * Only the transition from RELEASED to PRESSED
 * generates an event.
 */
static bool input_button_pressed(struct button_state* state, bool currently_pressed)
{
    if(currently_pressed != state->last_sample)
    {
        state->last_sample         = currently_pressed;
        state->consecutive_samples = 1U;
    }
    else if(state->consecutive_samples < BUTTON_DEBOUNCE_SAMPLES)
    {
        state->consecutive_samples++;
    }

    if(state->consecutive_samples >= BUTTON_DEBOUNCE_SAMPLES)
    {
        if(state->stable_pressed != currently_pressed)
        {
            state->stable_pressed = currently_pressed;

            if(currently_pressed)
            {
                return true;
            }
        }
    }

    return false;
}

/* -------------------------------------------------------------------------- */
/* Joystick Direction Handling                                                */
/* -------------------------------------------------------------------------- */

/**
 * @brief Convert X-axis movement to emoji-navigation events.
 *
 * LEFT  -> Previous emoji
 * RIGHT -> Next emoji
 *
 * A new movement is accepted only after returning
 * the joystick to its neutral position.
 */
static void input_process_x_axis(int16_t x, bool* armed)
{
    if(x >= JOY_CENTER_MIN && x <= JOY_CENTER_MAX)
    {
        *armed = true;
        return;
    }

    if(!*armed)
    {
        return;
    }

    if(x <= JOY_LOW_THRESHOLD)
    {
        input_publish_event(APP_EVENT_EMOJI_PREVIOUS, APP_EVENT_SOURCE_JOYSTICK);
        *armed = false;
    }
    else if(x >= JOY_HIGH_THRESHOLD)
    {
        input_publish_event(APP_EVENT_EMOJI_NEXT, APP_EVENT_SOURCE_JOYSTICK);
        *armed = false;
    }
}

/**
 * @brief Convert Y-axis movement to category-navigation events.
 *
 * UP   -> Previous category
 * DOWN -> Next category
 */
static void input_process_y_axis(int16_t y, bool* armed)
{
    if(y >= JOY_CENTER_MIN && y <= JOY_CENTER_MAX)
    {
        *armed = true;
        return;
    }

    if(!*armed)
    {
        return;
    }

    if(y <= JOY_LOW_THRESHOLD)
    {
        input_publish_event(APP_EVENT_CATEGORY_PREVIOUS, APP_EVENT_SOURCE_JOYSTICK);
        *armed = false;
    }
    else if(y >= JOY_HIGH_THRESHOLD)
    {
        input_publish_event(APP_EVENT_CATEGORY_NEXT, APP_EVENT_SOURCE_JOYSTICK);
        *armed = false;
    }
}

/* -------------------------------------------------------------------------- */
/* Hardware Initialization                                                    */
/* -------------------------------------------------------------------------- */

/**
 * @brief Initialize ADC X/Y and both push buttons.
 *
 * Called from main() before thread creation.
 */
int input_thread_init(void)
{
    int status;

    /* STEP 1: Check ADC and GPIO controllers. */
    if(!adc_is_ready_dt(&joystick_x) || !adc_is_ready_dt(&joystick_y))
    {
        printk("ERROR: Joystick ADC controller not ready\n");
        return -ENODEV;
    }

    if(!device_is_ready(gpio_dev) || !gpio_is_ready_dt(&prg_button))
    {
        printk("ERROR: Input GPIO controller not ready\n");
        return -ENODEV;
    }

    /* STEP 2: Configure joystick X-axis channel. */
    status = adc_channel_setup_dt(&joystick_x);

    if(status < 0)
    {
        printk("ERROR: Joystick X ADC setup: %d\n", status);
        return status;
    }

    /* STEP 3: Configure joystick Y-axis channel. */
    status = adc_channel_setup_dt(&joystick_y);

    if(status < 0)
    {
        printk("ERROR: Joystick Y ADC setup: %d\n", status);
        return status;
    }

    /* STEP 4: Configure onboard PRG button. */
    status = gpio_pin_configure_dt(&prg_button, GPIO_INPUT);

    if(status < 0)
    {
        printk("ERROR: PRG button setup: %d\n", status);
        return status;
    }

    /*
     * STEP 5: Configure joystick SW as active-low input.
     *
     * The joystick switch connects SW to GND when pressed.
     * The pull-up keeps the input HIGH when released.
     */
    status = gpio_pin_configure(gpio_dev, JOYSTICK_SW_PIN, GPIO_INPUT | GPIO_PULL_UP);

    if(status < 0)
    {
        printk("ERROR: Joystick SW setup: %d\n", status);
        return status;
    }

    printk("INPUT: Joystick and buttons initialized\n");

    return 0;
}

/* -------------------------------------------------------------------------- */
/* Input Thread                                                               */
/* -------------------------------------------------------------------------- */

/**
 * @brief Read joystick and button inputs periodically.
 *
 * The input thread is a producer.
 *
 * It sends UI events through app_event_queue and never
 * directly modifies the OLED framebuffer.
 */
void input_thread_entry(void* arg1, void* arg2, void* arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    bool x_armed = true;
    bool y_armed = true;

    int16_t x_value;
    int16_t y_value;

    printk("INPUT: Thread started\n");

    while(true)
    {
        int status;

        /* -------------------------------------------------------------- */
        /* STEP 1: Read joystick X-axis.                                   */
        /* -------------------------------------------------------------- */

        status = input_read_axis(&joystick_x, &x_value);

        if(status == 0)
        {
            input_process_x_axis(x_value, &x_armed);
        }
        else
        {
            printk("INPUT: X ADC read failed: %d\n", status);
        }

        /* -------------------------------------------------------------- */
        /* STEP 2: Read joystick Y-axis.                                   */
        /* -------------------------------------------------------------- */

        status = input_read_axis(&joystick_y, &y_value);

        if(status == 0)
        {
            input_process_y_axis(y_value, &y_armed);
        }
        else
        {
            printk("INPUT: Y ADC read failed: %d\n", status);
        }

        /* -------------------------------------------------------------- */
        /* STEP 3: Read onboard PRG button.                                */
        /* -------------------------------------------------------------- */

        int prg_pressed = gpio_pin_get_dt(&prg_button);

        /*
         * gpio_pin_get_dt() respects GPIO_ACTIVE_LOW,
         * so 1 means pressed.
         */
        if(prg_pressed >= 0)
        {
            if(input_button_pressed(&prg_state, prg_pressed != 0))
            {
                input_publish_event(APP_EVENT_TOGGLE_SCREEN, APP_EVENT_SOURCE_PRG);
            }
        }

        /* -------------------------------------------------------------- */
        /* STEP 4: Read joystick SW button.                                */
        /* -------------------------------------------------------------- */

        int sw_level = gpio_pin_get_raw(gpio_dev, JOYSTICK_SW_PIN);

        /*
         * SW is active LOW:
         *   0 = Pressed
         *   1 = Released
         */
        if(sw_level >= 0)
        {
            if(input_button_pressed(&joystick_sw_state, sw_level == 0))
            {
                input_publish_event(APP_EVENT_EMOJI_SELECT, APP_EVENT_SOURCE_JOYSTICK);
            }
        }

        /* -------------------------------------------------------------- */
        /* STEP 5: Sleep until next input scan.                            */
        /* -------------------------------------------------------------- */

        k_sleep(K_MSEC(INPUT_POLL_INTERVAL_MS));
    }
}
