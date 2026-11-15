#include "kom_config.h"
#if SENSMOS_KOM
#include "kom_home.h"
#include "kom_dframe.h"
#include "kom_frame.h"
#include "kom_id.h"
#include "kom_main.h"
#include "kom_radio.h"
#include <Arduino.h>
#include <Preferences.h>
#include <mbedtls/aes.h>
#include <mbedtls/sha256.h>

KomHome g_home = {};

enum { H_IDLE, H_WAIT, H_OK, H_FAIL };
static uint8_t  s_state = H_IDLE;
static uint32_t s_n = 0;                    // licznik wiadomości do domu (NVS), w treści „#n”
static uint8_t  s_tries = 0;
static uint32_t s_retry_at = 0, s_poll_at = 0;
static char     s_text[KOM_TEXT_MAX + 1];
static uint32_t s_last_crc = 0;             // ta sama ramka z domu dwa razy = jedna
static KomPing  s_ping = {};
static uint32_t s_ping_cnt = 0, s_ping_at = 0;

static bool due(uint32_t now, uint32_t t) { return (int32_t)(now - t) >= 0; }

static void ctr_mbed(const uint8_t key[32], const uint8_t iv[16], uint8_t* buf, size_t n) {
    uint8_t nc[16], sb[16];
    size_t off = 0;
    memcpy(nc, iv, 16);
    mbedtls_aes_context a;
    mbedtls_aes_init(&a);
    mbedtls_aes_setkey_enc(&a, key, 256);
    mbedtls_aes_crypt_ctr(&a, n, &off, nc, sb, buf, buf);
    mbedtls_aes_free(&a);
}

void kom_home_load() {
    Preferences p; p.begin("sensmos_kom", true);
    g_home.set = p.getBytes("h_id", g_home.id4, 4) == 4 && p.getBytes("h_key", g_home.key, 32) == 32;
    g_home.sub = p.getUChar("h_sub", 0);
    g_home.poll_min = p.getUChar("h_poll", 0);
    s_n = p.getUInt("h_n", 0);
    p.end();
    if (!g_home.sub) g_home.set = false;
    s_poll_at = millis() + 60000UL * (g_home.poll_min ? g_home.poll_min : 1);
}

bool kom_home_set(const char* id8, const char* phrase, uint8_t sub, uint8_t poll_min) {
    uint8_t id[4];
    if (!id8 || strlen(id8) != 8 || !kom_hex(id8, id, 4) || !phrase || strlen(phrase) < 8 || !sub || poll_min > 120)
        return false;
    memcpy(g_home.id4, id, 4);
    mbedtls_sha256((const uint8_t*)phrase, strlen(phrase), g_home.key, 0);
    g_home.sub = sub; g_home.poll_min = poll_min; g_home.set = true;
    Preferences p; p.begin("sensmos_kom", false);
    p.putBytes("h_id", g_home.id4, 4); p.putBytes("h_key", g_home.key, 32);
    p.putUChar("h_sub", sub); p.putUChar("h_poll", poll_min);
    p.end();
    s_state = H_IDLE;
    s_poll_at = millis() + 60000UL * (poll_min ? poll_min : 1);
    Serial.printf("[kom] dom: baza %s, sub %u, pytanie co %u min\n", id8, sub, poll_min);
    return true;
}

void kom_home_clear() {
    memset(&g_home, 0, sizeof(g_home));
    Preferences p; p.begin("sensmos_kom", false);
    p.remove("h_id"); p.remove("h_key"); p.remove("h_sub"); p.remove("h_poll");
    p.end();
    s_state = H_IDLE;
}

static int32_t tx_payload(const char* payload) {
    uint8_t f[KOM_D_HDR + KOM_D_NONCE + KOM_D_PAYLOAD_MAX + 4], nonce[4];
    kom_radio_random(nonce, 4);
    size_t n = kom_dframe_build(f, g_home.id4, g_home.sub, (const uint8_t*)payload, strlen(payload), g_home.key, nonce, ctr_mbed);
    if (!n) return -4;
    int32_t r = kom_radio_send_up(f, n);
    if (r >= 0) kom_radio_listen_up(KOM_HOME_LISTEN_MS);   // odpowiedź bazy przyjdzie na 868.1
    return r;
}

static int32_t tx_msg() {
    char p[KOM_D_PAYLOAD_MAX + 1];
    snprintf(p, sizeof(p), "#%lu %s", (unsigned long)s_n, s_text);
    int32_t r = tx_payload(p);
    Serial.printf("[kom] do domu #%lu (proba %u): %ld\n", (unsigned long)s_n, s_tries, (long)r);
    return r;
}

