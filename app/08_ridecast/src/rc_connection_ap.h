/* RideCast Wi-Fi access-point manager for Zephyr 4.4.
 * Portable application API; Zephyr's Wi-Fi driver owns the underlying radio.
 * SPDX-License-Identifier: MIT
 */
#ifndef RC_CONNECTION_AP_H_
#define RC_CONNECTION_AP_H_
#include <stdbool.h>
#include <stdint.h>
#include "rc_err_sts.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RC_AP_STOPPED = 0,
    RC_AP_STARTING,
    RC_AP_READY,
    RC_AP_ERROR,
} rc_ap_state_t;

typedef struct {
    const char *ssid;   /* 1..32 bytes, visible as Wi-Fi network name */
    const char *psk;    /* WPA2 PSK: 8..63 bytes; NEVER the 6-digit enrollment PIN */
    uint8_t channel;    /* 1..11 for this development demonstration */
} rc_conn_ap_config_t;

typedef struct {
    rc_ap_state_t state;
    rc_err_sts_t last_error;
    uint8_t associated_stations;  /* Wi-Fi links, NOT authenticated app sessions */
} rc_conn_ap_snapshot_t;

typedef void (*rc_conn_ap_event_cb_t)(const rc_conn_ap_snapshot_t *state, void *arg);

rc_err_sts_t rc_conn_ap_init(const rc_conn_ap_config_t *cfg,
                            rc_conn_ap_event_cb_t callback, void *arg);
rc_err_sts_t rc_conn_ap_start(void);
rc_err_sts_t rc_conn_ap_stop(void);
rc_err_sts_t rc_conn_ap_snapshot(rc_conn_ap_snapshot_t *out);
bool rc_conn_ap_is_ready(void);
const char *rc_conn_ap_state_name(rc_ap_state_t state);
#ifdef __cplusplus
}
#endif
#endif
