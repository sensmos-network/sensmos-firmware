#include "kom_config.h"
#if SENSMOS_KOM
#include "kom_ui.h"
#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>

static U8G2_SSD1306_128X64_NONAME_F_HW_I2C s_oled(U8G2_R0, KOM_OLED_RST, KOM_OLED_SCL, KOM_OLED_SDA);
static uint8_t s_screen = 0;
static const uint8_t SCREENS = 2;

void kom_ui_init() {
    pinMode(KOM_PIN_VEXT, OUTPUT);
    digitalWrite(KOM_PIN_VEXT, LOW);
    delay(50);
    s_oled.begin();
    s_oled.setFont(u8g2_font_6x12_tr);
}

void kom_ui_next() { s_screen = (s_screen + 1) % SCREENS; }

void kom_ui_msg(const char* l1, const char* l2) {
    s_oled.clearBuffer();
    s_oled.drawStr(0, 12, "SENSMOS KOM " KOM_FW_VERSION);
    if (l1) s_oled.drawStr(0, 34, l1);
    if (l2) s_oled.drawStr(0, 50, l2);
    s_oled.sendBuffer();
}

void kom_ui_draw(const KomUiState& s) {
    char l[32];
    s_oled.clearBuffer();
    if (s_screen == 0) {
        s_oled.drawStr(0, 12, "SENSMOS KOM " KOM_FW_VERSION);
        snprintf(l, sizeof(l), "ID %s", s.id8);
        s_oled.setFont(u8g2_font_9x15_tr);
        s_oled.drawStr(0, 32, l);
        s_oled.setFont(u8g2_font_6x12_tr);
        s_oled.drawStr(0, 48, s.fp);
        s_oled.drawStr(0, 62, !s.selftest_ok ? "SELFTEST FAIL" : s.radio_ok ? "radio OK" : "BRAK RADIA");
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
