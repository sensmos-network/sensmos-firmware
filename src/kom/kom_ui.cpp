#include "kom_config.h"
#if SENSMOS_KOM
#include "kom_ui.h"
#include "kom_store.h"
#include "kom_lang.h"
#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <U8g2lib.h>
#include <heltec-eink-modules.h>                 // wymaga -DWIRELESS_PAPER (piny Wireless Paper w bibliotece)
#include "Fonts/FreeSansBold9pt7b.h"
#include "Fonts/FreeSans9pt7b.h"

static U8G2_SSD1306_128X64_NONAME_F_HW_I2C s_oled(U8G2_R0, KOM_OLED_RST, KOM_OLED_SCL, KOM_OLED_SDA);
static uint8_t s_screen = 0;
static const uint8_t SCREENS = 2;
static bool s_oled_ok = false;

// OLED V3 wisi na I2C 17/18 za Vext. Na Wireless Paper pod 18 jest dioda, a ekran to e-papier na
// SPI — bez odpowiedzi pod 0x3C nie ruszamy U8g2 (inaczej migałaby dioda).
void kom_ui_init() {
    if (g_set.disp != KOM_DISP_AUTO && g_set.disp != KOM_DISP_OLED) return;
    pinMode(KOM_PIN_VEXT, OUTPUT);
    digitalWrite(KOM_PIN_VEXT, LOW);
    delay(50);
    Wire.begin(KOM_OLED_SDA, KOM_OLED_SCL);
    Wire.beginTransmission(0x3C);
    s_oled_ok = Wire.endTransmission() == 0;
    Wire.end();
    if (!s_oled_ok) return;
    s_oled.begin();
    s_oled.setFont(u8g2_font_6x12_tr);
}

bool kom_ui_has_oled() { return s_oled_ok; }

// ── e-papier (Wireless Paper) ────────────────────────────────────────
// Biblioteka heltec-eink-modules: własna szyna SPI (HSPI), Vext i piny płytki w środku. Trzy
// generacje panelu na tych samych pinach — bez wyboru w apce nie da się ich rozróżnić.
static BaseDisplay* s_epd = nullptr;
static const char*  s_disp_name = "none";
static bool     s_epd_fail = false;
static uint32_t s_epd_hash = 0, s_epd_at = 0;
static uint8_t  s_epd_parts = 0;
static char     s_epd_l1[40] = "", s_epd_l2[101] = "";

// Biblioteka czeka na BUSY bez limitu — zły typ panelu (inna polaryzacja BUSY) wieszałby cały
// komunikator. Tu limit 8 s i flaga: po niej ekran wyłączamy, reszta działa dalej.
static void epd_wait(int busy_level) {
    uint32_t t0 = millis();
    while (digitalRead(PIN_DISPLAY_BUSY) == busy_level) {
        if (millis() - t0 > 8000) { s_epd_fail = true; return; }
        yield();
    }
}
struct EpdWp10 : public EInkDisplay_WirelessPaperV1   { void wait() override { epd_wait(HIGH); } };   // SSD1680: zajęty = HIGH
struct EpdWp11 : public EInkDisplay_WirelessPaperV1_1 { void wait() override { epd_wait(LOW); } };    // JD79656: zajęty = LOW
struct EpdWp12 : public EInkDisplay_WirelessPaperV1_2 { void wait() override { epd_wait(HIGH); } };

void kom_ui_epaper(const char* board) {
    uint8_t d = g_set.disp;
    if (d == KOM_DISP_AUTO) d = s_oled_ok ? KOM_DISP_OLED : (board && !strncmp(board, "heltec", 6)) ? KOM_DISP_WP12 : KOM_DISP_NONE;
    if (d == KOM_DISP_OLED) { s_disp_name = s_oled_ok ? "oled" : "none"; return; }
    if (d == KOM_DISP_WP10)      { s_epd = new EpdWp10(); s_disp_name = "wp10"; }
    else if (d == KOM_DISP_WP11) { s_epd = new EpdWp11(); s_disp_name = "wp11"; }
    else if (d == KOM_DISP_WP12) { s_epd = new EpdWp12(); s_disp_name = "wp12"; }
    if (!s_epd) return;
    s_epd->landscape();
    s_epd->setTextColor(BLACK);
    Serial.printf("[kom] e-papier %s (Wireless Paper)\n", s_disp_name);
}

bool kom_ui_has_epaper() { return s_epd != nullptr; }
const char* kom_ui_display_name() { return s_disp_name; }

