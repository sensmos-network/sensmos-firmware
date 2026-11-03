#pragma once
#include <Arduino.h>

// SENSMOS logging — compact, English, one tag per module. Levels: E/W/I always on,
// D (per-item detail) compiled out unless LOG_DEBUG=1. Tags: boot wifi ws net mon cn
// script ha ota ble sys. Steady-state prints ~1 line/min ([health]); everything else
// is an actual event (state change, error) — no per-probe / per-entity spam.
//
// Every E/W/I line also lands in a RAM ring (LOG_RING_LINES × LOG_RING_LEN) so the backend
// can pull "what happened recently" over WS (`logs` command, FW ≥ 1.11) without a serial
// cable — the tester programme's "send node logs" and the "node went silent" diagnosis.

#ifndef LOG_DEBUG
#define LOG_DEBUG 0
#endif

#define LOGE(tag, fmt, ...) log_emit('E', tag, fmt, ##__VA_ARGS__)
#define LOGW(tag, fmt, ...) log_emit('W', tag, fmt, ##__VA_ARGS__)
#define LOGI(tag, fmt, ...) log_emit('I', tag, fmt, ##__VA_ARGS__)
#if LOG_DEBUG
#define LOGD(tag, fmt, ...) log_emit('D', tag, fmt, ##__VA_ARGS__)
#else
#define LOGD(tag, fmt, ...) do {} while (0)
#endif

#define LOG_RING_LINES 48
#define LOG_RING_LEN   96

void     log_emit(char lvl, const char* tag, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
// Last lines, oldest first, each "<uptime_s> <lvl> [tag] text". Returns lines copied. JSON-escaping
// is the caller's job (ws_client: logs reply).
int      log_ring_get(const char* out[], uint32_t ts[], int max);
uint32_t log_ring_dropped();   // lines overwritten since boot (ring is small on purpose)

void     log_heap_sample();   // call every loop() — tracks min largest-block (frag floor)
uint32_t log_heap_min();      // lowest largest-block seen since boot
void     log_health();        // one dense status line — call ~1/min
