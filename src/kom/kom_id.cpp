#include "kom_config.h"
#if SENSMOS_KOM
#include "kom_id.h"
#include <Arduino.h>
#include <Preferences.h>
#include <esp_random.h>
#include "bootloader_random.h"
#include <mbedtls/ecp.h>
#include <mbedtls/md.h>
#include <mbedtls/hkdf.h>
#include <mbedtls/sha256.h>

KomId g_kom;

static const char* NVS_NS = "sensmos_kom";
static const uint32_t CTR_BLOCK = 16;   // licznik zapisywany co 16 — po restarcie skok o blok w górę
static uint32_t s_ctr = 0, s_ctr_end = 0;

static int rng(void*, unsigned char* out, size_t n) { esp_fill_random(out, n); return 0; }

static void clamp(uint8_t k[32]) { k[0] &= 248; k[31] &= 127; k[31] |= 64; }

// X25519 na mbedtls (Montgomery: bajty little-endian, jak RFC 7748 i Node).
bool kom_x25519(const uint8_t priv[32], const uint8_t* peer, uint8_t out[32]) {
    uint8_t k[32];
    memcpy(k, priv, 32);
    clamp(k);
    mbedtls_ecp_group grp; mbedtls_mpi d; mbedtls_ecp_point P, R;
    mbedtls_ecp_group_init(&grp); mbedtls_mpi_init(&d);
    mbedtls_ecp_point_init(&P); mbedtls_ecp_point_init(&R);
    bool ok = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_CURVE25519) == 0 &&
              mbedtls_mpi_read_binary_le(&d, k, 32) == 0;
    if (ok) ok = peer ? mbedtls_ecp_point_read_binary(&grp, &P, peer, 32) == 0
                      : mbedtls_ecp_copy(&P, &grp.G) == 0;
    ok = ok && mbedtls_ecp_mul(&grp, &R, &d, &P, rng, nullptr) == 0;
    size_t olen = 0;
    ok = ok && mbedtls_ecp_point_write_binary(&grp, &R, MBEDTLS_ECP_PF_UNCOMPRESSED, &olen, out, 32) == 0 && olen == 32;
    if (ok) {                                   // klucz o małym rzędzie daje zerowy sekret
        uint8_t acc = 0;
        for (int i = 0; i < 32; i++) acc |= out[i];
        ok = acc != 0;
    }
    mbedtls_ecp_point_free(&R); mbedtls_ecp_point_free(&P);
    mbedtls_mpi_free(&d); mbedtls_ecp_group_free(&grp);
    memset(k, 0, sizeof(k));
    return ok;
}

void kom_sha256(const uint8_t* d, size_t n, uint8_t out[32]) { mbedtls_sha256(d, n, out, 0); }

void kom_hmac(const uint8_t* k, size_t kn, const uint8_t* d, size_t dn, uint8_t out[32]) {
    mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), k, kn, d, dn, out);
}

// K_net = HKDF-SHA256(X25519(priv, pub_BE), sól 32×00, "sensmos-ldev-net-v1" ‖ pub ‖ pub_BE)
bool kom_knet(const uint8_t priv[32], const uint8_t pub[32], const uint8_t bePub[32], uint8_t out[32]) {
    uint8_t ss[32];
    if (!kom_x25519(priv, bePub, ss)) return false;
    static const char L[] = "sensmos-ldev-net-v1";
    uint8_t info[sizeof(L) - 1 + 64];
    memcpy(info, L, sizeof(L) - 1);
    memcpy(info + sizeof(L) - 1, pub, 32);
    memcpy(info + sizeof(L) - 1 + 32, bePub, 32);
    uint8_t salt[32] = {0};
    int r = mbedtls_hkdf(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), salt, 32, ss, 32,
                         info, sizeof(info), out, 32);
    memset(ss, 0, sizeof(ss));
    return r == 0;
}