int32_t kom_home_send(const char* text, uint32_t* n) {
    if (!g_home.set) return -3;
    if (!kom_radio_ok()) return -2;
    if (!kom_text_ok(text) || strlen(text) > KOM_D_PAYLOAD_MAX - 8) return -4;
    if (s_state == H_WAIT) return -5;
    strlcpy(s_text, text, sizeof(s_text));
    s_n++;
    { Preferences p; p.begin("sensmos_kom", false); p.putUInt("h_n", s_n); p.end(); }
    s_tries = 1;
    int32_t r = tx_msg();
    if (r == -1) { s_n--; return -1; }
    if (r < 0) return r;
    s_state = H_WAIT;
    s_retry_at = millis() + KOM_HOME_LISTEN_MS;
    s_poll_at = millis() + 60000UL * (g_home.poll_min ? g_home.poll_min : 1);
    char line[KOM_TEXT_MAX + 16];
    snprintf(line, sizeof(line), "> Dom: %s", text);
    kom_box_note(true, line);
    if (n) *n = s_n;
    return r;
}

int32_t kom_home_ping(uint32_t* n) {
    if (!g_home.set) return -3;
    if (!kom_radio_ok()) return -2;
    if (s_ping.st == 1) return -5;
    char p[24];
    const uint32_t pn = KOM_PING_BASE + ++s_ping_cnt;
    snprintf(p, sizeof(p), "t #%lu", (unsigned long)pn);
    int32_t r = tx_payload(p);
    if (r < 0) { s_ping_cnt--; return r; }
    s_ping = {};
    s_ping.n = pn; s_ping.st = 1;
    s_ping_at = millis();
    kom_radio_listen_up(KOM_HOME_PING_MS);
    Serial.printf("[kom] test zasiegu #%lu: %ld ms w eterze\n", (unsigned long)pn, (long)r);
    if (n) *n = pn;
    return r;
}

const KomPing& kom_home_ping_last() { return s_ping; }

bool kom_home_rx(const uint8_t* b, size_t n, uint32_t now, float rssi, float snr) {
    if (!g_home.set) return false;
    char p[KOM_D_PAYLOAD_MAX + 1];
    int pn = kom_dframe_open(b, n, g_home.id4, g_home.sub, g_home.key, ctr_mbed, (uint8_t*)p, sizeof(p));
    if (pn < 0) return false;
    const uint32_t crc = kom_crc32(b, n);
    if (crc == s_last_crc) return true;
    s_last_crc = crc;
    uint32_t ack; const char* reply;
    kom_home_parse(p, &ack, &reply);
    Serial.printf("[kom] z domu: ack #%lu%s\n", (unsigned long)ack, reply ? ", z odpowiedzia" : "");
    if (ack && ack == s_ping.n && s_ping.st == 1) {         // odpowiedź na test zasięgu: bez wpisu w skrzynce
        s_ping.st = 2; s_ping.rssi = rssi; s_ping.snr = snr; s_ping.rtt_ms = now - s_ping_at;
        Serial.printf("[kom] test zasiegu #%lu: OK, RSSI %.0f, SNR %.1f, %lu ms\n", (unsigned long)ack, rssi, snr,
                      (unsigned long)s_ping.rtt_ms);
        if (!reply) return true;
    }
    if (ack && ack == s_n && s_state == H_WAIT) {
        s_state = H_OK;
        kom_box_note(false, "Dom: odebrane");
        kom_show("Dom", "odebrane");
    }
    if (reply) {
        char line[KOM_D_PAYLOAD_MAX + 8];
        snprintf(line, sizeof(line), "Dom: %s", reply);
        kom_box_note(false, line);
        kom_show("Dom", reply);
    }
    kom_radio_listen_up(KOM_HOME_LISTEN_MS / 2);            // dom mógł mieć więcej do powiedzenia
    return true;
}

void kom_home_tick(uint32_t now) {
    if (!g_home.set) return;
    if (s_ping.st == 1 && now - s_ping_at > KOM_HOME_PING_MS) s_ping.st = 3;
    if (s_state == H_WAIT && due(now, s_retry_at)) {
        if (s_tries >= KOM_HOME_TRIES) {
            s_state = H_FAIL;
            Serial.printf("[kom] do domu #%lu: brak potwierdzenia po %u probach\n", (unsigned long)s_n, s_tries);
            kom_box_note(false, "Dom: brak potwierdzenia");
            kom_show("Dom", "brak potwierdzenia");
            return;
        }
        s_tries++;
        int32_t r = tx_msg();
        s_retry_at = now + (r == -1 ? KOM_RETRY_MS : KOM_HOME_LISTEN_MS);
        if (r == -1) s_tries--;
        return;
    }
    if (g_home.poll_min && s_state != H_WAIT && due(now, s_poll_at)) {
        s_poll_at = now + 60000UL * g_home.poll_min;
        int32_t r = tx_payload("?");
        Serial.printf("[kom] do domu: pytanie o odpowiedz: %ld\n", (long)r);
    }
}

const char* kom_home_state() {
    if (!g_home.set) return "off";
    switch (s_state) { case H_WAIT: return "wait"; case H_OK: return "ok"; case H_FAIL: return "fail"; default: return "idle"; }
}

uint32_t kom_home_last() { return s_n; }

#endif
