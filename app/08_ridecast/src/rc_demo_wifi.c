/* Wi-Fi AP + TCP prototype; port 5050 is unencrypted. Development only. */
#include "rc_demo_wifi.h"
#include "rc_demo_manager.h"
#include "rc_connection_ap.h"
#include "rc_connection_tcp.h"
#include "rc_demo_nav.h"
#include "rc_app_message.h"
#include "rc_h7_bridge.h"
#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/printk.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define RC_TCP_PORT 5050U
#define RC_BEACON_PORT 39801U
static bool peer_accepted;
static bool peer_had_station;
static char rx_line[RC_JSON_LINE_MAX];
static size_t rx_used;
static bool dropping;

static void reply(const char *s)
{
    if (rc_conn_tcp_send((const uint8_t *)s, strlen(s)) != RC_OK) {
        /* The connection is no longer usable: let the TCP worker clean up. */
        (void)rc_conn_tcp_disconnect_peer();
    }
}

static bool unhex16(const char *s, uint8_t out[16])
{
    if (strlen(s) != 32) return false;
    for (size_t i = 0; i < 16; ++i) {
        unsigned v = 0U;
        for (unsigned j = 0; j < 2; ++j) {
            char c = s[i * 2U + j];
            unsigned n;
            if (c >= '0' && c <= '9') n = (unsigned)(c - '0');
            else if (c >= 'a' && c <= 'f') n = (unsigned)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') n = (unsigned)(c - 'A' + 10);
            else return false;
            v = (v << 4) | n;
        }
        out[i] = (uint8_t)v;
    }
    return true;
}

static void dispatch(char *line)
{
    if (!peer_accepted) {
        reply("ERR BUSY\n");
        return;
    }
    rc_demo_snapshot_t s = rc_demo_manager_get();
    if (s.wifi_authenticated) {
        if (strcmp(line, "DISCONNECT") == 0) {
            reply("OK BYE\n");
            peer_accepted = false;
            (void)rc_conn_tcp_disconnect_peer();
        } else if (strcmp(line, "PING") == 0) {
            reply("PONG\n");
        } else if (strcmp(line, "STATUS") == 0) {
            reply("OK WIFI AP ACTIVE\n");
        } else if (line[0] == '{') {
            int json_rc = rc_app_json_ingest(line, RC_APP_FROM_WIFI);
            if (json_rc == 0) {
                reply("OK JSON\n");
            } else if (json_rc == -EACCES) {
                reply("ERR ROLE\n");
            } else {
                reply("ERR JSON\n");
            }
        } else if (strncmp(line, "NAV ", 4) == 0) {
            if (rc_demo_nav_receive(line) == RC_OK) {
                reply("OK NAV\n");
            } else {
                reply("ERR NAV\n");
            }
        } else {
            reply("ERR COMMAND\n");
        }
        return;
    }
    if (strncmp(line, "AUTH ", 5) == 0 && strlen(line) == 11U) {
        uint8_t token[16] = {0};
        rc_err_sts_t rc = rc_demo_manager_wifi_pin(line + 5, token);
        if (rc != RC_OK) {
            reply(rc == RC_ERR_STORAGE ? "ERR STORE\n" : "ERR PIN\n");
            return;
        }
        char response[48];
        char *p = response;
        p += sprintf(p, "OK TOKEN ");
        for (size_t i = 0; i < sizeof(token); i++) {
            p += sprintf(p, "%02x", (unsigned)token[i]);
        }
        strcpy(p, "\n");
        reply(response);
        memset(token, 0, sizeof(token));
        printk("[RC WIFI] Enrollment saved, session OK\n");
        return;
    }
    if (strncmp(line, "TOKEN ", 6) == 0) {
        uint8_t token[16];
        if (unhex16(line + 6, token) &&
            rc_demo_manager_wifi_token(token) == RC_OK) {
            reply("OK CONNECTED\n");
            printk("[RC WIFI] Returning authorized session\n");
        } else {
            reply("ERR TOKEN\n");
        }
        memset(token, 0, sizeof(token));
        return;
    }
    reply("ERR AUTH_REQUIRED\n");
}

static void rx(const uint8_t *bytes, size_t len, void *user)
{
    ARG_UNUSED(user);
    for (size_t i = 0; i < len; ++i) {
        char ch = (char)bytes[i];
        if (ch == '\n') {
            if (dropping) {
                reply("ERR LINE\n");
            } else {
                if (rx_used > 0U && rx_line[rx_used - 1U] == '\r') rx_used--;
                rx_line[rx_used] = '\0';
                dispatch(rx_line);
                if (!peer_accepted) return; /* DISCONNECT ends this batch. */
            }
            rx_used = 0U;
            dropping = false;
        } else if (!dropping) {
            if ((unsigned char)ch < 32U || rx_used + 1U >= sizeof(rx_line)) {
                dropping = true;
            } else {
                rx_line[rx_used++] = ch;
            }
        }
    }
}

