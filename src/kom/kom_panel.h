#pragma once
#include <stdint.h>

// Panel WWW komunikatora: AP `SENSMOS-<id8>` (zawsze, z portalem przechwytującym) oraz — gdy
// w ustawieniach jest WiFi — jednocześnie klient domowej sieci (LAN, `kom-<id8>.local`).
// Włączany przytrzymaniem PRG, gaśnie po 5 min bez żądań.
void kom_panel_start();
void kom_panel_stop();
bool kom_panel_on();
void kom_panel_tick();
const char* kom_panel_lan_ip();          // "" gdy brak połączenia z LAN
