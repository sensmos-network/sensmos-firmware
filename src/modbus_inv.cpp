#include "modbus_inv.h"
#include "entity_store.h"
#include "log.h"
#include <Preferences.h>
#include <ArduinoJson.h>

// ── Czysta logika Modbus RTU ─────────────────────────────────────────────────
uint16_t modbus_crc16(const uint8_t* buf, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int b = 0; b < 8; b++) crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
    }
    return crc;
}

size_t modbus_build_read(uint8_t* out, uint8_t slave, uint16_t start, uint16_t count) {
    out[0] = slave; out[1] = 0x03;
    out[2] = start >> 8; out[3] = start & 0xFF;
    out[4] = count >> 8; out[5] = count & 0xFF;
    uint16_t crc = modbus_crc16(out, 6);
    out[6] = crc & 0xFF; out[7] = crc >> 8;   // CRC: młodszy bajt pierwszy
    return 8;
}

int modbus_parse_read(const uint8_t* buf, size_t len, uint8_t slave, uint16_t* regs, int max_regs) {
    if (len < 5 || buf[0] != slave) return 0;
    if (buf[1] & 0x80) return 0;                         // odpowiedź z wyjątkiem
    if (buf[1] != 0x03) return 0;
    int n = buf[2];
    if (n % 2 || len < (size_t)(3 + n + 2)) return 0;
    uint16_t crc = modbus_crc16(buf, 3 + n);
    if (buf[3 + n] != (crc & 0xFF) || buf[4 + n] != (crc >> 8)) return 0;
    int cnt = n / 2;
    if (cnt > max_regs) cnt = max_regs;
    for (int i = 0; i < cnt; i++) regs[i] = (uint16_t)(buf[3 + 2 * i] << 8 | buf[4 + 2 * i]);
    return cnt;
}

#ifdef ARDUINO
// ── Mapy rejestrów ───────────────────────────────────────────────────────────
// Typ: U16, S16 (ze znakiem), U32 (dwa rejestry: młodsze słowo pierwsze — konwencja Deye/Sunsynk).
enum RegType : uint8_t { R_U16, R_S16, R_U32, R_TEMP };   // R_TEMP: (surowe − 1000) × skala (Deye)
struct RegDef { const char* entity; uint16_t addr; RegType type; float scale; const char* unit; };
struct Model  { const char* id; const char* label; const RegDef* regs; uint8_t n; };

#include "modbus_maps.h"   // MODELS[] — mapy rejestrów falowników (osobny plik, żeby dokładać modele bez ruszania logiki)

// ── Stan ─────────────────────────────────────────────────────────────────────
static bool     s_on = false;
static char     s_model[12] = "deye1";
static int      s_rx = 16, s_tx = 17, s_de = -1, s_slave = 1;
static long     s_baud = 9600;
static const Model* s_m = nullptr;
static HardwareSerial s_uart(2);

static const uint32_t POLL_MS = 10000, REPLY_MS = 400, GAP_MS = 60;
static uint8_t  s_idx = 0;                // bieżący rejestr w mapie
static bool     s_waiting = false;
static uint32_t s_sent_at = 0, s_next_at = 0;
static uint8_t  s_rx_buf[64]; static size_t s_rx_len = 0;
static uint32_t s_ok = 0, s_err = 0, s_last_ok = 0;
static float    s_vals[24]; static bool s_have[24];
static float    s_pv_sum = 0; static uint8_t s_pv_n = 0;   // pv_power = suma stringów PV z jednego cyklu

static const Model* find_model(const char* id) {
    for (const Model& m : MODELS) if (!strcmp(m.id, id)) return &m;
    return nullptr;
}

static void uart_open() {
    s_uart.end();
    if (!s_on || !s_m) return;
    s_uart.begin(s_baud, SERIAL_8N1, s_rx, s_tx);
    if (s_de >= 0) { pinMode(s_de, OUTPUT); digitalWrite(s_de, LOW); }
    LOGI("inv", "Modbus %s: rx=%d tx=%d de=%d %ld bps slave=%d", s_m->id, s_rx, s_tx, s_de, s_baud, s_slave);
}

void modbus_inv_init() {
    Preferences p; p.begin("sensmos", true);
    s_on = p.getBool("inv_on", false);
    p.getString("inv_model", s_model, sizeof(s_model));
    if (!s_model[0]) strcpy(s_model, "deye1");
    s_rx = p.getInt("inv_rx", 16); s_tx = p.getInt("inv_tx", 17); s_de = p.getInt("inv_de", -1);
    s_baud = p.getLong("inv_baud", 9600); s_slave = p.getInt("inv_slave", 1);
    p.end();
    s_m = find_model(s_model);
    memset(s_have, 0, sizeof(s_have));
    uart_open();
}

bool modbus_inv_set_config(bool on, const char* model, int rx, int tx, int de, long baud, int slave) {
    if (on && (!model || !find_model(model))) return false;
    if (on && (rx < 0 || tx < 0 || rx == tx || rx > 48 || tx > 48)) return false;
    Preferences p; p.begin("sensmos", false);
    p.putBool("inv_on", on);
    if (model && model[0]) p.putString("inv_model", model);
    p.putInt("inv_rx", rx); p.putInt("inv_tx", tx); p.putInt("inv_de", de);
    p.putLong("inv_baud", baud > 0 ? baud : 9600); p.putInt("inv_slave", slave > 0 && slave < 248 ? slave : 1);
    p.end();
    s_waiting = false; s_idx = 0; s_ok = s_err = 0; s_next_at = 0;
    modbus_inv_init();
    return true;
}

