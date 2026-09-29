#pragma once
#include <stdint.h>
#include <stddef.h>

// Ustawienia komunikatora w NVS "sensmos_kom" (obok klucza i licznika z kom_id).
#define KOM_TPL_MAX     5
#define KOM_TPL_LEN     100
#define KOM_VIS_UNSET   255

struct KomSettings {
    char     ap_pass[9];                  // hasło WiFi panelu, losowane przy 1. starcie
    bool     pin_set;
    uint8_t  vis;                         // 0 ukryty, 1 w pobliżu, 2 mapa, 255 nie wybrano
    char     name[17];
    uint8_t  tpl_n;
    char     tpl[KOM_TPL_MAX][KOM_TPL_LEN + 1];
    char     wifi_ssid[33];
    char     wifi_pass[65];
};
extern KomSettings g_set;

void kom_store_load();
bool kom_pin_check(const char* pin);
void kom_pin_save(const char* pin);
void kom_vis_save(uint8_t vis);
void kom_name_save(const char* name);
void kom_tpl_save();
void kom_wifi_save(const char* ssid, const char* pass);
void kom_factory_reset();                 // nowy klucz = nowe ID; restart
