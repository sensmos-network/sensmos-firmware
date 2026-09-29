#pragma once
#include <stdint.h>

// OLED 0.96" SSD1306 Heltec V3. Tekst tylko ASCII (czcionka bez polskich znaków).
struct KomUiState {
    const char* id8;
    const char* fp;
    const char* board;
    bool     radio_ok;
    bool     selftest_ok;
    uint32_t rx_n;
    float    rx_rssi;
    uint32_t hello_n;
    uint32_t hello_ago_s;      // UINT32_MAX = jeszcze nie było
    uint32_t duty_ms;
};

void kom_ui_init();
void kom_ui_next();                           // krótkie naciśnięcie: kolejny ekran
void kom_ui_draw(const KomUiState& s);
void kom_ui_msg(const char* l1, const char* l2);
void kom_ui_panel(const char* ssid, const char* pass, const char* lan_ip);
