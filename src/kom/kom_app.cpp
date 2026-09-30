#include "kom_config.h"
#if SENSMOS_KOM
#include "kom_app.h"
#include "kom_id.h"
#include "kom_frame.h"
#include "kom_lang.h"
#include <Arduino.h>
#include <Preferences.h>

KomAppId g_app;
static const char* NS = "sensmos_kom";

// K_net tożsamości apki z PRODUKCYJNYM kluczem serwera (KOM_BE_PUB_HEX) — nie testowym z fixture.
static void derive() {
    uint8_t bePub[32];
    g_app.set = kom_hex(KOM_BE_PUB_HEX, bePub, 32) && kom_x25519(g_app.priv, nullptr, g_app.pub) &&
                kom_knet(g_app.priv, g_app.pub, bePub, g_app.knet);
    if (!g_app.set) return;
    uint8_t h[32];
    kom_sha256(g_app.pub, 32, h);
    memcpy(g_app.id4, h, 4);
    for (uint8_t i = 0; i < g_app.ng; i++) kom_group_id(g_app.gkey[i], g_app.gid[i]);
}

void kom_app_load() {
    memset(&g_app, 0, sizeof(g_app));
    Preferences p; p.begin(NS, false);
    if (p.getBytes("apriv", g_app.priv, 32) == 32) {
        g_app.ctr = p.getUInt("actr", 0);
        p.getString("aname", g_app.name, sizeof(g_app.name));
        g_app.ng = p.getUChar("ang", 0);
        if (g_app.ng > KOM_APP_GROUPS || p.getBytes("agk", g_app.gkey, 32 * g_app.ng) != 32u * g_app.ng ||
            p.getBytes("agn", g_app.gname, 17 * g_app.ng) != 17u * g_app.ng) g_app.ng = 0;
        g_app.nc = p.getUChar("anc", 0);
        if (g_app.nc > KOM_APP_CONTACTS || p.getBytes("acid", g_app.cid, 4 * g_app.nc) != 4u * g_app.nc ||
            p.getBytes("acn", g_app.cname, 17 * g_app.nc) != 17u * g_app.nc) g_app.nc = 0;
        derive();
    }
    p.end();
    if (g_app.set) Serial.printf("[kom] klucze apki: ID %02x%02x%02x%02x, grup %u, kontaktow %u\n",
                                 g_app.id4[0], g_app.id4[1], g_app.id4[2], g_app.id4[3], g_app.ng, g_app.nc);
}

static void save_groups() {
    Preferences p; p.begin(NS, false);
    p.putUChar("ang", g_app.ng);
    p.putBytes("agk", g_app.gkey, 32 * g_app.ng);
    p.putBytes("agn", g_app.gname, 17 * g_app.ng);
    p.end();
}

static void save_contacts() {
    Preferences p; p.begin(NS, false);
    p.putUChar("anc", g_app.nc);
    p.putBytes("acid", g_app.cid, 4 * g_app.nc);
    p.putBytes("acn", g_app.cname, 17 * g_app.nc);
    p.end();
}

bool kom_app_set(const uint8_t* plain, size_t n) {
    if (n < 37 || plain[36] > 20 || n != 37u + plain[36]) return false;
    memcpy(g_app.priv, plain, 32);
    uint32_t c = ((uint32_t)plain[32] << 24) | ((uint32_t)plain[33] << 16) | ((uint32_t)plain[34] << 8) | plain[35];
    if (c > g_app.ctr) g_app.ctr = c;
    memcpy(g_app.name, plain + 37, plain[36]); g_app.name[plain[36]] = 0;
    derive();
    if (!g_app.set) return false;
    Preferences p; p.begin(NS, false);
    p.putBytes("apriv", g_app.priv, 32);
    p.putUInt("actr", g_app.ctr);
    p.putString("aname", g_app.name);
    p.end();
    Serial.printf("[kom] klucze apki zapisane: ID %02x%02x%02x%02x\n", g_app.id4[0], g_app.id4[1], g_app.id4[2], g_app.id4[3]);
    return true;
}

void kom_app_clear() {
    memset(&g_app, 0, sizeof(g_app));
    Preferences p; p.begin(NS, false);
    for (const char* k : { "apriv", "actr", "aname", "ang", "agk", "agn", "anc", "acid", "acn" }) p.remove(k);
    p.end();
    Serial.println("[kom] klucze apki usuniete");
}

bool kom_app_group_add(const uint8_t* plain, size_t n) {
    if (n < 33 || plain[32] > 16 || n != 33u + plain[32]) return false;
    uint8_t i = 0;
    while (i < g_app.ng && memcmp(g_app.gkey[i], plain, 32)) i++;
    if (i == g_app.ng) { if (g_app.ng >= KOM_APP_GROUPS) return false; g_app.ng++; }
    memcpy(g_app.gkey[i], plain, 32);
    memcpy(g_app.gname[i], plain + 33, plain[32]); g_app.gname[i][plain[32]] = 0;
    kom_group_id(g_app.gkey[i], g_app.gid[i]);
    save_groups();
    return true;
}

