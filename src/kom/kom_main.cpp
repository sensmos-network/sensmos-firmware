#include "kom_config.h"
#if SENSMOS_KOM
#include "kom_main.h"
#include "kom_id.h"
#include "kom_frame.h"
#include "kom_radio.h"
#include "kom_ui.h"
#include "kom_store.h"
#include "kom_ble.h"
#include "kom_app.h"
#include "kom_lang.h"
#include "kom_fixture.h"
#include <Arduino.h>
#include <Preferences.h>

static bool     s_selftest = false;
static char     s_id8[9], s_fp[20];
static uint32_t s_rx_n = 0;
static float    s_rx_rssi = 0;
static uint32_t s_rx_at = 0;
static bool     s_rx_any = false;
static uint32_t s_hello_n = 0, s_hello_at = 0;
static bool     s_hello_any = false;

static const uint32_t BOOT_FULL[] = KOM_HELLO_BOOT_MS;
static const uint8_t  N_BOOT = sizeof(BOOT_FULL) / sizeof(BOOT_FULL[0]);
static uint8_t  s_boot_i = 0;
static uint32_t s_next_short = 0, s_next_full = 0, s_retry_at = 0;
static uint32_t s_msg_until = 0, s_draw_at = 0;
static bool     s_force_hello = false;
static uint32_t s_next_advert = KOM_ADVERT_FIRST_MS;
static uint8_t  s_led = KOM_PIN_LED_V3;

static bool due(uint32_t now, uint32_t t) { return (int32_t)(now - t) >= 0; }
static void flash_msg(uint32_t now, const char* l1, const char* l2);

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

void kom_request_hello() { s_force_hello = true; s_retry_at = millis(); s_next_advert = millis(); }

static int32_t send_up(const uint8_t* f, size_t n) {
    digitalWrite(s_led, HIGH);
    int32_t r = kom_radio_send_up(f, n);
    digitalWrite(s_led, LOW);
    return r;
}

// ── skrzynka ─────────────────────────────────────────────────────────
static const uint8_t BOX_N = 20;
static KomMsg   s_box[BOX_N];
static uint32_t s_seq = 0;

static const KomMsg* box_add(bool out, const char* text, uint32_t now) {
    KomMsg& m = s_box[++s_seq % BOX_N];
    m.seq = s_seq; m.out = out; m.at = now;
    strlcpy(m.text, text, sizeof(m.text));
    return &m;
}

uint8_t kom_msgs(uint32_t after, const KomMsg* out[], uint8_t cap, bool* more) {
    uint32_t s = s_seq > BOX_N ? s_seq - BOX_N + 1 : 1;             // najstarszy w pierścieniu
    if (after >= s) s = after < s_seq ? after + 1 : s_seq + 1;
    uint8_t n = 0;
    for (; s <= s_seq && n < cap; s++) out[n++] = &s_box[s % BOX_N];
    *more = s <= s_seq;
    return n;
}

int32_t kom_send_msg(const char* text, uint32_t* seq) {
    if (!s_selftest || !kom_radio_ok()) return -2;
    uint8_t f[KOM_FRAME_MAX];
    size_t n = kom_build_acct(f, g_kom.id4, g_kom.knet, kom_ctr_next(), text, false, nullptr);
    if (!n) return -4;
    int32_t r = send_up(f, n);
    Serial.printf("[kom] do konta \"%s\": %ld\n", text, (long)r);
    if (r >= 0) {
        const KomMsg* m = box_add(true, text, millis());
        if (seq) *seq = m->seq;
    }
    return r;
}

// Parowanie: apka podała adres portfela przez BLE. Pełne HELLO z OWN = znacznik tego portfela;
// serwer paruje, gdy ten sam portfel zgłosił parowanie podpisem. Trzy nadania (0, +20 s, +60 s)
// na wypadek zgubionej ramki.
static uint8_t  s_pair_owner[20];
static uint8_t  s_pair_left = 0;
static uint32_t s_pair_next = 0;
static const uint32_t PAIR_GAP_MS[] = { 0, 20000, 40000 };