static bool keep_peer(void *user)
{
    ARG_UNUSED(user);
    if (!peer_accepted) return false;
    /* A verified phone stays connected without an inactivity timeout.
     * Only the incomplete PIN/token handshake expires after 60 seconds. */
    rc_demo_snapshot_t s = rc_demo_manager_get();
    if (s.wifi_stations > 0) peer_had_station = true;
    /* A real AP association loss ends the old TCP session promptly. */
    if (peer_had_station && s.wifi_stations == 0) return false;
    return s.wifi_authenticated ||
           s.stage == RC_DEMO_WIFI_ENROLL || s.stage == RC_DEMO_WIFI_TOKEN;
}

static void peer(bool connected, void *user)
{
    ARG_UNUSED(user);
    rx_used = 0U;
    dropping = false;
    peer_accepted = false;
    peer_had_station = false;
    rc_demo_manager_wifi_tcp(connected);
    if (!connected) {
        rc_h7_bridge_invalidate(RC_APP_FROM_WIFI);
        rc_demo_manager_wifi_lost();
        return;
    }
    rc_err_sts_t rc = rc_demo_manager_wifi_peer();
    if (rc == RC_OK) {
        peer_accepted = true;
        peer_had_station = rc_demo_manager_get().wifi_stations > 0;
        reply(rc_demo_manager_get().wifi_registered ? "TOKEN_REQUIRED\n" :
              "PIN_REQUIRED\n");
        printk("[RC WIFI] Client arrived, waiting for enrollment/auth\n");
    } else {
        reply("ERR BUSY\n");
    }
}

static void ap_event(const rc_conn_ap_snapshot_t *s, void *arg)
{
    ARG_UNUSED(arg);
    rc_demo_manager_wifi_radio(s->state == RC_AP_READY);
    rc_demo_manager_wifi_station_count(s->associated_stations);
    if (s->state == RC_AP_ERROR) {
        rc_demo_manager_radio_failure(RC_DEMO_LINK_WIFI, s->last_error);
    }
    /* The TCP worker checks association changes every 500ms. Avoid socket
     * operations inside Zephyr's networking event callback. */
    printk("[RC AP] state=%s stations=%u err=%d\n",
           rc_conn_ap_state_name(s->state), (unsigned)s->associated_stations,
           (int)s->last_error);
}

/* Announcement travels inside our own Wi-Fi AP network, never includes secrets. */
static void beacon_thread(void *a, void *b, void *c)
{
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
    static const char beacon[] = "RIDECAST08,AP=192.168.4.1,AUTH_REQUIRED,TCP=5050";
    while (true) {
        if (!rc_conn_ap_is_ready()) {
            k_sleep(K_SECONDS(2));
            continue;
        }
        int fd = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (fd >= 0) {
            int enable = 1;
            (void)zsock_setsockopt(fd, SOL_SOCKET, SO_BROADCAST,
                                   &enable, sizeof(enable));
            struct sockaddr_in dst = {0};
            dst.sin_family = AF_INET;
            dst.sin_port = htons(RC_BEACON_PORT);
            /* 192.168.4.255 is our AP subnet broadcast address. */
            (void)zsock_inet_pton(AF_INET, "192.168.4.255", &dst.sin_addr);
            (void)zsock_sendto(fd, beacon, sizeof(beacon) - 1U, 0,
                               (struct sockaddr *)&dst, sizeof(dst));
            (void)zsock_close(fd);
        }
        k_sleep(K_SECONDS(5));
    }
}
K_THREAD_DEFINE(rc_beacon_tid, 2048, beacon_thread, NULL, NULL, NULL, 7, 0, 0);

rc_err_sts_t rc_demo_wifi_start(void)
{
    if (strlen(CONFIG_RC_WIFI_AP_SSID) == 0U ||
        strlen(CONFIG_RC_WIFI_AP_PSK) < 8U) {
        printk("[RC AP] Set local.conf AP SSID and PSK (>=8 chars)\n");
        return RC_ERR_NETWORK_CONFIG_INVALID;
    }
    const rc_conn_ap_config_t ap = {
        .ssid = CONFIG_RC_WIFI_AP_SSID,
        .psk = CONFIG_RC_WIFI_AP_PSK,
        .channel = CONFIG_RC_WIFI_AP_CHANNEL
    };
    const rc_conn_tcp_config_t tcp = {
        .listen_port = RC_TCP_PORT, .keep_peer = keep_peer,
    };
    rc_err_sts_t rc = rc_conn_ap_init(&ap, ap_event, NULL);
    if (rc != RC_OK) return rc;
    rc = rc_conn_tcp_init(&tcp, rx, peer, NULL);
    if (rc != RC_OK) return rc;
    rc = rc_conn_ap_start();
    if (rc != RC_OK) return rc;
    return rc_conn_tcp_start();
}
