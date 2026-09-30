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
// portfela do potwierdzenia parowania (pole OWN) albo nullptr; name ≤KOM_NAME_MAX B UTF-8 albo nullptr.
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

// Wspólne prymitywy (także dla kom_app): HKDF-SHA256 z solą 32×0 i AES-256-CTR z IV = nonce8 ‖ src4 ‖ 0.
void   kom_hkdf(const uint8_t* ikm, size_t n, const uint8_t* info, size_t infon, uint8_t* okm, size_t okmn);
void   kom_ctr_crypt(const uint8_t k[32], const uint8_t nonce[8], const uint8_t src4[4],
                     const uint8_t* in, size_t n, uint8_t* out);

// Grupa (tryb 1): klucz K zna apka (i komunikator z kopią kluczy). Odcisk gid4 = HMAC(K,
// "sensmos-grp-id-v1")[0:4]; okm = HKDF(K, "sensmos-grp-msg-v1", 64); IV = nonce8 ‖ src4 ‖ 0.
void   kom_group_id(const uint8_t key[32], uint8_t gid4[4]);
bool   kom_open_group(const uint8_t* f, size_t n, const uint8_t key[32], char* text, size_t cap);

// Prywatna E2E (tryb 0 z PUB) do tożsamości myPriv/myPub: okm = HKDF(X25519(my, sender),
// "sensmos-ldev-e2e-v1" ‖ senderPub ‖ myPub, 64); tag 8 B; senderPub = klucz z ramki.
bool   kom_open_priv(const uint8_t* f, size_t n, const uint8_t myPriv[32], const uint8_t myPub[32],
                     uint8_t senderPub[32], char* text, size_t cap);
// ACK E2E (tryb 3) do nadawcy: dst4 = sha256(peerPub)[0:4], okm jak wyżej z (myPub ‖ peerPub), bcode kluczem sieci nadawcy ACK.
size_t kom_build_ack_e2e(uint8_t* out, const uint8_t myPriv[32], const uint8_t myPub[32], const uint8_t myId4[4],
                         const uint8_t peerPub[32], const uint8_t knet[32], uint32_t ctr, uint32_t ref);
