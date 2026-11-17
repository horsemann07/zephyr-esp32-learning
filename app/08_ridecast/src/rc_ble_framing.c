#include "rc_ble_framing.h"
#include <errno.h>
#include <string.h>

static uint16_t crc_update(uint16_t crc, uint8_t value)
{
    crc ^= (uint16_t)value << 8U;
    for (unsigned int i = 0; i < 8U; ++i) {
        if ((crc & 0x8000U) != 0U) {
            crc = (uint16_t)(((uint32_t)crc << 1U) ^ 0x1021U);
        } else {
            crc = (uint16_t)((uint32_t)crc << 1U);
        }
    }
    return crc;
}

static void clear_partial(struct rc_ble_frame_rx *rx)
{
    rx->header_used = 0U;
    rx->payload_size = 0U;
    rx->payload_used = 0U;
    rx->current_crc = 0xffffU;
    rx->received_crc = 0U;
    rx->sequence = 0U;
    rx->crc_used = 0U;
}

void rc_ble_frame_init(struct rc_ble_frame_rx *rx)
{
    if (rx == NULL) return;
    memset(rx, 0, sizeof(*rx));
    clear_partial(rx);
}

static int reject_frame(struct rc_ble_frame_rx *rx, int err)
{
    clear_partial(rx);
    return err;
}

int rc_ble_frame_feed(struct rc_ble_frame_rx *rx, const uint8_t *bytes,
                      size_t count, uint32_t now_ms,
                      rc_ble_frame_handler_t handler, void *context)
{
    if (rx == NULL || handler == NULL || (bytes == NULL && count != 0U)) {
        return -EINVAL;
    }
    if (rx->header_used != 0U &&
        (uint32_t)(now_ms - rx->last_byte_ms) > RC_BLE_FRAME_IDLE_MS) {
        clear_partial(rx);
    }
    int result = RC_BLE_FEED_MORE;
    for (size_t i = 0U; i < count; ++i) {
        uint8_t ch = bytes[i];
        rx->last_byte_ms = now_ms;
        if (rx->header_used < RC_BLE_FRAME_HEADER_SIZE) {
            if ((rx->header_used == 0U && ch != 'R') ||
                (rx->header_used == 1U && ch != 'C')) {
                return reject_frame(rx, -EBADMSG);
            }
            rx->header[rx->header_used++] = ch;
            rx->current_crc = crc_update(rx->current_crc, ch);
            if (rx->header_used == RC_BLE_FRAME_HEADER_SIZE) {
                if (rx->header[2] != RC_BLE_FRAME_VERSION ||
                    rx->header[3] != 0U) {
                    return reject_frame(rx, -EPROTONOSUPPORT);
                }
                rx->sequence = (uint16_t)(rx->header[4] |
                              ((uint16_t)rx->header[5] << 8U));
                rx->payload_size = (uint16_t)(rx->header[6] |
                                   ((uint16_t)rx->header[7] << 8U));
                if (rx->payload_size == 0U ||
                    rx->payload_size >= RC_JSON_LINE_MAX) {
                    return reject_frame(rx, -EMSGSIZE);
                }
            }
        } else if (rx->payload_used < rx->payload_size) {
            if (ch == 0U) return reject_frame(rx, -EINVAL);
            rx->json[rx->payload_used++] = (char)ch;
            rx->current_crc = crc_update(rx->current_crc, ch);
        } else {
            rx->received_crc |= (uint16_t)ch << (8U * rx->crc_used);
            if (++rx->crc_used == RC_BLE_FRAME_CRC_SIZE) {
                if (rx->received_crc != rx->current_crc) {
                    return reject_frame(rx, -EBADMSG);
                }
                rx->json[rx->payload_size] = '\0';
                if (rx->json[0] != '{') {
                    return reject_frame(rx, -EINVAL);
                }
                if (rx->have_last) {
                    uint16_t delta = (uint16_t)(rx->sequence - rx->last_sequence);
                    if (delta == 0) {
                        if (rx->last_crc != rx->current_crc) {
                            return reject_frame(rx, -EALREADY);
                        }
                        if (result == RC_BLE_FEED_MORE) {
                            result = RC_BLE_FEED_DUPLICATE;
                        }
                        clear_partial(rx);
                        continue;
                    }
                    if (delta >= 0x8000U) {
                        return reject_frame(rx, -EALREADY);
                    }
                }
                int rc = handler(context, rx->sequence, rx->json);
                if (rc != 0) {
                    return reject_frame(rx, rc < 0 ? rc : -EINVAL);
                }
                rx->have_last = true;
                rx->last_sequence = rx->sequence;
                rx->last_crc = rx->current_crc;
                result = RC_BLE_FEED_COMPLETE;
                clear_partial(rx);
            }
        }
    }
    return result;
}

size_t rc_ble_frame_pack(uint16_t sequence, const char *json,
                         uint8_t *dest, size_t capacity)
{
    if (json == NULL || dest == NULL) return 0U;
    size_t n = strnlen(json, RC_JSON_LINE_MAX);
    if (n == 0U || n >= RC_JSON_LINE_MAX || json[0] != '{' ||
        capacity < RC_BLE_FRAME_HEADER_SIZE + n + RC_BLE_FRAME_CRC_SIZE) {
        return 0U;
    }
    dest[0] = 'R'; dest[1] = 'C';
    dest[2] = RC_BLE_FRAME_VERSION; dest[3] = 0U;
    dest[4] = (uint8_t)sequence;
    dest[5] = (uint8_t)(sequence >> 8U);
    dest[6] = (uint8_t)n;
    dest[7] = (uint8_t)(n >> 8U);
    memcpy(dest + RC_BLE_FRAME_HEADER_SIZE, json, n);
    uint16_t crc = 0xffffU;
    for (size_t i = 0U; i < RC_BLE_FRAME_HEADER_SIZE + n; ++i) {
        crc = crc_update(crc, dest[i]);
    }
    dest[RC_BLE_FRAME_HEADER_SIZE + n] = (uint8_t)crc;
    dest[RC_BLE_FRAME_HEADER_SIZE + n + 1U] = (uint8_t)(crc >> 8U);
    return RC_BLE_FRAME_HEADER_SIZE + n + RC_BLE_FRAME_CRC_SIZE;
}
