#include "kom_config.h"
#if SENSMOS_KOM
#include "kom_frame.h"
#include "kom_id.h"
#include <string.h>
#include <esp_random.h>
#include <mbedtls/aes.h>
#include <mbedtls/md.h>
#include <mbedtls/hkdf.h>

static size_t header(uint8_t* o, uint8_t flags, const uint8_t dst4[4], const uint8_t src4[4], uint32_t ctr) {
    o[0] = 0xE0; o[1] = 0x04; o[2] = flags;
    memcpy(o + 3, dst4, 4);
    memcpy(o + 7, src4, 4);
    o[11] = ctr >> 24; o[12] = ctr >> 16; o[13] = ctr >> 8; o[14] = ctr;
    return 15;
}

// bcode = HMAC-SHA256(K_net, wszystko przed nim)[0:4]
static size_t seal(uint8_t* o, size_t n, const uint8_t knet[32]) {
    uint8_t mac[32];
    kom_hmac(knet, 32, o, n, mac);
    memcpy(o + n, mac, 4);
    return n + 4;
}

size_t kom_build_hello(uint8_t* out, const uint8_t pub[32], const uint8_t id4[4], const uint8_t knet[32],
                       uint32_t ctr, uint8_t vis, bool withPub, const uint8_t* own8, const char* name) {
    static const uint8_t BCAST[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
    size_t n = header(out, KOM_MODE_HELLO, BCAST, id4, ctr);
    size_t nl = name ? strlen(name) : 0;
    if (nl > KOM_NAME_MAX) nl = KOM_NAME_MAX;
    uint8_t hf = vis & 3;
    if (withPub) hf |= 0x04;
    if (own8)    hf |= 0x08;
    if (nl)      hf |= 0x10;
    out[n++] = hf;
    if (withPub) { memcpy(out + n, pub, 32); n += 32; }
    if (own8)    { memcpy(out + n, own8, 8); n += 8; }
    if (nl)      { out[n++] = (uint8_t)nl; memcpy(out + n, name, nl); n += nl; }
    return seal(out, n, knet);
}

// UTF-8 bez znaków sterujących C0 i C1 (tak samo odrzuca serwer).
bool kom_text_ok(const char* t) {
    size_t n = t ? strlen(t) : 0;
    if (n < 1 || n > KOM_TEXT_MAX) return false;
    for (size_t i = 0; i < n; i++) {
        uint8_t c = (uint8_t)t[i];
        if (c < 0x20 || c == 0x7F) return false;
        if (c == 0xC2 && i + 1 < n && (uint8_t)t[i + 1] >= 0x80 && (uint8_t)t[i + 1] <= 0x9F) return false;
    }
    return true;
}

// okm = HKDF(K_net, "sensmos-ldev-acct-v1", 64): [0:32] AES-256-CTR, [32:64] HMAC (tag 8 B).
// IV = nonce8 ‖ src4 ‖ 00000000 — jak E2E w spec §3.2.
size_t kom_build_acct(uint8_t* out, const uint8_t id4[4], const uint8_t knet[32], uint32_t ctr,
                      const char* text, bool alert, const uint8_t* nonce) {
    if (!kom_text_ok(text)) return 0;
    size_t tl = strlen(text);
    size_t n = header(out, alert ? 0x20 : 0x00, id4, id4, ctr);
    uint8_t* nn = out + n;
    if (nonce) memcpy(nn, nonce, 8); else esp_fill_random(nn, 8);
    n += 8;

    static const char L[] = "sensmos-ldev-acct-v1";
    uint8_t okm[64], salt[32] = {0};
    mbedtls_hkdf(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), salt, 32, knet, 32,
                 (const uint8_t*)L, sizeof(L) - 1, okm, 64);
    uint8_t iv[16] = {0}, sb[16];
    size_t off = 0;
    memcpy(iv, nn, 8);
    memcpy(iv + 8, id4, 4);
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_enc(&aes, okm, 256);
    mbedtls_aes_crypt_ctr(&aes, tl, &off, iv, sb, (const uint8_t*)text, out + n);
    mbedtls_aes_free(&aes);
    n += tl;

    uint8_t mac[32];
    kom_hmac(okm + 32, 32, out, n, mac);
    memcpy(out + n, mac, 8);
    n += 8;
    memset(okm, 0, sizeof(okm));
    return seal(out, n, knet);
}

