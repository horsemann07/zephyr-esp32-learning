/* Simulated ESP32 -> STM32H7B3x boundary. No real UART/SPI IO in this demo. */
#ifndef RC_H7_BRIDGE_H_
#define RC_H7_BRIDGE_H_
#include "rc_app_message.h"
#include <stdbool.h>

void rc_h7_bridge_init(void);
/* Queue a fully validated, normalized message without waiting in radio CBs. */
bool rc_h7_bridge_enqueue(const rc_app_message_t *msg);
/* Prevent queued packets from a previous disconnected phone from rendering. */
void rc_h7_bridge_invalidate(rc_app_source_t source);
/* Main thread: fake the H7 receive path and update existing OLED models. */
void rc_h7_bridge_pump(void);
#endif
