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
#include <Preferences.h>

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
static uint8_t  s_led = KOM_PIN_LED_V3;

static bool due(uint32_t now, uint32_t t) { return (int32_t)(now - t) >= 0; }

// Kryptografia sprawdzona na wektorach z BE (RFC 7748 + testowy klucz BE). Bez zgodności
// urządzenie nie nadaje.
static bool selftest() {
    uint8_t pub[32], knet[32], h[32], f[KOM_FRAME_MAX];
    if (!kom_x25519(KOMT_DEV_PRIV, nullptr, pub) || memcmp(pub, KOMT_DEV_PUB, 32)) return false;
    if (!kom_knet(KOMT_DEV_PRIV, pub, KOMT_BE_PUB, knet) || memcmp(knet, KOMT_K_NET, 32)) return false;
    kom_sha256(pub, 32, h);
    size_t n = kom_build_hello(f, pub, h, knet, 1, 1, false, nullptr, nullptr);
    if (n != sizeof(KOMT_HELLO_SHORT) || memcmp(f, KOMT_HELLO_SHORT, n)) return false;
    uint8_t owner[20], own[8];
    memset(owner, 0x11, 20);
    kom_own_tag(knet, owner, own);
    n = kom_build_hello(f, pub, h, knet, 7, 0, true, own, nullptr);
    if (n != sizeof(KOMT_HELLO_PAIR) || memcmp(f, KOMT_HELLO_PAIR, n)) return false;
    n = kom_build_acct(f, h, knet, 6, "kod:ALARM", false, KOMT_NONCE);
    if (n != sizeof(KOMT_ACCT) || memcmp(f, KOMT_ACCT, n)) return false;
    uint32_t dctr = 0;
    char txt[101];
    if (!kom_open_down(KOMT_DOWN, sizeof(KOMT_DOWN), h, knet, &dctr, txt, sizeof(txt)) || dctr != 1 ||
        strcmp(txt, KOMT_DOWN_TEXT)) return false;
    n = kom_build_ack(f, h, knet, 8, 1);
    return n == sizeof(KOMT_ACCT_ACK) && !memcmp(f, KOMT_ACCT_ACK, n);
}

KomStats kom_stats() {
    return { s_rx_n, s_rx_rssi, s_hello_n, s_hello_any ? (millis() - s_hello_at) / 1000 : UINT32_MAX, s_selftest };
}

void kom_request_hello() { s_force_hello = true; s_retry_at = millis(); }

