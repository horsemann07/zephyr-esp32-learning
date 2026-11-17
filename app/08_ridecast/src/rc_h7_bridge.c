/* In-memory H7 transport stub.
 * Radio callback -> normalized message -> bounded queue -> simulated H7 RX
 * -> existing OLED snapshots. UART/SPI is intentionally NOT initialized.
 */
#include "rc_h7_bridge.h"
#include "rc_demo_nav.h"
#include "rc_demo_content.h"
#include "ridecast_config.h"
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <string.h>
#include <stdio.h>

#define RC_H7_QUEUE_DEPTH 6U

typedef struct {
    rc_app_message_t msg;
    uint32_t generation;
} queued_msg_t;
K_MSGQ_DEFINE(h7_tx_queue, sizeof(queued_msg_t), RC_H7_QUEUE_DEPTH, 4);
K_MUTEX_DEFINE(h7_link_lock);
static uint32_t wifi_generation = 1U;
static uint32_t ble_generation = 1U;

void rc_h7_bridge_init(void)
{
    k_mutex_lock(&h7_link_lock, K_FOREVER);
    k_msgq_purge(&h7_tx_queue);
    wifi_generation = 1U;
    ble_generation = 1U;
    k_mutex_unlock(&h7_link_lock);
}

bool rc_h7_bridge_enqueue(const rc_app_message_t *msg)
{
    if (msg == NULL || !rc_transport_allows(msg->source, msg->kind)) return false;
    queued_msg_t entry = {
        .msg = *msg,
    };
    k_mutex_lock(&h7_link_lock, K_FOREVER);
    entry.generation = msg->source == RC_APP_FROM_WIFI ? wifi_generation : ble_generation;
    int ret = k_msgq_put(&h7_tx_queue, &entry, K_NO_WAIT);
    if (ret != 0) {
        /* Latest data wins, particularly for frequent NAV position updates. */
        queued_msg_t old;
        (void)k_msgq_get(&h7_tx_queue, &old, K_NO_WAIT);
        ret = k_msgq_put(&h7_tx_queue, &entry, K_NO_WAIT);
    }
    k_mutex_unlock(&h7_link_lock);
    return ret == 0;
}

void rc_h7_bridge_invalidate(rc_app_source_t source)
{
    k_mutex_lock(&h7_link_lock, K_FOREVER);
    if (source == RC_APP_FROM_WIFI) ++wifi_generation;
    if (source == RC_APP_FROM_BLE) ++ble_generation;
    k_mutex_unlock(&h7_link_lock);
    /* Invalidate only the disconnected source, preserving other radio's data.
     * Do this outside h7_link_lock to avoid nested snapshot locks.
     */
    rc_demo_nav_clear_source(source);
    rc_demo_content_clear_source(source);
}

/* Fake H7 packet receiver: this is the ONLY place which maps normalized
 * messages onto the Heltec OLED's existing demo-only view models.
 * TODO(H7B3x): replace this function with UART/SPI transmit framing:
 *   magic | protocol_ver | kind | seq | payload_len | CBOR/binary | CRC.
 * TODO(H7B3x): implement actual H7 RX + acknowledgement/flow control.
 * Keep rc_app_message_t as the transport-neutral model; do not memcpy a C
 * struct over the wire (padding, endian and ABI are not portable).
 */
static void simulated_h7_receive(const rc_app_message_t *msg)
{
    if (msg->kind == RC_APP_NAV) {
        const rc_app_nav_t *n = &msg->payload.nav;
        heltec_nav_screen_t screen = {
            .distance_m = n->distance_m > 999U ? 999U : (uint16_t)n->distance_m,
            .speed_kmh = n->speed_kmh,
            .gps_valid = n->gps_valid,
            .link = HELTEC_LINK_ONLINE,
        };
        switch (n->maneuver) {
        case RC_NAV_LEFT: screen.turn = HELTEC_TURN_LEFT; break;
        case RC_NAV_RIGHT: screen.turn = HELTEC_TURN_RIGHT; break;
        case RC_NAV_ARRIVED: screen.turn = HELTEC_TURN_ARRIVED; break;
        default: screen.turn = HELTEC_TURN_STRAIGHT; break;
        }
        rc_demo_nav_publish_from(&screen, msg->source);
    } else if (msg->kind == RC_APP_NOTIFICATION) {
        const rc_app_notification_t *n = &msg->payload.notification;
        rc_demo_content_t content = { .kind = RC_CONTENT_NOTIFICATION };
        (void)snprintf(content.source, sizeof(content.source), "%s", n->app);
        (void)snprintf(content.sender, sizeof(content.sender), "%s", n->sender);
        (void)snprintf(content.text, sizeof(content.text), "%s", n->text);
        rc_demo_content_publish_from(&content, msg->source);
    } else if (msg->kind == RC_APP_MEDIA) {
        const rc_app_media_t *m = &msg->payload.media;
        rc_demo_content_t content = { .kind = RC_CONTENT_MUSIC };
        const char *state = m->state == RC_MEDIA_PLAY ? "PLAY" :
                            m->state == RC_MEDIA_PAUSE ? "PAUSE" : "STOP";
        (void)snprintf(content.source, sizeof(content.source), "%s", state);
        (void)snprintf(content.sender, sizeof(content.sender), "%s", m->artist);
        (void)snprintf(content.text, sizeof(content.text), "%s", m->title);
        rc_demo_content_publish_from(&content, msg->source);
    }
}

void rc_h7_bridge_pump(void)
{
    queued_msg_t entry;
    /* Consume at most a queueful per UI loop. No radio callback does I2C. */
    for (unsigned i = 0U; i < RC_H7_QUEUE_DEPTH; ++i) {
        k_mutex_lock(&h7_link_lock, K_FOREVER);
        if (k_msgq_get(&h7_tx_queue, &entry, K_NO_WAIT) != 0) {
            k_mutex_unlock(&h7_link_lock);
            break;
        }
        uint32_t current = entry.msg.source == RC_APP_FROM_WIFI ?
                           wifi_generation : ble_generation;
        if (entry.generation == current) {
            simulated_h7_receive(&entry.msg);
        }
        k_mutex_unlock(&h7_link_lock);
    }
}
