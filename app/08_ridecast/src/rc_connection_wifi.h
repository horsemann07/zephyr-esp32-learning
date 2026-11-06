/*
 * RideCast - portable logical Wi-Fi connection manager API, Zephyr backend.
 * Initial profile: Station (STA) on a WPA2-PSK 2.4 GHz network.
 * STM32H7B3I-DK Inventek now; TI CC3351 later through Zephyr net_mgmt.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RC_CONNECTION_WIFI_H_
#define RC_CONNECTION_WIFI_H_

#include <stdbool.h>
#include <stdint.h>
#include "rc_err_sts.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RC_CONN_WIFI_SSID_CAPACITY 33u
#define RC_CONN_WIFI_PSK_CAPACITY 65u

typedef enum {
    RC_CONN_WIFI_STOPPED = 0,
    RC_CONN_WIFI_CONNECTING,
    RC_CONN_WIFI_CONNECTED,
    RC_CONN_WIFI_RETRY_WAIT,
    RC_CONN_WIFI_ERROR,
} rc_conn_wifi_state_t;

typedef struct {
    /* Null-terminated SSID (1..32 bytes) and WPA2 PSK (8..63 bytes).
     * For open test access points, set open_network=true and psk="".
     * Values are copied on init; not written to flash.
     */
    const char *ssid;
    const char *psk;
    bool open_network;
    uint32_t retry_delay_ms;       /* 0 => 3000 ms */
    uint32_t connect_timeout_ms;   /* 0 => 15000 ms */
    uint8_t max_attempts;          /* 0 => retries forever */
} rc_conn_wifi_config_t;

typedef struct {
    rc_conn_wifi_state_t state;
    rc_err_sts_t last_error;
    uint32_t attempts;
    bool requested_on;
    bool associated;
} rc_conn_wifi_snapshot_t;

/* Callback executes in Zephyr networking/workqueue context.
 * Keep it short; do not call blocking stop/reconfigure from this callback.
 */
typedef void (*rc_conn_wifi_event_cb_t)(const rc_conn_wifi_snapshot_t *snapshot,
                                        void *user);

rc_err_sts_t rc_conn_wifi_init(const rc_conn_wifi_config_t *config,
                               rc_conn_wifi_event_cb_t callback, void *user);
rc_err_sts_t rc_conn_wifi_start(void);
rc_err_sts_t rc_conn_wifi_stop(void);
rc_err_sts_t rc_conn_wifi_get_snapshot(rc_conn_wifi_snapshot_t *out);
bool rc_conn_wifi_is_connected(void);
const char *rc_conn_wifi_state_name(rc_conn_wifi_state_t state);

#ifdef __cplusplus
}
#endif
#endif
