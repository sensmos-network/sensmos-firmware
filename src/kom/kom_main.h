#pragma once
#include <stdint.h>
#include <stddef.h>
#include "kom_config.h"

void kom_setup();
void kom_loop();

// Dla BLE.
int32_t  kom_send_msg(const char* text, uint32_t* seq = nullptr);   // do mojego konta; ms w eterze; -1 brak pasma, -2 radio, -4 zła treść
void     kom_pair_owner(const uint8_t owner[20]);   // zapamiętaj portfel i potwierdź radiem parowanie z nim
// Skrzynka: pierścień w RAM (przychodzące z konta i wysłane), seq rośnie od 1 od startu.
struct KomMsg { uint32_t seq; bool out; uint32_t at; char text[101]; };
uint8_t  kom_msgs(uint32_t after, const KomMsg* out[], uint8_t cap, bool* more);   // seq > after, rosnąco
// „W pobliżu”: kto nadaje na 869.525 — inne komunikatory (ogłoszenia) oraz bramy i nody (E0 05).
enum KomNearKind : uint8_t { KOM_NEAR_KOM = 0, KOM_NEAR_GW = 1, KOM_NEAR_NODE = 2 };
struct KomNear { uint8_t kind; uint8_t id4[4]; char name[KOM_NAME_MAX + 1]; float rssi; uint32_t at; bool has_pub; uint8_t pub[32]; };
uint8_t  kom_near(const KomNear** out);
// Radio dla apki: nadanie gotowej ramki (869.525 + 868.1) i ramki zebrane do jej ID i grup
// (lista w g_set.watch), gdy telefonu nie było.
int32_t  kom_raw_send(const uint8_t* f, size_t n);
bool     kom_frame_pop(uint8_t* f, size_t* n, bool* more);
void     kom_show(const char* l1, const char* l2);     // ekran: ostatnia wiadomość z apki
void     kom_request_hello();               // pełne HELLO przy najbliższej okazji (po zmianie nazwy/widoczności)