// Czcionki GFX mają tylko ASCII — polskie litery bez ogonków, reszta UTF-8 jako „?”.
static void ascii(const char* in, char* out, size_t cap) {
    static const char* PL = "\xc4\x85a\xc4\x87c\xc4\x99e\xc5\x82l\xc5\x84n\xc3\xb3o\xc5\x9bs\xc5\xbaz\xc5\xbcz"
                            "\xc4\x84A\xc4\x86C\xc4\x98E\xc5\x81L\xc5\x83N\xc3\x93O\xc5\x9aS\xc5\xb9Z\xc5\xbbZ";
    size_t o = 0;
    for (const uint8_t* p = (const uint8_t*)in; *p && o + 1 < cap; ) {
        if (*p < 0x80) { out[o++] = (char)*p++; continue; }
        char c = '?';
        for (const char* q = PL; *q; q += 3)
            if ((uint8_t)q[0] == p[0] && (uint8_t)q[1] == p[1]) { c = q[2]; break; }
        out[o++] = c;
        p++;
        while ((*p & 0xC0) == 0x80) p++;
    }
    out[o] = 0;
}

static uint32_t fnv(const char* s, uint32_t h = 2166136261u) {
    while (*s) { h ^= (uint8_t)*s++; h *= 16777619u; }
    return h;
}

// Czas „X min temu” w koszykach — e-papier nie ma się odświeżać co minutę.
static const char* ago_txt(uint32_t s, char* out, size_t cap) {
    if (s == UINT32_MAX) snprintf(out, cap, "-");
    else if (s < 60) snprintf(out, cap, "<1min");
    else if (s < 300) snprintf(out, cap, "<5min");
    else if (s < 900) snprintf(out, cap, "<15min");
    else if (s < 3600) snprintf(out, cap, "<1h");
    else snprintf(out, cap, "%luh", (unsigned long)(s / 3600));
    return out;
}

// Łamanie po słowach (GFX łamie po znaku), najwyżej `maxLines` linii — reszta ucięta z „...”.
static void epd_text(const char* text, int16_t x, int16_t y, int16_t maxw, uint8_t maxLines, int16_t lineH) {
    char lines[4][64];
    uint8_t n = 0;
    size_t ll = 0;
    bool more = false;
    int16_t bx, by; uint16_t bw, bh;
    const char* p = text;
    lines[0][0] = 0;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        const char* ws = p;
        while (*p && *p != ' ') p++;
        char cand[64];
        snprintf(cand, sizeof(cand), "%s%s%.*s", lines[n], ll ? " " : "", (int)(p - ws), ws);
        s_epd->getTextBounds(cand, 0, 0, &bx, &by, &bw, &bh);
        if (bw <= maxw || ll == 0) { strlcpy(lines[n], cand, sizeof(lines[n])); ll = strlen(lines[n]); continue; }
        if (n + 1 >= maxLines) { more = true; break; }
        n++;
        snprintf(lines[n], sizeof(lines[n]), "%.*s", (int)(p - ws), ws);
        ll = strlen(lines[n]);
    }
    if (more) {                                             // ostatnia linia z „...”, docięta do szerokości
        char* last = lines[n];
        for (;;) {
            char cand[68];
            snprintf(cand, sizeof(cand), "%s...", last);
            s_epd->getTextBounds(cand, 0, 0, &bx, &by, &bw, &bh);
            if (bw <= maxw || !last[0]) { strlcpy(last, cand, 64); break; }
            last[strlen(last) - 1] = 0;
        }
    }
    for (uint8_t i = 0; i <= n; i++) { s_epd->setCursor(x, y + i * lineH); s_epd->print(lines[i]); }
}

