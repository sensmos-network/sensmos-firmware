#include "modbus_rtu.h"
#include <string.h>
#include <stdio.h>

// ── Czysta logika Modbus RTU ─────────────────────────────────────────────────
uint16_t modbus_crc16(const uint8_t* buf, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int b = 0; b < 8; b++) crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
    }
    return crc;
}

size_t modbus_build_read(uint8_t* out, uint8_t slave, uint8_t fc, uint16_t start, uint16_t count) {
    out[0] = slave; out[1] = fc;
    out[2] = start >> 8; out[3] = start & 0xFF;
    out[4] = count >> 8; out[5] = count & 0xFF;
    uint16_t crc = modbus_crc16(out, 6);
    out[6] = crc & 0xFF; out[7] = crc >> 8;   // CRC: młodszy bajt pierwszy
    return 8;
}

int modbus_parse_read(const uint8_t* buf, size_t len, uint8_t slave, uint8_t fc, uint16_t* regs, int max_regs) {
    if (len < 5 || buf[0] != slave || buf[1] != fc) return 0;   // inna funkcja albo wyjątek (fc|0x80)
    int n = buf[2];
    if (n % 2 || len < (size_t)(3 + n + 2)) return 0;
    uint16_t crc = modbus_crc16(buf, 3 + n);
    if (buf[3 + n] != (crc & 0xFF) || buf[4 + n] != (crc >> 8)) return 0;
    int cnt = n / 2;
    if (cnt > max_regs) cnt = max_regs;
    for (int i = 0; i < cnt; i++) regs[i] = (uint16_t)(buf[3 + 2 * i] << 8 | buf[4 + 2 * i]);
    return cnt;
}

size_t modbus_rtu_reply_len(const uint8_t* buf, size_t len) {
    if (len < 2) return 0;
    uint8_t fc = buf[1];
    if (fc & 0x80) return 5;
    if (fc >= 1 && fc <= 4) return len < 3 ? 0 : (size_t)(3 + buf[2] + 2);
    if (fc == 5 || fc == 6 || fc == 15 || fc == 16) return 8;
    return 0;
}

float modbus_decode(const uint16_t* regs, uint8_t type, bool lo_first) {
    uint32_t w = lo_first ? ((uint32_t)regs[1] << 16 | regs[0]) : ((uint32_t)regs[0] << 16 | regs[1]);
    switch (type) {
        case 1:  return (float)(int16_t)regs[0];
        case 2:  return (float)w;
        case 3:  return (float)(int32_t)w;
        case 4:  { float f; memcpy(&f, &w, 4); return f; }
        case 5:  return (float)regs[0] - 1000.0f;
        default: return (float)regs[0];
    }
}

#ifdef ARDUINO
#include "entity_store.h"
#include "log.h"
#include <Preferences.h>
#include <ArduinoJson.h>
#include <WiFi.h>

#include "modbus_maps.h"

// ── Config ───────────────────────────────────────────────────────────────────
static const int MAX_REGS = 16;   // = pojemność bufora own[] w entity_store
struct Reg { char name[20]; char ent[20]; uint8_t slave, fc, type; bool lo; uint16_t addr; float scale; char unit[8]; };

static bool  s_on = false, s_bridge = false, s_write = false;
static int   s_rx = 16, s_tx = 17, s_de = -1, s_slave = 1;
static long  s_baud = 9600;
static char  s_parity = 'N';
static Reg   s_regs[MAX_REGS]; static uint8_t s_n = 0;
static HardwareSerial s_uart(2);
static WiFiServer s_tcp(502);
static bool  s_tcp_up = false;
static WiFiClient s_cli;

static const uint32_t POLL_MS = 10000, REPLY_MS = 500, GAP_MS = 60;

// ── Automat ──────────────────────────────────────────────────────────────────
enum Who : uint8_t { W_NONE, W_POLL, W_BRIDGE };
static Who      s_who = W_NONE;
static uint8_t  s_idx = 0;
static uint32_t s_sent_at = 0, s_next_at = 0;
static uint8_t  s_rx_buf[260]; static size_t s_rx_len = 0;
static uint32_t s_ok = 0, s_err = 0, s_last_ok = 0, s_br_ok = 0, s_br_err = 0;
static float    s_vals[MAX_REGS]; static bool s_have[MAX_REGS];
static float    s_pv_sum = 0; static uint8_t s_pv_n = 0;

