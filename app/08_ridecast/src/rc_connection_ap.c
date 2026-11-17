/* RideCast — Zephyr 4.4 SoftAP + DHCP server backend.
 * Follows Zephyr's official samples/net/wifi/apsta_mode AP setup.
 * SPDX-License-Identifier: MIT
 */
#include "rc_connection_ap.h"
#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/dhcpv4_server.h>
#include <zephyr/sys/printk.h>
#include <string.h>

#define AP_ADDR "192.168.4.1"
#define AP_NETMASK "255.255.255.0"
#define AP_DHCP_FIRST "192.168.4.10"
#define AP_EVENTS (NET_EVENT_WIFI_AP_ENABLE_RESULT | NET_EVENT_WIFI_AP_DISABLE_RESULT | \
                   NET_EVENT_WIFI_AP_STA_CONNECTED | NET_EVENT_WIFI_AP_STA_DISCONNECTED)

K_MUTEX_DEFINE(ap_lock);
static struct net_mgmt_event_callback ap_events;
static struct net_if *ap_iface;
static struct wifi_connect_req_params ap_params;
static char ap_ssid[33];
static char ap_psk[64];
static rc_conn_ap_snapshot_t ap_state = { .state = RC_AP_STOPPED };
static rc_conn_ap_event_cb_t notify;
static void *notify_arg;
static bool initialized;
static bool dhcp_running;
static bool ip_configured;

static void publish(rc_ap_state_t s, rc_err_sts_t error, int station_delta)
{
    rc_conn_ap_event_cb_t callback;
    void *arg;
    rc_conn_ap_snapshot_t snap;
    k_mutex_lock(&ap_lock, K_FOREVER);
    ap_state.state = s;
    ap_state.last_error = error;
    if (station_delta > 0 && ap_state.associated_stations < UINT8_MAX) {
        ap_state.associated_stations++;
    } else if (station_delta < 0 && ap_state.associated_stations > 0) {
        ap_state.associated_stations--;
    }
    if (s == RC_AP_STOPPED || s == RC_AP_ERROR) {
        ap_state.associated_stations = 0;
    }
    snap = ap_state;
    callback = notify;
    arg = notify_arg;
    k_mutex_unlock(&ap_lock);
    if (callback != NULL) {
        callback(&snap, arg);
    }
}

static void ap_event_handler(struct net_mgmt_event_callback *cb, uint64_t event,
                             struct net_if *iface)
{
    if (iface != ap_iface) {
        return;
    }
    switch (event) {
    case NET_EVENT_WIFI_AP_ENABLE_RESULT: {
        /* This event carries Zephyr struct wifi_status. */
        const struct wifi_status *result = cb->info;
        if (result == NULL || result->ap_status != WIFI_STATUS_AP_SUCCESS) {
            printk("[RC AP] Enable failed status=%d\n", result ? (int)result->ap_status : -1);
            publish(RC_AP_ERROR, RC_ERR_NETWORK_UNAVAILABLE, 0);
        } else {
            printk("[RC AP] ON: SSID=%s IP=%s\n", ap_ssid, AP_ADDR);
            publish(RC_AP_READY, RC_OK, 0);
        }
        break;
    }
    case NET_EVENT_WIFI_AP_DISABLE_RESULT:
        publish(RC_AP_STOPPED, RC_OK, 0);
        break;
    case NET_EVENT_WIFI_AP_STA_CONNECTED: {
        printk("[RC AP] Phone Wi-Fi joined (not app-authorized)\n");
        rc_conn_ap_snapshot_t current;
        (void)rc_conn_ap_snapshot(&current);
        publish(current.state, current.last_error, +1);
        break;
    }
    case NET_EVENT_WIFI_AP_STA_DISCONNECTED: {
        printk("[RC AP] Phone Wi-Fi left\n");
        rc_conn_ap_snapshot_t current;
        (void)rc_conn_ap_snapshot(&current);
        publish(current.state, current.last_error, -1);
        break;
    }
    default:
        break;
    }
}

static rc_err_sts_t setup_ipv4_dhcp(void)
{
    struct net_in_addr ip, mask, first;
    if (net_addr_pton(NET_AF_INET, AP_ADDR, &ip) != 0 ||
        net_addr_pton(NET_AF_INET, AP_NETMASK, &mask) != 0 ||
        net_addr_pton(NET_AF_INET, AP_DHCP_FIRST, &first) != 0) {
        return RC_ERR_NETWORK_CONFIG_INVALID;
    }
    if (!ip_configured) {
        net_if_ipv4_set_gw(ap_iface, &ip);
        if (net_if_ipv4_addr_add(ap_iface, &ip, NET_ADDR_MANUAL, 0) == NULL) {
            return RC_ERR_NETWORK_CONFIG_INVALID;
        }
        if (!net_if_ipv4_set_netmask_by_addr(ap_iface, &ip, &mask)) {
            return RC_ERR_NETWORK_CONFIG_INVALID;
        }
        ip_configured = true;
    }
    if (!dhcp_running) {
        int err = net_dhcpv4_server_start(ap_iface, &first);
        if (err != 0) {
            printk("[RC AP] DHCP start failed: %d\n", err);
            return RC_ERR_NETWORK_UNAVAILABLE;
        }
        dhcp_running = true;
        printk("[RC AP] DHCP pool starts at %s\n", AP_DHCP_FIRST);
    }
    return RC_OK;
}

