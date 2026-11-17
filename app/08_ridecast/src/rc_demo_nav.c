/* SPDX-License-Identifier: MIT */
#include "rc_demo_nav.h"
#include "ridecast_config.h"
#include <zephyr/kernel.h>
#include <string.h>
#include <stdio.h>

#define RC_NAV_EXPIRE_MS 8000
K_MUTEX_DEFINE(nav_lock);

struct nav_slot {
    heltec_nav_screen_t screen;
    int64_t last_update;
    bool have_update;
};
static struct nav_slot nav_slots[2]; /* [0] Wi-Fi, [1] BLE */

static int slot_index(rc_app_source_t source)
{
    if (source == RC_APP_FROM_WIFI) return 0;
    if (source == RC_APP_FROM_BLE) return 1;
    return -1;
}

rc_err_sts_t rc_demo_nav_receive_from(const char *text, rc_app_source_t source)
{
    if (!rc_transport_allows(source, RC_APP_NAV)) {
        return RC_ERR_CONTENT_CHANNEL_MISMATCH;
    }
    char direction[12] = {0};
    unsigned meters = 0, speed = 0, gps = 0;
    char extra = 0;
    if (text == NULL || strlen(text) > 85 ||
        sscanf(text, "NAV %11s %u %u %u %c", direction, &meters,
               &speed, &gps, &extra) != 4 ||
        meters > 999 || speed > 240 || gps > 1) {
        return RC_ERR_INVALID_ARG;
    }
    heltec_turn_t turn;
    if (strcmp(direction, "LEFT") == 0) turn = HELTEC_TURN_LEFT;
    else if (strcmp(direction, "RIGHT") == 0) turn = HELTEC_TURN_RIGHT;
    else if (strcmp(direction, "STRAIGHT") == 0) turn = HELTEC_TURN_STRAIGHT;
    else if (strcmp(direction, "ARRIVED") == 0) turn = HELTEC_TURN_ARRIVED;
    else return RC_ERR_INVALID_ARG;

    heltec_nav_screen_t incoming = { .turn = turn, .distance_m = (uint16_t)meters,
        .speed_kmh = (uint16_t)speed, .gps_valid = gps != 0, .link = HELTEC_LINK_ONLINE };
    rc_demo_nav_publish_from(&incoming, source);
    return RC_OK;
}

rc_err_sts_t rc_demo_nav_receive(const char *text)
{
    return rc_demo_nav_receive_from(text, RC_APP_FROM_WIFI);
}

void rc_demo_nav_publish_from(const heltec_nav_screen_t *screen,
                              rc_app_source_t source)
{
    const int i = slot_index(source);
    if (screen == NULL || i < 0 || !rc_transport_allows(source, RC_APP_NAV)) return;
    k_mutex_lock(&nav_lock, K_FOREVER);
    nav_slots[i].screen = *screen;
    nav_slots[i].last_update = k_uptime_get();
    nav_slots[i].have_update = true;
    k_mutex_unlock(&nav_lock);
}

void rc_demo_nav_publish(const heltec_nav_screen_t *screen)
{
    rc_demo_nav_publish_from(screen, RC_APP_FROM_WIFI);
}

bool rc_demo_nav_latest_authorized(heltec_nav_screen_t *out,
                                   bool wifi_authenticated,
                                   bool ble_authenticated)
{
    if (out == NULL) return false;
    k_mutex_lock(&nav_lock, K_FOREVER);
    const int64_t now = k_uptime_get();
    bool wifi_valid = wifi_authenticated && rc_transport_allows(RC_APP_FROM_WIFI, RC_APP_NAV) &&
                      nav_slots[0].have_update && now - nav_slots[0].last_update < RC_NAV_EXPIRE_MS;
    bool ble_valid = ble_authenticated && rc_transport_allows(RC_APP_FROM_BLE, RC_APP_NAV) &&
                     nav_slots[1].have_update && now - nav_slots[1].last_update < RC_NAV_EXPIRE_MS;
    int chosen = -1;
#if RC_NAV_PREFER_WIFI
    if (wifi_valid) chosen = 0;
    else if (ble_valid) chosen = 1;
#else
    if (ble_valid) chosen = 1;
    else if (wifi_valid) chosen = 0;
#endif
    if (chosen >= 0) *out = nav_slots[chosen].screen;
    k_mutex_unlock(&nav_lock);
    return chosen >= 0;
}

bool rc_demo_nav_latest(heltec_nav_screen_t *out)
{
    /* Legacy caller assumes both sessions valid; new UI uses authenticated API. */
    return rc_demo_nav_latest_authorized(out, true, true);
}

void rc_demo_nav_clear_source(rc_app_source_t source)
{
    const int i = slot_index(source);
    if (i < 0) return;
    k_mutex_lock(&nav_lock, K_FOREVER);
    nav_slots[i].have_update = false;
    k_mutex_unlock(&nav_lock);
}

void rc_demo_nav_clear(void)
{
    k_mutex_lock(&nav_lock, K_FOREVER);
    nav_slots[0].have_update = false;
    nav_slots[1].have_update = false;
    k_mutex_unlock(&nav_lock);
}
