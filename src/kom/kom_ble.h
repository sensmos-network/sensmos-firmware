#pragma once
#include "kom_main.h"

// BLE komunikatora: serwer GATT i ramki JSON (DOCS/dev/SPEC-komunikator-ble.md §2–§3).
// Telefon = ekran i klawiatura, komunikator = radio i skrzynka.
void kom_ble_start();                         // raz, w setup() — po kom_id_init i kom_store_load
void kom_ble_tick();                          // w pętli: obsługa żądań z kolejki
void kom_ble_msg_event(const KomMsg* m);      // ev:msg, gdy telefon połączony i subskrybuje
bool kom_ble_frame_event(const uint8_t* f, size_t n, float rssi);   // ev:frame dla apki; false = nie ma komu
