/* SPDX-License-Identifier: MIT
 * RideCast Heltec demo: same logical UI for mock navigation and Wi-Fi TCP.
 * Phase 0: default, no network; produces rotating sample navigation screens.
 * Phase 1: CONFIG_RC_HELTEC_WIFI_DEMO=y; uses existing connection services.
 * TCP test grammar (not the RideCast production wire protocol):
 *   PING\n                  -> PONG\n
 *   NAV,RIGHT,250,35,1\n   -> ACK,NAV\n
 */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "heltec_graphic.h"

#define DEMO_FRAME_MS 3500U
#define UI_TICK_MS    200U

#ifdef CONFIG_RC_HELTEC_WIFI_DEMO
    #include "rc_connection_wifi.h"
    #include "rc_connection_tcp.h"

    #define RC_RX_LINE_MAX 96U
K_MSGQ_DEFINE(nav_queue, sizeof(heltec_nav_screen_t), 4, 4);
static char rx_line[RC_RX_LINE_MAX];
static size_t rx_used;
static bool rx_dropping;

static void wifi_status_changed(const rc_conn_wifi_snapshot_t* s, void* user)
{
    ARG_UNUSED(user);
    printk("[Wi-Fi] %s attempt=%u err=%d\n", rc_conn_wifi_state_name(s->state),
           (unsigned int)s->attempts, (int)s->last_error);
}

static void peer_changed(bool connected, void* user)
{
    ARG_UNUSED(user);
    /* Callback runs in TCP worker thread. Only worker manipulates rx_line.
     * recv data callbacks from this worker are not concurrent.
     */
    rx_used     = 0U;
    rx_dropping = false;
    printk("[TCP] peer %s\n", connected ? "connected" : "disconnected");
}

static bool decode_nav_line(const char* line, heltec_nav_screen_t* out)
{
    char dir[16];
    char trailing;
    unsigned int meters = 0U, speed = 0U, gps = 0U;
    /* The extra %c rejects trailing garbage rather than accepting it. */
    int count =
    sscanf(line, "NAV,%15[^,],%u,%u,%u%c", dir, &meters, &speed, &gps, &trailing);
    if(count != 4 || meters > 9999U || speed > 240U || gps > 1U)
    {
        return false;
    }
    heltec_turn_t turn;
    if(strcmp(dir, "RIGHT") == 0)
    {
        turn = HELTEC_TURN_RIGHT;
    }
    else if(strcmp(dir, "LEFT") == 0)
    {
        turn = HELTEC_TURN_LEFT;
    }
    else if(strcmp(dir, "STRAIGHT") == 0)
    {
        turn = HELTEC_TURN_STRAIGHT;
    }
    else if(strcmp(dir, "ARRIVED") == 0)
    {
        turn = HELTEC_TURN_ARRIVED;
    }
    else
    {
        return false;
    }
    *out = (heltec_nav_screen_t){ .turn       = turn,
                                  .link       = HELTEC_LINK_ONLINE,
                                  .distance_m = (uint16_t)meters,
                                  .speed_kmh  = (uint16_t)speed,
                                  .gps_valid  = gps == 1U };
    return true;
}

static void send_response(const char* text)
{
    rc_err_sts_t rc = rc_conn_tcp_send((const uint8_t*)text, strlen(text));
    if(rc != RC_OK)
    {
        printk("[TCP] response failed: %d\n", (int)rc);
    }
}

static void dispatch_test_line(char* line)
{
    if(strcmp(line, "PING") == 0)
    {
        send_response("PONG\n");
        return;
    }
    heltec_nav_screen_t update;
    if(decode_nav_line(line, &update))
    {
        /* Never draw from a networking callback. UI thread reads the queue. */
        if(k_msgq_put(&nav_queue, &update, K_NO_WAIT) != 0)
        {
            heltec_nav_screen_t ignored;
            (void)k_msgq_get(&nav_queue, &ignored, K_NO_WAIT);
            (void)k_msgq_put(&nav_queue, &update, K_NO_WAIT);
        }
        send_response("ACK,NAV\n");
    }
    else
    {
        send_response("ERR,FORMAT\n");
    }
}

static void tcp_bytes_received(const uint8_t* data, size_t len, void* user)
{
    ARG_UNUSED(user);
    /* TCP is a byte stream: one receive may hold partial or many lines. */
    for(size_t i = 0U; i < len; ++i)
    {
        const char ch = (char)data[i];
        if(ch == '\n')
        {
            if(rx_dropping)
            {
                send_response("ERR,TOO_LONG\n");
            }
            else
            {
                if(rx_used > 0U && rx_line[rx_used - 1U] == '\r')
                {
                    --rx_used;
                }
                rx_line[rx_used] = '\0';
                dispatch_test_line(rx_line);
            }
            rx_used     = 0U;
            rx_dropping = false;
        }
        else if(!rx_dropping)
        {
            if((unsigned char)ch < 32U || (unsigned char)ch > 126U)
            {
                rx_dropping = true;
            }
            else if(rx_used + 1U < sizeof(rx_line))
            {
                rx_line[rx_used++] = ch;
            }
            else
            {
                rx_dropping = true;
            }
        }
    }
}

