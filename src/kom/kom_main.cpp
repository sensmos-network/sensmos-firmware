#include "kom_config.h"
#if SENSMOS_KOM
#include "kom_main.h"
#include "kom_id.h"
#include "kom_frame.h"
#include "kom_radio.h"
#include "kom_ui.h"
#include "kom_store.h"
#include "kom_panel.h"
#include "kom_fixture.h"
#include <Arduino.h>

static bool     s_selftest = false;
static char     s_id8[9], s_fp[20];
static uint32_t s_rx_n = 0;
static float    s_rx_rssi = 0;
static uint32_t s_hello_n = 0, s_hello_at = 0;
static bool     s_hello_any = false;

static const uint32_t BOOT_FULL[] = KOM_HELLO_BOOT_MS;
static const uint8_t  N_BOOT = sizeof(BOOT_FULL) / sizeof(BOOT_FULL[0]);
static uint8_t  s_boot_i = 0;
static uint32_t s_next_short = 0, s_next_full = 0, s_retry_at = 0;
static uint32_t s_msg_until = 0, s_draw_at = 0;
static bool     s_force_hello = false;

static bool due(uint32_t now, uint32_t t) { return (int32_t)(now - t) >= 0; }

// Kryptografia sprawdzona na wektorach z BE (RFC 7748 + testowy klucz BE). Bez zgodności
// urządzenie nie nadaje.
static bool selftest() {
    uint8_t pub[32], knet[32], h[32], f[KOM_FRAME_MAX];
    if (!kom_x25519(KOMT_DEV_PRIV, nullptr, pub) || memcmp(pub, KOMT_DEV_PUB, 32)) return false;
    if (!kom_knet(KOMT_DEV_PRIV, pub, KOMT_BE_PUB, knet) || memcmp(knet, KOMT_K_NET, 32)) return false;
    kom_sha256(pub, 32, h);
    size_t n = kom_build_hello(f, pub, h, knet, 1, 1, false, false, nullptr);
    if (n != sizeof(KOMT_HELLO_SHORT) || memcmp(f, KOMT_HELLO_SHORT, n)) return false;
    n = kom_build_hello(f, pub, h, knet, 7, 0, true, true, nullptr);
    if (n != sizeof(KOMT_HELLO_PAIR) || memcmp(f, KOMT_HELLO_PAIR, n)) return false;
    n = kom_build_acct(f, h, knet, 6, "kod:ALARM", false, KOMT_NONCE);
    return n == sizeof(KOMT_ACCT) && !memcmp(f, KOMT_ACCT, n);
}

KomStats kom_stats() {
    return { s_rx_n, s_rx_rssi, s_hello_n, s_hello_any ? (millis() - s_hello_at) / 1000 : UINT32_MAX, s_selftest };
}

void kom_request_hello() { s_force_hello = true; s_retry_at = millis(); }

static int32_t send_up(const uint8_t* f, size_t n) {
    digitalWrite(KOM_PIN_LED, HIGH);
    int32_t r = kom_radio_send_up(f, n);
    digitalWrite(KOM_PIN_LED, LOW);
    return r;
}

int32_t kom_send_msg(const char* text) {
    if (!s_selftest || !kom_radio_ok()) return -2;
    uint8_t f[KOM_FRAME_MAX];
    size_t n = kom_build_acct(f, g_kom.id4, g_kom.knet, kom_ctr_next(), text, false, nullptr);
    if (!n) return -4;
    int32_t r = send_up(f, n);
    Serial.printf("[kom] do konta \"%s\": %ld\n", text, (long)r);
    return r;
}

// Pełne HELLO z polem OWN: serwer przypina urządzenie do portfela, który w ciągu 3 min zgłosił
// parowanie w apce. Mignięcie diodą potwierdza nadanie (Wireless Paper nie ma jeszcze ekranu).
int32_t kom_pair() {
    if (!s_selftest || !kom_radio_ok()) return -2;
    uint8_t f[KOM_FRAME_MAX];
    uint32_t ctr = kom_ctr_next();
    uint8_t vis = g_set.vis == KOM_VIS_UNSET ? 0 : g_set.vis;
    size_t n = kom_build_hello(f, g_kom.pub, g_kom.id4, g_kom.knet, ctr, vis, true, true, g_set.name[0] ? g_set.name : nullptr);
    int32_t r = send_up(f, n);
    Serial.printf("[kom] parowanie: HELLO z OWN ctr %lu: %ld\n", (unsigned long)ctr, (long)r);
    return r;
}