void kom_pair_owner(const uint8_t owner[20]) {
    kom_owner_save(owner);
    memcpy(s_pair_owner, owner, 20);
    s_pair_left = 3;
    s_pair_next = millis();
    Serial.println("[kom] parowanie: adres portfela zapisany, potwierdzam radiem");
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

// ── radio dla apki ───────────────────────────────────────────────────
// Apka jest tożsamością (ID, klucze, grupy); komunikator nadaje jej gotowe ramki i zbiera te, które
// usłyszy do jej ID i jej grup (odciski) — także gdy telefonu nie ma, do KOM_BUF_N sztuk.
struct KomBuf { uint8_t n; uint8_t f[KOM_FRAME_MAX]; };
static KomBuf  s_buf[KOM_BUF_N];
static uint8_t s_buf_head = 0, s_buf_n = 0;

// Bezpośrednio na 869.525 (słyszą ją komunikatory w zasięgu) i do sieci na 868.1. Wynik = nadanie
// bezpośrednie (albo do sieci, gdy bezpośrednie się nie udało).
int32_t kom_raw_send(const uint8_t* f, size_t n) {
    if (!s_selftest || !kom_radio_ok()) return -2;
    digitalWrite(s_led, HIGH);
    int32_t r = kom_radio_send_direct(f, n);
    digitalWrite(s_led, LOW);
    int32_t u = send_up(f, n);
    Serial.printf("[kom] ramka z apki %u B: radio %ld, siec %ld\n", (unsigned)n, (long)r, (long)u);
    return r >= 0 ? r : u;
}

bool kom_frame_pop(uint8_t* f, size_t* n, bool* more) {
    if (!s_buf_n) return false;
    const KomBuf& b = s_buf[(s_buf_head + KOM_BUF_N - s_buf_n) % KOM_BUF_N];
    memcpy(f, b.f, b.n); *n = b.n;
    *more = --s_buf_n > 0;
    return true;
}

static bool watched(const uint8_t* dst4) {
    for (uint8_t i = 0; i < g_set.nw; i++) if (!memcmp(g_set.watch[i], dst4, 4)) return true;
    return false;
}

// Widoczność i nazwa z ustawień; do pierwszego wyboru w apce urządzenie jest ukryte (vis 0).
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

// Ogłoszenie na 869.525: pełne HELLO (klucz, nazwa) dla komunikatorów w zasięgu. Ukryty (vis 0,
// także przed pierwszym wyborem w apce) się nie ogłasza — tylko słucha.
static void advert_tick(uint32_t now) {
    if (!s_selftest || !kom_radio_ok() || !due(now, s_next_advert)) return;
    s_next_advert = now + KOM_ADVERT_MS + random(0, 60000);
    uint8_t vis = g_set.vis == KOM_VIS_UNSET ? 0 : g_set.vis;
    if (!vis) return;
    // Ogłasza się człowiek, nie urządzenie: HELLO apki, która się z nim łączy (ID, klucz, nazwa) —
    // słyszący może od razu do niej pisać. Bez apki własne HELLO bez klucza = „bez właściciela”.
    uint8_t f[KOM_FRAME_MAX];
    size_t n = g_set.adv_n;
    if (n) memcpy(f, g_set.adv, n);
    else n = kom_build_hello(f, g_kom.pub, g_kom.id4, g_kom.knet, kom_ctr_next(), vis, false, nullptr,
                             g_set.name[0] ? g_set.name : nullptr);
    digitalWrite(s_led, HIGH);
    int32_t r = kom_radio_send_direct(f, n);
    digitalWrite(s_led, LOW);
    Serial.printf("[kom] ogloszenie 869.525, %u B: %ld\n", (unsigned)n, (long)r);
    if (r == -1) s_next_advert = now + KOM_RETRY_MS;
}

// ── w pobliżu ────────────────────────────────────────────────────────
static KomNear  s_near[KOM_NEAR_MAX];
static uint8_t  s_near_n = 0;

uint8_t kom_near(const KomNear** out) { *out = s_near; return s_near_n; }

static uint8_t near_kom_count(uint32_t now) {
    uint8_t n = 0;
    for (uint8_t i = 0; i < s_near_n; i++) if (s_near[i].kind == KOM_NEAR_KOM && now - s_near[i].at < 3600000UL) n++;
    return n;
}

static void near_note(uint8_t kind, const uint8_t id4[4], const char* name, const uint8_t* pub, float rssi, uint32_t now) {
    KomNear* e = nullptr;
    for (uint8_t i = 0; i < s_near_n && !e; i++)
        if (s_near[i].kind == kind && !memcmp(s_near[i].id4, id4, 4)) e = &s_near[i];
    if (!e && s_near_n < KOM_NEAR_MAX) e = &s_near[s_near_n++];
    if (!e) {                                                   // pełna tabela — wypada najdawniej słyszany
        e = &s_near[0];
        for (uint8_t i = 1; i < s_near_n; i++) if ((int32_t)(s_near[i].at - e->at) < 0) e = &s_near[i];
        e->name[0] = 0; e->has_pub = false;
    }
    e->kind = kind; memcpy(e->id4, id4, 4); e->rssi = rssi; e->at = now;
    if (name) strlcpy(e->name, name, sizeof(e->name));
    if (pub) { memcpy(e->pub, pub, 32); e->has_pub = true; }
}

// Ogłoszenie innego komunikatora: HELLO (tryb 2, dst FFFFFFFF), pola jak w parseHello serwera.
static void near_hello(const uint8_t* b, size_t n, float rssi, uint32_t now) {
    if (n < 20 || (b[2] & 3) != KOM_MODE_HELLO || memcmp(b + 3, "\xff\xff\xff\xff", 4) ||
        !memcmp(b + 7, g_kom.id4, 4)) return;
    size_t o = 15, end = n - 4;
    uint8_t hf = b[o++];
    if ((hf & 3) == 0 || (hf & 0xC0)) return;
    const uint8_t* pub = nullptr;
    char name[KOM_NAME_MAX + 1] = "";
    if (hf & 0x04) { if (o + 32 > end) return; pub = b + o; o += 32; }
    if (hf & 0x08) { if (o + 8 > end) return; o += 8; }
    if (hf & 0x10) {
        if (o + 1 > end) return;
        uint8_t l = b[o++];
        if (l < 1 || l > KOM_NAME_MAX || o + l > end) return;
        for (uint8_t i = 0; i < l; i++) if (b[o + i] < 0x20 || b[o + i] == 0x7F) return;
        memcpy(name, b + o, l); name[l] = 0;
    }
    near_note(KOM_NEAR_KOM, b + 7, name, pub, rssi, now);
}

// Ogłoszenie bramy lub noda (nadaje je serwer, rzadko): [E0][05][ver 1][rodzaj 1|2][id4][minuta 4][0].
static void near_infra(const uint8_t* b, size_t n, float rssi, uint32_t now) {
    if (n != 13 || b[2] != 1 || (b[3] != KOM_NEAR_GW && b[3] != KOM_NEAR_NODE)) return;
    near_note(b[3], b + 4, nullptr, nullptr, rssi, now);
}

// Ta sama ramka radiem i przez sieć (albo powtórzona): ostatnie 32 (nadawca, licznik).
static uint8_t  s_seen[32][8];
static uint8_t  s_seen_i = 0;
static bool seen_before(const uint8_t* b) {
    for (auto& s : s_seen) if (!memcmp(s, b + 7, 8)) return true;
    memcpy(s_seen[s_seen_i++ % 32], b + 7, 8);
    return false;
}

// Z kopią kluczy apki komunikator sam czyta wiadomość i pokazuje ją; gdy telefonu nie ma,
// potwierdza odbiór prywatnej (E2E ACK tożsamością apki) — z telefonem potwierdza apka.
static void for_app(const uint8_t* b, size_t n, float rssi, uint32_t now) {
    if (!memcmp(b + 7, g_kom.id4, 4) || (g_app.set && !memcmp(b + 7, g_app.id4, 4)) || !watched(b + 3) || seen_before(b)) return;
    char from[40], text[101];
    uint8_t senderPub[32];
    bool priv = false, opened = kom_app_open(b, n, from, sizeof(from), text, sizeof(text), senderPub, &priv);
    if (opened) {
        kom_ble_msg_event(box_add(false, text, now));
        Serial.printf("[kom] wiadomosc od %s: %s\n", from, text);
        flash_msg(now, from, text);
    }
    if (kom_ble_frame_event(b, n, rssi)) return;
    KomBuf& k = s_buf[s_buf_head];
    memcpy(k.f, b, n); k.n = (uint8_t)n;
    s_buf_head = (s_buf_head + 1) % KOM_BUF_N;
    if (s_buf_n < KOM_BUF_N) s_buf_n++;
    Serial.printf("[kom] ramka dla apki zebrana (%u w buforze)\n", s_buf_n);
    if (opened && priv) {
        uint8_t a[KOM_FRAME_MAX];
        uint32_t ref = ((uint32_t)b[11] << 24) | ((uint32_t)b[12] << 16) | ((uint32_t)b[13] << 8) | b[14];
        size_t an = kom_build_ack_e2e(a, g_app.priv, g_app.pub, g_app.id4, senderPub, g_app.knet, kom_app_ctr_next(), ref);
        if (an) Serial.printf("[kom] ACK E2E za apke: %ld\n", (long)kom_raw_send(a, an));
    }
}

static void flash_msg(uint32_t now, const char* l1, const char* l2) {
    kom_ui_msg(l1, l2);
    s_msg_until = now + 2500;
}

void kom_show(const char* l1, const char* l2) { flash_msg(millis(), l1, l2); }

// ── wiadomości z konta ───────────────────────────────────────────────
// Ochrona przed powtórką nagranej ramki: najwyższy przyjęty licznik (NVS) + okno 64 w RAM.
static uint32_t s_dn_hi = 0;
static uint64_t s_dn_win = 0;

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
        kom_ble_msg_event(box_add(false, text, now));
        Serial.printf("[kom] wiadomosc z konta ctr %lu: %s\n", (unsigned long)ctr, text);
        flash_msg(now, kom_str(S_MESSAGE), text);
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
    s_rx_n++; s_rx_rssi = rssi; s_rx_at = now; s_rx_any = true;
    bool mine = n >= 19 && b[0] == 0xE0 && b[1] == 0x04 && !memcmp(b + 3, g_kom.id4, 4);
    Serial.printf("[kom] RX %u B, RSSI %.0f, SNR %.1f, %02x %02x%s\n", (unsigned)n, rssi, snr, b[0], n > 1 ? b[1] : 0,
                  mine ? " -> DO MNIE" : "");
    if (mine && !memcmp(b + 7, g_kom.id4, 4)) on_down(b, n, now);
    if (n >= 19 && n <= KOM_FRAME_MAX && b[0] == 0xE0 && b[1] == 0x04 && (b[2] & 3) != KOM_MODE_HELLO) for_app(b, n, rssi, now);
    if (n >= 13 && b[0] == 0xE0 && b[1] == 0x04) near_hello(b, n, rssi, now);
    if (n >= 13 && b[0] == 0xE0 && b[1] == 0x05) near_infra(b, n, rssi, now);
}