static int32_t send_up(const uint8_t* f, size_t n) {
    digitalWrite(s_led, HIGH);
    int32_t r = kom_radio_send_up(f, n);
    digitalWrite(s_led, LOW);
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

// Parowanie: apka podała adres portfela przez WiFi. Pełne HELLO z OWN = znacznik tego portfela;
// serwer paruje, gdy ten sam portfel zgłosił parowanie podpisem. Trzy nadania (0, +20 s, +60 s)
// na wypadek zgubionej ramki.
static uint8_t  s_pair_owner[20];
static uint8_t  s_pair_left = 0;
static uint32_t s_pair_next = 0;
static const uint32_t PAIR_GAP_MS[] = { 0, 20000, 40000 };

void kom_pair_owner(const uint8_t owner[20]) {
    memcpy(s_pair_owner, owner, 20);
    s_pair_left = 3;
    s_pair_next = millis();
    Serial.println("[kom] parowanie: adres portfela z sieci, potwierdzam radiem");
}

static void pair_tick(uint32_t now) {
    if (!s_pair_left || !s_selftest || !kom_radio_ok() || !due(now, s_pair_next)) return;
    uint8_t f[KOM_FRAME_MAX], own[8];
    kom_own_tag(g_kom.knet, s_pair_owner, own);
    uint32_t ctr = kom_ctr_next();
    uint8_t vis = g_set.vis == KOM_VIS_UNSET ? 0 : g_set.vis;
    size_t n = kom_build_hello(f, g_kom.pub, g_kom.id4, g_kom.knet, ctr, vis, true, own, g_set.name[0] ? g_set.name : nullptr);
    int32_t r = send_up(f, n);
    Serial.printf("[kom] parowanie: HELLO z OWN ctr %lu: %ld\n", (unsigned long)ctr, (long)r);
    if (r == -1) { s_pair_next = now + KOM_RETRY_MS; return; }
    s_pair_left--;
    if (s_pair_left) s_pair_next = now + PAIR_GAP_MS[3 - s_pair_left];
}

// Widoczność i nazwa z ustawień; do pierwszego wyboru w panelu urządzenie jest ukryte (vis 0).
static int32_t send_hello(bool full) {
    uint8_t f[KOM_FRAME_MAX];
    uint32_t ctr = kom_ctr_next();
    uint8_t vis = g_set.vis == KOM_VIS_UNSET ? 0 : g_set.vis;
    const char* name = full && g_set.name[0] ? g_set.name : nullptr;
    size_t n = kom_build_hello(f, g_kom.pub, g_kom.id4, g_kom.knet, ctr, vis, full, nullptr, name);
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

static void flash_msg(uint32_t now, const char* l1, const char* l2) {
    kom_ui_msg(l1, l2);
    s_msg_until = now + 2500;
}

// ── wiadomości z konta ───────────────────────────────────────────────
// Ochrona przed powtórką nagranej ramki: najwyższy przyjęty licznik (NVS) + okno 64 w RAM.
static KomMsg   s_inbox[5];
static uint8_t  s_inbox_n = 0;
static uint32_t s_dn_hi = 0;
static uint64_t s_dn_win = 0;

uint8_t kom_inbox(const KomMsg** out) { *out = s_inbox; return s_inbox_n; }

static bool dn_fresh(uint32_t ctr) {
    if (ctr > s_dn_hi) {
        uint32_t sh = ctr - s_dn_hi;
        s_dn_win = sh >= 64 ? 1 : (s_dn_win << sh) | 1;
        s_dn_hi = ctr;
        Preferences p; p.begin("sensmos_kom", false); p.putUInt("dnhi", s_dn_hi); p.end();
        return true;
    }
    uint32_t d = s_dn_hi - ctr;
    if (d >= 64 || (s_dn_win & (1ULL << d))) return false;
    s_dn_win |= 1ULL << d;
    return true;
}

static void on_down(const uint8_t* b, size_t n, uint32_t now) {
    uint32_t ctr;
    char text[101];
    if (!kom_open_down(b, n, g_kom.id4, g_kom.knet, &ctr, text, sizeof(text))) return;
    bool fresh = dn_fresh(ctr);
    if (fresh) {
        memmove(s_inbox + 1, s_inbox, sizeof(KomMsg) * 4);
        strlcpy(s_inbox[0].text, text, sizeof(s_inbox[0].text));
        s_inbox[0].at = now;
        if (s_inbox_n < 5) s_inbox_n++;
        Serial.printf("[kom] wiadomosc z konta ctr %lu: %s\n", (unsigned long)ctr, text);
        flash_msg(now, "Wiadomosc:", text);
    }
    uint8_t f[KOM_FRAME_MAX];
    size_t an = kom_build_ack(f, g_kom.id4, g_kom.knet, kom_ctr_next(), ctr);   // także dla powtórki — serwer przestanie ponawiać
    int32_t r = send_up(f, an);
    Serial.printf("[kom] ACK ctr %lu%s: %ld\n", (unsigned long)ctr, fresh ? "" : " (powtorka)", (long)r);
}

static void rx_tick(uint32_t now) {
    uint8_t b[256];
    size_t n; float rssi, snr;
    if (!kom_radio_poll(b, sizeof(b), &n, &rssi, &snr)) return;
    s_rx_n++; s_rx_rssi = rssi;
    bool mine = n >= 19 && b[0] == 0xE0 && b[1] == 0x04 && !memcmp(b + 3, g_kom.id4, 4);
    Serial.printf("[kom] RX %u B, RSSI %.0f, SNR %.1f, %02x %02x%s\n", (unsigned)n, rssi, snr, b[0], n > 1 ? b[1] : 0,
                  mine ? " -> DO MNIE" : "");
    if (mine && !memcmp(b + 7, g_kom.id4, 4)) on_down(b, n, now);
}


static void send_template(uint32_t now) {
    if (!g_set.tpl_n) return flash_msg(now, "Brak szablonu", "ustaw w panelu");
    int32_t r = kom_send_msg(g_set.tpl[0]);
    flash_msg(now, r >= 0 ? "Wyslano:" : r == -1 ? "Brak pasma" : "Blad wysylki", r >= 0 ? g_set.tpl[0] : nullptr);
}

static void blink(int n) {
    for (int i = 0; i < n; i++) { digitalWrite(s_led, HIGH); delay(120); digitalWrite(s_led, LOW); delay(120); }
}

// PRG: krótko = kolejny ekran, dwa razy = pierwszy szablon do konta, 3 s = AP WiFi wł./wył.
// (ratunek, gdy LAN nie działa). Dioda mignie przy 3 s.
static bool s_down = false, s_single = false, s_long = false;
static uint32_t s_down_at = 0, s_up_at = 0;
static void button_tick(uint32_t now) {
    bool down = digitalRead(KOM_PIN_BUTTON) == LOW;
    if (down && !s_down) { s_down = true; s_long = false; s_down_at = now; }
    else if (down && !s_long && now - s_down_at >= KOM_BTN_LONG_MS) { s_long = true; blink(1); }
    else if (!down && s_down) {
        s_down = false;
        if (s_long) {
            s_single = false;
            bool on = !kom_panel_on();
            kom_panel_ap(on);
            flash_msg(now, on ? "AP wlaczone" : "AP wylaczone", nullptr);
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

// Komendy po USB (115200): status | msg <tekst> | pair 0x<portfel> | hello | panel — test bez telefonu.
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
        } else if (!strncmp(line, "pair 0x", 7) && strlen(line) == 47) {
            uint8_t o[20];
            if (kom_hex(line + 7, o, 20)) kom_pair_owner(o);
        } else if (!strcmp(line, "hello")) {
            kom_request_hello();
        } else if (!strcmp(line, "panel")) {
            kom_panel_ap(!kom_panel_on());
        } else if (line[0]) {
            Serial.println("[kom] komendy: status | msg <tekst> | pair 0x<portfel> | hello | panel");
        }
    }
}

void kom_setup() {
    Serial.begin(115200);
    delay(300);
    Serial.printf("[kom] SENSMOS komunikator %s\n", KOM_FW_VERSION);
    pinMode(KOM_PIN_BUTTON, INPUT_PULLUP);
    kom_ui_init();
    s_led = kom_ui_has_oled() ? KOM_PIN_LED_V3 : KOM_PIN_LED_PAPER;
    pinMode(s_led, OUTPUT);
    digitalWrite(s_led, LOW);
    kom_ui_msg("start...", nullptr);

    s_selftest = selftest();
    Serial.printf("[kom] selftest %s\n", s_selftest ? "OK" : "BLAD — nie nadaje");
    kom_radio_init();
    if (!kom_id_init(kom_radio_random)) { s_selftest = false; Serial.println("[kom] klucz: blad"); }
    kom_store_load();
    { Preferences p; p.begin("sensmos_kom", true); s_dn_hi = p.getUInt("dnhi", 0); p.end(); }
    kom_id8(s_id8);
    kom_fingerprint(s_fp);
    s_next_full  = KOM_HELLO_FULL_MS;
    s_next_short = BOOT_FULL[N_BOOT - 1] + KOM_HELLO_EVERY_MS;
    kom_panel_start();
}

void kom_loop() {
    uint32_t now = millis();
    button_tick(now);
    serial_tick();
    kom_panel_tick();
    rx_tick(now);
    hello_tick(now);
    pair_tick(now);
    if (due(now, s_msg_until) && due(now, s_draw_at)) {
        if (kom_panel_on()) {
            char ssid[24];
            snprintf(ssid, sizeof(ssid), "SENSMOS-%s", s_id8);
            kom_ui_panel(ssid, KOM_AP_PASS, kom_panel_lan_ip());
        } else {
            KomUiState s = { s_id8, s_fp, kom_radio_board(), kom_radio_ok(), s_selftest, s_rx_n, s_rx_rssi,
                             s_hello_n, s_hello_any ? (now - s_hello_at) / 1000 : UINT32_MAX, kom_radio_duty_ms() };
            kom_ui_draw(s);
        }
        s_draw_at = now + 1000;
    }
    delay(2);
}

#endif
