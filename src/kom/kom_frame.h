#pragma once
#include <stdint.h>
#include <stddef.h>

// Ramka urządzenia 0xE0 0x04 (spec §3.1): [E0][04][flags][dst4][src4][ctr BE][...][bcode 4]
#define KOM_FRAME_MAX   204
#define KOM_TEXT_MAX    100
#define KOM_MODE_PRIV   0
#define KOM_MODE_GROUP  1
#define KOM_MODE_HELLO  2
#define KOM_MODE_ACK    3

// HELLO: vis 0 ukryty / 1 w pobliżu / 2 mapa; withPub = klucz publiczny w treści; own8 = znacznik
// portfela do potwierdzenia parowania (pole OWN) albo nullptr; name ≤16 B UTF-8 albo nullptr.
// Klucze z argumentów (a nie z g_kom) — ta sama funkcja liczy self-test na wektorach.
size_t kom_build_hello(uint8_t* out, const uint8_t pub[32], const uint8_t id4[4], const uint8_t knet[32],
                       uint32_t ctr, uint8_t vis, bool withPub, const uint8_t* own8, const char* name);

// Wiadomość do własnego konta (tryb 0, dst4 = src4): szyfr kluczem z K_net — serwer ją czyta
// i przekazuje właścicielowi do HA i apki. Tekst UTF-8 bez znaków sterujących, 1–100 B.
// nonce = nullptr → losowy.
bool   kom_text_ok(const char* t);
size_t kom_build_acct(uint8_t* out, const uint8_t id4[4], const uint8_t knet[32], uint32_t ctr,
                      const char* text, bool alert, const uint8_t* nonce);

// Wiadomość z konta do urządzenia (klucz „acct-dn”): sprawdza nagłówek, bcode i tag, odszyfrowuje.
// text: bufor ≥ 101 B, zakończony zerem. Zwraca false dla cudzej albo podrobionej ramki.
bool   kom_open_down(const uint8_t* f, size_t n, const uint8_t id4[4], const uint8_t knet[32],
                     uint32_t* ctr, char* text, size_t cap);
// Potwierdzenie odbioru (tryb 3, dst4 = src4, ref = licznik downlinku), klucz „acct”.
size_t kom_build_ack(uint8_t* out, const uint8_t id4[4], const uint8_t knet[32], uint32_t ctr, uint32_t ref);
