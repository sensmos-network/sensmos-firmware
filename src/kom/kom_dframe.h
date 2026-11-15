#pragma once
// Ramka DATA 0x02 w komunikatorze („Dom bez internetu”): ten sam format co nody, Hub i komponent
// ESPHome sensmos_lora, więc nod-baza w domu przyjmuje ją jak ramkę czujnika i pokazuje w /lora/inbox
// (integracja HA czyta ją w LAN, bez serwera). Bez Arduino: test na komputerze w test/kom_dframe/.
//   [E0][02][flags][dst4][sub][nonce4][AES-256-CTR(payload ‖ CRC32 LE)]   flags: bit0 AES, bit1 LAST (nadała baza)
//   IV = nonce4 ‖ dst4 ‖ sub ‖ 0×7, klucz = SHA-256(fraza-klucz bazy).
// Treść komunikatora: „#<n> <tekst>” do domu, „?” = pytanie o odpowiedź (co poll_min minut),
// z domu: „ok #<n>” (potwierdzenie), „ok #<n>|<odpowiedź>”, „ok|<odpowiedź>” albo sam tekst.
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

#define KOM_D_HDR          8
#define KOM_D_NONCE        4
#define KOM_D_PAYLOAD_MAX  128
#define KOM_D_FLAG_AES     0x01
#define KOM_D_FLAG_LAST    0x02

typedef void (*kom_ctr_fn)(const uint8_t key[32], const uint8_t iv[16], uint8_t* buf, size_t n);

inline uint32_t kom_crc32(const uint8_t* d, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= d[i];
        for (int b = 0; b < 8; b++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
    }
    return c ^ 0xFFFFFFFFu;
}

inline void kom_d_iv(uint8_t iv[16], const uint8_t nonce[4], const uint8_t dst[4], uint8_t sub) {
    memset(iv, 0, 16);
    memcpy(iv, nonce, 4);
    memcpy(iv + 4, dst, 4);
    iv[8] = sub;
}

// Zawsze z AES (komunikator nie nadaje jawnych). Zwraca długość ramki albo 0.
inline size_t kom_dframe_build(uint8_t* out, const uint8_t dst[4], uint8_t sub, const uint8_t* p, size_t pn,
                               const uint8_t key[32], const uint8_t nonce[4], kom_ctr_fn ctr) {
    if (!p || !pn || pn > KOM_D_PAYLOAD_MAX || !key || !ctr) return 0;
    out[0] = 0xE0; out[1] = 0x02; out[2] = KOM_D_FLAG_AES;
    memcpy(out + 3, dst, 4);
    out[7] = sub;
    memcpy(out + KOM_D_HDR, nonce, KOM_D_NONCE);
    uint8_t* body = out + KOM_D_HDR + KOM_D_NONCE;
    memcpy(body, p, pn);
    const uint32_t c = kom_crc32(p, pn);
    body[pn] = c & 0xff; body[pn + 1] = (c >> 8) & 0xff; body[pn + 2] = (c >> 16) & 0xff; body[pn + 3] = c >> 24;
    uint8_t iv[16];
    kom_d_iv(iv, nonce, dst, sub);
    ctr(key, iv, body, pn + 4);
    return KOM_D_HDR + KOM_D_NONCE + pn + 4;
}

// Ramka od bazy do mnie (dst = baza, sub = mój, AES + LAST). Zwraca długość treści albo -1.
inline int kom_dframe_open(const uint8_t* f, size_t n, const uint8_t dst[4], uint8_t sub, const uint8_t key[32],
                           kom_ctr_fn ctr, uint8_t* out, size_t cap) {
    if (n < KOM_D_HDR + KOM_D_NONCE + 5 || f[0] != 0xE0 || f[1] != 0x02) return -1;
    if (!(f[2] & KOM_D_FLAG_AES) || !(f[2] & KOM_D_FLAG_LAST)) return -1;
    if (memcmp(f + 3, dst, 4) || f[7] != sub) return -1;
    const size_t bn = n - KOM_D_HDR - KOM_D_NONCE, pn = bn - 4;
    if (pn > KOM_D_PAYLOAD_MAX || pn + 1 > cap) return -1;
    uint8_t body[KOM_D_PAYLOAD_MAX + 4], iv[16];
    memcpy(body, f + KOM_D_HDR + KOM_D_NONCE, bn);
    kom_d_iv(iv, f + KOM_D_HDR, dst, sub);
    ctr(key, iv, body, bn);
    const uint32_t c = (uint32_t)body[pn] | ((uint32_t)body[pn + 1] << 8) | ((uint32_t)body[pn + 2] << 16) |
                       ((uint32_t)body[pn + 3] << 24);
    if (kom_crc32(body, pn) != c) return -1;
    memcpy(out, body, pn);
    out[pn] = 0;
    return (int)pn;
}

// Treść z domu: ack = numer potwierdzonej wiadomości (0 = brak), reply = tekst albo nullptr.
// „ok #3” → ack 3; „ok #3|Jadę” → ack 3, „Jadę”; „ok|Jadę” → „Jadę”; „Obiad gotowy” → sam tekst.
inline void kom_home_parse(char* p, uint32_t* ack, const char** reply) {
    *ack = 0; *reply = nullptr;
    if (!strncmp(p, "ok", 2) && (p[2] == 0 || p[2] == ' ' || p[2] == '|')) {
        char* s = p + 2;
        while (*s == ' ') s++;
        if (*s == '#') { *ack = (uint32_t)strtoul(s + 1, &s, 10); while (*s == ' ') s++; }
        if (*s == '|' && s[1]) *reply = s + 1;
        return;
    }
    if (p[0]) *reply = p;
}
