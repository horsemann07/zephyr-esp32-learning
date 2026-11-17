/* RideCast display previews from authenticated Wi-Fi or BLE app messages. */
#ifndef RC_DEMO_CONTENT_H_
#define RC_DEMO_CONTENT_H_
#include <stdbool.h>
#include "rc_app_message.h"

typedef enum {
    RC_CONTENT_NONE = 0,
    RC_CONTENT_NOTIFICATION,
    RC_CONTENT_MUSIC
} rc_content_kind_t;

typedef struct {
    rc_content_kind_t kind;
    char source[16];   /* App or music PLAY/PAUSE/STOP */
    char sender[36];   /* Sender or music artist */
    char text[84];     /* Message or track title */
} rc_demo_content_t;

/* Legacy line protocol: NOTIFY|WHATSAPP|sender|preview,
 * NOTIFY|SNAPCHAT|sender|preview or MUSIC|PLAY|artist|title.
 * Original wrapper defaults to BLE; call only after authentication.
 */
bool rc_demo_content_receive(const char *line);
bool rc_demo_content_receive_from(const char *line, rc_app_source_t source);

/* Bridge and legacy paths must retain the source of a preview. */
void rc_demo_content_publish(const rc_demo_content_t *item);
void rc_demo_content_publish_from(const rc_demo_content_t *item,
                                  rc_app_source_t source);

/* Notifications overlay navigation for 4s; music appears when NAV is idle,
 * or briefly on an update. Only authenticated, policy-enabled sources show.
 */
bool rc_demo_content_latest(rc_demo_content_t *out, bool navigation_active);
bool rc_demo_content_latest_authorized(rc_demo_content_t *out,
                                       bool navigation_active,
                                       bool wifi_authenticated,
                                       bool ble_authenticated);
void rc_demo_content_clear_source(rc_app_source_t source);
void rc_demo_content_clear(void);
#endif