// Widoczność i nazwa z ustawień; do pierwszego wyboru w panelu urządzenie jest ukryte (vis 0).
static int32_t send_hello(bool full) {
    uint8_t f[KOM_FRAME_MAX];
    uint32_t ctr = kom_ctr_next();
    uint8_t vis = g_set.vis == KOM_VIS_UNSET ? 0 : g_set.vis;
    const char* name = full && g_set.name[0] ? g_set.name : nullptr;
    size_t n = kom_build_hello(f, g_kom.pub, g_kom.id4, g_kom.knet, ctr, vis, full, false, name);
    int32_t r = send_up(f, n);
    if (r >= 0) {
        s_hello_n++; s_hello_at = millis(); s_hello_any = true;
        Serial.printf("[kom] HELLO%s ctr %lu, vis %u, %u B, %ld ms, pasmo %lu ms/h\n", full ? " pelne" : "",
                      (unsigned long)ctr, vis, (unsigned)n, (long)r, (unsigned long)kom_radio_duty_ms());
    } else {
        Serial.printf("[kom] HELLO nie poszlo (%s)\n", r == -1 ? "brak budzetu pasma" : "blad radia");
    }
    return r;
}

static void hello_tick(uint32_t now) {
    if (!s_selftest || !kom_radio_ok() || !due(now, s_retry_at)) return;
    bool full;
    if (s_force_hello)                                            full = true;
    else if (s_boot_i < N_BOOT && due(now, BOOT_FULL[s_boot_i])) full = true;
    else if (s_boot_i >= N_BOOT && due(now, s_next_full))        full = true;
    else if (s_boot_i >= N_BOOT && due(now, s_next_short))       full = false;
    else return;
    int32_t r = send_hello(full);
    if (r < 0) { s_retry_at = now + KOM_RETRY_MS; return; }
    if (s_force_hello) s_force_hello = false;
    else if (s_boot_i < N_BOOT) s_boot_i++;
    else if (full) s_next_full = now + KOM_HELLO_FULL_MS;
    s_next_short = now + KOM_HELLO_EVERY_MS;
}

static void rx_tick() {
    uint8_t b[256];
    size_t n; float rssi, snr;
    if (!kom_radio_poll(b, sizeof(b), &n, &rssi, &snr)) return;
    s_rx_n++; s_rx_rssi = rssi;
    bool mine = n >= 19 && b[0] == 0xE0 && b[1] == 0x04 && !memcmp(b + 3, g_kom.id4, 4);
    Serial.printf("[kom] RX %u B, RSSI %.0f, SNR %.1f, %02x %02x%s\n", (unsigned)n, rssi, snr, b[0], n > 1 ? b[1] : 0,
                  mine ? " -> DO MNIE" : "");
}

static void flash_msg(uint32_t now, const char* l1, const char* l2) {
    kom_ui_msg(l1, l2);
    s_msg_until = now + 2500;
}

static void send_template(uint32_t now) {
    if (!g_set.tpl_n) return flash_msg(now, "Brak szablonu", "ustaw w panelu");
    int32_t r = kom_send_msg(g_set.tpl[0]);
    flash_msg(now, r >= 0 ? "Wyslano:" : r == -1 ? "Brak pasma" : "Blad wysylki", r >= 0 ? g_set.tpl[0] : nullptr);
}

static void blink(int n) {
    for (int i = 0; i < n; i++) { digitalWrite(KOM_PIN_LED, HIGH); delay(120); digitalWrite(KOM_PIN_LED, LOW); delay(120); }
}

