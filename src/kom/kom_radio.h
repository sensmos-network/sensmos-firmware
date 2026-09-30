#pragma once
#include <stdint.h>
#include <stddef.h>

// SX1262 komunikatora: spoczynek = odbiór na 869.525 SF9. Nadanie do sieci = przestrojenie na 868.1
// SF11 (budżet 1%/h); nadanie bezpośrednie = 869.525 SF9, tam gdzie słuchają inne komunikatory
// (osobny budżet). CAD przed nadaniem i natychmiastowy powrót do odbioru.
bool     kom_radio_init();
bool     kom_radio_ok();
const char* kom_radio_board();
void     kom_radio_random(uint8_t* buf, size_t n);          // szum szerokopasmowy SX1262
int32_t  kom_radio_send_up(const uint8_t* f, size_t n);     // airtime ms; -1 brak budżetu, -2 błąd radia
int32_t  kom_radio_send_direct(const uint8_t* f, size_t n); // jw., na 869.525 SF9
bool     kom_radio_poll(uint8_t* buf, size_t cap, size_t* len, float* rssi, float* snr);
uint32_t kom_radio_duty_ms();                                // zużyte ms nadawania w ostatnich 60 min
uint32_t kom_radio_duty_dn_ms();                             // to samo dla 869.525
uint32_t kom_airtime_ms(uint8_t sf, size_t len);
