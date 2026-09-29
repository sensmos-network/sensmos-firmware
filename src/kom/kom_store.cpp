#include "kom_config.h"
#if SENSMOS_KOM
#include "kom_store.h"
#include "kom_id.h"
#include <Arduino.h>
#include <Preferences.h>

KomSettings g_set;
static const char* NS = "sensmos_kom";

static void pin_hash(const char* pin, uint8_t out[32]) {
    uint8_t buf[64];
    size_t n = strnlen(pin, 16);
    memcpy(buf, pin, n);
    memcpy(buf + n, g_kom.id4, 4);                 // ten sam PIN na dwóch urządzeniach = różne skróty
    kom_sha256(buf, n + 4, out);
}

void kom_store_load() {
    Preferences p;
    p.begin(NS, false);
    memset(&g_set, 0, sizeof(g_set));
    g_set.pin_set  = p.isKey("pinh");
    g_set.vis      = p.getUChar("vis", KOM_VIS_UNSET);
    p.getString("name", g_set.name, sizeof(g_set.name));
    g_set.tpl_n    = p.getUChar("tpln", 0);
    if (g_set.tpl_n > KOM_TPL_MAX) g_set.tpl_n = 0;
    for (int i = 0; i < g_set.tpl_n; i++) {
        char k[6]; snprintf(k, sizeof(k), "tpl%d", i);
        p.getString(k, g_set.tpl[i], sizeof(g_set.tpl[i]));
    }
    p.getString("wssid", g_set.wifi_ssid, sizeof(g_set.wifi_ssid));
    p.getString("wpass", g_set.wifi_pass, sizeof(g_set.wifi_pass));
    p.end();
}

bool kom_pin_check(const char* pin) {
    uint8_t want[32], have[32];
    Preferences p; p.begin(NS, true);
    bool ok = p.getBytes("pinh", want, 32) == 32;
    p.end();
    if (!ok) return false;
    pin_hash(pin, have);
    uint8_t d = 0;
    for (int i = 0; i < 32; i++) d |= want[i] ^ have[i];
    return d == 0;
}

void kom_pin_save(const char* pin) {
    uint8_t h[32];
    pin_hash(pin, h);
    Preferences p; p.begin(NS, false); p.putBytes("pinh", h, 32); p.end();
    g_set.pin_set = true;
}

void kom_vis_save(uint8_t vis) {
    Preferences p; p.begin(NS, false); p.putUChar("vis", vis); p.end();
    g_set.vis = vis;
}

void kom_name_save(const char* name) {
    strlcpy(g_set.name, name ? name : "", sizeof(g_set.name));
    Preferences p; p.begin(NS, false); p.putString("name", g_set.name); p.end();
}


void kom_tpl_save() {
    Preferences p; p.begin(NS, false);
    p.putUChar("tpln", g_set.tpl_n);
    for (int i = 0; i < KOM_TPL_MAX; i++) {
        char k[6]; snprintf(k, sizeof(k), "tpl%d", i);
        if (i < g_set.tpl_n) p.putString(k, g_set.tpl[i]); else p.remove(k);
    }
    p.end();
}

void kom_wifi_save(const char* ssid, const char* pass) {
    strlcpy(g_set.wifi_ssid, ssid ? ssid : "", sizeof(g_set.wifi_ssid));
    strlcpy(g_set.wifi_pass, pass ? pass : "", sizeof(g_set.wifi_pass));
    Preferences p; p.begin(NS, false);
    p.putString("wssid", g_set.wifi_ssid);
    p.putString("wpass", g_set.wifi_pass);
    p.end();
}

void kom_factory_reset() {
    Preferences p; p.begin(NS, false); p.clear(); p.end();
    delay(200);
    ESP.restart();
}

#endif