// PRG: krótko = kolejny ekran, dwa razy = pierwszy szablon do konta, 3–5 s = panel WiFi wł./wył.,
// ≥5 s = parowanie z portfelem. Dioda mignie raz przy 3 s i dwa razy przy 5 s.
static bool s_down = false, s_single = false;
static uint8_t s_stage = 0;
static uint32_t s_down_at = 0, s_up_at = 0;
static void button_tick(uint32_t now) {
    bool down = digitalRead(KOM_PIN_BUTTON) == LOW;
    if (down && !s_down) { s_down = true; s_stage = 0; s_down_at = now; }
    else if (down) {
        uint32_t held = now - s_down_at;
        if (held >= KOM_BTN_PAIR_MS && s_stage < 2) { s_stage = 2; blink(2); }
        else if (held >= KOM_BTN_LONG_MS && s_stage < 1) { s_stage = 1; blink(1); }
    } else if (s_down) {
        s_down = false;
        if (s_stage == 2) {
            s_single = false;
            int32_t r = kom_pair();
            flash_msg(now, r >= 0 ? "Parowanie wyslane" : "Blad parowania", nullptr);
        } else if (s_stage == 1) {
            s_single = false;
            if (kom_panel_on()) { kom_panel_stop(); flash_msg(now, "Panel wylaczony", nullptr); }
            else kom_panel_start();
        } else if (now - s_down_at > 30) {
            if (s_single && now - s_up_at <= KOM_BTN_DOUBLE_MS) { s_single = false; send_template(now); }
            else { s_single = true; s_up_at = now; }
        }
    }
    if (s_single && !s_down && now - s_up_at > KOM_BTN_DOUBLE_MS) {
        s_single = false;
        kom_ui_next(); s_draw_at = now;
    }
}

// Komendy po USB (115200): status | msg <tekst> | pair | hello | panel — test bez telefonu.
static void serial_tick() {
    static char line[140];
    static size_t n = 0;
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\r') continue;
        if (c != '\n') { if (n < sizeof(line) - 1) line[n++] = c; continue; }
        line[n] = 0; n = 0;
        if (!strcmp(line, "status")) {
            Serial.printf("[kom] status: ID %s, vis %u, nazwa '%s', PIN %s, szablonow %u, pasmo %lu ms/h\n",
                          s_id8, g_set.vis, g_set.name, g_set.pin_set ? "tak" : "nie",
                          g_set.tpl_n, (unsigned long)kom_radio_duty_ms());
            for (int i = 0; i < g_set.tpl_n; i++) Serial.printf("[kom] szablon %d: %s\n", i + 1, g_set.tpl[i]);
        } else if (!strncmp(line, "msg ", 4)) {
            kom_send_msg(line + 4);
        } else if (!strcmp(line, "pair")) {
            kom_pair();
        } else if (!strcmp(line, "hello")) {
            kom_request_hello();
        } else if (!strcmp(line, "panel")) {
            if (kom_panel_on()) kom_panel_stop(); else kom_panel_start();
        } else if (line[0]) {
            Serial.println("[kom] komendy: status | msg <tekst> | pair | hello | panel");
        }
    }
}

void kom_setup() {
    Serial.begin(115200);
    delay(300);
    Serial.printf("[kom] SENSMOS komunikator %s\n", KOM_FW_VERSION);
    pinMode(KOM_PIN_BUTTON, INPUT_PULLUP);
    pinMode(KOM_PIN_LED, OUTPUT);
    digitalWrite(KOM_PIN_LED, LOW);
    kom_ui_init();
    kom_ui_msg("start...", nullptr);

    s_selftest = selftest();
    Serial.printf("[kom] selftest %s\n", s_selftest ? "OK" : "BLAD — nie nadaje");
    kom_radio_init();
    if (!kom_id_init(kom_radio_random)) { s_selftest = false; Serial.println("[kom] klucz: blad"); }
    kom_store_load();
    kom_id8(s_id8);
    kom_fingerprint(s_fp);
    s_next_full  = KOM_HELLO_FULL_MS;
    s_next_short = BOOT_FULL[N_BOOT - 1] + KOM_HELLO_EVERY_MS;
    // Pierwsze uruchomienie: bez PIN-u panel startuje sam — inaczej nie da się niczego ustawić.
    if (!g_set.pin_set) kom_panel_start();
}

void kom_loop() {
    uint32_t now = millis();
    button_tick(now);
    serial_tick();
    kom_panel_tick();
    rx_tick();
    hello_tick(now);
    if (due(now, s_msg_until) && due(now, s_draw_at)) {
        if (kom_panel_on()) {
            char ssid[24];
            snprintf(ssid, sizeof(ssid), "SENSMOS-%s", s_id8);
            kom_ui_panel(ssid, g_set.ap_pass, kom_panel_lan_ip());
        } else {
            KomUiState s = { s_id8, s_fp, kom_radio_board(), kom_radio_ok(), s_selftest, s_rx_n, s_rx_rssi,
                             s_hello_n, s_hello_any ? (now - s_hello_at) / 1000 : UINT32_MAX, kom_radio_duty_ms() };
            kom_ui_draw(s);
        }
        s_draw_at = now + 1000;
    }
    delay(kom_panel_on() ? 2 : 5);
}

#endif
