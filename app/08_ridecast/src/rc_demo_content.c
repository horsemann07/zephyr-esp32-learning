#include "rc_demo_content.h"
#include "ridecast_config.h"
#include <zephyr/kernel.h>
#include <string.h>

#define POPUP_MS 4000
K_MUTEX_DEFINE(content_lock);
struct content_slot {
    rc_demo_content_t notice;
    rc_demo_content_t music;
    int64_t notice_at;
    int64_t music_at;
    bool have_notice;
    bool have_music;
};
static struct content_slot content_slots[2]; /* Wi-Fi, BLE */

static int slot_index(rc_app_source_t source)
{
    if (source == RC_APP_FROM_WIFI) return 0;
    if (source == RC_APP_FROM_BLE) return 1;
    return -1;
}

static void copy_field(char *out, size_t size, const char *p, size_t n)
{
    if (n >= size) n = size - 1U;
    for (size_t i = 0U; i < n; ++i) {
        unsigned char c = (unsigned char)p[i];
        /* The 128x64 OLED's compact font is ASCII-only. */
        out[i] = c >= 32U && c <= 126U ? (char)c : '?';
    }
    out[n] = '\0';
}

bool rc_demo_content_receive_from(const char *line, rc_app_source_t source)
{
    if (slot_index(source) < 0 || !line) return false;
    size_t len = 0U;
    while (len <= 160U && line[len] != '\0') ++len;
    if (len == 0U || len > 160U) return false;
    const char *a = strchr(line, '|');
    if (!a) return false;
    const char *b = strchr(a + 1, '|');
    if (!b) return false;
    const char *c = strchr(b + 1, '|');
    if (!c || strchr(c + 1, '|') != NULL) return false;

    bool is_notice = (a - line == 6 && memcmp(line, "NOTIFY", 6) == 0);
    bool is_music = (a - line == 5 && memcmp(line, "MUSIC", 5) == 0);
    if (!is_notice && !is_music) return false;
    rc_app_kind_t kind = is_notice ? RC_APP_NOTIFICATION : RC_APP_MEDIA;
    if (!rc_transport_allows(source, kind)) return false;

    size_t source_len = (size_t)(b - (a + 1));
    if (is_notice && !((source_len == 8U && memcmp(a + 1, "WHATSAPP", 8) == 0) ||
                       (source_len == 8U && memcmp(a + 1, "SNAPCHAT", 8) == 0))) return false;
    if (is_music && !((source_len == 4U && memcmp(a + 1, "PLAY", 4) == 0) ||
                      (source_len == 5U && memcmp(a + 1, "PAUSE", 5) == 0) ||
                      (source_len == 4U && memcmp(a + 1, "STOP", 4) == 0))) return false;
    rc_demo_content_t new_content = { .kind = is_notice ? RC_CONTENT_NOTIFICATION : RC_CONTENT_MUSIC };
    copy_field(new_content.source, sizeof(new_content.source), a + 1, source_len);
    copy_field(new_content.sender, sizeof(new_content.sender), b + 1, (size_t)(c - (b + 1)));
    copy_field(new_content.text, sizeof(new_content.text), c + 1, len - (size_t)(c + 1 - line));
    rc_demo_content_publish_from(&new_content, source);
    return true;
}

bool rc_demo_content_receive(const char *line)
{
    return rc_demo_content_receive_from(line, RC_APP_FROM_BLE);
}

void rc_demo_content_publish_from(const rc_demo_content_t *item,
                                  rc_app_source_t source)
{
    int i = slot_index(source);
    if (!item || i < 0) return;
    rc_app_kind_t kind = item->kind == RC_CONTENT_NOTIFICATION ? RC_APP_NOTIFICATION :
                         item->kind == RC_CONTENT_MUSIC ? RC_APP_MEDIA : 0;
    if (!rc_transport_allows(source, kind)) return;

    k_mutex_lock(&content_lock, K_FOREVER);
    struct content_slot *slot = &content_slots[i];
    if (item->kind == RC_CONTENT_NOTIFICATION) {
        slot->notice = *item;
        slot->notice_at = k_uptime_get();
        slot->have_notice = true;
    } else if (item->kind == RC_CONTENT_MUSIC) {
        slot->music = *item;
        slot->music_at = k_uptime_get();
        slot->have_music = strcmp(item->source, "STOP") != 0;
    }
    k_mutex_unlock(&content_lock);
}

void rc_demo_content_publish(const rc_demo_content_t *item)
{
    rc_demo_content_publish_from(item, RC_APP_FROM_BLE);
}

bool rc_demo_content_latest_authorized(rc_demo_content_t *out,
                                       bool navigation_active,
                                       bool wifi_authenticated,
                                       bool ble_authenticated)
{
    if (!out) return false;
    k_mutex_lock(&content_lock, K_FOREVER);
    int chosen_notice = -1;
    int chosen_music = -1;
    const int64_t now = k_uptime_get();
    for (int i = 0; i < 2; ++i) {
        const rc_app_source_t src = i == 0 ? RC_APP_FROM_WIFI : RC_APP_FROM_BLE;
        bool authenticated = i == 0 ? wifi_authenticated : ble_authenticated;
        if (!authenticated) continue;
        const struct content_slot *slot = &content_slots[i];
        if (rc_transport_allows(src, RC_APP_NOTIFICATION) && slot->have_notice &&
            now - slot->notice_at < POPUP_MS &&
            (chosen_notice < 0 || slot->notice_at >= content_slots[chosen_notice].notice_at)) {
            chosen_notice = i;
        }
        if (rc_transport_allows(src, RC_APP_MEDIA) && slot->have_music &&
            (!navigation_active || now - slot->music_at < POPUP_MS) &&
            (chosen_music < 0 || slot->music_at >= content_slots[chosen_music].music_at)) {
            chosen_music = i;
        }
    }
    bool valid = chosen_notice >= 0 || chosen_music >= 0;
    if (chosen_notice >= 0) *out = content_slots[chosen_notice].notice;
    else if (chosen_music >= 0) *out = content_slots[chosen_music].music;
    k_mutex_unlock(&content_lock);
    return valid;
}

bool rc_demo_content_latest(rc_demo_content_t *out, bool navigation_active)
{
    return rc_demo_content_latest_authorized(out, navigation_active, true, true);
}

void rc_demo_content_clear_source(rc_app_source_t source)
{
    int i = slot_index(source);
    if (i < 0) return;
    k_mutex_lock(&content_lock, K_FOREVER);
    content_slots[i].have_notice = false;
    content_slots[i].have_music = false;
    k_mutex_unlock(&content_lock);
}

void rc_demo_content_clear(void)
{
    k_mutex_lock(&content_lock, K_FOREVER);
    for (int i = 0; i < 2; ++i) {
        content_slots[i].have_notice = false;
        content_slots[i].have_music = false;
    }
    k_mutex_unlock(&content_lock);
}
