/* RideCast - Wi-Fi station manager using Zephyr network management API.
 * No Inventek or TI driver includes: switching radio doesn't change this code.
 * SPDX-License-Identifier: MIT
 */
#include "rc_connection_wifi.h"

#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(rc_wifi_connection, LOG_LEVEL_INF);

#define RC_WIFI_RETRY_DEFAULT_MS   3000u
#define RC_WIFI_CONNECT_DEFAULT_MS 15000u

K_MUTEX_DEFINE(rc_wifi_lock);
static struct net_mgmt_event_callback rc_wifi_mgmt_cb;
static struct k_work_delayable rc_wifi_attempt_work;
static struct k_work_delayable rc_wifi_timeout_work;
static struct net_if* rc_wifi_iface;
static char rc_wifi_ssid[RC_CONN_WIFI_SSID_CAPACITY];
static char rc_wifi_psk[RC_CONN_WIFI_PSK_CAPACITY];
static struct wifi_connect_req_params rc_wifi_req;
static rc_conn_wifi_event_cb_t rc_wifi_callback;
static void* rc_wifi_user;
static rc_conn_wifi_snapshot_t rc_wifi_snapshot;
static uint32_t rc_wifi_retry_delay;
static uint32_t rc_wifi_connect_timeout;
static uint8_t rc_wifi_max_attempts;
static bool rc_wifi_initialized;

static size_t bounded_len(const char* s, size_t max)
{
    size_t i;
    for(i = 0; i < max && s[i] != '\0'; ++i)
    {
    }
    return i;
}

static void rc_wifi_publish(rc_conn_wifi_state_t state, rc_err_sts_t err)
{
    rc_conn_wifi_snapshot_t snap;
    rc_conn_wifi_event_cb_t cb;
    void* user;

    k_mutex_lock(&rc_wifi_lock, K_FOREVER);
    rc_wifi_snapshot.state      = state;
    rc_wifi_snapshot.last_error = err;
    rc_wifi_snapshot.associated = (state == RC_CONN_WIFI_CONNECTED);
    snap                        = rc_wifi_snapshot;
    cb                          = rc_wifi_callback;
    user                        = rc_wifi_user;
    k_mutex_unlock(&rc_wifi_lock);

    LOG_INF("Wi-Fi state=%s status=%d attempt=%u", rc_conn_wifi_state_name(state),
            (int)err, (unsigned int)snap.attempts);
    if(cb != NULL)
    {
        cb(&snap, user);
    }
}

static void rc_wifi_retry(rc_err_sts_t reason)
{
    bool enabled;
    bool exhausted;
    uint32_t delay;

    k_mutex_lock(&rc_wifi_lock, K_FOREVER);
    enabled = rc_wifi_snapshot.requested_on;
    exhausted = (rc_wifi_max_attempts != 0u && rc_wifi_snapshot.attempts >= rc_wifi_max_attempts);
    delay = rc_wifi_retry_delay;
    k_mutex_unlock(&rc_wifi_lock);

    if(!enabled)
    {
        return;
    }
    if(exhausted)
    {
        rc_wifi_publish(RC_CONN_WIFI_ERROR, RC_ERR_RETRY_LIMIT);
        return;
    }
    rc_wifi_publish(RC_CONN_WIFI_RETRY_WAIT, reason);
    (void)k_work_reschedule(&rc_wifi_attempt_work, K_MSEC(delay));
}

static void rc_wifi_timeout_handler(struct k_work* work)
{
    ARG_UNUSED(work);
    k_mutex_lock(&rc_wifi_lock, K_FOREVER);
    bool waiting = rc_wifi_snapshot.requested_on &&
                   rc_wifi_snapshot.state == RC_CONN_WIFI_CONNECTING;
    k_mutex_unlock(&rc_wifi_lock);
    if(waiting)
    {
        LOG_WRN("Wi-Fi connect result timed out");
        /* Clear any pending association before the next attempt. */
        (void)net_mgmt(NET_REQUEST_WIFI_DISCONNECT, rc_wifi_iface, NULL, 0);
        rc_wifi_retry(RC_ERR_CONNECTION_TIMEOUT);
    }
}

