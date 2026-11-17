/* RideCast - single-peer TCP byte-stream ingress/egress for Wi-Fi demo.
 * The API transports arbitrary bytes, not application frames. A future router
 * feeds those bytes to the agreed RideCast protocol decoder.
 * SPDX-License-Identifier: MIT
 */
#ifndef RC_CONNECTION_TCP_H_
#define RC_CONNECTION_TCP_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "rc_err_sts.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Return false to close the peer. Evaluated at least twice per second,
 * including when the TCP client sends no bytes. */
typedef bool (*rc_conn_tcp_peer_keep_cb_t)(void *user);

typedef struct {
    uint16_t listen_port;   /* nonzero; e.g. 5050 */
    rc_conn_tcp_peer_keep_cb_t keep_peer; /* optional admission/expiry policy */
} rc_conn_tcp_config_t;

typedef void (*rc_conn_tcp_data_cb_t)(const uint8_t *data, size_t length,
                                      void *user);
typedef void (*rc_conn_tcp_peer_cb_t)(bool connected, void *user);

/* One instance and at most one connected peer for this first milestone.
 * Demo TCP transport is UNENCRYPTED. Only the app enrollment layer authorizes
 * commands; do not transport real personal data or deploy without TLS.
 */
rc_err_sts_t rc_conn_tcp_init(const rc_conn_tcp_config_t *config,
                              rc_conn_tcp_data_cb_t data_callback,
                              rc_conn_tcp_peer_cb_t peer_callback,
                              void *user);
rc_err_sts_t rc_conn_tcp_start(void);
/* Stop is best-effort with some socket-offload drivers; returns TIMEOUT if
 * the vendor driver's blocking accept/recv fails to unblock on close.
 */
rc_err_sts_t rc_conn_tcp_stop(void);
/* Request a graceful shutdown; worker owns close and disconnect notification. */
rc_err_sts_t rc_conn_tcp_disconnect_peer(void);
/* Writes are bounded to 2s, even if an authenticated peer stops reading. */
rc_err_sts_t rc_conn_tcp_send(const uint8_t *data, size_t length);
bool rc_conn_tcp_peer_connected(void);

#ifdef __cplusplus
}
#endif
#endif
