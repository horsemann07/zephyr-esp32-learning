/* SPDX-License-Identifier: Apache-2.0 */
/* KY-023 joystick + onboard PRG GPIO0. One event per deflection/press. */
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

#define USER_NODE DT_PATH(zephyr_user)
#define PRG_NODE DT_ALIAS(sw0)
#define GPIO_NODE DT_NODELABEL(gpio0)

#if !DT_NODE_EXISTS(USER_NODE)
#error "app.overlay must declare zephyr,user { io-channels = <&adc0 0>, <&adc0 3>; };"
#endif
#if !DT_NODE_EXISTS(PRG_NODE)
#error "Board requires sw0 (onboard PRG button) alias"
#endif

/* Connections: VRx=GPIO36 / ADC1_CH0, VRy=GPIO39 / ADC1_CH3, SW=GPIO13. */
#define JOY_SW_PIN 13
#define POLL_MS 50
#define BUTTON_STABLE_SAMPLES 3
#define JOY_LOW 1100
#define JOY_HIGH 3200
#define JOY_CENTER_MIN 1500
#define JOY_CENTER_MAX 2700

/* Change to 1 if your joystick is mounted in the opposite orientation. */
#define JOY_REVERSE_X 0
#define JOY_REVERSE_Y 0

static const struct adc_dt_spec x_axis = ADC_DT_SPEC_GET_BY_IDX(USER_NODE, 0);
static const struct adc_dt_spec y_axis = ADC_DT_SPEC_GET_BY_IDX(USER_NODE, 1);
static const struct gpio_dt_spec prg = GPIO_DT_SPEC_GET(PRG_NODE, gpios);
static const struct device *const port0 = DEVICE_DT_GET(GPIO_NODE);

struct key_debounce {
    bool last_sample;
    bool pressed;
    uint8_t stable_count;
};

static struct key_debounce prg_key;
static struct key_debounce sw_key;

static void send_event(enum app_event_type type)
{
    struct app_event msg = { .type = type, .timestamp_ms = k_uptime_get_32() };
    if (k_msgq_put(&app_event_queue, &msg, K_NO_WAIT) != 0) {
        printk("INPUT: queue full, event %d dropped\n", (int)type);
    }
}

/* True only on the debounced RELEASED->PRESSED transition. */
static bool just_pressed(struct key_debounce *key, bool pressed)
{
    if (key->last_sample != pressed) {
        key->last_sample = pressed;
        key->stable_count = 1;
    } else if (key->stable_count < BUTTON_STABLE_SAMPLES) {
        key->stable_count++;
    }
    if (key->stable_count >= BUTTON_STABLE_SAMPLES && key->pressed != pressed) {
        key->pressed = pressed;
        return pressed;
    }
    return false;
}

static int read_adc(const struct adc_dt_spec *channel, int16_t *result)
{
    int16_t raw = 0;
    struct adc_sequence sequence = {
        .buffer = &raw,
        .buffer_size = sizeof(raw),
    };
    int rc = adc_sequence_init_dt(channel, &sequence);
    if (rc != 0) { return rc; }
    rc = adc_read_dt(channel, &sequence);
    if (rc == 0) { *result = raw; }
    return rc;
}

int input_thread_init(void)
{
    if (!adc_is_ready_dt(&x_axis) || !adc_is_ready_dt(&y_axis) ||
        !gpio_is_ready_dt(&prg) || !device_is_ready(port0)) {
        return -ENODEV;
    }
    int rc = adc_channel_setup_dt(&x_axis);
    if (rc != 0) { return rc; }
    rc = adc_channel_setup_dt(&y_axis);
    if (rc != 0) { return rc; }
    rc = gpio_pin_configure_dt(&prg, GPIO_INPUT);
    if (rc != 0) { return rc; }
    rc = gpio_pin_configure(port0, JOY_SW_PIN, GPIO_INPUT | GPIO_PULL_UP);
    if (rc != 0) { return rc; }
    printk("INPUT: joystick ADC and SW, PRG ready\n");
    return 0;
}

void input_thread_entry(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    bool joystick_armed = true;
    int16_t x, y;

    while (true) {
        if (read_adc(&x_axis, &x) == 0 && read_adc(&y_axis, &y) == 0) {
            /* A shared latch rejects diagonal double-events. */
            bool centered = (x >= JOY_CENTER_MIN && x <= JOY_CENTER_MAX &&
                             y >= JOY_CENTER_MIN && y <= JOY_CENTER_MAX);
            if (centered) {
                joystick_armed = true;
            } else if (joystick_armed) {
                enum app_event_type type;
                bool has_event = true;
                int dx = (x > 2048) ? (x - 2048) : (2048 - x);
                int dy = (y > 2048) ? (y - 2048) : (2048 - y);
                if (dx >= dy && (x < JOY_LOW || x > JOY_HIGH)) {
                    bool right = (x > JOY_HIGH);
                    if (JOY_REVERSE_X) { right = !right; }
                    type = right ? APP_EVENT_SCENE_NEXT : APP_EVENT_SCENE_PREVIOUS;
                } else if (y < JOY_LOW || y > JOY_HIGH) {
                    bool down = (y > JOY_HIGH);
                    if (JOY_REVERSE_Y) { down = !down; }
                    type = down ? APP_EVENT_CATEGORY_NEXT : APP_EVENT_CATEGORY_PREVIOUS;
                } else {
                    has_event = false;
                }
                if (has_event) {
                    send_event(type);
                    joystick_armed = false;
                }
            }
        }

        int p = gpio_pin_get_dt(&prg);
        /* GPIO_ACTIVE_LOW in board DTS: logical 1 means pressed. */
        if (p >= 0 && just_pressed(&prg_key, p != 0)) {
            send_event(APP_EVENT_TOGGLE_HELP);
        }

        int s = gpio_pin_get_raw(port0, JOY_SW_PIN);
        /* Physical KY-023 switch connects SW to GND on press. */
        if (s >= 0 && just_pressed(&sw_key, s == 0)) {
            send_event(APP_EVENT_SELECT);
        }
        k_sleep(K_MSEC(POLL_MS));
    }
}