void kom_app_groups_clear() { g_app.ng = 0; save_groups(); }

void kom_app_contact(const uint8_t id4[4], const char* name) {
    uint8_t i = 0;
    while (i < g_app.nc && memcmp(g_app.cid[i], id4, 4)) i++;
    if (i == g_app.nc) {
        if (g_app.nc >= KOM_APP_CONTACTS) { memmove(g_app.cid[0], g_app.cid[1], 4 * (KOM_APP_CONTACTS - 1)); memmove(g_app.cname[0], g_app.cname[1], 17 * (KOM_APP_CONTACTS - 1)); i = KOM_APP_CONTACTS - 1; }
        else g_app.nc++;
    }
    memcpy(g_app.cid[i], id4, 4);
    strlcpy(g_app.cname[i], name ? name : "", 17);
    save_contacts();
}

void kom_app_contacts_clear() { g_app.nc = 0; save_contacts(); }

uint32_t kom_app_ctr_next() {
    g_app.ctr++;
    Preferences p; p.begin(NS, false); p.putUInt("actr", g_app.ctr); p.end();
    return g_app.ctr;
}

const char* kom_app_name_of(const uint8_t id4[4], char* hex9) {
    for (uint8_t i = 0; i < g_app.nc; i++) if (!memcmp(g_app.cid[i], id4, 4) && g_app.cname[i][0]) return g_app.cname[i];
    snprintf(hex9, 9, "%02x%02x%02x%02x", id4[0], id4[1], id4[2], id4[3]);
    return hex9;
}

bool kom_app_unseal(const uint8_t* b, size_t n, uint8_t* out, size_t* outn) {
    if (n < 32 + 8 + 8) return false;
    static const char L[] = "sensmos-kom-keys-v1";
    uint8_t ss[32], info[sizeof(L) - 1 + 64], okm[64], mac[32];
    if (!kom_x25519(g_kom.priv, b, ss)) return false;
    memcpy(info, L, sizeof(L) - 1); memcpy(info + sizeof(L) - 1, b, 32); memcpy(info + sizeof(L) - 1 + 32, g_kom.pub, 32);
    kom_hkdf(ss, 32, info, sizeof(info), okm, 64);
    kom_hmac(okm + 32, 32, b, n - 8, mac);
    bool ok = !memcmp(mac, b + n - 8, 8);
    if (ok) {
        static const uint8_t Z4[4] = {0};
        *outn = n - 48;
        kom_ctr_crypt(okm, b + 32, Z4, b + 40, *outn, out);
    }
    memset(okm, 0, sizeof(okm)); memset(ss, 0, sizeof(ss));
    return ok;
}

// Nazwa nadawcy w treści: pierwszy bajt < 0x20 = długość (jak w apce).
static void split_name(char* text, char* from, size_t fcap) {
    uint8_t l = (uint8_t)text[0];
    if (!l || l >= 0x20 || l > strlen(text) - 1) return;
    size_t cp = l < fcap - 1 ? l : fcap - 1;
    memcpy(from, text + 1, cp); from[cp] = 0;
    memmove(text, text + 1 + l, strlen(text + 1 + l) + 1);
}

bool kom_app_open(const uint8_t* f, size_t n, char* from, size_t fcap, char* text, size_t tcap,
                  uint8_t senderPub[32], bool* priv) {
    if (!g_app.set || n < 19) return false;
    char hex9[9];
    from[0] = 0;
    uint8_t mode = f[2] & 3;
    if (mode == KOM_MODE_PRIV) {
        if (memcmp(f + 3, g_app.id4, 4) || !kom_open_priv(f, n, g_app.priv, g_app.pub, senderPub, text, tcap)) return false;
        *priv = true;
        split_name(text, from, fcap);
        if (!from[0]) strlcpy(from, kom_app_name_of(f + 7, hex9), fcap);
        return true;
    }
    if (mode == KOM_MODE_GROUP) {
        for (uint8_t i = 0; i < g_app.ng; i++) {
            if (memcmp(f + 3, g_app.gid[i], 4) || !kom_open_group(f, n, g_app.gkey[i], text, tcap)) continue;
            *priv = false;
            char who[24];
            who[0] = 0;
            split_name(text, who, sizeof(who));
            snprintf(from, fcap, "%s: %s", g_app.gname[i][0] ? g_app.gname[i] : kom_str(S_GROUP), who[0] ? who : kom_app_name_of(f + 7, hex9));
            return true;
        }
    }
    return false;
}

#endif
