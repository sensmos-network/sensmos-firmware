#include "kom_config.h"
#if SENSMOS_KOM
#include "kom_ble.h"
#include "kom_id.h"
#include "kom_store.h"
#include "kom_frame.h"
#include "kom_radio.h"
#include "kom_ui.h"
#include "kom_app.h"
#include "kom_lang.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <ArduinoJson.h>

// Bez parowania BLE (telefon nie pokazuje żadnego okna). Opcjonalny PIN sprawdza sam komunikator:
// gdy ustawiony, połączenie dostaje tylko info i auth, dopóki auth nie poda PIN-u.
// Jedno połączenie naraz — reklama wraca po rozłączeniu.
// Callback zapisu tylko kopiuje żądanie do kolejki (task hosta NimBLE ma mały stos); JSON, radio
// i NVS obsługuje kom_ble_tick() w pętli. Kolejka, bo nadanie LoRa blokuje pętlę na sekundy.
struct KomBleReq { char b[KOM_BLE_JSON_MAX + 1]; };
static QueueHandle_t         s_q = nullptr;
static KomBleReq             s_in;                    // zapisy przychodzą po kolei z taska hosta
static NimBLEServer*         s_server = nullptr;
static NimBLECharacteristic* s_tx = nullptr;
static volatile uint16_t     s_conn = BLE_HS_CONN_HANDLE_NONE;
static volatile bool         s_sub = false;           // powiadomienia włączone (CCCD)
static volatile bool         s_new = false;           // nowe połączenie — kom_ble_tick zeruje auth
static bool                  s_authed = false;        // PIN podany na tym połączeniu
static bool                  s_reset = false;
static bool                  s_restart = false;     // po zmianie wyświetlacza
static uint8_t               s_fails = 0;
static uint32_t              s_lock_until = 0;

class KomBleConn : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer*, NimBLEConnInfo& ci) override {
        s_conn = ci.getConnHandle(); s_sub = false; s_new = true;
        Serial.println("[kom] BLE: polaczono");
    }
    void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int) override {
        s_conn = BLE_HS_CONN_HANDLE_NONE; s_sub = false; s_new = true;
        xQueueReset(s_q);                                  // zaległe żądania nie trafią do następnego telefonu
        Serial.println("[kom] BLE: rozlaczono");
    }
};

class KomBleSub : public NimBLECharacteristicCallbacks {
    void onSubscribe(NimBLECharacteristic*, NimBLEConnInfo&, uint16_t v) override { s_sub = v & 1; }
};

class KomBleWrite : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* ch, NimBLEConnInfo&) override {
        NimBLEAttValue v = ch->getValue();
        size_t n = v.length() < KOM_BLE_JSON_MAX ? v.length() : KOM_BLE_JSON_MAX;
        memcpy(s_in.b, v.data(), n);
        s_in.b[n] = 0;
        if (xQueueSend(s_q, &s_in, 0) != pdTRUE) Serial.println("[kom] BLE: kolejka pelna, zapis odrzucony");
    }
};

static void notify_json(JsonDocument& d) {
    uint16_t c = s_conn;
    if (c == BLE_HS_CONN_HANDLE_NONE || !s_sub) return;
    char out[KOM_BLE_JSON_MAX + 1];
    size_t n = serializeJson(d, out, sizeof(out));
    s_tx->notify((const uint8_t*)out, n, c);
}

// Rzutowanie na const char*: ArduinoJson traktuje const char[N] jak literał (długość N-1).
static void msg_json(JsonObject o, const KomMsg* m) {
    o["seq"] = m->seq;
    o["dir"] = m->out ? "out" : "in";
    o["text"] = (const char*)m->text;
    o["ago_s"] = (millis() - m->at) / 1000;
}

// Nazwa w HELLO: UTF-8 bez znaków sterujących, ≤KOM_NAME_MAX bajtów (serwer odrzuca resztę).
static bool name_ok(const char* s) {
    size_t n = strlen(s);
    if (n > KOM_NAME_MAX) return false;
    for (size_t i = 0; i < n; i++) if ((uint8_t)s[i] < 0x20 || s[i] == 0x7F) return false;
    return true;
}

static bool pin_ok(const char* p) { return strlen(p) == 6 && strspn(p, "0123456789") == 6; }

