#pragma once
#include <stdint.h>
#include <stddef.h>

// Ramka urządzenia 0xE0 0x04 (spec §3.1): [E0][04][flags][dst4][src4][ctr BE][...][bcode 4]
#define KOM_FRAME_MAX   204
#define KOM_MODE_PRIV   0
#define KOM_MODE_GROUP  1
#define KOM_MODE_HELLO  2
#define KOM_MODE_ACK    3

// HELLO: vis 0 ukryty / 1 w pobliżu / 2 mapa; withPub = klucz publiczny w treści; name ≤16 B UTF-8 albo nullptr.
// Klucze z argumentów (a nie z g_kom) — ta sama funkcja liczy self-test na wektorach.
size_t kom_build_hello(uint8_t* out, const uint8_t pub[32], const uint8_t id4[4], const uint8_t knet[32],
                       uint32_t ctr, uint8_t vis, bool withPub, const char* name);
