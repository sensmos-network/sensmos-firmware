#include "gnss.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

// ── Czysta logika ─────────────────────────────────────────────

static int hexv(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

bool gnss_nmea_checksum_ok(const char* line) {
    if (!line || line[0] != '$') return false;
    uint8_t x = 0;
    const char* p = line + 1;
    for (; *p && *p != '*'; p++) x ^= (uint8_t)*p;
    if (*p != '*') return false;
    int a = hexv(p[1]), b = hexv(p[2]);
    if (a < 0 || b < 0) return false;
    return x == (uint8_t)(a * 16 + b);
}

// ddmm.mmmm / dddmm.mmmm → stopnie
static bool nmea_deg(const char* f, size_t len, char hemi, double& out) {
    if (!len || len > 15) return false;
    char buf[16]; memcpy(buf, f, len); buf[len] = 0;
    char* end = nullptr;
    double v = strtod(buf, &end);
    if (end == buf || v < 0) return false;
    double deg = floor(v / 100.0);
    double min = v - deg * 100.0;
    if (min >= 60.0) return false;
    out = deg + min / 60.0;
    if (hemi == 'S' || hemi == 'W') out = -out;
    else if (hemi != 'N' && hemi != 'E') return false;
    return true;
}

bool gnss_parse_gga(const char* line, GnssFix& out) {
    memset(&out, 0, sizeof(out));
    if (!gnss_nmea_checksum_ok(line)) return false;
    // $GPGGA / $GNGGA / $GLGGA ... : talker 2 znaki + "GGA"
    if (strlen(line) < 7 || strncmp(line + 3, "GGA,", 4) != 0) return false;
    const char* fld[16]; size_t len[16]; int n = 0;
    const char* p = line + 7;
    while (n < 16) {
        const char* q = p;
        while (*q && *q != ',' && *q != '*') q++;
        fld[n] = p; len[n] = (size_t)(q - p); n++;
        if (*q != ',') break;
        p = q + 1;
    }
    // 0 czas, 1 lat, 2 N/S, 3 lon, 4 E/W, 5 jakość, 6 satelity, 7 HDOP
    if (n < 8) return false;
    int q = len[5] ? atoi(fld[5]) : 0;
    if (q < 1 || q > 8 || q == 6) return true;   // brak fixu albo estymacja (6) — ramka poprawna, fix nie
    if (!len[2] || !len[4]) return true;
    double la, lo;
    if (!nmea_deg(fld[1], len[1], fld[2][0], la) || !nmea_deg(fld[3], len[3], fld[4][0], lo)) return true;
    if (fabs(la) > 90 || fabs(lo) > 180 || (la == 0 && lo == 0)) return true;
    out.lat = la; out.lon = lo;
    out.sats = (uint8_t)(len[6] ? atoi(fld[6]) : 0);
    out.hdop = len[7] ? (float)atof(fld[7]) : 99.0f;
    out.valid = true;
    return true;
}

void gnss_stable_reset(GnssStable& s) { s.good = 0; s.lat0 = s.lon0 = 0; }

static double km(double a1, double o1, double a2, double o2) {
    const double R = 6371, rad = M_PI / 180;
    double dA = (a2 - a1) * rad, dO = (o2 - o1) * rad;
    double s = sin(dA / 2) * sin(dA / 2) + cos(a1 * rad) * cos(a2 * rad) * sin(dO / 2) * sin(dO / 2);
    return 2 * R * asin(fmin(1.0, sqrt(s)));
}

bool gnss_stable_feed(GnssStable& s, const GnssFix& f) {
    if (!f.valid || f.sats < 5 || f.hdop > 3.0f || f.hdop <= 0) { gnss_stable_reset(s); return false; }
    if (s.good == 0) { s.lat0 = f.lat; s.lon0 = f.lon; s.good = 1; return true; }
    if (km(s.lat0, s.lon0, f.lat, f.lon) > 0.2) {   // skok > 200 m: nowa seria od tego punktu
        s.lat0 = f.lat; s.lon0 = f.lon; s.good = 1; return true;
    }
    if (s.good < 0xFFFFFFF0u) s.good++;
    return true;
}

size_t gnss_build_frame(char* out, size_t cap, const GnssFix& f, uint32_t stable_s) {
    if (!f.valid) return 0;
    // 0,01° (~1 km): BE porównuje z kratką deklaracji, dokładna pozycja nie opuszcza noda.
    double la = round(f.lat * 100.0) / 100.0, lo = round(f.lon * 100.0) / 100.0;
    int n = snprintf(out, cap, "{\"type\":\"gnss\",\"lat\":%.2f,\"lon\":%.2f,\"sats\":%u,\"hdop\":%.1f,\"stable_s\":%lu}",
                     la, lo, (unsigned)f.sats, (double)f.hdop, (unsigned long)stable_s);
    return (n > 0 && (size_t)n < cap) ? (size_t)n : 0;
}

// ── Sprzęt (tylko ESP32) ──────────────────────────────────────
#ifdef ARDUINO
#include <ArduinoJson.h>
#include <Preferences.h>
#include "ws_client.h"
#include "log.h"

static const unsigned long GNSS_REPORT_MS = 30UL * 60 * 1000;   // raport co 30 min (pierwszy zaraz po stabilnym)
static HardwareSerial s_gps(1);   // UART1: UART2 ma Modbus
static bool     s_on = false;
static int      s_rx = -1;
static uint32_t s_baud = 9600;
static char     s_line[100];
static size_t   s_len = 0;
static GnssFix    s_last = {};
static GnssStable s_st = {};
static unsigned long s_next = 0, s_last_ok_ms = 0, s_sent_ms = 0;
static uint32_t s_frames = 0, s_bad = 0;
static bool s_reported = false;

static void uart_open() {
    s_gps.end();
    s_len = 0; gnss_stable_reset(s_st); s_reported = false;
    if (!s_on || s_rx < 0) return;
    s_gps.begin(s_baud, SERIAL_8N1, s_rx, -1);
    LOGI("gnss", "UART1 rx=%d %lu bd", s_rx, (unsigned long)s_baud);
}

static void cfg_save() {
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"on\":%s,\"rx\":%d,\"baud\":%lu}", s_on ? "true" : "false", s_rx, (unsigned long)s_baud);
    Preferences p; p.begin("sensmos", false); p.putString("gnss_cfg", buf); p.end();
}