// Most: jedno zapytanie TCP naraz (klient Modbus TCP i tak czeka na odpowiedź przed kolejnym).
static bool     s_br_pending = false;
static uint8_t  s_br_req[260]; static size_t s_br_len = 0;   // ramka RTU (unit + PDU + CRC)
static uint8_t  s_br_mbap[4];                                 // transaction id + protocol id
static uint8_t  s_br_unit = 0;
static uint8_t  s_tcp_buf[260]; static size_t s_tcp_len = 0;

static void sanitize(char* dst, size_t cap, const char* src) {
    size_t j = 0;
    for (size_t i = 0; src && src[i] && j + 1 < cap; i++) {
        char c = src[i];
        if (c >= 'A' && c <= 'Z') c += 32;
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') dst[j++] = c;
        else if (c == ' ' || c == '-' || c == '.') dst[j++] = '_';
    }
    dst[j] = 0;
}

static bool parse_regs(JsonArrayConst a) {
    static Reg tmp[MAX_REGS];
    uint8_t n = 0;
    for (JsonObjectConst o : a) {
        if (n >= MAX_REGS) return false;
        Reg& r = tmp[n];
        sanitize(r.name, sizeof(r.name), o["n"] | "");
        if (!r.name[0]) snprintf(r.name, sizeof(r.name), "r%u", (unsigned)n + 1);
        const char* e = o["e"] | "";
        strlcpy(r.ent, e, sizeof(r.ent));
        int sl = o["s"] | 0, fc = o["f"] | 3, t = o["t"] | 0;
        long ad = o["a"] | -1L;
        if (ad < 0 || ad > 65535 || (fc != 3 && fc != 4) || t < 0 || t > 5 || sl < 0 || sl > 247) return false;
        r.slave = (uint8_t)sl; r.fc = (uint8_t)fc; r.type = (uint8_t)t; r.addr = (uint16_t)ad;
        r.lo = o["o"] | false;
        r.scale = o["k"] | 1.0f;
        strlcpy(r.unit, o["u"] | "", sizeof(r.unit));
        n++;
    }
    memcpy(s_regs, tmp, sizeof(Reg) * n); s_n = n;
    return true;
}

static void reg_from_preset(Reg& r, const PresetReg& q) {
    strlcpy(r.name, q.name, sizeof(r.name)); strlcpy(r.ent, q.ent, sizeof(r.ent));
    r.slave = 0; r.fc = q.fc; r.type = q.type; r.lo = q.lo; r.addr = q.addr; r.scale = q.scale;
    strlcpy(r.unit, q.unit, sizeof(r.unit));
}

static void load_preset(const Preset& p) {
    s_n = 0;
    for (uint8_t i = 0; i < p.n && i < MAX_REGS; i++) reg_from_preset(s_regs[s_n++], p.regs[i]);
}

// 1.13 zapisywał w NVS rozwiniętą listę presetu (Deye: 12 rejestrów), nowsze presety mają ich więcej (błędy
// falownika). Lista będąca dokładnym początkiem presetu dostaje brakujące rejestry; własne listy bez zmian.
static bool extend_preset() {
    for (const Preset& p : PRESETS) {
        if (s_n == 0 || s_n >= p.n) continue;
        bool same = true;
        for (uint8_t i = 0; i < s_n && same; i++) {
            const Reg& r = s_regs[i]; const PresetReg& q = p.regs[i];
            same = r.addr == q.addr && r.fc == q.fc && r.type == q.type && r.lo == q.lo && r.slave == 0
                && !strcmp(r.ent, q.ent) && !strcmp(r.name, q.name);
        }
        if (!same) continue;
        for (uint8_t i = s_n; i < p.n && s_n < MAX_REGS; i++) reg_from_preset(s_regs[s_n++], p.regs[i]);
        return true;
    }
    return false;
}

static void cfg_to_json(JsonDocument& d) {
    d["on"] = s_on; d["bridge"] = s_bridge; d["write"] = s_write;
    d["rx"] = s_rx; d["tx"] = s_tx; d["de"] = s_de; d["baud"] = s_baud;
    char par[2] = { s_parity, 0 }; d["parity"] = par; d["slave"] = s_slave;
    JsonArray a = d["regs"].to<JsonArray>();
    for (uint8_t i = 0; i < s_n; i++) {
        const Reg& r = s_regs[i]; JsonObject o = a.add<JsonObject>();
        o["n"] = r.name; if (r.ent[0]) o["e"] = r.ent; if (r.slave) o["s"] = r.slave;
        o["f"] = r.fc; o["a"] = r.addr; o["t"] = r.type; if (r.lo) o["o"] = true;
        o["k"] = r.scale; o["u"] = r.unit;
    }
}

