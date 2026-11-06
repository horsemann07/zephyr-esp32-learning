/* Wi-Fi STA + TCP prototype; port 5050 is unencrypted. Trusted lab LAN only. */
#include "rc_demo_wifi.h"
#include "rc_demo_manager.h"
#include "rc_connection_wifi.h"
#include "rc_connection_tcp.h"
#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/printk.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define RC_TCP_PORT    5050U
#define RC_BEACON_PORT 39801U
static bool peer_accepted;
static char rx_line[100];
static size_t rx_used;
static bool dropping;

static void reply(const char* s)
{
    (void)rc_conn_tcp_send((const uint8_t*)s, strlen(s));
}

static bool unhex16(const char* s, uint8_t out[16])
{
    if(strlen(s) != 32)
        return false;
    for(size_t i = 0; i < 16; ++i)
    {
        unsigned v = 0U;
        for(unsigned j = 0; j < 2; ++j)
        {
            char c = s[i * 2U + j];
            unsigned n;
            if(c >= '0' && c <= '9')
                n = (unsigned)(c - '0');
            else if(c >= 'a' && c <= 'f')
                n = (unsigned)(c - 'a' + 10);
            else if(c >= 'A' && c <= 'F')
                n = (unsigned)(c - 'A' + 10);
            else
                return false;
            v = (v << 4) | n;
        }
        out[i] = (uint8_t)v;
    }
    return true;
}

static void dispatch(const char* line)
{
    if(!peer_accepted)
    {
        reply("ERR BUSY\n");
        return;
    }
    rc_demo_snapshot_t s = rc_demo_manager_get();
    if(s.active == RC_DEMO_LINK_WIFI)
    {
        if(strcmp(line, "PING") == 0)
        {
            reply("PONG\n");
        }
        else if(strcmp(line, "STATUS") == 0)
        {
            reply("OK WIFI ACTIVE\n");
        }
        else
        {
            reply("ERR COMMAND\n");
        }
        return;
    }
    if(strncmp(line, "AUTH ", 5) == 0 && strlen(line) == 11U)
    {
        uint8_t token[16] = { 0 };
        rc_err_sts_t rc   = rc_demo_manager_wifi_pin(line + 5, token);
        if(rc != RC_OK)
        {
            reply(rc == RC_ERR_STORAGE ? "ERR STORE\n" : "ERR PIN\n");
            return;
        }
        char response[48];
        char* p = response;
        p += sprintf(p, "OK TOKEN ");
        for(size_t i = 0; i < sizeof(token); i++)
        {
            p += sprintf(p, "%02x", (unsigned)token[i]);
        }
        strcpy(p, "\n");
        reply(response);
        memset(token, 0, sizeof(token));
        printk("[RC WIFI] Enrollment saved, session OK\n");
        return;
    }
    if(strncmp(line, "TOKEN ", 6) == 0)
    {
        uint8_t token[16];
        if(unhex16(line + 6, token) && rc_demo_manager_wifi_token(token) == RC_OK)
        {
            reply("OK CONNECTED\n");
            printk("[RC WIFI] Returning authorized session\n");
        }
        else
        {
            reply("ERR TOKEN\n");
        }
        memset(token, 0, sizeof(token));
        return;
    }
    reply("ERR AUTH_REQUIRED\n");
}

static void rx(const uint8_t* bytes, size_t len, void* user)
{
    ARG_UNUSED(user);
    for(size_t i = 0; i < len; ++i)
    {
        char ch = (char)bytes[i];
        if(ch == '\n')
        {
            if(dropping)
            {
                reply("ERR LINE\n");
            }
            else
            {
                if(rx_used > 0U && rx_line[rx_used - 1U] == '\r')
                    rx_used--;
                rx_line[rx_used] = '\0';
                dispatch(rx_line);
            }
            rx_used  = 0U;
            dropping = false;
        }
        else if(!dropping)
        {
            if(ch < 32 || ch > 126 || rx_used + 1 >= sizeof(rx_line))
            {
                dropping = true;
            }
            else
            {
                rx_line[rx_used++] = ch;
            }
        }
    }
}

static void peer(bool connected, void* user)
{
    ARG_UNUSED(user);
    rx_used       = 0U;
    dropping      = false;
    peer_accepted = false;
    if(!connected)
    {
        rc_demo_manager_wifi_lost();
        return;
    }
    rc_err_sts_t rc = rc_demo_manager_wifi_peer();
    if(rc == RC_OK)
    {
        peer_accepted = true;
        reply(rc_demo_manager_get().wifi_registered ? "TOKEN_REQUIRED\n" : "PIN_REQUIRED\n");
        printk("[RC WIFI] Client arrived, waiting for enrollment/auth\n");
    }
    else
    {
        reply("ERR BUSY\n");
    }
}

static void wifi_event(const rc_conn_wifi_snapshot_t* s, void* arg)
{
    ARG_UNUSED(arg);
    rc_demo_manager_wifi_radio(s->associated);
    printk("[RC WIFI] state=%s err=%d\n", rc_conn_wifi_state_name(s->state),
           (int)s->last_error);
}

/* Discovery beacon intentionally contains NO PIN, token or personal data. */
static void beacon_thread(void* a, void* b, void* c)
{
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);
    static const char beacon[] = "RIDECAST08,AUTH_REQUIRED,TCP=5050";
    while(true)
    {
        if(!rc_conn_wifi_is_connected())
        {
            k_sleep(K_SECONDS(2));
            continue;
        }
        int fd = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if(fd >= 0)
        {
            int enable = 1;
            (void)zsock_setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &enable, sizeof(enable));
            struct sockaddr_in dst = { 0 };
            dst.sin_family         = AF_INET;
            dst.sin_port           = htons(RC_BEACON_PORT);
            dst.sin_addr.s_addr    = htonl(INADDR_BROADCAST);
            (void)zsock_sendto(fd, beacon, sizeof(beacon) - 1U, 0,
                               (struct sockaddr*)&dst, sizeof(dst));
            (void)zsock_close(fd);
        }
        k_sleep(K_SECONDS(5));
    }
}
K_THREAD_DEFINE(rc_beacon_tid, 2048, beacon_thread, NULL, NULL, NULL, 7, 0, 0);

rc_err_sts_t rc_demo_wifi_start(void)
{
    if(strlen(CONFIG_RC_WIFI_SSID) == 0U || strlen(CONFIG_RC_WIFI_PSK) < 8U)
    {
        printk("[RC WIFI] Set local.conf with 2.4GHz network credentials\n");
        return RC_ERR_NETWORK_CONFIG_INVALID;
    }
    const rc_conn_wifi_config_t wifi = { .ssid           = CONFIG_RC_WIFI_SSID,
                                         .psk            = CONFIG_RC_WIFI_PSK,
                                         .retry_delay_ms = 3000U,
                                         .connect_timeout_ms = 15000U };
    const rc_conn_tcp_config_t tcp   = { .listen_port = RC_TCP_PORT };
    rc_err_sts_t rc = rc_conn_wifi_init(&wifi, wifi_event, NULL);
    if(rc != RC_OK)
        return rc;
    rc = rc_conn_tcp_init(&tcp, rx, peer, NULL);
    if(rc != RC_OK)
        return rc;
    rc = rc_conn_wifi_start();
    if(rc != RC_OK)
        return rc;
    return rc_conn_tcp_start();
}
