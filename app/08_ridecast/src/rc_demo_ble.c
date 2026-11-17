/* RideCast BLE GATT, Phase 3B.
 * - Native Bluetooth L4 Secure Connections passkey + stored bonds.
 * - Legacy authenticated newline-delimited JSON/NOTIFY/MUSIC characteristic.
 * - New authenticated framed JSON characteristic for any BLE-enabled role.
 * - Small RX queue: JSON parser and H7 bridge run in workqueue, NOT GATT RX.
 * - Read/notify ACK returns the result of JSON acceptance into H7 bridge;
 *   it does not claim OLED rendering or delivery to a real STM32H7.
 */
#include "rc_demo_ble.h"
#include "rc_demo_manager.h"
#include "rc_demo_content.h"
#include "rc_app_message.h"
#include "rc_ble_framing.h"
#include "rc_h7_bridge.h"
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/printk.h>
#include <string.h>
#include <errno.h>

#define RC_UUID_SERVICE_VAL BT_UUID_128_ENCODE(0x4ef13480, 0x7330, 0x49ce, 0x9f17, 0x524343303830)
#define RC_UUID_CONTENT_VAL BT_UUID_128_ENCODE(0x4ef13482, 0x7330, 0x49ce, 0x9f17, 0x524343303830)
#define RC_UUID_COMMAND_VAL BT_UUID_128_ENCODE(0x4ef13481, 0x7330, 0x49ce, 0x9f17, 0x524343303830)
#define RC_UUID_FRAME_VAL   BT_UUID_128_ENCODE(0x4ef13483, 0x7330, 0x49ce, 0x9f17, 0x524343303830)
#define RC_UUID_ACK_VAL     BT_UUID_128_ENCODE(0x4ef13484, 0x7330, 0x49ce, 0x9f17, 0x524343303830)

#define RC_BLE_RX_QUEUE_DEPTH 2U
#define RC_BLE_ACK_SIZE 8U

static struct bt_uuid_128 rc_uuid_service = BT_UUID_INIT_128(RC_UUID_SERVICE_VAL);
static struct bt_uuid_128 rc_uuid_content = BT_UUID_INIT_128(RC_UUID_CONTENT_VAL);
static struct bt_uuid_128 rc_uuid_command = BT_UUID_INIT_128(RC_UUID_COMMAND_VAL);
static struct bt_uuid_128 rc_uuid_frame = BT_UUID_INIT_128(RC_UUID_FRAME_VAL);
static struct bt_uuid_128 rc_uuid_ack = BT_UUID_INIT_128(RC_UUID_ACK_VAL);

static struct bt_conn *current;
static char ble_reply[12] = "RC READY";
static struct rc_ble_frame_rx framed_rx;
static char legacy_line[RC_JSON_LINE_MAX];
static size_t legacy_used;
static bool legacy_drop;
static uint32_t ble_generation = 1U;
static bool frame_pending;

/* ACK wire: 'R','A',1,0, seq_lo,seq_hi, status_le16. Negative status
 * is a standard errno, 0 means accepted by the in-memory H7 bridge queue.
 * Read ACK to recover from a lost notification; match the sequence number.
 */
static uint8_t last_ack[RC_BLE_ACK_SIZE] = {
    'R', 'A', 1U, 0U, 0U, 0U, (uint8_t)-EINPROGRESS,
    (uint8_t)((uint16_t)(int16_t)-EINPROGRESS >> 8U),
};

struct ble_rx_packet {
    char line[RC_JSON_LINE_MAX];
    uint32_t generation;
    uint16_t sequence;
    bool framed;
};
K_MSGQ_DEFINE(ble_rx_queue, sizeof(struct ble_rx_packet),
              RC_BLE_RX_QUEUE_DEPTH, 4);
K_MUTEX_DEFINE(ble_rx_lock);

static void process_ble_rx(struct k_work *work);
K_WORK_DEFINE(ble_rx_work, process_ble_rx);

