#include "kom_config.h"
#if SENSMOS_KOM
#include "kom_panel.h"
#include "kom_panel_html.h"
#include "kom_main.h"
#include "kom_id.h"
#include "kom_store.h"
#include "kom_frame.h"
#include "kom_radio.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <esp_random.h>

// Sieć komunikatora:
// - pierwsze uruchomienie (brak WiFi w ustawieniach): AP SENSMOS-<id8>, hasło KOM_AP_PASS, strona
//   z jednym formularzem — Twoje WiFi;
// - potem LAN na stałe: panel pod http://<IP> i kom-<id8>.local, apka Sensmos znajduje urządzenie
//   w sieci i paruje je z portfelem;
// - AP wraca sam, gdy LAN nie wstanie w 45 s, albo po przytrzymaniu PRG 3 s; gaśnie minutę po
//   połączeniu z LAN.
static WebServer  s_web(80);
static DNSServer  s_dns;
static bool       s_started = false, s_ap = false, s_mdns = false;
static uint32_t   s_sta_since = 0, s_lan_at = 0, s_ap_keep = 0;
static char       s_session[33] = "";
static char       s_lan_ip[16] = "";
static uint8_t    s_fails = 0;
static uint32_t   s_locked_until = 0;

static const uint32_t LAN_WAIT_MS = 45000, AP_AFTER_LAN_MS = 60000;
static const IPAddress AP_IP(192, 168, 4, 1);

const char* kom_panel_lan_ip() { return s_lan_ip; }
bool kom_panel_on() { return s_ap; }

static void new_session() {
    uint8_t r[16];
    esp_fill_random(r, 16);
    for (int i = 0; i < 16; i++) snprintf(s_session + 2 * i, 3, "%02x", r[i]);
}

static void reply(int code, JsonDocument& d) {
    String out;
    serializeJson(d, out);
    s_web.sendHeader("Cache-Control", "no-store");
    s_web.sendHeader("Access-Control-Allow-Origin", "*");
    s_web.send(code, "application/json", out);
}
static void err(int code, const char* e) { JsonDocument d; d["err"] = e; reply(code, d); }
static void ok() { JsonDocument d; d["ok"] = true; reply(200, d); }

static bool body(JsonDocument& d) {
    if (deserializeJson(d, s_web.arg("plain")) != DeserializationError::Ok) { err(400, "json"); return false; }
    return true;
}

static bool authed() {
    if (s_session[0] && s_web.header("X-Kom-Session") == s_session) return true;
    err(401, "session");
    return false;
}

static bool pin_valid(const char* p) {
    size_t n = p ? strlen(p) : 0;
    if (n < 4 || n > 8) return false;
    for (size_t i = 0; i < n; i++) if (p[i] < '0' || p[i] > '9') return false;
    return true;
}

// Nazwa w HELLO: UTF-8 bez znaków sterujących, ≤16 bajtów (serwer odrzuca resztę).
static bool name_valid(const char* s) {
    size_t n = strlen(s);
    if (n > 16) return false;
    for (size_t i = 0; i < n; i++) if ((uint8_t)s[i] < 0x20 || s[i] == 0x7F) return false;
    return true;
}

static void h_index() { s_web.send_P(200, "text/html; charset=utf-8", KOM_PANEL_HTML); }

// Publiczne: apka rozpoznaje po nim komunikator w sieci („kom”: 1).
static void h_id() {
    char id8[9], fp[20];
    kom_id8(id8); kom_fingerprint(fp);
    JsonDocument d;
    d["kom"] = 1; d["id8"] = id8; d["fp"] = fp; d["fw"] = KOM_FW_VERSION; d["name"] = g_set.name;
    d["pin_set"] = g_set.pin_set; d["vis"] = g_set.vis;
    d["wifi_set"] = g_set.wifi_ssid[0] != 0; d["lan_ip"] = s_lan_ip;
    reply(200, d);
}

// Parowanie z portfelem: apka (ta sama sieć) podaje adres, urządzenie potwierdza go radiem
// ramką podpisaną swoim kluczem. Wcześniej apka zgłasza parowanie na serwerze podpisem portfela.
static void h_pair() {
    JsonDocument d;
    if (!body(d)) return;
    uint8_t owner[20];
    const char* o = d["owner"] | "";
    if (strlen(o) != 42 || o[0] != '0' || (o[1] != 'x' && o[1] != 'X') || !kom_hex(o + 2, owner, 20)) return err(400, "owner");
    kom_pair_owner(owner);
    char id8[9];
    kom_id8(id8);
    JsonDocument r; r["ok"] = true; r["id8"] = id8;
    reply(200, r);
}

static void h_pin() {
    JsonDocument d;
    if (!body(d)) return;
    const char* pin = d["pin"] | "";
    if (!pin_valid(pin)) return err(400, "pin_format");
    if (g_set.pin_set) {                               // zmiana: sesja + stary PIN
        if (!authed()) return;
        if (!kom_pin_check(d["old"] | "")) return err(403, "old_pin");
    }
    kom_pin_save(pin);
    new_session();
    JsonDocument r; r["ok"] = true; r["session"] = s_session;
    reply(200, r);
}

