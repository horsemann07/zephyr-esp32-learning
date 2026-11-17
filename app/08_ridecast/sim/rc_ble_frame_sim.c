/* Simulates BLE ATT writes without real Bluetooth or a phone.
 * The framing/CRC/replay code is shared with rc_demo_ble.c.
 * This does NOT validate actual BLE pairing, MTU negotiation, or CCC.
 */
#include "rc_ble_frame_sim.h"
#include "rc_ble_framing.h"
#include "rc_demo_manager.h"
#include "rc_app_message.h"
#include <zephyr/sys/printk.h>
#include <errno.h>
#include <string.h>

struct sim_ble_frame_context { int received; int last_parser_rc; };

static int handle_sim_frame(void *arg, uint16_t sequence, const char *json)
{
    struct sim_ble_frame_context *ctx = arg;
    if (!rc_demo_manager_get().ble_authenticated) return -EACCES;
    char mutable_json[RC_JSON_LINE_MAX];
    size_t n = strnlen(json, sizeof(mutable_json));
    if (n == 0U || n >= sizeof(mutable_json)) return -EINVAL;
    memcpy(mutable_json, json, n + 1U);
    ctx->last_parser_rc = rc_app_json_ingest(mutable_json, RC_APP_FROM_BLE);
    if (ctx->last_parser_rc == 0) ++ctx->received;
    printk("[RC BLE SIM] frame seq=%u JSON/H7 rc=%d\n",
           (unsigned)sequence, ctx->last_parser_rc);
    return ctx->last_parser_rc;
}

void rc_ble_frame_sim_run(void)
{
    static const char notice[] =
        "{\"v\":1,\"type\":\"notification\",\"seq\":901,\"app\":\"whatsapp\","
        "\"sender\":\"BLE Framing\",\"text\":\"20-byte chunk test passed\"}";
    static const char nav[] =
        "{\"v\":1,\"type\":\"nav\",\"seq\":902,\"maneuver\":\"turn_left\","
        "\"distance_m\":75,\"speed_kmh\":30}";
    struct rc_ble_frame_rx rx;
    struct sim_ble_frame_context ctx = {0};
    uint8_t frame[RC_BLE_FRAME_MAX_BYTES];
    uint32_t now = 1000U;
    rc_ble_frame_init(&rx);
    printk("[RC BLE SIM] Phase 3B framed transport checks\n");

    size_t n = rc_ble_frame_pack(1U, notice, frame, sizeof(frame));
    if (n == 0U) {
        printk("[RC BLE SIM] frame packing failed\n");
        return;
    }
    int rc = -EINVAL;
    for (size_t offset = 0U; n > 0U && offset < n;) {
        size_t chunk = n - offset > 20U ? 20U : n - offset;
        rc = rc_ble_frame_feed(&rx, frame + offset, chunk,
                               now++, handle_sim_frame, &ctx);
        offset += chunk;
        if (rc < 0) break;
    }
    printk("[RC BLE SIM] 20-byte fragment reassembly: rc=%d, delivered=%d (expect 1,1)\n",
           rc, ctx.received);
    rc = rc_ble_frame_feed(&rx, frame, n, now++, handle_sim_frame, &ctx);
    printk("[RC BLE SIM] exact retry: rc=%d, delivered=%d (expect 2,1)\n",
           rc, ctx.received);

    frame[n - 1U] ^= 0x01U;
    rc = rc_ble_frame_feed(&rx, frame, n, now++, handle_sim_frame, &ctx);
    printk("[RC BLE SIM] corrupted CRC: rc=%d (expect %d)\n", rc, -EBADMSG);

    n = rc_ble_frame_pack(2U, nav, frame, sizeof(frame));
    rc = rc_ble_frame_feed(&rx, frame, n, now++, handle_sim_frame, &ctx);
    printk("[RC BLE SIM] BLE navigation policy: rc=%d (0=on, %d=off)\n",
           rc, -EACCES);
    rc_ble_frame_init(&rx); /* Same reset performed for a real disconnect. */
    printk("[RC BLE SIM] receiver reset on simulated disconnect\n");
}
