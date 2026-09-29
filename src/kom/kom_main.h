#pragma once
#include <stdint.h>

void kom_setup();
void kom_loop();

// Dla panelu.
struct KomStats { uint32_t rx_n; float rx_rssi; uint32_t hello_n; uint32_t hello_ago_s; bool selftest; };
KomStats kom_stats();
int32_t  kom_send_msg(const char* text);    // do mojego konta; ms w eterze; -1 brak pasma, -2 radio, -4 zła treść
void     kom_pair_owner(const uint8_t owner[20]);   // potwierdź radiem parowanie z tym portfelem
// Skrzynka wiadomości z konta (najnowsza pierwsza).
struct KomMsg { char text[101]; uint32_t at; };
uint8_t  kom_inbox(const KomMsg** out);
void     kom_request_hello();               // pełne HELLO przy najbliższej okazji (po zmianie nazwy/widoczności)
