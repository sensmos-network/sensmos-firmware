#include "kom_config.h"
#if SENSMOS_KOM
#include "kom_store.h"
#include <Arduino.h>
#include <Preferences.h>

KomSettings g_set;
static const char* NS = "sensmos_kom";

void kom_store_load() {
    Preferences p;
    p.begin(NS, false);
    memset(&g_set, 0, sizeof(g_set));
    p.getString("bpin", g_set.pin, sizeof(g_set.pin));
    g_set.vis       = p.getUChar("vis", KOM_VIS_UNSET);
    g_set.disp      = p.getUChar("disp", KOM_DISP_AUTO);
    if (g_set.disp > KOM_DISP_MAX) g_set.disp = KOM_DISP_AUTO;
    g_set.lang      = p.getUChar("lang", 0);
    p.getString("name", g_set.name, sizeof(g_set.name));
    g_set.owner_set = p.isKey("owner") && p.getBytes("owner", g_set.owner, 20) == 20;
    g_set.nw = p.getUChar("nw", 0);
    if (g_set.nw > KOM_WATCH_MAX || p.getBytes("watch", g_set.watch, 4 * g_set.nw) != 4u * g_set.nw) g_set.nw = 0;
    g_set.adv_n = p.getUChar("advn", 0);
    if (g_set.adv_n > KOM_ADV_MAX || p.getBytes("adv", g_set.adv, g_set.adv_n) != g_set.adv_n) g_set.adv_n = 0;
    p.end();
}

void kom_pin_save(const char* pin) {
    strlcpy(g_set.pin, pin ? pin : "", sizeof(g_set.pin));
    Preferences p; p.begin(NS, false); p.putString("bpin", g_set.pin); p.end();
}

void kom_vis_save(uint8_t vis) {
    Preferences p; p.begin(NS, false); p.putUChar("vis", vis); p.end();
    g_set.vis = vis;
}

void kom_disp_save(uint8_t disp) {
    Preferences p; p.begin(NS, false); p.putUChar("disp", disp); p.end();
    g_set.disp = disp;
}

void kom_lang_save(uint8_t lang) {
    Preferences p; p.begin(NS, false); p.putUChar("lang", lang); p.end();
    g_set.lang = lang;
}

void kom_name_save(const char* name) {
    strlcpy(g_set.name, name ? name : "", sizeof(g_set.name));
    Preferences p; p.begin(NS, false); p.putString("name", g_set.name); p.end();
}

void kom_owner_save(const uint8_t owner[20]) {
    memcpy(g_set.owner, owner, 20);
    g_set.owner_set = true;
    Preferences p; p.begin(NS, false); p.putBytes("owner", owner, 20); p.end();
}

void kom_watch_save(const uint8_t* ids, uint8_t n) {
    if (n > KOM_WATCH_MAX) n = KOM_WATCH_MAX;
    memcpy(g_set.watch, ids, 4 * n);
    g_set.nw = n;
    Preferences p; p.begin(NS, false);
    p.putUChar("nw", n);
    p.putBytes("watch", g_set.watch, 4 * n);
    p.end();
}

void kom_adv_save(const uint8_t* f, uint8_t n) {
    if (n > KOM_ADV_MAX) n = 0;
    memcpy(g_set.adv, f, n);
    g_set.adv_n = n;
    Preferences p; p.begin(NS, false);
    p.putUChar("advn", n);
    if (n) p.putBytes("adv", g_set.adv, n); else p.remove("adv");
    p.end();
}

void kom_factory_reset() {
    Preferences p; p.begin(NS, false); p.clear(); p.end();
    delay(200);
    ESP.restart();
}

#endif
