#pragma once
#include <stdint.h>
#include <stddef.h>

// SX1262 komunikatora: spoczynek = odbiór na 869.525 SF9; nadanie = przestrojenie na 868.1 SF11,
// CAD, nadanie i natychmiastowy powrót do odbioru. Budżet 1% na godzinę pilnowany tutaj.
bool     kom_radio_init();
bool     kom_radio_ok();
const char* kom_radio_board();
void     kom_radio_random(uint8_t* buf, size_t n);          // szum szerokopasmowy SX1262
int32_t  kom_radio_send_up(const uint8_t* f, size_t n);     // airtime ms; -1 brak budżetu, -2 błąd radia
bool     kom_radio_poll(uint8_t* buf, size_t cap, size_t* len, float* rssi, float* snr);
uint32_t kom_radio_duty_ms();                                // zużyte ms nadawania w ostatnich 60 min
uint32_t kom_airtime_ms(uint8_t sf, size_t len);