static void acct_keys(const uint8_t knet[32], const char* label, uint8_t okm[64]) {
    uint8_t salt[32] = {0};
    mbedtls_hkdf(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), salt, 32, knet, 32,
                 (const uint8_t*)label, strlen(label), okm, 64);
}

bool kom_open_down(const uint8_t* f, size_t n, const uint8_t id4[4], const uint8_t knet[32],
                   uint32_t* ctr, char* text, size_t cap) {
    // nagłówek 15 + nonce 8 + ≥1 + tag 8 + bcode 4
    if (n < 36 || n > KOM_FRAME_MAX || f[0] != 0xE0 || f[1] != 0x04 || f[2] != KOM_MODE_PRIV) return false;
    if (memcmp(f + 3, id4, 4) || memcmp(f + 7, id4, 4)) return false;
    uint8_t mac[32], okm[64];
    kom_hmac(knet, 32, f, n - 4, mac);
    if (memcmp(mac, f + n - 4, 4)) return false;
    acct_keys(knet, "sensmos-ldev-acct-dn-v1", okm);
    kom_hmac(okm + 32, 32, f, n - 12, mac);
    bool ok = !memcmp(mac, f + n - 12, 8);
    size_t tl = n - 15 - 8 - 12;
    if (ok && tl < cap) {
        uint8_t iv[16] = {0}, sb[16];
        size_t off = 0;
        memcpy(iv, f + 15, 8);
        memcpy(iv + 8, id4, 4);
        mbedtls_aes_context aes;
        mbedtls_aes_init(&aes);
        mbedtls_aes_setkey_enc(&aes, okm, 256);
        mbedtls_aes_crypt_ctr(&aes, tl, &off, iv, sb, f + 23, (uint8_t*)text);
        mbedtls_aes_free(&aes);
        text[tl] = 0;
        *ctr = ((uint32_t)f[11] << 24) | ((uint32_t)f[12] << 16) | ((uint32_t)f[13] << 8) | f[14];
    } else ok = false;
    memset(okm, 0, sizeof(okm));
    return ok;
}

void kom_hkdf(const uint8_t* ikm, size_t n, const uint8_t* info, size_t infon, uint8_t* okm, size_t okmn) {
    uint8_t salt[32] = {0};
    mbedtls_hkdf(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), salt, 32, ikm, n, info, infon, okm, okmn);
}

void kom_ctr_crypt(const uint8_t k[32], const uint8_t nonce[8], const uint8_t src4[4],
                   const uint8_t* in, size_t n, uint8_t* out) {
    uint8_t iv[16] = {0}, sb[16];
    size_t off = 0;
    memcpy(iv, nonce, 8);
    memcpy(iv + 8, src4, 4);
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_enc(&aes, k, 256);
    mbedtls_aes_crypt_ctr(&aes, n, &off, iv, sb, in, out);
    mbedtls_aes_free(&aes);
}

void kom_group_id(const uint8_t key[32], uint8_t gid4[4]) {
    static const char L[] = "sensmos-grp-id-v1";
    uint8_t mac[32];
    kom_hmac(key, 32, (const uint8_t*)L, sizeof(L) - 1, mac);
    memcpy(gid4, mac, 4);
}

bool kom_open_group(const uint8_t* f, size_t n, const uint8_t key[32], char* text, size_t cap) {
    if (n < 36 || n > KOM_FRAME_MAX || f[0] != 0xE0 || f[1] != 0x04 || (f[2] & 3) != KOM_MODE_GROUP) return false;
    static const char L[] = "sensmos-grp-msg-v1";
    uint8_t gid[4], okm[64], mac[32];
    kom_group_id(key, gid);
    if (memcmp(f + 3, gid, 4)) return false;
    kom_hkdf(key, 32, (const uint8_t*)L, sizeof(L) - 1, okm, 64);
    kom_hmac(okm + 32, 32, f, n - 12, mac);
    size_t tl = n - 15 - 8 - 12;
    bool ok = !memcmp(mac, f + n - 12, 8) && tl < cap;
    if (ok) { kom_ctr_crypt(okm, f + 15, f + 7, f + 23, tl, (uint8_t*)text); text[tl] = 0; }
    memset(okm, 0, sizeof(okm));
    return ok;
}