static void rc_wifi_attempt_handler(struct k_work* work)
{
    ARG_UNUSED(work);
    k_mutex_lock(&rc_wifi_lock, K_FOREVER);
    if(!rc_wifi_snapshot.requested_on)
    {
        k_mutex_unlock(&rc_wifi_lock);
        return;
    }
    rc_wifi_snapshot.attempts++;
    k_mutex_unlock(&rc_wifi_lock);

    rc_wifi_publish(RC_CONN_WIFI_CONNECTING, RC_OK);
    (void)k_work_reschedule(&rc_wifi_timeout_work, K_MSEC(rc_wifi_connect_timeout));

    int ret = net_mgmt(NET_REQUEST_WIFI_CONNECT, rc_wifi_iface, &rc_wifi_req,
                       sizeof(rc_wifi_req));
    if(ret < 0)
    {
        LOG_WRN("Wi-Fi connect request rejected: %d", ret);
        (void)k_work_cancel_delayable(&rc_wifi_timeout_work);
        rc_wifi_retry(RC_ERR_CONNECTION_FAILED);
    }
}

static void rc_wifi_event_handler(struct net_mgmt_event_callback* cb,
                                  uint64_t event,
                                  struct net_if* iface)
{
    if(iface != rc_wifi_iface)
    {
        return;
    }
    k_mutex_lock(&rc_wifi_lock, K_FOREVER);
    bool enabled    = rc_wifi_snapshot.requested_on;
    bool connecting = (rc_wifi_snapshot.state == RC_CONN_WIFI_CONNECTING);
    k_mutex_unlock(&rc_wifi_lock);
    if(!enabled)
    {
        return;
    }

    if(event == NET_EVENT_WIFI_CONNECT_RESULT)
    {
        if(!connecting)
        {
            return; /* stale result from a timed-out previous attempt */
        }
        const struct wifi_status* status = (const struct wifi_status*)cb->info;
        (void)k_work_cancel_delayable(&rc_wifi_timeout_work);
        if(status != NULL && status->status == 0)
        {
            rc_wifi_publish(RC_CONN_WIFI_CONNECTED, RC_OK);
        }
        else
        {
            rc_wifi_retry(RC_ERR_CONNECTION_FAILED);
        }
    }
    else if(event == NET_EVENT_WIFI_DISCONNECT_RESULT)
    {
        (void)k_work_cancel_delayable(&rc_wifi_timeout_work);
        /* Some drivers emit DISCONNECT_RESULT for a failed cleanup attempt.
         * Only reattempt when we were actually associated.
         */
        k_mutex_lock(&rc_wifi_lock, K_FOREVER);
        bool was_connected = (rc_wifi_snapshot.state == RC_CONN_WIFI_CONNECTED);
        k_mutex_unlock(&rc_wifi_lock);
        if(was_connected)
        {
            rc_wifi_retry(RC_ERR_CONNECTION_LOST);
        }
    }
}

rc_err_sts_t rc_conn_wifi_init(const rc_conn_wifi_config_t* config,
                               rc_conn_wifi_event_cb_t callback,
                               void* user)
{
    if(config == NULL || config->ssid == NULL || config->psk == NULL)
    {
        return RC_ERR_INVALID_ARG;
    }
    size_t ssid_len = bounded_len(config->ssid, sizeof(rc_wifi_ssid));
    size_t psk_len  = bounded_len(config->psk, sizeof(rc_wifi_psk));
    if(ssid_len == 0u || ssid_len >= sizeof(rc_wifi_ssid) || psk_len >= sizeof(rc_wifi_psk) ||
       (config->open_network ? (psk_len != 0u) : (psk_len < 8u || psk_len > 63u)))
    {
        return RC_ERR_NETWORK_CONFIG_INVALID;
    }
    k_mutex_lock(&rc_wifi_lock, K_FOREVER);
    if(rc_wifi_initialized)
    {
        k_mutex_unlock(&rc_wifi_lock);
        return RC_ERR_ALREADY_INITIALIZED;
    }
    rc_wifi_iface = net_if_get_wifi_sta();
    if(rc_wifi_iface == NULL)
    {
        rc_wifi_iface = net_if_get_first_wifi();
    }
    if(rc_wifi_iface == NULL)
    {
        k_mutex_unlock(&rc_wifi_lock);
        return RC_ERR_WIFI_UNAVAILABLE;
    }
    memcpy(rc_wifi_ssid, config->ssid, ssid_len + 1u);
    memcpy(rc_wifi_psk, config->psk, psk_len + 1u);
    memset(&rc_wifi_req, 0, sizeof(rc_wifi_req));
    rc_wifi_req.ssid        = (const uint8_t*)rc_wifi_ssid;
    rc_wifi_req.ssid_length = ssid_len;
    rc_wifi_req.psk         = (const uint8_t*)rc_wifi_psk;
    rc_wifi_req.psk_length  = psk_len;
    rc_wifi_req.security = config->open_network ? WIFI_SECURITY_TYPE_NONE : WIFI_SECURITY_TYPE_PSK;
    rc_wifi_req.channel = WIFI_CHANNEL_ANY;
    rc_wifi_req.band    = WIFI_FREQ_BAND_2_4_GHZ;
    rc_wifi_retry_delay =
    config->retry_delay_ms ? config->retry_delay_ms : RC_WIFI_RETRY_DEFAULT_MS;
    rc_wifi_connect_timeout =
    config->connect_timeout_ms ? config->connect_timeout_ms : RC_WIFI_CONNECT_DEFAULT_MS;
    rc_wifi_max_attempts = config->max_attempts;
    rc_wifi_callback     = callback;
    rc_wifi_user         = user;
    rc_wifi_snapshot =
    (rc_conn_wifi_snapshot_t){ .state = RC_CONN_WIFI_STOPPED, .last_error = RC_OK };
    k_work_init_delayable(&rc_wifi_attempt_work, rc_wifi_attempt_handler);
    k_work_init_delayable(&rc_wifi_timeout_work, rc_wifi_timeout_handler);
    net_mgmt_init_event_callback(&rc_wifi_mgmt_cb, rc_wifi_event_handler,
                                 NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT);
    net_mgmt_add_event_callback(&rc_wifi_mgmt_cb);
    rc_wifi_initialized = true;
    k_mutex_unlock(&rc_wifi_lock);
    return RC_OK;
}

