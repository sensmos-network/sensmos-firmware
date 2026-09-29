#pragma once
#include <stdint.h>

void kom_setup();
void kom_loop();

// Dla panelu.
struct KomStats { uint32_t rx_n; float rx_rssi; uint32_t hello_n; uint32_t hello_ago_s; bool selftest; };
KomStats kom_stats();
int32_t  kom_send_msg(const char* text);    // do mojego konta; ms w eterze; -1 brak pasma, -2 radio, -4 zła treść
int32_t  kom_pair();                         // HELLO z prośbą o parowanie (po zgłoszeniu w apce)
void     kom_request_hello();               // pełne HELLO przy najbliższej okazji (po zmianie nazwy/widoczności)
