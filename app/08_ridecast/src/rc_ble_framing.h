/* RideCast BLE v1 framed JSON stream; no Zephyr dependencies.
 * UUID / GATT transport is implemented separately in rc_demo_ble.c.
 *
 * Wire: 'R' 'C' | version(1) | flags(0) | seq_le16 | json_len_le16 |
 *       JSON bytes (no trailing NUL/newline) | crc16_le
 * CRC16-CCITT-FALSE: polynomial 0x1021, init 0xffff over header+JSON.
 * ACK is a different GATT characteristic; the decoder never sends ACKs.
 */
#ifndef RC_BLE_FRAMING_H_
#define RC_BLE_FRAMING_H_

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "rc_app_message.h"

#define RC_BLE_FRAME_VERSION       1U
#define RC_BLE_FRAME_HEADER_SIZE   8U
#define RC_BLE_FRAME_CRC_SIZE      2U
#define RC_BLE_FRAME_MAX_BYTES     (RC_BLE_FRAME_HEADER_SIZE + RC_JSON_LINE_MAX - 1U + RC_BLE_FRAME_CRC_SIZE)
#define RC_BLE_FRAME_IDLE_MS       3000U

/* Non-error return values; never pass these as rc_err_sts_t. */
#define RC_BLE_FEED_MORE          0
#define RC_BLE_FEED_COMPLETE      1
#define RC_BLE_FEED_DUPLICATE     2

struct rc_ble_frame_rx {
    uint8_t header[RC_BLE_FRAME_HEADER_SIZE];
    char json[RC_JSON_LINE_MAX];
    uint16_t header_used;
    uint16_t payload_size;
    uint16_t payload_used;
    uint16_t current_crc;
    uint16_t received_crc;
    uint16_t sequence;
    uint8_t crc_used;
    uint32_t last_byte_ms;
    bool have_last;
    uint16_t last_sequence;
    uint16_t last_crc;
};

/* Callback must copy/process json synchronously; it is a mutable shared buffer.
 * The GATT implementation ONLY enqueues it; JSON parsing runs in workqueue.
 * Return 0 if accepted for processing, negative errno otherwise.
 */
typedef int (*rc_ble_frame_handler_t)(void *context, uint16_t sequence,
                                       const char *json);

void rc_ble_frame_init(struct rc_ble_frame_rx *rx);

/* Feed arbitrary ATT fragments; may contain part of, or multiple, frames.
 * Returns MORE, COMPLETE, DUPLICATE, or negative errno. An error discards
 * the partial frame. State is per authenticated BLE connection.
 */
int rc_ble_frame_feed(struct rc_ble_frame_rx *rx, const uint8_t *bytes,
                      size_t count, uint32_t now_ms,
                      rc_ble_frame_handler_t handler, void *context);

/* Helper for phone/host simulation; returns number of bytes or 0 on failure. */
size_t rc_ble_frame_pack(uint16_t sequence, const char *json,
                         uint8_t *dest, size_t capacity);

#endif
