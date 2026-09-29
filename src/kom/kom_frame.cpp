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
    if (nl > 16) nl = 16;
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

#endif