static void send_request() {
    const RegDef& r = s_m->regs[s_idx];
    uint8_t req[8];
    size_t n = modbus_build_read(req, (uint8_t)s_slave, r.addr, r.type == R_U32 ? 2 : 1);
    while (s_uart.available()) s_uart.read();   // śmieci z poprzedniej ramki
    if (s_de >= 0) digitalWrite(s_de, HIGH);
    s_uart.write(req, n); s_uart.flush();
    if (s_de >= 0) digitalWrite(s_de, LOW);
    s_rx_len = 0; s_waiting = true; s_sent_at = millis();
}

static void publish(uint8_t i, float v) {
    const RegDef& r = s_m->regs[i];
    s_vals[i] = v; s_have[i] = true;
    if (!r.entity || !r.entity[0]) return;
    if (!strcmp(r.entity, "_pv")) { s_pv_sum += v; s_pv_n++; return; }
    char buf[24];
    // Moc sieci ze znakiem (+ pobór, − oddawanie) → dwie natywne encje, jak w BE (grid_import / grid_export, W).
    if (!strcmp(r.entity, "grid_power")) {
        snprintf(buf, sizeof(buf), "%.0f", v > 0 ? v : 0.0f);  entity_push("pub.grid_import", buf, "W");
        snprintf(buf, sizeof(buf), "%.0f", v < 0 ? -v : 0.0f); entity_push("pub.grid_export", buf, "W");
        return;
    }
    snprintf(buf, sizeof(buf), "%.2f", v);
    char eid[36];
    if (!strncmp(r.entity, "own.", 4)) snprintf(eid, sizeof(eid), "%s", r.entity);   // poza listą natywnych (np. temperatura falownika)
    else snprintf(eid, sizeof(eid), "pub.%s", r.entity);
    entity_push(eid, buf, r.unit);
}

static void next_reg(uint32_t now) {
    s_waiting = false;
    if (++s_idx >= s_m->n) {
        if (s_pv_n) { char buf[16]; snprintf(buf, sizeof(buf), "%.0f", s_pv_sum); entity_push("pub.pv_power", buf, "W"); }
        s_pv_sum = 0; s_pv_n = 0;
        s_idx = 0; s_next_at = now + POLL_MS;
    }
    else s_next_at = now + GAP_MS;
}

void modbus_inv_tick() {
    if (!s_on || !s_m) return;
    uint32_t now = millis();
    if (!s_waiting) {
        if ((int32_t)(now - s_next_at) >= 0) send_request();
        return;
    }
    while (s_uart.available() && s_rx_len < sizeof(s_rx_buf)) s_rx_buf[s_rx_len++] = s_uart.read();
    const RegDef& r = s_m->regs[s_idx];
    int want = r.type == R_U32 ? 2 : 1;
    uint16_t regs[2];
    if (s_rx_len >= (size_t)(5 + 2 * want)) {
        int got = modbus_parse_read(s_rx_buf, s_rx_len, (uint8_t)s_slave, regs, 2);
        if (got == want) {
            float v;
            if (r.type == R_U32)      v = (float)((uint32_t)regs[1] << 16 | regs[0]) * r.scale;
            else if (r.type == R_S16) v = (float)(int16_t)regs[0] * r.scale;
            else if (r.type == R_TEMP) v = ((float)regs[0] - 1000.0f) * r.scale;
            else                      v = (float)regs[0] * r.scale;
            publish(s_idx, v);
            s_ok++; s_last_ok = now;
        } else s_err++;
        next_reg(now);
    } else if (now - s_sent_at > REPLY_MS) {
        s_err++;
        if (s_err % 50 == 1) LOGW("inv", "brak odpowiedzi falownika (reg %u) — sprawdź A/B RS485, piny i slave", r.addr);
        next_reg(now);
    }
}

void modbus_inv_status_json(char* out, size_t cap) {
    JsonDocument d;
    d["on"] = s_on; d["model"] = s_model; d["rx"] = s_rx; d["tx"] = s_tx; d["de"] = s_de;
    d["baud"] = s_baud; d["slave"] = s_slave; d["ok"] = s_ok; d["err"] = s_err;
    d["age_s"] = s_last_ok ? (long)((millis() - s_last_ok) / 1000) : -1;
    JsonArray models = d["models"].to<JsonArray>();
    for (const Model& m : MODELS) models.add(m.id);
    if (s_m) {
        JsonObject v = d["values"].to<JsonObject>();
        for (uint8_t i = 0; i < s_m->n && i < 24; i++) {
            if (!s_have[i]) continue;
            const char* e = s_m->regs[i].entity;
            if (!strcmp(e, "_pv")) { char k[8]; snprintf(k, sizeof(k), "pv%u", (unsigned)i + 1); v[k] = s_vals[i]; }
            else v[e] = s_vals[i];
        }
    }
    serializeJson(d, out, cap);
}
#endif
