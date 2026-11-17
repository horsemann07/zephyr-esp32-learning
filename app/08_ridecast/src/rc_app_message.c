/* JSON to transport-independent RideCast v1 data model.
 * Uses Zephyr's bounded JSON lexer; it intentionally does not allocate heap.
 */
#include "rc_app_message.h"
#include "rc_h7_bridge.h"
#include "ridecast_config.h"
#include <zephyr/data/json.h>
#include <zephyr/sys/util.h>
#include <errno.h>
#include <stddef.h>
#include <string.h>

struct wire_gps {
    int32_t valid;
    int32_t lat_e7;
    int32_t lon_e7;
};

/* Numeric fields default to -1 when absent, so required fields can be tested.
 * GPS fields are nested in the JSON object, matching the phone-side contract. */
struct wire_msg {
    int32_t v, seq, distance_m, speed_kmh, remaining_m, eta_sec;
    int32_t position_ms, duration_ms;
    const char *type, *maneuver, *road, *app, *sender, *title, *text;
    const char *artist, *album, *state;
    struct wire_gps gps;
};

static const struct json_obj_descr gps_desc[] = {
    JSON_OBJ_DESCR_PRIM(struct wire_gps, valid, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(struct wire_gps, lat_e7, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(struct wire_gps, lon_e7, JSON_TOK_NUMBER),
};
static const struct json_obj_descr msg_desc[] = {
    JSON_OBJ_DESCR_PRIM(struct wire_msg, v, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(struct wire_msg, seq, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(struct wire_msg, type, JSON_TOK_STRING),
    JSON_OBJ_DESCR_PRIM(struct wire_msg, maneuver, JSON_TOK_STRING),
    JSON_OBJ_DESCR_PRIM(struct wire_msg, distance_m, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(struct wire_msg, speed_kmh, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(struct wire_msg, remaining_m, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(struct wire_msg, eta_sec, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(struct wire_msg, road, JSON_TOK_STRING),
    JSON_OBJ_DESCR_OBJECT(struct wire_msg, gps, gps_desc),
    JSON_OBJ_DESCR_PRIM(struct wire_msg, app, JSON_TOK_STRING),
    JSON_OBJ_DESCR_PRIM(struct wire_msg, sender, JSON_TOK_STRING),
    JSON_OBJ_DESCR_PRIM(struct wire_msg, title, JSON_TOK_STRING),
    JSON_OBJ_DESCR_PRIM(struct wire_msg, text, JSON_TOK_STRING),
    JSON_OBJ_DESCR_PRIM(struct wire_msg, artist, JSON_TOK_STRING),
    JSON_OBJ_DESCR_PRIM(struct wire_msg, album, JSON_TOK_STRING),
    JSON_OBJ_DESCR_PRIM(struct wire_msg, state, JSON_TOK_STRING),
    JSON_OBJ_DESCR_PRIM(struct wire_msg, position_ms, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(struct wire_msg, duration_ms, JSON_TOK_NUMBER),
};

/* Zephyr validates JSON escaping but deliberately leaves string escapes in
 * place. Convert standard escapes and \uXXXX to UTF-8 for a future H7 UI.
 * Reject strings too long rather than silently losing content. */
static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool read_u16(const char **pp, uint32_t *out)
{
    const char *p = *pp;
    uint32_t n = 0U;
    for (int i = 0; i < 4; ++i) {
        if (p[i] == '\0') return false;
        int x = hexval(p[i]);
        if (x < 0) return false;
        n = (n << 4U) | (uint32_t)x;
    }
    *pp = p + 4;
    *out = n;
    return true;
}

static bool emit_codepoint(char *out, size_t cap, size_t *used, uint32_t cp)
{
    uint8_t tmp[4];
    size_t count;
    if (cp < 0x80U) {
        tmp[0] = (uint8_t)cp; count = 1U;
    } else if (cp < 0x800U) {
        tmp[0] = (uint8_t)(0xc0U | (cp >> 6));
        tmp[1] = (uint8_t)(0x80U | (cp & 0x3fU)); count = 2U;
    } else if (cp < 0x10000U) {
        if (cp >= 0xd800U && cp <= 0xdfffU) return false;
        tmp[0] = (uint8_t)(0xe0U | (cp >> 12));
        tmp[1] = (uint8_t)(0x80U | ((cp >> 6) & 0x3fU));
        tmp[2] = (uint8_t)(0x80U | (cp & 0x3fU)); count = 3U;
    } else if (cp <= 0x10ffffU) {
        tmp[0] = (uint8_t)(0xf0U | (cp >> 18));
        tmp[1] = (uint8_t)(0x80U | ((cp >> 12) & 0x3fU));
        tmp[2] = (uint8_t)(0x80U | ((cp >> 6) & 0x3fU));
        tmp[3] = (uint8_t)(0x80U | (cp & 0x3fU)); count = 4U;
    } else return false;
    if (*used + count >= cap) return false;
    for (size_t j = 0U; j < count; ++j) out[(*used)++] = (char)tmp[j];
    return true;
}

static bool copy_json_string(char *out, size_t cap, const char *in)
{
    if (!out || cap == 0U) return false;
    out[0] = '\0';
    if (in == NULL) return true;  /* optional string */
    size_t used = 0U;
    for (const char *p = in; *p; ++p) {
        unsigned char c = (unsigned char)*p;
        if (c == '\\') {
            c = (unsigned char)*++p;
            if (c == '\0') return false;
            if (c == 'u') {
                ++p;
                uint32_t cp;
                if (!read_u16(&p, &cp)) return false;
                if (cp >= 0xd800U && cp <= 0xdbffU) {
                    if (p[0] != '\\' || p[1] != 'u') return false;
                    p += 2;
                    uint32_t lo;
                    if (!read_u16(&p, &lo) || lo < 0xdc00U || lo > 0xdfffU) return false;
                    cp = 0x10000U + ((cp - 0xd800U) << 10) + (lo - 0xdc00U);
                }
                if (!emit_codepoint(out, cap, &used, cp)) return false;
                --p;
                continue;
            }
            if (c == 'n' || c == 'r' || c == 't' || c == 'b' || c == 'f') c = ' ';
            else if (c != '"' && c != '/' && c != '\\') return false;
        } else if (c < 32U) {
            return false;
        }
        if (used + 1U >= cap) return false;
        out[used++] = (char)c;
    }
    out[used] = '\0';
    return true;
}

int rc_app_json_ingest(char *line, rc_app_source_t source)
{
    if (line == NULL || (source != RC_APP_FROM_WIFI && source != RC_APP_FROM_BLE))
        return -EINVAL;
    size_t len = strnlen(line, RC_JSON_LINE_MAX);
    if (len == 0U || len >= RC_JSON_LINE_MAX || line[0] != '{') return -EINVAL;

    struct wire_msg w = {
        .v = -1, .seq = -1, .distance_m = -1, .speed_kmh = -1,
        .remaining_m = 0, .eta_sec = 0, .position_ms = 0, .duration_ms = 0,
        .gps = {.valid = 0},
    };
    int64_t parsed = json_obj_parse(line, len, msg_desc, ARRAY_SIZE(msg_desc), &w);
    if (parsed < 0 || w.v != 1 || w.seq < 0 || w.type == NULL) return -EINVAL;

    rc_app_message_t m = { .version = 1, .seq = (uint32_t)w.seq, .source = source };
    /* Identify the type first. Disabled source/type pairs are explicitly
     * rejected (EACCES), not misreported as malformed JSON (EINVAL).
     */
    if (strcmp(w.type, "nav") == 0) m.kind = RC_APP_NAV;
    else if (strcmp(w.type, "notification") == 0) m.kind = RC_APP_NOTIFICATION;
    else if (strcmp(w.type, "media") == 0) m.kind = RC_APP_MEDIA;
    else return -EINVAL;

    if (!rc_transport_allows(source, m.kind)) return -EACCES;

    if (m.kind == RC_APP_NAV) {
        if (w.maneuver == NULL || w.distance_m < 0 || w.distance_m > 10000000 ||
            w.speed_kmh < 0 || w.speed_kmh > 240 || w.remaining_m < 0 ||
            w.remaining_m > 100000000 || w.eta_sec < 0 || w.eta_sec > 2592000 ||
            w.gps.valid < 0 || w.gps.valid > 1 ||
            w.gps.lat_e7 < -900000000 || w.gps.lat_e7 > 900000000 ||
            w.gps.lon_e7 < -1800000000 || w.gps.lon_e7 > 1800000000) return -EINVAL;
        rc_app_nav_t *n = &m.payload.nav;
        m.kind = RC_APP_NAV;
        if (strcmp(w.maneuver, "turn_left") == 0) n->maneuver = RC_NAV_LEFT;
        else if (strcmp(w.maneuver, "turn_right") == 0) n->maneuver = RC_NAV_RIGHT;
        else if (strcmp(w.maneuver, "straight") == 0) n->maneuver = RC_NAV_STRAIGHT;
        else if (strcmp(w.maneuver, "arrived") == 0) n->maneuver = RC_NAV_ARRIVED;
        else return -EINVAL;
        n->distance_m = (uint32_t)w.distance_m;
        n->speed_kmh = (uint16_t)w.speed_kmh;
        n->remaining_m = (uint32_t)w.remaining_m;
        n->eta_sec = (uint32_t)w.eta_sec;
        n->gps_valid = w.gps.valid == 1;
        n->lat_e7 = w.gps.lat_e7;
        n->lon_e7 = w.gps.lon_e7;
        if (!copy_json_string(n->road, sizeof(n->road), w.road)) return -EINVAL;
    } else if (m.kind == RC_APP_NOTIFICATION) {
        rc_app_notification_t *n = &m.payload.notification;
        if (w.app == NULL || w.sender == NULL || w.text == NULL ||
            !copy_json_string(n->app, sizeof(n->app), w.app) ||
            !copy_json_string(n->sender, sizeof(n->sender), w.sender) ||
            !copy_json_string(n->title, sizeof(n->title), w.title) ||
            !copy_json_string(n->text, sizeof(n->text), w.text) ||
            n->app[0] == '\0') return -EINVAL;
        m.kind = RC_APP_NOTIFICATION;
    } else if (m.kind == RC_APP_MEDIA) {
        rc_app_media_t *media = &m.payload.media;
        if (w.state == NULL || w.position_ms < 0 || w.duration_ms < 0 ||
            w.position_ms > 86400000 || w.duration_ms > 86400000) return -EINVAL;
        if (strcmp(w.state, "playing") == 0) media->state = RC_MEDIA_PLAY;
        else if (strcmp(w.state, "paused") == 0) media->state = RC_MEDIA_PAUSE;
        else if (strcmp(w.state, "stopped") == 0) media->state = RC_MEDIA_STOP;
        else return -EINVAL;
        if (!copy_json_string(media->app, sizeof(media->app), w.app) ||
            !copy_json_string(media->title, sizeof(media->title), w.title) ||
            !copy_json_string(media->artist, sizeof(media->artist), w.artist) ||
            !copy_json_string(media->album, sizeof(media->album), w.album)) return -EINVAL;
        media->position_ms = (uint32_t)w.position_ms;
        media->duration_ms = (uint32_t)w.duration_ms;
        m.kind = RC_APP_MEDIA;
    } else {
        return -EINVAL;
    }
    return rc_h7_bridge_enqueue(&m) ? 0 : -ENOBUFS;
}
