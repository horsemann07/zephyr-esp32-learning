/* RideCast deterministic phone-app integration preview.
 * Compile ONLY when RC_APP_SIM_MODE is 1 or 2 in rc_app_sim_config.h.
 *
 * Fake phone app (this file) -> SAME rc_app_json_ingest() used by the real
 * authenticated Wi-Fi/BLE paths -> rc_h7_bridge queue -> main.c pump ->
 * existing OLED graphics. main.c, graphics, JSON parser and bridge are intact.
 *
 * RC_APP_SIM_MODE=1: simulate both authenticated transports (NO physical radios).
 * RC_APP_SIM_MODE=2: keep real radios; send each scenario message only when its
 *                 respective transport has been genuinely authenticated.
 */
#include "rc_app_message.h"
#include "rc_demo_manager.h"
#include "rc_demo_nav.h"
#include "rc_demo_content.h"
#include "rc_h7_bridge.h"
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <errno.h>
#include <string.h>

#include "rc_app_sim_config.h"

struct scenario_event {
    rc_app_source_t source;
    const char *label;
    const char *json;
    uint32_t hold_ms;
};

/* Sample data is deliberately small enough to fit the parser's 512 byte
 * limit; the H7 bridge retains fields that cannot fit on the 128x64 OLED.
 * Each JSON object's source is a stand-in for the phone transport only.
 */