static bool is_authorized(struct bt_conn *conn)
{
    return conn != NULL && conn == current &&
           bt_conn_get_security(conn) >= BT_SECURITY_L4 &&
           rc_demo_manager_get().ble_authenticated;
}

static void ack_store(uint16_t seq, int code)
{
    int16_t status = (int16_t)code;
    last_ack[0] = 'R'; last_ack[1] = 'A';
    last_ack[2] = 1U; last_ack[3] = 0U;
    last_ack[4] = (uint8_t)seq;
    last_ack[5] = (uint8_t)(seq >> 8U);
    last_ack[6] = (uint8_t)status;
    last_ack[7] = (uint8_t)((uint16_t)status >> 8U);
}

static void reset_rx_locked(void)
{
    ++ble_generation;
    legacy_used = 0U;
    legacy_drop = false;
    frame_pending = false;
    rc_ble_frame_init(&framed_rx);
    ack_store(0U, -EINPROGRESS);
}

static int queue_rx_locked(uint16_t seq, const char *line, bool framed)
{
    if (framed && frame_pending) return -EBUSY; /* stop-and-wait framed protocol */
    struct ble_rx_packet packet = {
        .generation = ble_generation, .sequence = seq, .framed = framed,
    };
    size_t n = strnlen(line, sizeof(packet.line));
    if (n == 0U || n >= sizeof(packet.line)) return -EMSGSIZE;
    memcpy(packet.line, line, n + 1U);
    if (k_msgq_put(&ble_rx_queue, &packet, K_NO_WAIT) != 0) return -ENOBUFS;
    if (framed) {
        frame_pending = true;
        /* A read before worker completion reports pending for this seq. */
        ack_store(seq, -EINPROGRESS);
    }
    return 0;
}

static int enqueue_complete_frame(void *ctx, uint16_t sequence, const char *json)
{
    ARG_UNUSED(ctx);
    /* Called under ble_rx_lock, so this does not block or parse JSON. */
    return queue_rx_locked(sequence, json, true);
}

static void process_ble_rx(struct k_work *work)
{
    ARG_UNUSED(work);
    struct ble_rx_packet packet;
    while (k_msgq_get(&ble_rx_queue, &packet, K_NO_WAIT) == 0) {
        /* Do not process buffered data from a disconnected/old connection. */
        k_mutex_lock(&ble_rx_lock, K_FOREVER);
        bool live = packet.generation == ble_generation &&
                    is_authorized(current);
        k_mutex_unlock(&ble_rx_lock);
        if (!live) {
            k_mutex_lock(&ble_rx_lock, K_FOREVER);
            if (packet.framed && packet.generation == ble_generation) {
                frame_pending = false;
                ack_store(packet.sequence, -EACCES);
            }
            k_mutex_unlock(&ble_rx_lock);
            continue;
        }

        int rc;
        if (packet.line[0] == '{') {
            rc = rc_app_json_ingest(packet.line, RC_APP_FROM_BLE);
        } else {
            rc = rc_demo_content_receive_from(packet.line, RC_APP_FROM_BLE)
                     ? 0 : -EINVAL;
        }

        /* Source-specific bridge generation prevents stale data displaying
         * after disconnect. This ACK means ingress accepted, not rendered.
         */
        struct bt_conn *notify_conn = NULL;
        uint8_t ack_copy[RC_BLE_ACK_SIZE];
        k_mutex_lock(&ble_rx_lock, K_FOREVER);
        if (packet.generation == ble_generation && current != NULL &&
            packet.framed) {
            ack_store(packet.sequence, rc);
            memcpy(ack_copy, last_ack, sizeof(ack_copy));
            frame_pending = false;
            if (is_authorized(current)) {
                notify_conn = bt_conn_ref(current);
            }
        }
        k_mutex_unlock(&ble_rx_lock);

        if (packet.framed) {
            printk("[RC BLE] framed seq=%u, JSON/H7 rc=%d\n",
                   (unsigned)packet.sequence, rc);
        }
        if (notify_conn != NULL) {
            /* ATT notify may fail or not be subscribed; the client can read
             * the same ACK instead. No secrets or content in the ACK.
             */
            extern const struct bt_gatt_service_static rc_ble_service;
            const struct bt_gatt_attr *ack_attr = &rc_ble_service.attrs[8];
            if (bt_gatt_is_subscribed(notify_conn, ack_attr,
                                      BT_GATT_CCC_NOTIFY)) {
                int err = bt_gatt_notify(notify_conn, ack_attr,
                                         ack_copy, sizeof(ack_copy));
                if (err != 0) printk("[RC BLE] ACK notify failed %d\n", err);
            }
            bt_conn_unref(notify_conn);
        }
    }
}