static void uart_open() {
    s_uart.end();
    if (!s_on && !s_bridge) return;
    uint32_t mode = s_parity == 'E' ? SERIAL_8E1 : s_parity == 'O' ? SERIAL_8O1 : SERIAL_8N1;
    s_uart.begin(s_baud, mode, s_rx, s_tx);
    if (s_de >= 0) { pinMode(s_de, OUTPUT); digitalWrite(s_de, LOW); }
    LOGI("modbus", "RS485 rx=%d tx=%d de=%d %ld 8%c1, rejestry %u, most %s", s_rx, s_tx, s_de, s_baud, s_parity,
         (unsigned)s_n, s_bridge ? (s_write ? "TCP:502 (zapis)" : "TCP:502 (odczyt)") : "wył.");
}

static const char* apply_json(const JsonDocument& d) {
    bool on = d["on"] | s_on, br = d["bridge"] | s_bridge, wr = d["write"] | s_write;
    int rx = d["rx"] | s_rx, tx = d["tx"] | s_tx, de = d["de"] | s_de, sl = d["slave"] | s_slave;
    long baud = d["baud"] | s_baud;
    const char* par = d["parity"] | "";
    if ((on || br) && (rx < 0 || tx < 0 || rx == tx || rx > 48 || tx > 48 || de > 48)) return "bad pins";
    if (baud < 1200 || baud > 115200) return "bad baud";
    if (sl < 1 || sl > 247) return "bad slave";
    const char* pre = d["preset"] | "";
    if (pre[0]) {
        const Preset* p = nullptr;
        for (const Preset& x : PRESETS) if (!strcmp(x.id, pre)) p = &x;
        if (!p) return "unknown preset";
        load_preset(*p);
    } else if (d["regs"].is<JsonArrayConst>()) {
        if (!parse_regs(d["regs"].as<JsonArrayConst>())) return "bad regs (max 16; f 3|4; t 0-5; a 0-65535)";
    }
    s_on = on; s_bridge = br; s_write = wr; s_rx = rx; s_tx = tx; s_de = de; s_slave = sl; s_baud = baud;
    if (par[0] == 'N' || par[0] == 'E' || par[0] == 'O') s_parity = par[0];
    return nullptr;
}

void modbus_init() {
    s_on = s_bridge = s_write = false; s_n = 0;
    Preferences p; p.begin("sensmos", true);
    String js = p.getString("mb_cfg", "");
    p.end();
    if (js.length()) {
        JsonDocument d;
        if (!deserializeJson(d, js)) apply_json(d);
        if (extend_preset()) {
            JsonDocument c; cfg_to_json(c);
            String out; serializeJson(c, out);
            Preferences w; w.begin("sensmos", false); w.putString("mb_cfg", out); w.end();
            LOGI("modbus", "preset uzupełniony do %u rejestrów (nowe w tej wersji FW)", (unsigned)s_n);
        }
    }
    memset(s_have, 0, sizeof(s_have));
    uart_open();
}

const char* modbus_set_config_json(const char* json) {
    JsonDocument d;
    if (deserializeJson(d, json)) return "bad json";
    const char* err = apply_json(d);
    if (err) return err;   // apply_json waliduje przed zmianą stanu
    JsonDocument c; cfg_to_json(c);
    String out; serializeJson(c, out);
    Preferences p; p.begin("sensmos", false); p.putString("mb_cfg", out); p.end();
    s_who = W_NONE; s_idx = 0; s_ok = s_err = s_br_ok = s_br_err = 0; s_next_at = 0; s_br_pending = false;
    if (!s_bridge) { if (s_cli) s_cli.stop(); if (s_tcp_up) { s_tcp.end(); s_tcp_up = false; } }
    memset(s_have, 0, sizeof(s_have));
    uart_open();
    return nullptr;
}

static void uart_send(const uint8_t* f, size_t n) {
    while (s_uart.available()) s_uart.read();   // śmieci z poprzedniej ramki
    if (s_de >= 0) digitalWrite(s_de, HIGH);
    s_uart.write(f, n); s_uart.flush();
    if (s_de >= 0) digitalWrite(s_de, LOW);
    s_rx_len = 0; s_sent_at = millis();
}