// PRG: krótko = kolejny ekran.
static bool s_down = false;
static uint32_t s_down_at = 0;
static void button_tick(uint32_t now) {
    bool down = digitalRead(KOM_PIN_BUTTON) == LOW;
    if (down && !s_down) { s_down = true; s_down_at = now; }
    else if (!down && s_down) {
        s_down = false;
        if (now - s_down_at > 30) { kom_ui_next(); s_draw_at = now; }
    }
}

// Komendy po USB (115200): status | msg <tekst> | pair 0x<portfel> | hello — test bez telefonu.
static void serial_tick() {
    static char line[140];
    static size_t n = 0;
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\r') continue;
        if (c != '\n') { if (n < sizeof(line) - 1) line[n++] = c; continue; }
        line[n] = 0; n = 0;
        if (!strcmp(line, "status")) {
            Serial.printf("[kom] status: ID %s, vis %u, nazwa '%s', PIN %s, pasmo %lu ms/h\n",
                          s_id8, g_set.vis, g_set.name, g_set.pin[0] ? "tak" : "nie",
                          (unsigned long)kom_radio_duty_ms());
        } else if (!strncmp(line, "msg ", 4)) {
            kom_send_msg(line + 4);
        } else if (!strncmp(line, "pair 0x", 7) && strlen(line) == 47) {
            uint8_t o[20];
            if (kom_hex(line + 7, o, 20)) kom_pair_owner(o);
        } else if (!strcmp(line, "hello")) {
            kom_request_hello();
        } else if (line[0]) {
            Serial.println("[kom] komendy: status | msg <tekst> | pair 0x<portfel> | hello");
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
    kom_ui_epaper(kom_radio_board());
    if (!kom_id_init(kom_radio_random)) { s_selftest = false; Serial.println("[kom] klucz: blad"); }
    kom_store_load();
    kom_app_load();
    { Preferences p; p.begin("sensmos_kom", true); s_dn_hi = p.getUInt("dnhi", 0); p.end(); }
    kom_id8(s_id8);
    kom_fingerprint(s_fp);
    s_next_full  = KOM_HELLO_FULL_MS;
    s_next_short = BOOT_FULL[N_BOOT - 1] + KOM_HELLO_EVERY_MS;
    kom_ble_start();
}

void kom_loop() {
    uint32_t now = millis();
    button_tick(now);
    serial_tick();
    kom_ble_tick();
    rx_tick(now);
    hello_tick(now);
    advert_tick(now);
    pair_tick(now);
    if (due(now, s_msg_until) && due(now, s_draw_at)) {
        KomUiState s = { s_id8, s_fp, kom_radio_board(), kom_radio_ok(), s_selftest,
                         s_rx_n, s_rx_rssi, s_hello_n, s_hello_any ? (now - s_hello_at) / 1000 : UINT32_MAX,
                         kom_radio_duty_ms(), s_buf_n, near_kom_count(now), s_rx_any ? (now - s_rx_at) / 1000 : UINT32_MAX };
        kom_ui_draw(s);
        s_draw_at = now + 1000;
    }
    delay(2);
}

#endif