static ssize_t read_reply(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                          void *buf, uint16_t len, uint16_t offset)
{
    if (!is_authorized(conn)) return BT_GATT_ERR(BT_ATT_ERR_AUTHORIZATION);
    return bt_gatt_attr_read(conn, attr, buf, len, offset,
                             ble_reply, strlen(ble_reply));
}

static ssize_t write_cmd(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                         const void *buf, uint16_t len, uint16_t offset,
                         uint8_t flags)
{
    ARG_UNUSED(attr);
    if (!is_authorized(conn)) return BT_GATT_ERR(BT_ATT_ERR_AUTHORIZATION);
    if (offset != 0U || flags != 0U || len != 4U ||
        memcmp(buf, "PING", 4) != 0) {
        return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
    }
    memcpy(ble_reply, "PONG", 5U);
    printk("[RC BLE] PING -> PONG\n");
    return len;
}

static ssize_t read_ack(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                         void *buf, uint16_t len, uint16_t offset)
{
    if (!is_authorized(conn)) return BT_GATT_ERR(BT_ATT_ERR_AUTHORIZATION);
    uint8_t snapshot[RC_BLE_ACK_SIZE];
    k_mutex_lock(&ble_rx_lock, K_FOREVER);
    memcpy(snapshot, last_ack, sizeof(snapshot));
    k_mutex_unlock(&ble_rx_lock);
    return bt_gatt_attr_read(conn, attr, buf, len, offset,
                             snapshot, sizeof(snapshot));
}

/* Compatibility path: same newline protocol, but never parse on BT RX stack. */
static ssize_t write_content(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                             const void *buf, uint16_t len, uint16_t offset,
                             uint8_t flags)
{
    ARG_UNUSED(attr);
    if (!is_authorized(conn)) return BT_GATT_ERR(BT_ATT_ERR_AUTHORIZATION);
    if (offset != 0U || flags != 0U) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }
    const uint8_t *bytes = buf;
    bool submitted = false;
    int rc = 0;
    k_mutex_lock(&ble_rx_lock, K_FOREVER);
    for (uint16_t i = 0U; i < len; ++i) {
        uint8_t ch = bytes[i];
        if (ch == '\n') {
            if (legacy_drop) {
                rc = -EMSGSIZE;
            } else if (legacy_used != 0U) {
                legacy_line[legacy_used] = '\0';
                rc = queue_rx_locked(0U, legacy_line, false);
                if (rc == 0) submitted = true;
            }
            legacy_used = 0U;
            legacy_drop = false;
            if (rc != 0) break;
        } else if (!legacy_drop) {
            if (ch < 32U || legacy_used >= sizeof(legacy_line) - 1U) {
                legacy_drop = true;
                legacy_used = 0U;
            } else {
                legacy_line[legacy_used++] = (char)ch;
            }
        }
    }
    k_mutex_unlock(&ble_rx_lock);
    if (submitted) (void)k_work_submit(&ble_rx_work);
    if (rc != 0) return BT_GATT_ERR(rc == -ENOBUFS ?
                             BT_ATT_ERR_INSUFFICIENT_RESOURCES :
                             BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    return len;
}

/* Reliable framed JSON path: ATT Write Request (not Write Command), with
 * 20-byte fragments supported at default ATT MTU. The final fragment gets
 * an ATT response immediately; the subsequent ACK reports parser acceptance.
 */