static void h_login() {
    if ((int32_t)(millis() - s_locked_until) < 0) {
        JsonDocument r; r["err"] = "locked"; r["wait"] = (s_locked_until - millis()) / 1000 + 1;
        return reply(429, r);
    }
    JsonDocument d;
    if (!body(d)) return;
    if (!g_set.pin_set) return err(409, "no_pin");
    if (!kom_pin_check(d["pin"] | "")) {
        if (++s_fails >= 5) { s_fails = 0; s_locked_until = millis() + 30000; }
        return err(401, "pin");
    }
    s_fails = 0;
    new_session();
    JsonDocument r; r["ok"] = true; r["session"] = s_session;
    reply(200, r);
}

static void h_status() {
    if (!authed()) return;
    char id8[9], fp[20];
    kom_id8(id8); kom_fingerprint(fp);
    KomStats st = kom_stats();
    JsonDocument d;
    d["id8"] = id8; d["fp"] = fp; d["fw"] = KOM_FW_VERSION; d["name"] = g_set.name; d["vis"] = g_set.vis;
    d["board"] = kom_radio_board(); d["radio"] = kom_radio_ok(); d["selftest"] = st.selftest;
    d["duty_ms"] = kom_radio_duty_ms(); d["duty_max"] = KOM_DUTY_UP_MS_H;
    d["hello_n"] = st.hello_n;
    if (st.hello_ago_s != UINT32_MAX) d["hello_ago_s"] = st.hello_ago_s;
    d["rx_n"] = st.rx_n; d["rx_rssi"] = st.rx_rssi;
    JsonArray t = d["tpl"].to<JsonArray>();
    for (int i = 0; i < g_set.tpl_n; i++) t.add(g_set.tpl[i]);
    JsonObject w = d["wifi"].to<JsonObject>();
    w["ssid"] = g_set.wifi_ssid; w["lan_ip"] = s_lan_ip;
    char host[24]; snprintf(host, sizeof(host), "kom-%s.local", id8);
    w["host"] = host;
    reply(200, d);
}

static void h_vis() {
    if (!authed()) return;
    JsonDocument d;
    if (!body(d)) return;
    int v = d["vis"] | -1;
    if (v < 0 || v > 2) return err(400, "vis");
    kom_vis_save((uint8_t)v);
    kom_request_hello();
    ok();
}

static void h_name() {
    if (!authed()) return;
    JsonDocument d;
    if (!body(d)) return;
    const char* n = d["name"] | "";
    if (!name_valid(n)) return err(400, "name");
    kom_name_save(n);
    kom_request_hello();
    ok();
}

static void h_templates() {
    if (!authed()) return;
    JsonDocument d;
    if (!body(d)) return;
    JsonArray a = d["list"].as<JsonArray>();
    if (a.isNull() || a.size() > KOM_TPL_MAX) return err(400, "list");
    for (JsonVariant v : a) {
        const char* s = v | "";
        if (strlen(s) > KOM_TPL_LEN || !kom_text_ok(s)) return err(400, "text");
    }
    g_set.tpl_n = 0;
    for (JsonVariant v : a) strlcpy(g_set.tpl[g_set.tpl_n++], v | "", KOM_TPL_LEN + 1);
    kom_tpl_save();
    ok();
}

static void h_send() {
    if (!authed()) return;
    JsonDocument d;
    if (!body(d)) return;
    int32_t r = kom_send_msg(d["text"] | "");
    if (r >= 0) { JsonDocument o; o["ok"] = true; o["air_ms"] = r; return reply(200, o); }
    err(r == -1 ? 429 : 400, r == -1 ? "budget" : r == -4 ? "text" : "radio");
}

static void h_hello() { if (!authed()) return; kom_request_hello(); ok(); }

// Pierwsze uruchomienie (brak WiFi): bez PIN-u — chroni hasło AP. Zmiana później: z sesją.
static void h_wifi() {
    if (g_set.wifi_ssid[0] && !authed()) return;
    JsonDocument d;
    if (!body(d)) return;
    const char* ssid = d["ssid"] | "";
    const char* pass = d["pass"] | "";
    if (!ssid[0] || strlen(ssid) > 32 || strlen(pass) > 64 || (pass[0] && strlen(pass) < 8)) return err(400, "wifi");
    kom_wifi_save(ssid, pass);
    s_lan_ip[0] = 0;
    s_lan_at = 0;
    s_sta_since = millis();
    WiFi.mode(s_ap ? WIFI_AP_STA : WIFI_STA);
    WiFi.begin(ssid, pass);
    Serial.printf("[kom] LAN: lacze z \"%s\"\n", ssid);
    ok();
}

static void h_reset() {
    if (!authed()) return;
    JsonDocument d;
    if (!body(d)) return;
    if (strcmp(d["confirm"] | "", "RESET") != 0) return err(400, "confirm");
    ok();
    delay(300);
    kom_factory_reset();
}