rc_err_sts_t rc_conn_wifi_start(void)
{
    k_mutex_lock(&rc_wifi_lock, K_FOREVER);
    if(!rc_wifi_initialized)
    {
        k_mutex_unlock(&rc_wifi_lock);
        return RC_ERR_NOT_INITIALIZED;
    }
    if(rc_wifi_snapshot.requested_on)
    {
        k_mutex_unlock(&rc_wifi_lock);
        return RC_ERR_BUSY;
    }
    rc_wifi_snapshot.requested_on = true;
    rc_wifi_snapshot.attempts     = 0u;
    k_mutex_unlock(&rc_wifi_lock);
    (void)k_work_reschedule(&rc_wifi_attempt_work, K_NO_WAIT);
    return RC_OK;
}

rc_err_sts_t rc_conn_wifi_stop(void)
{
    k_mutex_lock(&rc_wifi_lock, K_FOREVER);
    if(!rc_wifi_initialized)
    {
        k_mutex_unlock(&rc_wifi_lock);
        return RC_ERR_NOT_INITIALIZED;
    }
    rc_wifi_snapshot.requested_on = false;
    k_mutex_unlock(&rc_wifi_lock);
    (void)k_work_cancel_delayable(&rc_wifi_attempt_work);
    (void)k_work_cancel_delayable(&rc_wifi_timeout_work);
    int ret = net_mgmt(NET_REQUEST_WIFI_DISCONNECT, rc_wifi_iface, NULL, 0);
    rc_wifi_publish(RC_CONN_WIFI_STOPPED, RC_OK);
    /* Some drivers report 'already disconnected' as an error. */
    if(ret < 0 && ret != -ENOTCONN && ret != -EALREADY)
    {
        return RC_ERR_RADIO_STOP_FAILED;
    }
    return RC_OK;
}

rc_err_sts_t rc_conn_wifi_get_snapshot(rc_conn_wifi_snapshot_t* out)
{
    if(out == NULL)
    {
        return RC_ERR_INVALID_ARG;
    }
    k_mutex_lock(&rc_wifi_lock, K_FOREVER);
    if(!rc_wifi_initialized)
    {
        k_mutex_unlock(&rc_wifi_lock);
        return RC_ERR_NOT_INITIALIZED;
    }
    *out = rc_wifi_snapshot;
    k_mutex_unlock(&rc_wifi_lock);
    return RC_OK;
}

bool rc_conn_wifi_is_connected(void)
{
    bool yes;
    k_mutex_lock(&rc_wifi_lock, K_FOREVER);
    yes = rc_wifi_initialized && rc_wifi_snapshot.associated;
    k_mutex_unlock(&rc_wifi_lock);
    return yes;
}

const char* rc_conn_wifi_state_name(rc_conn_wifi_state_t state)
{
    switch(state)
    {
        case RC_CONN_WIFI_STOPPED:
            return "STOPPED";
        case RC_CONN_WIFI_CONNECTING:
            return "CONNECTING";
        case RC_CONN_WIFI_CONNECTED:
            return "CONNECTED";
        case RC_CONN_WIFI_RETRY_WAIT:
            return "RETRY_WAIT";
        case RC_CONN_WIFI_ERROR:
            return "ERROR";
        default:
            return "UNKNOWN";
    }
}