// okm kierunku nadawca → adresat: HKDF(X25519(my, peer), "sensmos-ldev-e2e-v1" ‖ senderPub ‖ recipientPub)
static bool e2e_keys(const uint8_t myPriv[32], const uint8_t peerPub[32], const uint8_t senderPub[32],
                     const uint8_t recipientPub[32], uint8_t okm[64]) {
    static const char L[] = "sensmos-ldev-e2e-v1";
    uint8_t ss[32], info[sizeof(L) - 1 + 64];
    if (!kom_x25519(myPriv, peerPub, ss)) return false;
    memcpy(info, L, sizeof(L) - 1); memcpy(info + sizeof(L) - 1, senderPub, 32); memcpy(info + sizeof(L) - 1 + 32, recipientPub, 32);
    kom_hkdf(ss, 32, info, sizeof(info), okm, 64);
    memset(ss, 0, sizeof(ss));
    return true;
}

bool kom_open_priv(const uint8_t* f, size_t n, const uint8_t myPriv[32], const uint8_t myPub[32],
                   uint8_t senderPub[32], char* text, size_t cap) {
    if (n < 15 + 32 + 8 + 1 + 12 || n > KOM_FRAME_MAX || f[0] != 0xE0 || f[1] != 0x04 || (f[2] & 3) != KOM_MODE_PRIV || !(f[2] & 0x10)) return false;
    uint8_t h[32], okm[64], mac[32];
    kom_sha256(f + 15, 32, h);
    if (memcmp(h, f + 7, 4)) return false;                       // PUB nie pasuje do src4
    if (!e2e_keys(myPriv, f + 15, f + 15, myPub, okm)) return false;
    kom_hmac(okm + 32, 32, f, n - 12, mac);
    size_t tl = n - 15 - 32 - 8 - 12;
    bool ok = !memcmp(mac, f + n - 12, 8) && tl < cap;
    if (ok) { kom_ctr_crypt(okm, f + 47, f + 7, f + 55, tl, (uint8_t*)text); text[tl] = 0; memcpy(senderPub, f + 15, 32); }
    memset(okm, 0, sizeof(okm));
    return ok;
}

size_t kom_build_ack_e2e(uint8_t* out, const uint8_t myPriv[32], const uint8_t myPub[32], const uint8_t myId4[4],
                         const uint8_t peerPub[32], const uint8_t knet[32], uint32_t ctr, uint32_t ref) {
    uint8_t h[32], okm[64], mac[32];
    if (!e2e_keys(myPriv, peerPub, myPub, peerPub, okm)) return 0;
    kom_sha256(peerPub, 32, h);
    size_t n = header(out, KOM_MODE_ACK, h, myId4, ctr);
    out[n++] = ref >> 24; out[n++] = ref >> 16; out[n++] = ref >> 8; out[n++] = ref;
    kom_hmac(okm + 32, 32, out, n, mac);
    memcpy(out + n, mac, 8); n += 8;
    memset(okm, 0, sizeof(okm));
    return seal(out, n, knet);
}

size_t kom_build_ack(uint8_t* out, const uint8_t id4[4], const uint8_t knet[32], uint32_t ctr, uint32_t ref) {
    size_t n = header(out, KOM_MODE_ACK, id4, id4, ctr);
    out[n++] = ref >> 24; out[n++] = ref >> 16; out[n++] = ref >> 8; out[n++] = ref;
    uint8_t okm[64], mac[32];
    acct_keys(knet, "sensmos-ldev-acct-v1", okm);
    kom_hmac(okm + 32, 32, out, n, mac);
    memcpy(out + n, mac, 8);
    n += 8;
    memset(okm, 0, sizeof(okm));
    return seal(out, n, knet);
}

#endif