// Słowa bitowe błędów falownika (own.inv_*, u32): dokładna liczba całkowita, bez zaokrąglenia floata powyżej 2^24.
static void publish_bits(uint8_t i, const uint16_t* regs) {
    const Reg& r = s_regs[i];
    uint32_t w = r.lo ? ((uint32_t)regs[1] << 16 | regs[0]) : ((uint32_t)regs[0] << 16 | regs[1]);
    s_vals[i] = (float)w; s_have[i] = true;
    char buf[16]; snprintf(buf, sizeof(buf), "%lu", (unsigned long)w);
    entity_push(r.ent, buf, r.unit);
}

static void publish(uint8_t i, float v) {
    const Reg& r = s_regs[i];
    s_vals[i] = v; s_have[i] = true;
    char buf[24], eid[36];
    if (!strcmp(r.ent, "_pv")) { s_pv_sum += v; s_pv_n++; return; }
    if (!strcmp(r.ent, "grid_power")) {   // ze znakiem (+ pobór, − oddawanie) → dwie natywne encje jak w BE
        snprintf(buf, sizeof(buf), "%.0f", v > 0 ? v : 0.0f);  entity_push("pub.grid_import", buf, "W");
        snprintf(buf, sizeof(buf), "%.0f", v < 0 ? -v : 0.0f); entity_push("pub.grid_export", buf, "W");
        return;
    }
    snprintf(buf, sizeof(buf), "%.2f", v);
    if (!r.ent[0])                       snprintf(eid, sizeof(eid), "own.mb_%s", r.name);
    else if (!strncmp(r.ent, "own.", 4)) snprintf(eid, sizeof(eid), "%s", r.ent);
    else                                 snprintf(eid, sizeof(eid), "pub.%s", r.ent);
    entity_push(eid, buf, r.unit);
}

static void poll_next(uint32_t now) {
    s_who = W_NONE;
    if (++s_idx >= s_n) {
        if (s_pv_n) { char buf[16]; snprintf(buf, sizeof(buf), "%.0f", s_pv_sum); entity_push("pub.pv_power", buf, "W"); }
        s_pv_sum = 0; s_pv_n = 0; s_idx = 0; s_next_at = now + POLL_MS;
    } else s_next_at = now + GAP_MS;
}

static void poll_send() {
    const Reg& r = s_regs[s_idx];
    uint8_t req[8];
    size_t n = modbus_build_read(req, r.slave ? r.slave : (uint8_t)s_slave, r.fc, r.addr, r.type >= 2 && r.type <= 4 ? 2 : 1);
    uart_send(req, n);
    s_who = W_POLL;
}

static void tcp_reply(const uint8_t* pdu, size_t n) {   // pdu = unit + funkcja + dane (bez CRC)
    if (!s_cli || !s_cli.connected()) return;
    uint8_t h[6] = { s_br_mbap[0], s_br_mbap[1], s_br_mbap[2], s_br_mbap[3], (uint8_t)(n >> 8), (uint8_t)n };
    s_cli.write(h, 6); s_cli.write(pdu, n);
}

static void tcp_exception(uint8_t fc, uint8_t code) {
    uint8_t p[3] = { s_br_unit, (uint8_t)(fc | 0x80), code };
    tcp_reply(p, 3);
}

// Modbus TCP: MBAP (tid 2, proto 2, len 2, unit 1) + PDU. Ramkę TCP zamieniamy na RTU (unit + PDU + CRC).
static void tcp_service() {
    if (!s_bridge) return;
    if (WiFi.status() != WL_CONNECTED) return;
    if (!s_tcp_up) { s_tcp.begin(); s_tcp.setNoDelay(true); s_tcp_up = true; }
    WiFiClient nc = s_tcp.accept();
    if (nc) { if (s_cli) s_cli.stop(); s_cli = nc; s_tcp_len = 0; LOGI("modbus", "most: klient %s", nc.remoteIP().toString().c_str()); }
    if (!s_cli || !s_cli.connected() || s_br_pending || s_who == W_BRIDGE) return;
    while (s_cli.available() && s_tcp_len < sizeof(s_tcp_buf)) s_tcp_buf[s_tcp_len++] = s_cli.read();
    if (s_tcp_len < 8) return;
    size_t len = (size_t)s_tcp_buf[4] << 8 | s_tcp_buf[5];
    if (s_tcp_buf[2] || s_tcp_buf[3] || len < 2 || len > 254) { s_cli.stop(); s_tcp_len = 0; return; }   // nie Modbus TCP
    if (s_tcp_len < 6 + len) return;
    memcpy(s_br_mbap, s_tcp_buf, 4);
    uint8_t unit = s_tcp_buf[6], fc = s_tcp_buf[7];
    s_br_unit = unit;
    bool read = fc >= 1 && fc <= 4;
    bool write = fc == 5 || fc == 6 || fc == 15 || fc == 16;
    size_t used = 6 + len;
    if (!read && !(write && s_write)) {
        tcp_exception(fc, 0x01);   // ILLEGAL FUNCTION — zapis wyłączony albo nieobsługiwana funkcja
    } else {
        s_br_req[0] = (unit == 0 || unit == 255) ? (uint8_t)s_slave : unit;
        memcpy(s_br_req + 1, s_tcp_buf + 7, len - 1);
        uint16_t crc = modbus_crc16(s_br_req, len);
        s_br_req[len] = crc & 0xFF; s_br_req[len + 1] = crc >> 8;
        s_br_len = len + 2; s_br_pending = true;
    }
    memmove(s_tcp_buf, s_tcp_buf + used, s_tcp_len - used); s_tcp_len -= used;
}