static bool start_wifi_demo(void)
{
    if(CONFIG_RC_DEMO_WIFI_SSID[0] == '\0' || CONFIG_RC_DEMO_WIFI_PSK[0] == '\0')
    {
        printk("[Wi-Fi] Configure SSID/PSK in untracked local.conf\n");
        return false;
    }
    const rc_conn_wifi_config_t cfg    = { .ssid = CONFIG_RC_DEMO_WIFI_SSID,
                                           .psk  = CONFIG_RC_DEMO_WIFI_PSK,
                                           .open_network       = false,
                                           .retry_delay_ms     = 3000U,
                                           .connect_timeout_ms = 15000U,
                                           .max_attempts       = 0U };
    const rc_conn_tcp_config_t tcp_cfg = { .listen_port = 5050U };
    rc_err_sts_t st = rc_conn_wifi_init(&cfg, wifi_status_changed, NULL);
    if(st != RC_OK)
    {
        printk("[Wi-Fi] init error=%d\n", (int)st);
        return false;
    }
    st = rc_conn_tcp_init(&tcp_cfg, tcp_bytes_received, peer_changed, NULL);
    if(st != RC_OK)
    {
        printk("[TCP] init error=%d\n", (int)st);
        return false;
    }
    st = rc_conn_wifi_start();
    if(st != RC_OK)
    {
        printk("[Wi-Fi] start error=%d\n", (int)st);
        return false;
    }
    st = rc_conn_tcp_start();
    if(st != RC_OK)
    {
        printk("[TCP] start error=%d\n", (int)st);
        return false;
    }
    printk("[TCP] development NAV protocol on port 5050\n");
    return true;
}
#endif /* CONFIG_RC_HELTEC_WIFI_DEMO */

int main(void)
{
    rc_err_sts_t st = heltec_graphic_init();
    if(st != RC_OK)
    {
        printk("RideCast OLED init error=%d\n", (int)st);
        return 0;
    }
    printk("RideCast Heltec 128x64 demo ready\n");

    heltec_nav_screen_t ui = { .turn       = HELTEC_TURN_RIGHT,
                               .link       = HELTEC_LINK_DEMO,
                               .distance_m = 250U,
                               .speed_kmh  = 35U,
                               .gps_valid  = true };

#ifdef CONFIG_RC_HELTEC_WIFI_DEMO
    /* No simulated ONLINE status: live mode starts offline until Wi-Fi and
     * the TCP test peer are actually connected. */
    ui.turn       = HELTEC_TURN_NONE;
    ui.link       = HELTEC_LINK_OFFLINE;
    ui.distance_m = 0U;
    ui.speed_kmh  = 0U;
    ui.gps_valid  = false;
    (void)start_wifi_demo();
    int64_t last_nav_ms = -1;
#else
    static const heltec_nav_screen_t frames[] = { { .turn = HELTEC_TURN_RIGHT,
                                                    .link = HELTEC_LINK_DEMO,
                                                    .distance_m = 250U,
                                                    .speed_kmh  = 35U,
                                                    .gps_valid  = true },
                                                  { .turn = HELTEC_TURN_STRAIGHT,
                                                    .link = HELTEC_LINK_DEMO,
                                                    .distance_m = 900U,
                                                    .speed_kmh  = 42U,
                                                    .gps_valid  = true },
                                                  { .turn = HELTEC_TURN_LEFT,
                                                    .link = HELTEC_LINK_DEMO,
                                                    .distance_m = 120U,
                                                    .speed_kmh  = 24U,
                                                    .gps_valid  = true },
                                                  { .turn = HELTEC_TURN_ARRIVED,
                                                    .link = HELTEC_LINK_DEMO,
                                                    .distance_m = 0U,
                                                    .speed_kmh  = 0U,
                                                    .gps_valid  = true } };
    size_t next_frame     = 1U;
    int64_t next_frame_ms = k_uptime_get() + DEMO_FRAME_MS;
#endif

    /* One thread owns all CFB operations. Incoming network data is queued. */
    (void)heltec_graphic_render(&ui);
    while(true)
    {
        bool changed = false;
#ifdef CONFIG_RC_HELTEC_WIFI_DEMO
        heltec_nav_screen_t update;
        while(k_msgq_get(&nav_queue, &update, K_NO_WAIT) == 0)
        {
            ui.turn       = update.turn;
            ui.distance_m = update.distance_m;
            ui.speed_kmh  = update.speed_kmh;
            ui.gps_valid  = update.gps_valid;
            last_nav_ms   = k_uptime_get();
            changed       = true;
        }
        heltec_link_t current_link = HELTEC_LINK_OFFLINE;
        if(rc_conn_wifi_is_connected())
        {
            current_link = rc_conn_tcp_peer_connected() ? HELTEC_LINK_ONLINE : HELTEC_LINK_WIFI;
        }
        if(ui.link != current_link)
        {
            ui.link = current_link;
            /* A disconnected peer must not retain a "GPS:OK" indicator. */
            if(current_link != HELTEC_LINK_ONLINE)
            {
                ui.gps_valid = false;
            }
            changed = true;
        }
        if(last_nav_ms >= 0 && (k_uptime_get() - last_nav_ms) > 5000 && ui.gps_valid)
        {
            ui.gps_valid = false; /* Stale data must not claim valid GPS. */
            changed      = true;
        }
#else
        if(k_uptime_get() >= next_frame_ms)
        {
            ui = frames[next_frame];
            next_frame = (next_frame + 1U) % (sizeof(frames) / sizeof(frames[0]));
            next_frame_ms = k_uptime_get() + DEMO_FRAME_MS;
            changed       = true;
        }
#endif
        if(changed)
        {
            st = heltec_graphic_render(&ui);
            if(st != RC_OK)
            {
                printk("RideCast render error=%d\n", (int)st);
            }
        }
        k_msleep(UI_TICK_MS);
    }
    return 0;
}