rc_err_sts_t rc_conn_ap_init(const rc_conn_ap_config_t *cfg,
                            rc_conn_ap_event_cb_t cb, void *arg)
{
    if (cfg == NULL || cfg->ssid == NULL || cfg->psk == NULL) {
        return RC_ERR_INVALID_ARG;
    }
    const size_t ssid_len = strlen(cfg->ssid);
    const size_t psk_len = strlen(cfg->psk);
    if (ssid_len < 1 || ssid_len > 32 || psk_len < 8 || psk_len > 63 ||
        cfg->channel < 1 || cfg->channel > 11) {
        return RC_ERR_NETWORK_CONFIG_INVALID;
    }
    if (initialized) {
        return RC_ERR_ALREADY_INITIALIZED;
    }
    ap_iface = net_if_get_wifi_sap();
    if (ap_iface == NULL) {
        printk("[RC AP] No Wi-Fi AP interface found\n");
        return RC_ERR_NETWORK_UNAVAILABLE;
    }
    memcpy(ap_ssid, cfg->ssid, ssid_len + 1);
    memcpy(ap_psk, cfg->psk, psk_len + 1);
    memset(&ap_params, 0, sizeof(ap_params));
    ap_params.ssid = (const uint8_t *)ap_ssid;
    ap_params.ssid_length = ssid_len;
    ap_params.psk = (const uint8_t *)ap_psk;
    ap_params.psk_length = psk_len;
    ap_params.security = WIFI_SECURITY_TYPE_PSK;
    ap_params.channel = cfg->channel;
    ap_params.band = WIFI_FREQ_BAND_2_4_GHZ;
    notify = cb;
    notify_arg = arg;
    net_mgmt_init_event_callback(&ap_events, ap_event_handler, AP_EVENTS);
    net_mgmt_add_event_callback(&ap_events);
    initialized = true;
    return RC_OK;
}

rc_err_sts_t rc_conn_ap_start(void)
{
    if (!initialized) {
        return RC_ERR_NOT_INITIALIZED;
    }
    if (rc_conn_ap_is_ready()) {
        return RC_OK;
    }
    rc_err_sts_t r = setup_ipv4_dhcp();
    if (r != RC_OK) {
        publish(RC_AP_ERROR, r, 0);
        return r;
    }
    publish(RC_AP_STARTING, RC_OK, 0);
    printk("[RC AP] Starting SSID=%s, IP=%s\n", ap_ssid, AP_ADDR);
    int err = net_mgmt(NET_REQUEST_WIFI_AP_ENABLE, ap_iface, &ap_params,
                       sizeof(ap_params));
    if (err != 0) {
        printk("[RC AP] NET_REQUEST_WIFI_AP_ENABLE: %d\n", err);
        publish(RC_AP_ERROR, RC_ERR_NETWORK_UNAVAILABLE, 0);
        return RC_ERR_NETWORK_UNAVAILABLE;
    }
    return RC_OK; /* Actual readiness is reported by AP_ENABLE_RESULT event. */
}

rc_err_sts_t rc_conn_ap_stop(void)
{
    if (!initialized) {
        return RC_ERR_NOT_INITIALIZED;
    }
    const int err = net_mgmt(NET_REQUEST_WIFI_AP_DISABLE, ap_iface, NULL, 0);
    if (err != 0) {
        return RC_ERR_IO;
    }
    if (dhcp_running) {
        (void)net_dhcpv4_server_stop(ap_iface);
        dhcp_running = false;
    }
    publish(RC_AP_STOPPED, RC_OK, 0);
    return RC_OK;
}

rc_err_sts_t rc_conn_ap_snapshot(rc_conn_ap_snapshot_t *out)
{
    if (out == NULL) {
        return RC_ERR_INVALID_ARG;
    }
    k_mutex_lock(&ap_lock, K_FOREVER);
    *out = ap_state;
    k_mutex_unlock(&ap_lock);
    return RC_OK;
}

bool rc_conn_ap_is_ready(void)
{
    rc_conn_ap_snapshot_t current;
    (void)rc_conn_ap_snapshot(&current);
    return current.state == RC_AP_READY;
}

const char *rc_conn_ap_state_name(rc_ap_state_t state)
{
    switch (state) {
    case RC_AP_STOPPED: return "STOPPED";
    case RC_AP_STARTING: return "STARTING";
    case RC_AP_READY: return "READY";
    case RC_AP_ERROR: return "ERROR";
    default: return "UNKNOWN";
    }
}
