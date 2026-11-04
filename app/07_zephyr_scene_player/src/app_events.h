/* SPDX-License-Identifier: Apache-2.0 */
#ifndef APP_EVENTS_H_
#define APP_EVENTS_H_

#include <zephyr/kernel.h>

/* Producer: input_thread.c; consumer: display_thread.c. */
enum app_event_type
{
    APP_EVENT_TOGGLE_HELP,       /* Onboard PRG */
    APP_EVENT_CATEGORY_PREVIOUS, /* Joystick up */
    APP_EVENT_CATEGORY_NEXT,     /* Joystick down */
    APP_EVENT_SCENE_PREVIOUS,    /* Joystick left */
    APP_EVENT_SCENE_NEXT,        /* Joystick right */
    APP_EVENT_SELECT,            /* Joystick SW: play / pause / resume */
};

struct app_event
{
    enum app_event_type type;
    uint32_t timestamp_ms;
};

/* Created exactly once in main.c using K_MSGQ_DEFINE(). */
extern struct k_msgq app_event_queue;

int input_thread_init(void);
void input_thread_entry(void* arg1, void* arg2, void* arg3);
void display_thread_entry(void* arg1, void* arg2, void* arg3);

#endif /* APP_EVENTS_H_ */
