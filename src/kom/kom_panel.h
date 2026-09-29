#pragma once
#include <stdint.h>

// Sieć i panel WWW komunikatora: AP przy pierwszym uruchomieniu (formularz WiFi), potem LAN
// na stałe (panel, parowanie z apki). AP wraca, gdy LAN nie wstanie, albo po PRG 3 s.
void kom_panel_start();                  // raz, w setup()
void kom_panel_tick();
void kom_panel_ap(bool on);              // ręczne AP (10 min)
void kom_panel_stop();                   // wyłącza AP
bool kom_panel_on();                     // AP włączone
const char* kom_panel_lan_ip();          // "" gdy brak połączenia z LAN
