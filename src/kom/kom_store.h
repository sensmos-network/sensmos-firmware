#pragma once
#include <stdint.h>
#include <stddef.h>
#include "kom_config.h"

// Ustawienia komunikatora w NVS "sensmos_kom" (obok klucza i licznika z kom_id).
#define KOM_VIS_UNSET   255

struct KomSettings {
    char     pin[7];                      // opcjonalny PIN apki (6 cyfr), "" = bez PIN-u
    uint8_t  vis;                         // 0 ukryty, 1 w pobliżu, 2 mapa, 255 nie wybrano
    uint8_t  disp;                        // KOM_DISP_*
    uint8_t  lang;                        // KomLang (napisy ekranu), z apki
    char     name[KOM_NAME_MAX + 1];
    bool     owner_set;
    uint8_t  owner[20];                   // portfel z ostatniego parowania
    uint8_t  nw;
    uint8_t  watch[KOM_WATCH_MAX][4];     // ID apki i odciski jej grup — te ramki zbieramy dla niej
    uint8_t  adv_n;
    uint8_t  adv[KOM_ADV_MAX];            // HELLO apki, która się z nim łączy — ogłaszane zamiast własnego
};
extern KomSettings g_set;

void kom_store_load();
void kom_pin_save(const char* pin);     // "" usuwa
void kom_vis_save(uint8_t vis);
void kom_disp_save(uint8_t disp);
void kom_lang_save(uint8_t lang);
void kom_name_save(const char* name);
void kom_owner_save(const uint8_t owner[20]);
void kom_watch_save(const uint8_t* ids, uint8_t n);
void kom_adv_save(const uint8_t* f, uint8_t n);   // n = 0 usuwa
void kom_factory_reset();                 // nowy klucz = nowe ID; restart