static ssize_t write_frame(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                           const void *buf, uint16_t len, uint16_t offset,
                           uint8_t flags)
{
    ARG_UNUSED(attr);
    if (!is_authorized(conn)) return BT_GATT_ERR(BT_ATT_ERR_AUTHORIZATION);
    if (offset != 0U || flags != 0U) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }
    k_mutex_lock(&ble_rx_lock, K_FOREVER);
    int rc = rc_ble_frame_feed(&framed_rx, buf, len,
                               k_uptime_get_32(), enqueue_complete_frame, NULL);
    k_mutex_unlock(&ble_rx_lock);
    if (rc < 0) {
        /* An earlier complete frame in the same write may be queued. */
        k_mutex_lock(&ble_rx_lock, K_FOREVER);
        bool has_pending = frame_pending;
        k_mutex_unlock(&ble_rx_lock);
        if (has_pending) (void)k_work_submit(&ble_rx_work);
        uint8_t att_err = (rc == -ENOBUFS || rc == -EBUSY) ?
            BT_ATT_ERR_INSUFFICIENT_RESOURCES :
            ((rc == -EMSGSIZE) ? BT_ATT_ERR_INVALID_ATTRIBUTE_LEN :
             BT_ATT_ERR_VALUE_NOT_ALLOWED);
        printk("[RC BLE] frame rejected, rc=%d\n", rc);
        return BT_GATT_ERR(att_err);
    }
    if (rc == RC_BLE_FEED_COMPLETE) {
        (void)k_work_submit(&ble_rx_work);
    }
    return len;
}

/* Attribute indices: 0 svc; 1-2 command; 3-4 legacy content;
 * 5-6 binary framed writes; 7-8 ACK read/notify; 9 CCC.
 */
BT_GATT_SERVICE_DEFINE(rc_ble_service,
    BT_GATT_PRIMARY_SERVICE(&rc_uuid_service.uuid),
    BT_GATT_CHARACTERISTIC(&rc_uuid_command.uuid,
        BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
        BT_GATT_PERM_READ_AUTHEN | BT_GATT_PERM_WRITE_AUTHEN,
        read_reply, write_cmd, NULL),
    BT_GATT_CHARACTERISTIC(&rc_uuid_content.uuid,
        BT_GATT_CHRC_WRITE,
        BT_GATT_PERM_WRITE_AUTHEN,
        NULL, write_content, NULL),
    BT_GATT_CHARACTERISTIC(&rc_uuid_frame.uuid,
        BT_GATT_CHRC_WRITE,
        BT_GATT_PERM_WRITE_AUTHEN,
        NULL, write_frame, NULL),
    BT_GATT_CHARACTERISTIC(&rc_uuid_ack.uuid,
        BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
        BT_GATT_PERM_READ_AUTHEN,
        read_ack, NULL, NULL),
    BT_GATT_CCC(NULL,
        BT_GATT_PERM_READ_AUTHEN | BT_GATT_PERM_WRITE_AUTHEN),
);

static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
            sizeof(CONFIG_BT_DEVICE_NAME) - 1U),
};

static void restart_adv(void)
{
    int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad),
                              NULL, 0);
    if (err != 0 && err != -EALREADY) {
        printk("[RC BLE] advertise failed %d\n", err);
        rc_demo_manager_radio_failure(RC_DEMO_LINK_BLE, RC_ERR_RADIO);
    } else {
        rc_demo_manager_ble_radio(true);
    }
}

static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err != 0U) {
        printk("[RC BLE] conn error %u\n", err);
        return;
    }
    if (rc_demo_manager_ble_peer() != RC_OK) {
        (void)bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
        return;
    }
    k_mutex_lock(&ble_rx_lock, K_FOREVER);
    if (current != NULL) {
        k_mutex_unlock(&ble_rx_lock);
        (void)bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
        return;
    }
    current = bt_conn_ref(conn);
    reset_rx_locked();
    k_mutex_unlock(&ble_rx_lock);
    /* New phone: native passkey dialog; bonded phone reuses stored keys. */
    int ret = bt_conn_set_security(conn, BT_SECURITY_L4);
    if (ret != 0) {
        printk("[RC BLE] secure pairing start failed %d\n", ret);
        (void)bt_conn_disconnect(conn, BT_HCI_ERR_AUTH_FAIL);
    }
}