static const char* cfg_apply(const char* json, bool save) {
    JsonDocument d;
    if (deserializeJson(d, json ? json : "")) return "bad json";
    bool on = d["on"] | s_on;
    int rx = d["rx"] | s_rx;
    long bd = d["baud"] | (long)s_baud;
    if (on && (rx < 0 || rx > 48)) return "rx pin required (0-48)";
    if (bd != 4800 && bd != 9600 && bd != 19200 && bd != 38400 && bd != 57600 && bd != 115200) return "baud: 4800..115200";
    s_on = on; s_rx = rx; s_baud = (uint32_t)bd;
    if (save) cfg_save();
    uart_open();
    return nullptr;
}

void gnss_init() {
    Preferences p; p.begin("sensmos", true);
    String c = p.getString("gnss_cfg", "");
    p.end();
    if (c.length()) cfg_apply(c.c_str(), false);
}

const char* gnss_set_config_json(const char* json) { return cfg_apply(json, true); }

void gnss_status_json(char* out, size_t cap) {
    uint32_t st = s_st.good;
    bool fresh = s_last_ok_ms && millis() - s_last_ok_ms < 10000UL;
    snprintf(out, cap, "{\"on\":%s,\"rx\":%d,\"baud\":%lu,\"frames\":%lu,\"bad\":%lu,\"fix\":%s,"
                       "\"sats\":%u,\"hdop\":%.1f,\"stable\":%s,\"stable_s\":%lu,\"lat\":%.2f,\"lon\":%.2f,\"sent_ago_s\":%ld}",
             s_on ? "true" : "false", s_rx, (unsigned long)s_baud, (unsigned long)s_frames, (unsigned long)s_bad,
             (fresh && s_last.valid) ? "true" : "false", (unsigned)s_last.sats, (double)s_last.hdop,
             st >= GNSS_STABLE_MIN ? "true" : "false", (unsigned long)st,
             s_last.valid ? round(s_last.lat * 100) / 100 : 0.0, s_last.valid ? round(s_last.lon * 100) / 100 : 0.0,
             s_sent_ms ? (long)((millis() - s_sent_ms) / 1000) : -1L);
}

static void on_line() {
    s_line[s_len] = 0;
    if (s_len < 7) return;
    GnssFix f;
    if (!gnss_parse_gga(s_line, f)) { if (strstr(s_line, "GGA,")) s_bad++; return; }
    s_frames++;
    s_last = f;
    if (f.valid) s_last_ok_ms = millis();
    gnss_stable_feed(s_st, f);
}

void gnss_tick() {
    if (!s_on || s_rx < 0) return;
    int budget = 256;   // nie zajmuj loop(): przy 9600 bd to i tak ~1 s danych
    while (budget-- > 0 && s_gps.available()) {
        char c = (char)s_gps.read();
        if (c == '\r') continue;
        if (c == '\n') { on_line(); s_len = 0; continue; }
        if (c == '$') s_len = 0;
        if (s_len < sizeof(s_line) - 1) s_line[s_len++] = c; else s_len = 0;
    }
    if (s_st.good < GNSS_STABLE_MIN || !ws_client_connected()) return;
    unsigned long now = millis();
    if (s_reported && (long)(now - s_next) < 0) return;
    char frame[160];
    if (gnss_build_frame(frame, sizeof(frame), s_last, s_st.good) && ws_client_send_raw(frame)) {
        s_reported = true; s_sent_ms = now; s_next = now + GNSS_REPORT_MS;
        LOGI("gnss", "stabilny fix wyslany (%u sat, hdop %.1f)", (unsigned)s_last.sats, (double)s_last.hdop);
    }
}
#endif
