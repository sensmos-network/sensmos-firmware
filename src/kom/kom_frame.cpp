#include "kom_config.h"
#if SENSMOS_KOM
#include "kom_frame.h"
#include "kom_id.h"
#include <string.h>

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
                       uint32_t ctr, uint8_t vis, bool withPub, const char* name) {
    static const uint8_t BCAST[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
    size_t n = header(out, KOM_MODE_HELLO, BCAST, id4, ctr);
    size_t nl = name ? strlen(name) : 0;
    if (nl > 16) nl = 16;
    uint8_t hf = vis & 3;
    if (withPub) hf |= 0x04;
    if (nl)      hf |= 0x10;
    out[n++] = hf;
    if (withPub) { memcpy(out + n, pub, 32); n += 32; }
    if (nl)      { out[n++] = (uint8_t)nl; memcpy(out + n, name, nl); n += nl; }
    return seal(out, n, knet);
}

#endif