static const struct scenario_event events[] = {
    /* 01: Start navigation */
    {RC_APP_FROM_WIFI, "NAV: straight 650 m",
     "{\"v\":1,\"type\":\"nav\",\"seq\":1,\"maneuver\":\"straight\",\"distance_m\":650,\"speed_kmh\":42,\"remaining_m\":5800,\"eta_sec\":820,\"road\":\"MG Road\",\"gps\":{\"valid\":1,\"lat_e7\":185204000,\"lon_e7\":738567000}}", 4000U},

    /* 02: Turn right */
    {RC_APP_FROM_WIFI, "NAV: turn right 160 m",
     "{\"v\":1,\"type\":\"nav\",\"seq\":2,\"maneuver\":\"turn_right\",\"distance_m\":160,\"speed_kmh\":36,\"remaining_m\":5100,\"eta_sec\":740,\"road\":\"Main Street\"}", 3500U},

    /* 03: WhatsApp message */
    {RC_APP_FROM_BLE, "WhatsApp: Rahul",
     "{\"v\":1,\"type\":\"notification\",\"seq\":3,\"app\":\"whatsapp\",\"sender\":\"Rahul\",\"title\":\"New message\",\"text\":\"I reached the pickup point\"}", 5000U},

    /* 04: Continue straight */
    {RC_APP_FROM_WIFI, "NAV: straight 400 m",
     "{\"v\":1,\"type\":\"nav\",\"seq\":4,\"maneuver\":\"straight\",\"distance_m\":400,\"speed_kmh\":38,\"remaining_m\":4700,\"eta_sec\":680,\"road\":\"Main Street\"}", 3000U},

    /* 05: Spotify starts */
    {RC_APP_FROM_BLE, "Spotify: playing",
     "{\"v\":1,\"type\":\"media\",\"seq\":5,\"app\":\"spotify\",\"state\":\"playing\",\"title\":\"Night Drive\",\"artist\":\"Demo Artist\",\"album\":\"Road Trip\",\"position_ms\":10000,\"duration_ms\":215000}", 5000U},

    /* 06: Turn left */
    {RC_APP_FROM_WIFI, "NAV: turn left 250 m",
     "{\"v\":1,\"type\":\"nav\",\"seq\":6,\"maneuver\":\"turn_left\",\"distance_m\":250,\"speed_kmh\":30,\"remaining_m\":4200,\"eta_sec\":610,\"road\":\"Market Lane\"}", 3500U},

    /* 07: Snapchat */
    {RC_APP_FROM_BLE, "Snapchat: Alex",
     "{\"v\":1,\"type\":\"notification\",\"seq\":7,\"app\":\"snapchat\",\"sender\":\"Alex\",\"title\":\"New Snap\",\"text\":\"You have a new snap\"}", 5000U},

    /* 08: Navigation straight */
    {RC_APP_FROM_WIFI, "NAV: straight 900 m",
     "{\"v\":1,\"type\":\"nav\",\"seq\":8,\"maneuver\":\"straight\",\"distance_m\":900,\"speed_kmh\":45,\"remaining_m\":3900,\"eta_sec\":540,\"road\":\"University Road\"}", 3500U},

    /* 09: WhatsApp */
    {RC_APP_FROM_BLE, "WhatsApp: Priya",
     "{\"v\":1,\"type\":\"notification\",\"seq\":9,\"app\":\"whatsapp\",\"sender\":\"Priya\",\"title\":\"New message\",\"text\":\"Please call me when free\"}", 5000U},

    /* 10: Spotify pause */
    {RC_APP_FROM_BLE, "Spotify: paused",
     "{\"v\":1,\"type\":\"media\",\"seq\":10,\"app\":\"spotify\",\"state\":\"paused\",\"title\":\"Night Drive\",\"artist\":\"Demo Artist\"}", 3500U},

    /* 11: Turn right */
    {RC_APP_FROM_WIFI, "NAV: turn right 90 m",
     "{\"v\":1,\"type\":\"nav\",\"seq\":11,\"maneuver\":\"turn_right\",\"distance_m\":90,\"speed_kmh\":25,\"remaining_m\":3000,\"eta_sec\":460,\"road\":\"FC Road\"}", 3500U},

    /* 12: Incoming call simulation */
    {RC_APP_FROM_BLE, "Incoming call: Rahul",
     "{\"v\":1,\"type\":\"notification\",\"seq\":12,\"app\":\"phone\",\"sender\":\"Rahul\",\"title\":\"Incoming call\",\"text\":\"Rahul is calling\"}", 6000U},

    /* 13: Continue navigation */
    {RC_APP_FROM_WIFI, "NAV: straight 500 m",
     "{\"v\":1,\"type\":\"nav\",\"seq\":13,\"maneuver\":\"straight\",\"distance_m\":500,\"speed_kmh\":32,\"remaining_m\":2700,\"eta_sec\":410,\"road\":\"FC Road\"}", 3000U},

    /* 14: Spotify resumes */
    {RC_APP_FROM_BLE, "Spotify: resumed",
     "{\"v\":1,\"type\":\"media\",\"seq\":14,\"app\":\"spotify\",\"state\":\"playing\",\"title\":\"Night Drive\",\"artist\":\"Demo Artist\",\"position_ms\":45000,\"duration_ms\":215000}", 4500U},

    /* 15: Traffic warning */
    {RC_APP_FROM_BLE, "Maps: traffic alert",
     "{\"v\":1,\"type\":\"notification\",\"seq\":15,\"app\":\"maps\",\"sender\":\"Navigation\",\"title\":\"Traffic alert\",\"text\":\"Slow traffic ahead\"}", 5000U},

    /* 16: Turn left */
    {RC_APP_FROM_WIFI, "NAV: turn left 120 m",
     "{\"v\":1,\"type\":\"nav\",\"seq\":16,\"maneuver\":\"turn_left\",\"distance_m\":120,\"speed_kmh\":20,\"remaining_m\":2200,\"eta_sec\":350,\"road\":\"JM Road\"}", 3500U},

    /* 17: Snapchat */
    {RC_APP_FROM_BLE, "Snapchat: Sam",
     "{\"v\":1,\"type\":\"notification\",\"seq\":17,\"app\":\"snapchat\",\"sender\":\"Sam\",\"title\":\"New message\",\"text\":\"Where are you riding today?\"}", 5000U},

    /* 18: Spotify next song */
    {RC_APP_FROM_BLE, "Spotify: next track",
     "{\"v\":1,\"type\":\"media\",\"seq\":18,\"app\":\"spotify\",\"state\":\"playing\",\"title\":\"Open Highway\",\"artist\":\"Demo Band\",\"album\":\"Road Trip\",\"position_ms\":0,\"duration_ms\":198000}", 4500U},

    /* 19: Straight ahead */
    {RC_APP_FROM_WIFI, "NAV: straight 750 m",
     "{\"v\":1,\"type\":\"nav\",\"seq\":19,\"maneuver\":\"straight\",\"distance_m\":750,\"speed_kmh\":40,\"remaining_m\":1800,\"eta_sec\":270,\"road\":\"JM Road\"}", 3500U},

    /* 20: WhatsApp */
    {RC_APP_FROM_BLE, "WhatsApp: Team",
     "{\"v\":1,\"type\":\"notification\",\"seq\":20,\"app\":\"whatsapp\",\"sender\":\"Ride Group\",\"title\":\"Group message\",\"text\":\"Everyone meet at the cafe\"}", 5000U},

    /* 21: Turn right */
    {RC_APP_FROM_WIFI, "NAV: turn right 200 m",
     "{\"v\":1,\"type\":\"nav\",\"seq\":21,\"maneuver\":\"turn_right\",\"distance_m\":200,\"speed_kmh\":28,\"remaining_m\":1000,\"eta_sec\":170,\"road\":\"Station Road\"}", 3500U},

    /* 22: Low battery notification */
    {RC_APP_FROM_BLE, "Phone: low battery",
     "{\"v\":1,\"type\":\"notification\",\"seq\":22,\"app\":\"system\",\"sender\":\"Phone\",\"title\":\"Low battery\",\"text\":\"Battery level is 15 percent\"}", 4500U},

    /* 23: Continue straight */
    {RC_APP_FROM_WIFI, "NAV: straight 450 m",
     "{\"v\":1,\"type\":\"nav\",\"seq\":23,\"maneuver\":\"straight\",\"distance_m\":450,\"speed_kmh\":24,\"remaining_m\":750,\"eta_sec\":130,\"road\":\"Station Road\"}", 3000U},

    /* 24: Spotify paused */
    {RC_APP_FROM_BLE, "Spotify: paused again",
     "{\"v\":1,\"type\":\"media\",\"seq\":24,\"app\":\"spotify\",\"state\":\"paused\",\"title\":\"Open Highway\",\"artist\":\"Demo Band\"}", 3500U},

    /* 25: Near destination */
    {RC_APP_FROM_WIFI, "NAV: left 100 m",
     "{\"v\":1,\"type\":\"nav\",\"seq\":25,\"maneuver\":\"turn_left\",\"distance_m\":100,\"speed_kmh\":15,\"remaining_m\":300,\"eta_sec\":65,\"road\":\"Destination Lane\"}", 3500U},

    /* 26: WhatsApp */
    {RC_APP_FROM_BLE, "WhatsApp: Rahul",
     "{\"v\":1,\"type\":\"notification\",\"seq\":26,\"app\":\"whatsapp\",\"sender\":\"Rahul\",\"title\":\"New message\",\"text\":\"I can see you approaching\"}", 5000U},

    /* 27: Destination approaching */
    {RC_APP_FROM_WIFI, "NAV: destination 50 m",
     "{\"v\":1,\"type\":\"nav\",\"seq\":27,\"maneuver\":\"straight\",\"distance_m\":50,\"speed_kmh\":10,\"remaining_m\":50,\"eta_sec\":15,\"road\":\"Destination Lane\"}", 3500U},

    /* 28: Arrived */
    {RC_APP_FROM_WIFI, "NAV: arrived",
     "{\"v\":1,\"type\":\"nav\",\"seq\":28,\"maneuver\":\"arrived\",\"distance_m\":0,\"speed_kmh\":0,\"remaining_m\":0,\"eta_sec\":0,\"road\":\"Destination\"}", 9000U},

    /* 29: Spotify stopped */
    {RC_APP_FROM_BLE, "Spotify: stopped",
     "{\"v\":1,\"type\":\"media\",\"seq\":29,\"app\":\"spotify\",\"state\":\"stopped\",\"title\":\"Open Highway\",\"artist\":\"Demo Band\"}", 3500U},

    /* 30: Final notification */
    {RC_APP_FROM_BLE, "WhatsApp: ride completed",
     "{\"v\":1,\"type\":\"notification\",\"seq\":30,\"app\":\"whatsapp\",\"sender\":\"Ride Group\",\"title\":\"Ride complete\",\"text\":\"Great ride! See you next time\"}", 5000U},
};