// Zwraca kod błędu albo nullptr (ok); pola odpowiedzi dopisuje do r.
static const char* run(const char* cmd, JsonDocument& q, JsonDocument& r) {
    if (!strcmp(cmd, "auth")) {
        if (!g_set.pin[0]) { s_authed = true; return nullptr; }
        int32_t left = (int32_t)(s_lock_until - millis());
        if (left > 0) { r["wait"] = left / 1000 + 1; return "locked"; }
        if (!strcmp(q["pin"] | "", g_set.pin)) { s_authed = true; s_fails = 0; return nullptr; }
        if (++s_fails >= KOM_BLE_PIN_TRIES) { s_fails = 0; s_lock_until = millis() + KOM_BLE_LOCK_MS; }
        return "pin";
    }
    if (g_set.pin[0] && !s_authed && strcmp(cmd, "info")) return "auth";
    if (!strcmp(cmd, "info")) {
        char id8[9], fp[20], owner[43] = "0x";
        kom_id8(id8); kom_fingerprint(fp);
        r["id8"] = id8; r["fp"] = fp; r["fw"] = KOM_FW_VERSION; r["board"] = kom_radio_board();
        r["name"] = g_set.name; r["vis"] = g_set.vis;
        if (g_set.owner_set) {
            for (int i = 0; i < 20; i++) snprintf(owner + 2 + 2 * i, 3, "%02x", g_set.owner[i]);
            r["owner"] = owner;
        } else {
            r["owner"] = nullptr;
        }
        r["radio"] = kom_radio_ok(); r["duty_ms"] = kom_radio_duty_ms(); r["duty_max"] = KOM_DUTY_UP_MS_H;
        r["pin_set"] = g_set.pin[0] != 0;
        r["display"] = g_set.disp; r["display_on"] = kom_ui_display_name(); r["lang"] = kom_lang_name(g_set.lang);
        char pub[65];
        for (int i = 0; i < 32; i++) snprintf(pub + 2 * i, 3, "%02x", g_kom.pub[i]);
        r["pub"] = (const char*)pub;
        if (g_app.set) {
            char aid[9];
            for (int i = 0; i < 4; i++) snprintf(aid + 2 * i, 3, "%02x", g_app.id4[i]);
            r["keys_id8"] = (const char*)aid; r["keys_ng"] = g_app.ng; r["keys_nc"] = g_app.nc; r["keys_ctr"] = g_app.ctr;
        } else {
            r["keys_id8"] = nullptr;
        }
        return nullptr;
    }
    if (!strcmp(cmd, "msgs")) {
        const KomMsg* m[3];
        bool more;
        uint8_t n = kom_msgs(q["after"] | (uint32_t)0, m, 3, &more);
        JsonArray a = r["msgs"].to<JsonArray>();
        r["more"] = false;
        for (uint8_t i = 0; i < n; i++) {
            msg_json(a.add<JsonObject>(), m[i]);
            if (measureJson(r) > KOM_BLE_JSON_MAX) { a.remove(i); more = true; break; }
        }
        r["more"] = more;
        return nullptr;
    }
    if (!strcmp(cmd, "send")) {
        const char* t = q["text"] | "";
        if (!kom_text_ok(t)) return "text";
        uint32_t seq = 0;
        int32_t air = kom_send_msg(t, &seq);
        if (air < 0) return air == -1 ? "budget" : air == -4 ? "text" : "radio";
        r["seq"] = seq; r["air_ms"] = air;
        return nullptr;
    }
    if (!strcmp(cmd, "set")) {
        JsonVariantConst n = q["name"], v = q["vis"], d = q["display"], lg = q["lang"];
        if (!lg.isNull() && (!lg.is<const char*>() || kom_lang_code(lg.as<const char*>()) < 0)) return "lang";
        if (!n.isNull() && (!n.is<const char*>() || !name_ok(n.as<const char*>()))) return "name";
        if (!v.isNull() && (!v.is<uint8_t>() || v.as<uint8_t>() > 2)) return "vis";
        if (!d.isNull() && (!d.is<uint8_t>() || d.as<uint8_t>() > KOM_DISP_MAX)) return "display";
        if (!lg.isNull()) kom_lang_save((uint8_t)kom_lang_code(lg.as<const char*>()));   // ekran przerysuje się sam (zmiana treści)
        if (!n.isNull()) kom_name_save(n.as<const char*>());
        if (!v.isNull()) kom_vis_save(v.as<uint8_t>());
        if (!n.isNull() || !v.isNull()) kom_request_hello();
        if (!d.isNull() && d.as<uint8_t>() != g_set.disp) { kom_disp_save(d.as<uint8_t>()); s_restart = true; }   // inne piny/SPI — od nowa
        return nullptr;
    }
    if (!strcmp(cmd, "pin")) {
        const char* p = q["pin"] | "";
        if (p[0] && !pin_ok(p)) return "pin";
        kom_pin_save(p);
        s_authed = true;
        return nullptr;
    }
    if (!strcmp(cmd, "pair")) {
        const char* o = q["owner"] | "";
        uint8_t w[20];
        if (strlen(o) != 42 || o[0] != '0' || (o[1] != 'x' && o[1] != 'X') ||
            strspn(o + 2, "0123456789abcdefABCDEF") != 40 || !kom_hex(o + 2, w, 20)) return "owner";
        kom_pair_owner(w);
        return nullptr;
    }
    if (!strcmp(cmd, "raw")) {
        const char* h = q["hex"] | "";
        size_t hl = strlen(h);
        uint8_t f[KOM_FRAME_MAX];
        if (hl < 38 || hl > 2 * KOM_FRAME_MAX || (hl & 1) || strspn(h, "0123456789abcdefABCDEF") != hl ||
            !kom_hex(h, f, hl / 2) || f[0] != 0xE0 || f[1] != 0x04) return "hex";
        int32_t air = kom_raw_send(f, hl / 2);
        if (air < 0) return air == -1 ? "budget" : "radio";
        r["air_ms"] = air;
        return nullptr;
    }
    if (!strcmp(cmd, "watch")) {
        uint8_t ids[4 * KOM_WATCH_MAX];
        uint8_t n = 0;
        for (const char* key : { "ids", "gids" })
            for (JsonVariantConst v : q[key].as<JsonArrayConst>()) {
                const char* s = v | "";
                if (n >= KOM_WATCH_MAX || strlen(s) != 8 || strspn(s, "0123456789abcdefABCDEF") != 8 ||
                    !kom_hex(s, ids + 4 * n, 4)) return "watch";
                n++;
            }
        kom_watch_save(ids, n);
        return nullptr;
    }
    if (!strcmp(cmd, "advert")) {                                // HELLO apki do ogłoszeń; "" = bez właściciela
        const char* h = q["hex"] | "";
        size_t hl = strlen(h);
        uint8_t f[KOM_ADV_MAX];
        if (hl && (hl < 40 || hl > 2 * KOM_ADV_MAX || (hl & 1) || strspn(h, "0123456789abcdefABCDEF") != hl ||
                   !kom_hex(h, f, hl / 2) || f[0] != 0xE0 || f[1] != 0x04 || (f[2] & 3) != KOM_MODE_HELLO ||
                   memcmp(f + 3, "\xff\xff\xff\xff", 4))) return "hex";
        if (hl / 2 != g_set.adv_n || memcmp(f, g_set.adv, hl / 2)) kom_adv_save(f, hl / 2);
        return nullptr;
    }
    // Kopia kluczy apki: pakiety zaszyfrowane do klucza urządzenia (kom_app_unseal). "" = usuń wszystko.
    if (!strcmp(cmd, "keys") || !strcmp(cmd, "gkey")) {
        const char* h = q["hex"] | "";
        size_t hl = strlen(h);
        if (!hl) { if (!strcmp(cmd, "keys")) kom_app_clear(); else kom_app_groups_clear(); return nullptr; }
        uint8_t b[320], plain[320];
        size_t pn;
        if (hl < 96 || hl > 2 * sizeof(b) || (hl & 1) || strspn(h, "0123456789abcdefABCDEF") != hl || !kom_hex(h, b, hl / 2)) return "hex";
        if (!kom_app_unseal(b, hl / 2, plain, &pn)) return "seal";
        bool ok = !strcmp(cmd, "keys") ? kom_app_set(plain, pn) : kom_app_group_add(plain, pn);
        memset(plain, 0, sizeof(plain));
        return ok ? nullptr : "keys";
    }
    if (!strcmp(cmd, "cname")) {
        const char* id = q["id8"] | "";
        uint8_t id4[4];
        if (!id[0]) { kom_app_contacts_clear(); return nullptr; }
        if (strlen(id) != 8 || !kom_hex(id, id4, 4)) return "id";
        const char* nm = q["name"] | "";
        if (!name_ok(nm)) return "name";
        kom_app_contact(id4, nm);
        return nullptr;
    }
    if (!strcmp(cmd, "frames")) {
        uint8_t f[KOM_FRAME_MAX];
        size_t n;
        bool more = false;
        if (kom_frame_pop(f, &n, &more)) {
            char h[2 * KOM_FRAME_MAX + 1];
            for (size_t i = 0; i < n; i++) snprintf(h + 2 * i, 3, "%02x", f[i]);
            r["hex"] = (const char*)h;
        }
        r["more"] = more;
        return nullptr;
    }
    if (!strcmp(cmd, "show")) {                                  // ekran: ostatnia wiadomość z apki
        kom_show(q["l1"] | "", q["l2"] | "");
        return nullptr;
    }
    if (!strcmp(cmd, "near")) {
        const KomNear* e;
        uint8_t n = kom_near(&e);
        uint8_t from = q["from"] | 0;
        JsonArray a = r["near"].to<JsonArray>();
        uint8_t i = from;
        static const char* KIND[] = { "kom", "gw", "node" };
        for (; i < n; i++) {
            char id8[9];
            for (int k = 0; k < 4; k++) snprintf(id8 + 2 * k, 3, "%02x", e[i].id4[k]);
            JsonObject o = a.add<JsonObject>();
            o["id8"] = id8; o["kind"] = KIND[e[i].kind];
            if (e[i].name[0]) o["name"] = (const char*)e[i].name;
            if (e[i].has_pub) {
                char p[65];
                for (int k = 0; k < 32; k++) snprintf(p + 2 * k, 3, "%02x", e[i].pub[k]);
                o["pub"] = (const char*)p;
            }
            o["rssi"] = (int)e[i].rssi; o["ago_s"] = (millis() - e[i].at) / 1000;
            if (measureJson(r) > KOM_BLE_JSON_MAX - 20) { a.remove(a.size() - 1); break; }
        }
        r["next"] = i; r["more"] = i < n;
        return nullptr;
    }
    if (!strcmp(cmd, "hello")) {
        kom_request_hello();
        return nullptr;
    }
    if (!strcmp(cmd, "reset")) {
        if (strcmp(q["confirm"] | "", "RESET")) return "confirm";
        s_reset = true;
        return nullptr;
    }
    return "cmd";
}