// Portal przechwytujący (tylko przy AP): telefon sprawdza internet pod znanymi adresami — każde
// obce żądanie odsyłamy na stronę główną, więc system sam proponuje „Zaloguj się do sieci”.
static void h_other() {
    String host = s_web.hostHeader();
    if (s_ap && host != AP_IP.toString() && (!s_lan_ip[0] || host != s_lan_ip) && !host.endsWith(".local")) {
        s_web.sendHeader("Location", "http://192.168.4.1/", true);
        s_web.send(302, "text/plain", "");
        return;
    }
    s_web.send(404, "text/plain", "404");
}

static void ap_start() {
    if (s_ap) return;
    char id8[9], ssid[32];
    kom_id8(id8);
    snprintf(ssid, sizeof(ssid), "SENSMOS-%s", id8);
    WiFi.mode(g_set.wifi_ssid[0] ? WIFI_AP_STA : WIFI_AP);
    WiFi.softAPConfig(AP_IP, AP_IP, IPAddress(255, 255, 255, 0));
    bool okap = WiFi.softAP(ssid, KOM_AP_PASS);
    // ESP32-S3 z małą anteną (Heltec) przy pełnej mocy potrafi „gubić” AP — telefon go nie widzi.
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    s_dns.setErrorReplyCode(DNSReplyCode::NoError);
    s_dns.start(53, "*", AP_IP);
    s_ap = true;
    Serial.printf("[kom] AP %s %s (haslo %s)\n", ssid, okap ? "OK" : "BLAD", KOM_AP_PASS);
}

static void ap_stop() {
    if (!s_ap) return;
    s_dns.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(g_set.wifi_ssid[0] ? WIFI_STA : WIFI_OFF);
    s_ap = false;
    Serial.println("[kom] AP wylaczony");
}

void kom_panel_start() {
    if (s_started) return;
    s_started = true;
    WiFi.persistent(false);
    WiFi.setHostname("sensmos-kom");
    static const char* HDR[] = { "X-Kom-Session" };
    s_web.collectHeaders(HDR, 1);
    s_web.on("/", HTTP_GET, h_index);
    s_web.on("/api/id", HTTP_GET, h_id);
    s_web.on("/api/pair", HTTP_POST, h_pair);
    s_web.on("/api/pin", HTTP_POST, h_pin);
    s_web.on("/api/login", HTTP_POST, h_login);
    s_web.on("/api/status", HTTP_GET, h_status);
    s_web.on("/api/vis", HTTP_POST, h_vis);
    s_web.on("/api/name", HTTP_POST, h_name);
    s_web.on("/api/templates", HTTP_POST, h_templates);
    s_web.on("/api/send", HTTP_POST, h_send);
    s_web.on("/api/hello", HTTP_POST, h_hello);
    s_web.on("/api/wifi", HTTP_POST, h_wifi);
    s_web.on("/api/reset", HTTP_POST, h_reset);
    s_web.onNotFound(h_other);
    if (g_set.wifi_ssid[0]) {
        WiFi.mode(WIFI_STA);
        WiFi.begin(g_set.wifi_ssid, g_set.wifi_pass);
        s_sta_since = millis();
        Serial.printf("[kom] LAN: lacze z \"%s\"\n", g_set.wifi_ssid);
    } else {
        ap_start();
    }
    s_web.begin();
}

void kom_panel_ap(bool on) {
    if (on) { ap_start(); s_ap_keep = millis() + 600000; }
    else ap_stop();
}

void kom_panel_stop() { ap_stop(); }

void kom_panel_tick() {
    if (!s_started) return;
    if (s_ap) s_dns.processNextRequest();
    s_web.handleClient();
    if (!g_set.wifi_ssid[0]) return;
    uint32_t now = millis();
    bool up = WiFi.status() == WL_CONNECTED;
    if (up && !s_lan_ip[0]) {
        strlcpy(s_lan_ip, WiFi.localIP().toString().c_str(), sizeof(s_lan_ip));
        s_lan_at = now;
        char id8[9], host[20];
        kom_id8(id8);
        snprintf(host, sizeof(host), "kom-%s", id8);
        if (!s_mdns && MDNS.begin(host)) { MDNS.addService("http", "tcp", 80); s_mdns = true; }
        Serial.printf("[kom] LAN %s (%s.local)\n", s_lan_ip, host);
    } else if (!up && s_lan_ip[0]) {
        s_lan_ip[0] = 0;
        s_sta_since = now;
        Serial.println("[kom] LAN rozlaczony");
    }
    if (!up && !s_ap && now - s_sta_since > LAN_WAIT_MS) ap_start();              // LAN nie wstał — AP na ratunek
    if (up && s_ap && s_lan_at && now - s_lan_at > AP_AFTER_LAN_MS && (int32_t)(now - s_ap_keep) > 0)
        ap_stop();                                                               // LAN działa — AP zbędny
}

#endif
