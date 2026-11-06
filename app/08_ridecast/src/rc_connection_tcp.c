/* RideCast TCP server - single-client, bounded recv-chunk streaming.
 * Native Zephyr socket API permits Wi-Fi backend changes without touching
 * this file. TCP data is a stream (fragmentation/coalescing expected).
 * SPDX-License-Identifier: MIT
 */
#include "rc_connection_tcp.h"
#include "rc_connection_wifi.h"

#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(rc_wifi_tcp, LOG_LEVEL_INF);

#define RC_TCP_RX_CHUNK        512u
#define RC_TCP_THREAD_PRIORITY 6
#define RC_TCP_THREAD_STACK    4096u

K_MUTEX_DEFINE(rc_tcp_lock);
K_THREAD_STACK_DEFINE(rc_tcp_stack, RC_TCP_THREAD_STACK);
static struct k_thread rc_tcp_thread;
static k_tid_t rc_tcp_tid;
static rc_conn_tcp_data_cb_t rc_tcp_data_cb;
static rc_conn_tcp_peer_cb_t rc_tcp_peer_cb;
static void* rc_tcp_user;
static uint16_t rc_tcp_port;
static int rc_tcp_listen_fd = -1;
static int rc_tcp_peer_fd   = -1;
static bool rc_tcp_initialized;
static bool rc_tcp_running;
static bool rc_tcp_thread_exited = true;

static bool tcp_is_running(void)
{
    bool enabled;
    k_mutex_lock(&rc_tcp_lock, K_FOREVER);
    enabled = rc_tcp_running;
    k_mutex_unlock(&rc_tcp_lock);
    return enabled;
}

static void tcp_publish_peer(bool connected)
{
    rc_conn_tcp_peer_cb_t cb;
    void* user;
    k_mutex_lock(&rc_tcp_lock, K_FOREVER);
    cb   = rc_tcp_peer_cb;
    user = rc_tcp_user;
    k_mutex_unlock(&rc_tcp_lock);
    if(cb != NULL)
    {
        cb(connected, user);
    }
}

static void tcp_close_owned_fd(int* fd)
{
    int old;
    k_mutex_lock(&rc_tcp_lock, K_FOREVER);
    old = *fd;
    *fd = -1;
    k_mutex_unlock(&rc_tcp_lock);
    if(old >= 0)
    {
        (void)zsock_close(old);
    }
}

static void rc_tcp_worker(void* a, void* b, void* c)
{
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);
    uint8_t buffer[RC_TCP_RX_CHUNK];

    while(tcp_is_running())
    {
        if(!rc_conn_wifi_is_connected())
        {
            k_msleep(300);
            continue;
        }
        int listener = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if(listener < 0)
        {
            LOG_ERR("socket() failed errno=%d", errno);
            k_msleep(1000);
            continue;
        }
        k_mutex_lock(&rc_tcp_lock, K_FOREVER);
        rc_tcp_listen_fd = listener;
        k_mutex_unlock(&rc_tcp_lock);

        struct sockaddr_in local = { 0 };
        local.sin_family         = AF_INET;
        local.sin_port           = htons(rc_tcp_port);
        local.sin_addr.s_addr    = htonl(INADDR_ANY);

        if(zsock_bind(listener, (struct sockaddr*)&local, sizeof(local)) < 0 ||
           zsock_listen(listener, 1) < 0)
        {
            LOG_ERR("TCP bind/listen port=%u failed errno=%d",
                    (unsigned int)rc_tcp_port, errno);
            tcp_close_owned_fd(&rc_tcp_listen_fd);
            k_msleep(1000);
            continue;
        }
        LOG_INF("TCP listening on port %u (Wi-Fi STA LAN)", (unsigned int)rc_tcp_port);

        /* The Inventek offloaded socket provider supports an accept path;
         * some releases may block here until a peer connects. stop() closes
         * this socket to request wakeup; the driver may not honor it promptly.
         */
        int peer = zsock_accept(listener, NULL, NULL);
        if(peer >= 0 && tcp_is_running() && rc_conn_wifi_is_connected())
        {
            k_mutex_lock(&rc_tcp_lock, K_FOREVER);
            rc_tcp_peer_fd = peer;
            k_mutex_unlock(&rc_tcp_lock);
            LOG_INF("TCP peer connected");
            tcp_publish_peer(true);
            while(tcp_is_running() && rc_conn_wifi_is_connected())
            {
                ssize_t received = zsock_recv(peer, buffer, sizeof(buffer), 0);
                if(received > 0)
                {
                    /* Caller must copy data if it needs to retain it. */
                    if(rc_tcp_data_cb != NULL)
                    {
                        rc_tcp_data_cb(buffer, (size_t)received, rc_tcp_user);
                    }
                }
                else if(received == 0)
                {
                    break; /* orderly peer close */
                }
                else if(errno != EINTR)
                {
                    LOG_WRN("TCP recv failed errno=%d", errno);
                    break;
                }
            }
            /* A callback may have closed the socket via stop(). */
            tcp_close_owned_fd(&rc_tcp_peer_fd);
            tcp_publish_peer(false);
            LOG_INF("TCP peer disconnected");
        }
        else if(peer >= 0)
        {
            (void)zsock_close(peer);
        }
        tcp_close_owned_fd(&rc_tcp_listen_fd);
        if(tcp_is_running())
        {
            k_msleep(250);
        }
    }
    k_mutex_lock(&rc_tcp_lock, K_FOREVER);
    rc_tcp_thread_exited = true;
    k_mutex_unlock(&rc_tcp_lock);
}

