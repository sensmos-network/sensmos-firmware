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
    uint8_t  waiting;          // ramki dla apki zebrane bez telefonu
    uint8_t  near_kom;         // komunikatory słyszane w ostatniej godzinie
    uint32_t rx_ago_s;         // ostatni odbiór radiowy; UINT32_MAX = jeszcze nie było
};

void kom_ui_init();
bool kom_ui_has_oled();                       // Heltec V3 — tak; Wireless Paper (e-papier) — nie
// Po wykryciu radia: e-papier wg ustawienia (auto = płytka Heltec bez OLED → Wireless Paper V1.1).
// Odświeżany tylko przy zmianie treści (nazwa, ostatnia wiadomość), najwyżej co KOM_EPD_MIN_MS.
void kom_ui_epaper(const char* board);
bool kom_ui_has_epaper();
const char* kom_ui_display_name();            // co działa: "none" | "oled" | "wp10" | "wp11" | "wp12"
void kom_ui_next();                           // krótkie naciśnięcie: kolejny ekran
void kom_ui_draw(const KomUiState& s);
void kom_ui_msg(const char* l1, const char* l2);
