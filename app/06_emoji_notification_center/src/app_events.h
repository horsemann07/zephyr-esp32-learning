/* ============================================================================
 * File        : app_events.h
 * Description : Shared application events and message queue declarations
 * Application : 06_emoji_notification_center
 * ========================================================================== */

#ifndef APP_EVENTS_H_
#define APP_EVENTS_H_

#include <zephyr/kernel.h>
#include <stdint.h>

/* -------------------------------------------------------------------------- */
/* Event Types                                                                */
/* -------------------------------------------------------------------------- */

enum app_event_type
{
    /* Onboard PRG button: toggle help / emoji browser. */
    APP_EVENT_TOGGLE_SCREEN,

    /* Joystick vertical movement: change category. */
    APP_EVENT_CATEGORY_PREVIOUS,
    APP_EVENT_CATEGORY_NEXT,

    /* Joystick horizontal movement: change emoji. */
    APP_EVENT_EMOJI_PREVIOUS,
    APP_EVENT_EMOJI_NEXT,

    /* Joystick SW button: select an emoji. */
    APP_EVENT_EMOJI_SELECT,

    /* Future: automatically generated notifications. */
    APP_EVENT_NOTIFICATION
};

/* -------------------------------------------------------------------------- */
/* Event Sources                                                              */
/* -------------------------------------------------------------------------- */

enum app_event_source
{
    APP_EVENT_SOURCE_PRG,
    APP_EVENT_SOURCE_JOYSTICK,
    APP_EVENT_SOURCE_SYSTEM
};

/* -------------------------------------------------------------------------- */
/* Message Structure                                                          */
/* -------------------------------------------------------------------------- */

/**
 * @brief Message exchanged between application threads.
 *
 * type      : What happened?
 * source    : Which component generated the event?
 * value     : Optional data, e.g. notification emoji ID.
 * timestamp : System uptime when the event was created.
 */
struct app_event
{
    enum app_event_type type;
    enum app_event_source source;

    int32_t value;
    uint32_t timestamp_ms;
};

/* -------------------------------------------------------------------------- */
/* Shared Zephyr Message Queue                                                */
/* -------------------------------------------------------------------------- */

/*
 * The queue is defined exactly once in main.c:
 *
 * K_MSGQ_DEFINE(app_event_queue,
 *               sizeof(struct app_event),
 *               16,
 *               4);
 *
 * All other source files access the same queue.
 */
extern struct k_msgq app_event_queue;

/* -------------------------------------------------------------------------- */
/* Input Thread Public Functions                                              */
/* -------------------------------------------------------------------------- */

/**
 * @brief Initialize the joystick ADC channels and buttons.
 *
 * Call once from main() before starting the input thread.
 */
int input_thread_init(void);

/**
 * @brief Input thread entry function.
 */
void input_thread_entry(void* arg1, void* arg2, void* arg3);

void display_thread_entry(void* arg1, void* arg2, void* arg3);

void notification_thread_entry(void* arg1, void* arg2, void* arg3);

#endif /* APP_EVENTS_H_ */