void modbus_tick() {
    if (!s_on && !s_bridge) return;
    tcp_service();
    uint32_t now = millis();

    if (s_who == W_NONE) {
        if (s_br_pending) { uart_send(s_br_req, s_br_len); s_br_pending = false; s_who = W_BRIDGE; return; }
        if (s_on && s_n && (int32_t)(now - s_next_at) >= 0) poll_send();
        return;
    }

    while (s_uart.available() && s_rx_len < sizeof(s_rx_buf)) s_rx_buf[s_rx_len++] = s_uart.read();
    size_t want = modbus_rtu_reply_len(s_rx_buf, s_rx_len);
    bool done = want && s_rx_len >= want;
    bool timeout = now - s_sent_at > REPLY_MS;
    if (!done && !timeout) return;

    if (s_who == W_BRIDGE) {
        uint16_t crc = done && want >= 4 ? modbus_crc16(s_rx_buf, want - 2) : 0;
        if (done && s_rx_buf[want - 2] == (crc & 0xFF) && s_rx_buf[want - 1] == (crc >> 8)) {
            s_rx_buf[0] = s_br_unit;   // klient oczekuje unit id ze swojego zapytania
            tcp_reply(s_rx_buf, want - 2); s_br_ok++;
        } else {
            tcp_exception(s_br_req[1], 0x0B); s_br_err++;   // GATEWAY TARGET FAILED TO RESPOND
        }
        s_who = W_NONE;
        if ((int32_t)(s_next_at - now) < (int32_t)GAP_MS) s_next_at = now + GAP_MS;
        return;
    }

    const Reg& r = s_regs[s_idx];
    int words = r.type >= 2 && r.type <= 4 ? 2 : 1;
    uint16_t regs[2] = { 0, 0 };
    if (done && modbus_parse_read(s_rx_buf, s_rx_len, r.slave ? r.slave : (uint8_t)s_slave, r.fc, regs, 2) == words) {
        if (r.type == 2 && !strncmp(r.ent, "own.inv_", 8)) publish_bits(s_idx, regs);
        else publish(s_idx, modbus_decode(regs, r.type, r.lo) * r.scale);
        s_ok++; s_last_ok = now;
    } else {
        s_err++;
        if (s_err % 50 == 1) LOGW("modbus", "brak odpowiedzi (rejestr %s @%u) — sprawdź A/B RS485, piny, prędkość i slave", r.name, r.addr);
    }
    poll_next(now);
}

void modbus_status_json(char* out, size_t cap) {
    JsonDocument d;
    cfg_to_json(d);
    d["port"] = 502; d["client"] = (bool)(s_cli && s_cli.connected());
    d["ok"] = s_ok; d["err"] = s_err; d["br_ok"] = s_br_ok; d["br_err"] = s_br_err;
    d["age_s"] = s_last_ok ? (long)((millis() - s_last_ok) / 1000) : -1;
    JsonArray pr = d["presets"].to<JsonArray>();
    for (const Preset& p : PRESETS) { JsonObject o = pr.add<JsonObject>(); o["id"] = p.id; o["label"] = p.label; }
    JsonObject v = d["values"].to<JsonObject>();
    for (uint8_t i = 0; i < s_n; i++) if (s_have[i]) v[s_regs[i].name] = s_vals[i];
    serializeJson(d, out, cap);
}
#endif