static void security_changed(struct bt_conn *conn, bt_security_t level,
                             enum bt_security_err err)
{
    if (conn != current) return;
    if (err != BT_SECURITY_ERR_SUCCESS || level < BT_SECURITY_L4 ||
        rc_demo_manager_ble_authenticated() != RC_OK) {
        (void)rc_demo_manager_link_failure(RC_DEMO_LINK_BLE, RC_ERR_AUTH_FAILED);
        printk("[RC BLE] authentication rejected (%d, level %d)\n",
               (int)err, (int)level);
        (void)bt_conn_disconnect(conn, BT_HCI_ERR_AUTH_FAIL);
        return;
    }
    printk("[RC BLE] bond authenticated; connection OK\n");
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    printk("[RC BLE] disconnected, reason=%u\n", reason);
    k_mutex_lock(&ble_rx_lock, K_FOREVER);
    bool was_current = current == conn;
    if (was_current) {
        reset_rx_locked();
        current = NULL;
    }
    k_mutex_unlock(&ble_rx_lock);
    if (was_current) {
        bt_conn_unref(conn);
        rc_h7_bridge_invalidate(RC_APP_FROM_BLE);
        rc_demo_manager_ble_lost();
    }
}

BT_CONN_CB_DEFINE(ridecast_bt_conn_cb) = {
    .connected = connected,
    .disconnected = disconnected,
    .security_changed = security_changed,
    .recycled = restart_adv,
};

static void passkey_display(struct bt_conn *conn, unsigned int passkey)
{
    if (conn != current) return;
    rc_demo_manager_ble_passkey(passkey);
    printk("[RC BLE] passkey displayed on OLED\n");
}

static void pairing_cancel(struct bt_conn *conn)
{
    if (conn != current) return;
    printk("[RC BLE] pairing cancelled\n");
    (void)bt_conn_disconnect(conn, BT_HCI_ERR_AUTH_FAIL);
}

static void paired(struct bt_conn *conn, bool bonded)
{
    if (conn != current) return;
    printk("[RC BLE] pairing complete; bonded=%d\n", bonded ? 1 : 0);
    if (!bonded) {
        (void)rc_demo_manager_link_failure(RC_DEMO_LINK_BLE, RC_ERR_AUTH_FAILED);
        (void)bt_conn_disconnect(conn, BT_HCI_ERR_AUTH_FAIL);
    }
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
    if (conn != current) return;
    printk("[RC BLE] pairing failed=%d\n", (int)reason);
    (void)rc_demo_manager_link_failure(RC_DEMO_LINK_BLE, RC_ERR_AUTH_FAILED);
    (void)bt_conn_disconnect(conn, BT_HCI_ERR_AUTH_FAIL);
}

static const struct bt_conn_auth_cb auth_cb = {
    .passkey_display = passkey_display,
    .cancel = pairing_cancel,
};
static struct bt_conn_auth_info_cb auth_info_cb = {
    .pairing_complete = paired,
    .pairing_failed = pairing_failed,
};

rc_err_sts_t rc_demo_ble_start(void)
{
    rc_ble_frame_init(&framed_rx);
    int err = bt_enable(NULL);
    if (err != 0) return RC_ERR_RADIO;
    err = settings_load_subtree("bt");
    if (err != 0) return RC_ERR_STORAGE;
    err = bt_conn_auth_cb_register(&auth_cb);
    if (err != 0) return RC_ERR_INTERNAL;
    err = bt_conn_auth_info_cb_register(&auth_info_cb);
    if (err != 0) return RC_ERR_INTERNAL;
    restart_adv();
    printk("[RC BLE] Advertising RideCast-08 (framed GATT v1 ready)\n");
    return RC_OK;
}