// 250×122: nagłówek (nazwa, ID, stan), pod kreską ostatnia wiadomość z apki (przez BLE „show”)
// albo — gdy telefonu nie ma — ile ramek czeka; na dole: kto w pobliżu, ostatni odbiór, pasmo.
static void epd_draw(const KomUiState& s) {
    char name[20], l1[40], l2[101], head[48], st[48], foot[64], ago[10], rssi[10];
    ascii(g_set.name[0] ? g_set.name : "", name, sizeof(name));
    ascii(s_epd_l1, l1, sizeof(l1));
    ascii(s_epd_l2, l2, sizeof(l2));
    snprintf(head, sizeof(head), "%s %s", name[0] ? name : "Sensmos", s.id8);
    snprintf(st, sizeof(st), "%s  FW %s", kom_str(!s.selftest_ok ? S_SELFTEST : s.radio_ok ? S_RADIO_OK : S_NO_RADIO),
             KOM_FW_VERSION);
    if (s.waiting && !l2[0]) {
        snprintf(l1, sizeof(l1), kom_str(s.waiting == 1 ? S_WAIT_1 : S_WAIT_N), s.waiting);
        snprintf(l2, sizeof(l2), "%s", kom_str(S_CONNECT));
    }
    snprintf(foot, sizeof(foot), kom_str(S_FOOT), s.near_kom, ago_txt(s.rx_ago_s, ago, sizeof(ago)),
             s.rx_ago_s == UINT32_MAX ? "" : (snprintf(rssi, sizeof(rssi), "%.0fdBm", s.rx_rssi), rssi),
             s.duty_ms / 1000.0f, (unsigned long)(KOM_DUTY_UP_MS_H / 1000));
    uint32_t h = fnv(foot, fnv(l2, fnv(l1, fnv(st, fnv(head)))));
    uint32_t now = millis();
    if (h == s_epd_hash || (s_epd_at && now - s_epd_at < KOM_EPD_MIN_MS)) return;
    s_epd_hash = h; s_epd_at = now;
    bool full = s_epd_parts == 0 || s_epd_parts >= 10;
    Serial.printf("[kom] e-papier rysuje (%s): %s | %s | %s\n", full ? "pelne" : "czesciowe", head, l1, l2);
    if (full) { s_epd->fastmodeOff(); s_epd_parts = 1; }
    else { s_epd->fastmodeOn(false); s_epd_parts++; }
    s_epd->clearMemory();
    s_epd->setTextSize(1);
    s_epd->setTextWrap(false);
    s_epd->setFont(&FreeSansBold9pt7b);
    s_epd->setCursor(2, 14);
    s_epd->print(head);
    s_epd->setFont(nullptr);
    s_epd->setCursor(2, 22);
    s_epd->print(st);
    s_epd->drawLine(0, 32, s_epd->width() - 1, 32, BLACK);
    if (l1[0]) {
        s_epd->setFont(&FreeSansBold9pt7b);
        s_epd->setCursor(2, 48);
        s_epd->print(l1);
    }
    if (l2[0]) {
        s_epd->setFont(&FreeSans9pt7b);
        epd_text(l2, 2, l1[0] ? 66 : 50, s_epd->width() - 4, l1[0] ? 3 : 4, 15);
    }
    s_epd->setFont(nullptr);
    s_epd->drawLine(0, 111, s_epd->width() - 1, 111, BLACK);
    s_epd->setCursor(2, 114);
    s_epd->print(foot);
    s_epd->update();
    if (s_epd_fail) {
        Serial.println("[kom] e-papier nie odpowiada (BUSY) — zly typ ekranu? Wylaczam do restartu");
        s_epd = nullptr; s_disp_name = "fail";
    }
}

void kom_ui_next() { s_screen = (s_screen + 1) % SCREENS; }

void kom_ui_msg(const char* l1, const char* l2) {
    if (s_epd) {                                        // e-papier trzyma ostatnią wiadomość do następnej
        strlcpy(s_epd_l1, l1 ? l1 : "", sizeof(s_epd_l1));
        strlcpy(s_epd_l2, l2 ? l2 : "", sizeof(s_epd_l2));
        return;
    }
    if (!s_oled_ok) return;
    s_oled.clearBuffer();
    s_oled.drawStr(0, 12, "SENSMOS KOM " KOM_FW_VERSION);
    if (l1) s_oled.drawStr(0, 34, l1);
    if (l2) s_oled.drawStr(0, 50, l2);
    s_oled.sendBuffer();
}

void kom_ui_draw(const KomUiState& s) {
    if (s_epd) return epd_draw(s);
    if (!s_oled_ok) return;
    char l[32];
    s_oled.clearBuffer();
    if (s_screen == 0) {
        s_oled.drawStr(0, 12, "SENSMOS KOM " KOM_FW_VERSION);
        snprintf(l, sizeof(l), "ID %s", s.id8);
        s_oled.setFont(u8g2_font_9x15_tr);
        s_oled.drawStr(0, 32, l);
        s_oled.setFont(u8g2_font_6x12_tr);
        s_oled.drawStr(0, 48, s.fp);
        s_oled.drawStr(0, 62, kom_str(!s.selftest_ok ? S_SELFTEST : s.radio_ok ? S_RADIO_OK : S_NO_RADIO));
    } else {
        snprintf(l, sizeof(l), "RX 869.525 SF%d", KOM_DN_SF);
        s_oled.drawStr(0, 10, l);
        if (s.rx_n) snprintf(l, sizeof(l), "odebrane %lu  %.0f dBm", (unsigned long)s.rx_n, s.rx_rssi);
        else        snprintf(l, sizeof(l), "odebrane 0");
        s_oled.drawStr(0, 23, l);
        if (s.hello_ago_s == UINT32_MAX) snprintf(l, sizeof(l), "HELLO: jeszcze nie");
        else snprintf(l, sizeof(l), "HELLO %lu, %lu min temu", (unsigned long)s.hello_n, (unsigned long)(s.hello_ago_s / 60));
        s_oled.drawStr(0, 36, l);
        snprintf(l, sizeof(l), "pasmo %.1f / %lu s", s.duty_ms / 1000.0f, (unsigned long)(KOM_DUTY_UP_MS_H / 1000));
        s_oled.drawStr(0, 49, l);
        s_oled.drawStr(0, 62, s.board);
    }
    s_oled.sendBuffer();
}

#endif