static void handle(const char* s) {
    JsonDocument q, r;
    if (deserializeJson(q, s)) {
        r["ok"] = false; r["err"] = "json";
        Serial.println("[kom] BLE: zly JSON");
        return notify_json(r);
    }
    const char* cmd = q["cmd"] | "";
    r["cmd"] = q["cmd"];
    r["id"] = q["id"];
    r["ok"] = true;
    const char* e = run(cmd, q, r);
    if (e) { r["ok"] = false; r["err"] = e; }
    Serial.printf("[kom] BLE %s: %s\n", cmd, e ? e : "ok");
    notify_json(r);
    if (s_reset) {                                           // odpowiedź zdąży wyjść
        delay(500);
        kom_factory_reset();
    }
    if (s_restart) {
        delay(500);
        ESP.restart();
    }
}

void kom_ble_start() {
    s_q = xQueueCreate(4, sizeof(KomBleReq));
    char id8[9], name[20];
    kom_id8(id8);
    snprintf(name, sizeof(name), "Sensmos %s", id8);
    NimBLEDevice::init(name);
    NimBLEDevice::setMTU(512);
    s_server = NimBLEDevice::createServer();
    s_server->setCallbacks(new KomBleConn());
    s_server->advertiseOnDisconnect(true);
    NimBLEService* svc = s_server->createService(KOM_BLE_SVC_UUID);
    NimBLECharacteristic* rx = svc->createCharacteristic(KOM_BLE_RX_UUID, NIMBLE_PROPERTY::WRITE);
    rx->setCallbacks(new KomBleWrite());
    s_tx = svc->createCharacteristic(KOM_BLE_TX_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    s_tx->setCallbacks(new KomBleSub());
    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->addServiceUUID(KOM_BLE_SVC_UUID);
    adv->enableScanResponse(true);                         // UUID 128 b + nazwa nie mieszczą się w 31 B
    adv->setName(name);
    NimBLEDevice::startAdvertising();
    Serial.printf("[kom] BLE \"%s\", PIN %s\n", name, g_set.pin[0] ? "tak" : "nie");
}

void kom_ble_tick() {
    static KomBleReq q;
    if (s_new) { s_new = false; s_authed = false; }
    if (s_q && xQueueReceive(s_q, &q, 0) == pdTRUE) handle(q.b);
}

bool kom_ble_frame_event(const uint8_t* f, size_t n, float rssi) {
    if (s_conn == BLE_HS_CONN_HANDLE_NONE || !s_sub || (g_set.pin[0] && !s_authed)) return false;
    char h[2 * KOM_FRAME_MAX + 1];
    for (size_t i = 0; i < n; i++) snprintf(h + 2 * i, 3, "%02x", f[i]);
    JsonDocument d;
    d["ev"] = "frame"; d["hex"] = (const char*)h; d["rssi"] = (int)rssi;
    notify_json(d);
    return true;
}

void kom_ble_msg_event(const KomMsg* m) {
    JsonDocument d;
    JsonObject o = d.to<JsonObject>();
    o["ev"] = "msg";
    msg_json(o, m);
    notify_json(d);
}

#endif
