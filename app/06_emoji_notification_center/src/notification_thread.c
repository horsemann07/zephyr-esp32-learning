/* ============================================================================
 * File        : notification_thread.c
 * Description : Simulated system notifications sent to the OLED UI queue
 * Application : 06_emoji_notification_center
 * ============================================================================
 *
 * No physical battery, network, or weather sensor is read here.
 * This is a message-queue PRODUCER for learning asynchronous UI events.
 * ========================================================================== */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include <stdint.h>

#include "app_events.h"
#include "emoji_draw.h"

#define NOTIFICATION_INTERVAL_SECONDS 15

struct demo_notification
{
    enum emoji_type icon;
    const char* description;
};

static const struct demo_notification notifications[] = {
    { EMOJI_BELL, "New alert" },
    { EMOJI_WIFI, "Wi-Fi demo" },
    { EMOJI_BATTERY, "Battery demo" },
    { EMOJI_RAIN, "Rain demo" },
    { EMOJI_HEART, "Heart demo" }
};

void notification_thread_entry(void* arg1, void* arg2, void* arg3)
{
    size_t index = 0U;

    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    printk("NOTIFY: simulation thread started\n");

    while(true)
    {
        struct app_event event;
        int rc;

        k_sleep(K_SECONDS(NOTIFICATION_INTERVAL_SECONDS));

        event.type         = APP_EVENT_NOTIFICATION;
        event.source       = APP_EVENT_SOURCE_SYSTEM;
        event.value        = (int32_t)notifications[index].icon;
        event.timestamp_ms = k_uptime_get_32();

        rc = k_msgq_put(&app_event_queue, &event, K_NO_WAIT);
        if(rc == 0)
        {
            printk("NOTIFY: queued %s\n", notifications[index].description);
        }
        else
        {
            printk("NOTIFY: queue full, notification dropped (%d)\n", rc);
        }

        index = (index + 1U) % ARRAY_SIZE(notifications);
    }
}