/* JSON is mutable because the Zephyr parser returns string pointers inside
 * the input buffer. This is separate per-thread storage, no extra heap.
 */
static void inject_json(const struct scenario_event *ev)
{
    rc_demo_snapshot_t s = rc_demo_manager_get();
    bool authorized = ev->source == RC_APP_FROM_WIFI ?
                      s.wifi_authenticated : s.ble_authenticated;
    if (!authorized) {
        printk("[RC SIM] SKIP %s: %s not authenticated\n", ev->label,
               ev->source == RC_APP_FROM_WIFI ? "Wi-Fi" : "BLE");
        return;
    }
    size_t n = strlen(ev->json);
    if (n + 1U > RC_JSON_LINE_MAX) {
        printk("[RC SIM] JSON too long: %s\n", ev->label);
        return;
    }
    char line[RC_JSON_LINE_MAX];
    memcpy(line, ev->json, n + 1U);
    int rc = rc_app_json_ingest(line, ev->source);
    printk("[RC SIM] %s | %s | rc=%d\n",
           ev->source == RC_APP_FROM_WIFI ? "WIFI" : "BLE",
           ev->label, rc);
}

static void scenario_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);
    /* main.c initializes manager, bridge and display before the first event. */
    k_sleep(K_SECONDS(4));
    printk("[RC SIM] Phone-app JSON scenario enabled. main.c unchanged.\n");
    for (;;) {
#if RC_APP_SIM_MODE == 1
        /* The OLED starts on broadcast/wait, then shows CONN OK. */
        rc_demo_manager_simulate_links(true, true);
        printk("[RC SIM] Virtual Wi-Fi + BLE authorized\n");
        k_sleep(K_SECONDS(3));
#endif
        for (size_t i = 0U; i < sizeof(events) / sizeof(events[0]); ++i) {
            inject_json(&events[i]);
            k_msleep(events[i].hold_ms);
        }
#if RC_APP_SIM_MODE == 1
        /* Demonstrate the existing connection screen again on disconnect. */
        rc_demo_manager_simulate_links(false, false);
        rc_h7_bridge_invalidate(RC_APP_FROM_WIFI);
        rc_h7_bridge_invalidate(RC_APP_FROM_BLE);
        rc_demo_nav_clear();
        rc_demo_content_clear();
        printk("[RC SIM] Virtual client disconnected; returning to READY\n");
        k_sleep(K_SECONDS(5));
#else
        /* Live mode never overrides the connection manager or actual security. */
        k_sleep(K_SECONDS(4));
#endif
    }
}

K_THREAD_DEFINE(rc_app_sim_tid, 4096, scenario_thread,
                NULL, NULL, NULL, K_PRIO_PREEMPT(10), 0, 0);