rc_err_sts_t rc_conn_tcp_init(const rc_conn_tcp_config_t* config,
                              rc_conn_tcp_data_cb_t data_callback,
                              rc_conn_tcp_peer_cb_t peer_callback,
                              void* user)
{
    if(config == NULL || config->listen_port == 0u || data_callback == NULL)
    {
        return RC_ERR_INVALID_ARG;
    }
    k_mutex_lock(&rc_tcp_lock, K_FOREVER);
    if(rc_tcp_initialized)
    {
        k_mutex_unlock(&rc_tcp_lock);
        return RC_ERR_ALREADY_INITIALIZED;
    }
    rc_tcp_port        = config->listen_port;
    rc_tcp_data_cb     = data_callback;
    rc_tcp_peer_cb     = peer_callback;
    rc_tcp_user        = user;
    rc_tcp_initialized = true;
    k_mutex_unlock(&rc_tcp_lock);
    return RC_OK;
}

rc_err_sts_t rc_conn_tcp_start(void)
{
    k_mutex_lock(&rc_tcp_lock, K_FOREVER);
    if(!rc_tcp_initialized)
    {
        k_mutex_unlock(&rc_tcp_lock);
        return RC_ERR_NOT_INITIALIZED;
    }
    if(rc_tcp_running || !rc_tcp_thread_exited)
    {
        k_mutex_unlock(&rc_tcp_lock);
        return RC_ERR_BUSY;
    }
    rc_tcp_running       = true;
    rc_tcp_thread_exited = false;
    k_mutex_unlock(&rc_tcp_lock);
    rc_tcp_tid = k_thread_create(&rc_tcp_thread, rc_tcp_stack,
                                 K_THREAD_STACK_SIZEOF(rc_tcp_stack), rc_tcp_worker,
                                 NULL, NULL, NULL, RC_TCP_THREAD_PRIORITY, 0, K_NO_WAIT);
    if(rc_tcp_tid == NULL)
    {
        k_mutex_lock(&rc_tcp_lock, K_FOREVER);
        rc_tcp_running       = false;
        rc_tcp_thread_exited = true;
        k_mutex_unlock(&rc_tcp_lock);
        return RC_ERR_THREAD_CREATE_FAILED;
    }
    return RC_OK;
}

rc_err_sts_t rc_conn_tcp_stop(void)
{
    k_mutex_lock(&rc_tcp_lock, K_FOREVER);
    if(!rc_tcp_initialized)
    {
        k_mutex_unlock(&rc_tcp_lock);
        return RC_ERR_NOT_INITIALIZED;
    }
    if(!rc_tcp_running && rc_tcp_thread_exited)
    {
        k_mutex_unlock(&rc_tcp_lock);
        return RC_OK;
    }
    rc_tcp_running = false;
    /* Detach fds under mutex. The server worker will tolerate -1. */
    int peer         = rc_tcp_peer_fd;
    int listener     = rc_tcp_listen_fd;
    rc_tcp_peer_fd   = -1;
    rc_tcp_listen_fd = -1;
    k_mutex_unlock(&rc_tcp_lock);
    if(peer >= 0)
    {
        (void)zsock_close(peer);
    }
    if(listener >= 0)
    {
        (void)zsock_close(listener);
    }
    if(k_thread_join(&rc_tcp_thread, K_MSEC(1500)) != 0)
    {
        return RC_ERR_TIMEOUT;
    }
    return RC_OK;
}

rc_err_sts_t rc_conn_tcp_send(const uint8_t* data, size_t length)
{
    if((data == NULL && length != 0u) || length == 0u)
    {
        return RC_ERR_INVALID_ARG;
    }
    k_mutex_lock(&rc_tcp_lock, K_FOREVER);
    if(!rc_tcp_initialized || !rc_tcp_running || rc_tcp_peer_fd < 0)
    {
        k_mutex_unlock(&rc_tcp_lock);
        return RC_ERR_NOT_CONNECTED;
    }
    size_t sent = 0u;
    while(sent < length)
    {
        ssize_t count = zsock_send(rc_tcp_peer_fd, data + sent, length - sent, 0);
        if(count <= 0)
        {
            k_mutex_unlock(&rc_tcp_lock);
            return RC_ERR_TX_FAILED;
        }
        sent += (size_t)count;
    }
    k_mutex_unlock(&rc_tcp_lock);
    return RC_OK;
}

bool rc_conn_tcp_peer_connected(void)
{
    bool connected;
    k_mutex_lock(&rc_tcp_lock, K_FOREVER);
    connected = (rc_tcp_running && rc_tcp_peer_fd >= 0);
    k_mutex_unlock(&rc_tcp_lock);
    return connected;
}
