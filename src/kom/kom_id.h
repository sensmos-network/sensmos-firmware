#pragma once
#include <stdint.h>
#include <stddef.h>

// Tożsamość komunikatora: para X25519 z TRNG przy pierwszym starcie (NVS "sensmos_kom"),
// ID = SHA-256(pub)[0:4], klucz sieci K_net = HKDF(X25519(urządzenie, BE)).
struct KomId {
    uint8_t  priv[32];
    uint8_t  pub[32];
    uint8_t  id4[4];
    uint8_t  knet[32];
};
extern KomId g_kom;

bool kom_x25519(const uint8_t priv[32], const uint8_t* peer, uint8_t out[32]);   // peer = nullptr → klucz publiczny
void kom_sha256(const uint8_t* d, size_t n, uint8_t out[32]);
void kom_hmac(const uint8_t* k, size_t kn, const uint8_t* d, size_t dn, uint8_t out[32]);
bool kom_knet(const uint8_t priv[32], const uint8_t pub[32], const uint8_t bePub[32], uint8_t out[32]);

// extra: dodatkowa entropia (szum SX1262) mieszana z TRNG układu.
bool kom_id_init(void (*extra)(uint8_t* buf, size_t n));
uint32_t kom_ctr_next();
void kom_id8(char out[9]);
void kom_fingerprint(char out[20]);   // "ab12 cd34 ef56 7890"
bool kom_hex(const char* hex, uint8_t* out, size_t n);
// OWN w HELLO: HMAC(K_net, "sensmos-ldev-own-v1" ‖ adres portfela 20 B)[0:8]
void kom_own_tag(const uint8_t knet[32], const uint8_t owner[20], uint8_t out[8]);