void kom_own_tag(const uint8_t knet[32], const uint8_t owner[20], uint8_t out[8]) {
    static const char L[] = "sensmos-ldev-own-v1";
    uint8_t buf[sizeof(L) - 1 + 20], mac[32];
    memcpy(buf, L, sizeof(L) - 1);
    memcpy(buf + sizeof(L) - 1, owner, 20);
    kom_hmac(knet, 32, buf, sizeof(buf), mac);
    memcpy(out, mac, 8);
}

bool kom_hex(const char* hex, uint8_t* out, size_t n) {
    if (strlen(hex) != n * 2) return false;
    for (size_t i = 0; i < n; i++) {
        char b[3] = { hex[2 * i], hex[2 * i + 1], 0 };
        char* e;
        out[i] = (uint8_t)strtoul(b, &e, 16);
        if (*e) return false;
    }
    return true;
}

static void derive_public() {
    kom_x25519(g_kom.priv, nullptr, g_kom.pub);
    uint8_t h[32];
    kom_sha256(g_kom.pub, 32, h);
    memcpy(g_kom.id4, h, 4);
}

static bool id4_reserved() {
    uint32_t v = ((uint32_t)g_kom.id4[0] << 24) | (g_kom.id4[1] << 16) | (g_kom.id4[2] << 8) | g_kom.id4[3];
    return v == 0 || v == 0xFFFFFFFFu;
}

// Klucz z TRNG: bez włączonego RF generator ESP32-S3 daje liczby pseudolosowe, dopóki nie
// włączy się bootloader_random_enable(). Do tego szum SX1262 — dwa niezależne źródła.
static void generate(void (*extra)(uint8_t*, size_t)) {
    do {
        uint8_t mix[64];
        bootloader_random_enable();
        esp_fill_random(mix, 32);
        bootloader_random_disable();
        if (extra) extra(mix + 32, 32); else esp_fill_random(mix + 32, 32);
        kom_sha256(mix, sizeof(mix), g_kom.priv);
        memset(mix, 0, sizeof(mix));
        clamp(g_kom.priv);
        derive_public();
    } while (id4_reserved());
}

bool kom_id_init(void (*extra)(uint8_t*, size_t)) {
    Preferences p;
    p.begin(NVS_NS, false);
    bool fresh = p.getBytes("priv", g_kom.priv, 32) != 32;
    if (fresh) {
        generate(extra);
        p.putBytes("priv", g_kom.priv, 32);
        p.putUInt("ctrblk", 0);
    } else {
        derive_public();
    }
    uint32_t blk = p.getUInt("ctrblk", 0);
    s_ctr = blk;
    s_ctr_end = blk + CTR_BLOCK;
    p.putUInt("ctrblk", s_ctr_end);
    p.end();

    uint8_t bePub[32];
    if (!kom_hex(KOM_BE_PUB_HEX, bePub, 32) || !kom_knet(g_kom.priv, g_kom.pub, bePub, g_kom.knet)) return false;
    char id8[9];
    kom_id8(id8);
    Serial.printf("[kom] ID %s (%s), licznik od %lu\n", id8, fresh ? "nowy klucz" : "z NVS", (unsigned long)s_ctr + 1);
    return true;
}

uint32_t kom_ctr_next() {
    if (s_ctr + 1 >= s_ctr_end) {
        s_ctr_end += CTR_BLOCK;
        Preferences p;
        p.begin(NVS_NS, false);
        p.putUInt("ctrblk", s_ctr_end);
        p.end();
    }
    return ++s_ctr;
}

void kom_id8(char out[9]) {
    snprintf(out, 9, "%02x%02x%02x%02x", g_kom.id4[0], g_kom.id4[1], g_kom.id4[2], g_kom.id4[3]);
}

void kom_fingerprint(char out[20]) {
    uint8_t h[32];
    kom_sha256(g_kom.pub, 32, h);
    snprintf(out, 20, "%02x%02x %02x%02x %02x%02x %02x%02x", h[0], h[1], h[2], h[3], h[4], h[5], h[6], h[7]);
}

#endif
