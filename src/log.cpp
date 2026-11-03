#include "log.h"
#include <stdarg.h>
#include "ws_client.h"
#include "monitors.h"
#include "net_worker.h"
#include "checknet.h"
#include <WiFi.h>

static uint32_t s_min_largest = 0xFFFFFFFF;

// ── RAM ring of recent log lines ─────────────────────────────────────────────
// Static (no heap): 48 × 96 B ≈ 4.6 kB. Writers are loop() and the net worker — a short
// critical section keeps two tasks from interleaving one slot.
static char      s_ring[LOG_RING_LINES][LOG_RING_LEN];
static uint32_t  s_ring_ts[LOG_RING_LINES];
static uint16_t  s_ring_head = 0;     // next slot to write
static uint16_t  s_ring_count = 0;
static uint32_t  s_ring_dropped = 0;
static portMUX_TYPE s_ring_mux = portMUX_INITIALIZER_UNLOCKED;

void log_emit(char lvl, const char* tag, const char* fmt, ...) {
    char line[LOG_RING_LEN];
    int n = snprintf(line, sizeof(line), "%c [%s] ", lvl, tag);
    va_list ap; va_start(ap, fmt);
    vsnprintf(line + n, sizeof(line) - n, fmt, ap);
    va_end(ap);
    Serial.println(line + 2);   // serial keeps the old "[tag] text" shape
    portENTER_CRITICAL(&s_ring_mux);
    strncpy(s_ring[s_ring_head], line, LOG_RING_LEN - 1);
    s_ring[s_ring_head][LOG_RING_LEN - 1] = 0;
    s_ring_ts[s_ring_head] = millis() / 1000;
    s_ring_head = (s_ring_head + 1) % LOG_RING_LINES;
    if (s_ring_count < LOG_RING_LINES) s_ring_count++; else s_ring_dropped++;
    portEXIT_CRITICAL(&s_ring_mux);
}

int log_ring_get(const char* out[], uint32_t ts[], int max) {
    int n = 0;
    int total = s_ring_count;
    int start = (s_ring_head - total + LOG_RING_LINES * 2) % LOG_RING_LINES;
    for (int i = 0; i < total && n < max; i++) {
        int idx = (start + i) % LOG_RING_LINES;
        out[n] = s_ring[idx]; ts[n] = s_ring_ts[idx]; n++;
    }
    return n;
}

uint32_t log_ring_dropped() { return s_ring_dropped; }

void log_heap_sample() {
    uint32_t b = ESP.getMaxAllocHeap();
    if (b < s_min_largest) s_min_largest = b;
}

uint32_t log_heap_min() { return s_min_largest == 0xFFFFFFFF ? 0 : s_min_largest; }

void log_health() {
    // free/largest now, min largest since boot (fragmentation floor), link + subsystems.
    LOGI("health",
         "up=%lus heap=%uk blk=%uk min=%uk rssi=%d ws=%s mon=%u lag=%.2f busy=%u%% cn=%s",
         (unsigned long)(millis() / 1000),
         (unsigned)(ESP.getFreeHeap()      / 1024),
         (unsigned)(ESP.getMaxAllocHeap()  / 1024),
         (unsigned)(log_heap_min()         / 1024),
         WiFi.RSSI(),
         ws_client_connected() ? "up" : "down",
         (unsigned)monitors_count(),
         monitors_qlag(),
         (unsigned)net_worker_last_busy(),
         checknet_busy() ? "run" : "idle");
}
