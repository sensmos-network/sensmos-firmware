#include "kom_config.h"
#if SENSMOS_KOM
#include "kom_main.h"
#include "kom_id.h"
#include "kom_frame.h"
#include "kom_radio.h"
#include "kom_ui.h"
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
// urządzenie nie nadaje — ramki z błędnym podpisem tylko zaśmiecałyby pasmo.
static bool selftest() {
    uint8_t pub[32], knet[32], h[32], f[KOM_FRAME_MAX];
    if (!kom_x25519(KOMT_DEV_PRIV, nullptr, pub) || memcmp(pub, KOMT_DEV_PUB, 32)) return false;
    if (!kom_knet(KOMT_DEV_PRIV, pub, KOMT_BE_PUB, knet) || memcmp(knet, KOMT_K_NET, 32)) return false;
    kom_sha256(pub, 32, h);
    size_t n = kom_build_hello(f, pub, h, knet, 1, 1, false, nullptr);
    return n == sizeof(KOMT_HELLO_SHORT) && !memcmp(f, KOMT_HELLO_SHORT, n);
}

// vis 0 (ukryty) i bez nazwy, dopóki właściciel nie ustawi ich w panelu.
static int32_t send_hello(bool full) {
    uint8_t f[KOM_FRAME_MAX];
    uint32_t ctr = kom_ctr_next();
    size_t n = kom_build_hello(f, g_kom.pub, g_kom.id4, g_kom.knet, ctr, 0, full, nullptr);
    digitalWrite(KOM_PIN_LED, HIGH);
    int32_t r = kom_radio_send_up(f, n);
    digitalWrite(KOM_PIN_LED, LOW);
    if (r >= 0) {
        s_hello_n++; s_hello_at = millis(); s_hello_any = true;
        Serial.printf("[kom] HELLO%s ctr %lu, %u B, %ld ms, pasmo %lu ms/h\n", full ? " pelne" : "",
                      (unsigned long)ctr, (unsigned)n, (long)r, (unsigned long)kom_radio_duty_ms());
    } else {
        Serial.printf("[kom] HELLO nie poszlo (%s)\n", r == -1 ? "brak budzetu pasma" : "blad radia");
    }
    return r;
}

static void hello_tick(uint32_t now) {
    if (!s_selftest || !kom_radio_ok() || !due(now, s_retry_at)) return;
    bool full;
    if (s_force_hello)                                         full = true;
    else if (s_boot_i < N_BOOT && due(now, BOOT_FULL[s_boot_i])) full = true;
    else if (s_boot_i >= N_BOOT && due(now, s_next_full))     full = true;
    else if (s_boot_i >= N_BOOT && due(now, s_next_short))    full = false;
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

// Krótko: kolejny ekran. Przytrzymanie 3 s: HELLO od razu (test zasięgu; w K3 ten gest dostanie panel WiFi).
static bool s_down = false, s_long = false;
static uint32_t s_down_at = 0;
static void button_tick(uint32_t now) {
    bool down = digitalRead(KOM_PIN_BUTTON) == LOW;
    if (down && !s_down) { s_down = true; s_long = false; s_down_at = now; }
    else if (down && !s_long && now - s_down_at >= KOM_BTN_LONG_MS) {
        s_long = true;
        s_force_hello = true; s_retry_at = now;
        kom_ui_msg("HELLO teraz", nullptr);
        s_msg_until = now + 2000;
    } else if (!down && s_down) {
        s_down = false;
        if (!s_long && now - s_down_at > 30) { kom_ui_next(); s_draw_at = now; }
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
    kom_id8(s_id8);
    kom_fingerprint(s_fp);
    s_next_full  = KOM_HELLO_FULL_MS;
    s_next_short = BOOT_FULL[N_BOOT - 1] + KOM_HELLO_EVERY_MS;
}

void kom_loop() {
    uint32_t now = millis();
    button_tick(now);
    rx_tick();
    hello_tick(now);
    if (due(now, s_msg_until) && due(now, s_draw_at)) {
        KomUiState s = { s_id8, s_fp, kom_radio_board(), kom_radio_ok(), s_selftest, s_rx_n, s_rx_rssi,
                         s_hello_n, s_hello_any ? (now - s_hello_at) / 1000 : UINT32_MAX, kom_radio_duty_ms() };
        kom_ui_draw(s);
        s_draw_at = now + 1000;
    }
    delay(5);
}

#endif
