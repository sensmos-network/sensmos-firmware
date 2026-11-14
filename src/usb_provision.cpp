/**
 * SENSMOS — dodawanie noda z komputera przez USB (Web Serial, Chrome/Edge na sensmos.com/my/add/).
 *
 * Odpowiednik BLE register + trust_round + trust_sign, tylko kablem. Przeglądarka nie ma klucza
 * portfela, więc w tej ścieżce NIE ma sig_wallet: obecność przy nodzie dowodzi kabel (fizyczny
 * dostęp), a serwer sprawdza dodatkowo, że przeglądarka wychodzi do internetu z tego samego IP
 * co node (jak przy Hubie). Taki node dostaje poziom "usb" (kwarantanna) zamiast "attested".
 *
 *   {"cmd":"usb_register","owner":"0x..","ssid":"..","password":"..","pin":".."?}
 *      → zapisuje właściciela i WiFi, podpisuje kanoniczny message jak BLE register
 *      ← {"status":"ok","cmd":"usb_register","message":"..","sig_esp":"..","pubkey":"..","proof":".."}
 *      Przeglądarka wysyła to do POST /v1/register, potem {"cmd":"done"} = restart w WiFi.
 *   {"cmd":"usb_round","c":"<16-64 hex>"}            ← {"r":"sha256(c+device_id)","i":N}
 *   {"cmd":"usb_sign","seed":"..","owner":"0x..","gps_lat":"..","gps_lon":".."}
 *      ← {"n","em","rd","up","sig","pk"} — przeglądarka rekonstruuje atest i wysyła do
 *        POST /v1/attest/usb. Kanoniczny atest (DOKŁADNIE ten ciąg podpisuje node):
 *        {"v":3,"via":"usb","device_id":"..","owner":"0x..","seed":"..","nonce":"..",
 *         "efuse_mac":"..","rounds":"<sha256|->","uptime_s":N,"gps_lat":"..","gps_lon":".."}
 *      "via":"usb" sprawia, że podpisu nie da się przenieść do ceremonii BLE (/attest/verify).
 *
 * Zmiana właściciela już przypisanego noda wymaga PIN-u noda (ten sam co w BLE), o ile jest
 * ustawiony. Bufory na heapie tylko na czas komendy (tryb WiFi nie ma luzu w .bss).
 */
#include "usb_provision.h"
#include "identity.h"
#include "ble_config.h"
#include "wifi_manager.h"
#include <ArduinoJson.h>
#include <Preferences.h>
#include <esp_random.h>
#include <esp_mac.h>

#define USB_MAX_ROUNDS 8
#define USB_ROUNDS_LEN (USB_MAX_ROUNDS * 128 + 1)

static char* s_rounds = nullptr;   // c+r kolejnych rund, zwalniane po usb_sign
static int   s_rounds_n = 0;

static void out(const char* json) { Serial.printf("[serial] %s\n", json); }
static void err(const char* cmd, const char* msg) {
    char b[112];
    snprintf(b, sizeof(b), "{\"status\":\"error\",\"cmd\":\"%s\",\"msg\":\"%s\"}", cmd, msg);
    out(b);
}
static bool is_addr(const char* a) {
    if (!a || strlen(a) != 42 || a[0] != '0' || (a[1] != 'x' && a[1] != 'X')) return false;
    for (int i = 2; i < 42; i++) if (!isxdigit((unsigned char)a[i])) return false;
    return true;
}
static bool is_hex(const char* s, size_t lo, size_t hi) {
    size_t n = s ? strlen(s) : 0;
    if (n < lo || n > hi) return false;
    for (size_t i = 0; i < n; i++) if (!isxdigit((unsigned char)s[i])) return false;
    return true;
}
// Współrzędna jako string verbatim (wchodzi do podpisu): -?ddd.ddddddd
static bool is_coord(const char* s) {
    size_t n = s ? strlen(s) : 0;
    if (n == 0 || n > 12) return false;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (!(isdigit((unsigned char)c) || c == '.' || (c == '-' && i == 0))) return false;
    }
    return true;
}
static void lower(char* s) { for (; *s; s++) *s = tolower((unsigned char)*s); }

void usb_cmd_register(JsonDocument& doc, const char* cmd) {
    const char* owner_in = doc["owner"];
    const char* ssid     = doc["ssid"];
    const char* password = doc["password"] | "";
    const char* pin      = doc["pin"] | "";
    if (!is_addr(owner_in))       { err(cmd, "missing_owner"); return; }
    if (!ssid || !strlen(ssid) || strlen(ssid) > 32) { err(cmd, "missing_ssid"); return; }
    if (strlen(password) > 64)    { err(cmd, "bad_password"); return; }

    char owner[43]; strncpy(owner, owner_in, 42); owner[42] = 0; lower(owner);

    // Inny właściciel niż zapisany: wymagany PIN noda (jeśli ustawiony).
    if (strlen(g_owner_address) && strcasecmp(g_owner_address, owner) != 0) {
        Preferences p; p.begin("sensmos", true);
        String saved = p.getString("ble_pin", "");
        p.end();
        if (saved.length() && saved != pin) { err(cmd, "pin_required"); return; }
    }

    strncpy(g_owner_address, owner, sizeof(g_owner_address) - 1);
    if (!strlen(g_backend_url)) strcpy(g_backend_url, "https://api.sensmos.com/v1");
    wifi_save_config(ssid, password);
    { Preferences p; p.begin("sensmos", false);
      p.putString("owner_addr", owner);
      p.putString("backend_url", g_backend_url);
      p.end(); }

    struct RegBuf { char nonce[65]; char message[256]; char sig_hex[145]; char pub_hex[131];
                    char tmp[400]; char resp[900]; };
    RegBuf* buf = (RegBuf*)calloc(1, sizeof(RegBuf));
    if (!buf) { err(cmd, "no_mem"); return; }
    char* nonce = buf->nonce; char* message = buf->message; char* sig_hex = buf->sig_hex;
    char* pub_hex = buf->pub_hex; char* tmp = buf->tmp; char* resp = buf->resp;

    uint8_t rnd[32]; esp_fill_random(rnd, 32); bytes_to_hex(rnd, 32, nonce);
    // Ten sam kształt co BLE register — BE (/v1/register) weryfikuje podpis nad całym message.
    snprintf(message, 256, "{\"device_id\":\"%s\",\"owner\":\"%s\",\"nonce\":\"%s\",\"ts\":0}",
             g_device_id, owner, nonce);
    uint8_t h[32]; sha256_string(message, h);
    uint8_t sig[72]; size_t sig_len = 0;
    if (!identity_sign(h, sig, &sig_len)) { free(buf); err(cmd, "sign_failed"); return; }
    bytes_to_hex(sig, sig_len, sig_hex);
    identity_get_pubkey_hex(pub_hex, 131);

    snprintf(tmp, sizeof(buf->tmp), "%s%s%s", nonce, sig_hex, g_device_id);
    uint8_t ph[32]; sha256_string(tmp, ph);
    char proof[65]; bytes_to_hex(ph, 32, proof);

    // message jako string w JSON-ie: same cudzysłowy do ucieczki (pola są hex/adresem)
    char* esc = tmp; size_t j = 0;
    for (const char* m = message; *m && j < sizeof(buf->tmp) - 3; m++) { if (*m == '"') esc[j++] = '\\'; esc[j++] = *m; }
    esc[j] = 0;
    snprintf(resp, sizeof(buf->resp),
        "{\"status\":\"ok\",\"cmd\":\"usb_register\",\"message\":\"%s\",\"sig_esp\":\"%s\","
        "\"pubkey\":\"%s\",\"proof\":\"%s\"}", esc, sig_hex, pub_hex, proof);
    out(resp);
    free(buf);
}

void usb_cmd_round(JsonDocument& doc, const char* cmd) {
    const char* c = doc["c"];
    if (!is_hex(c, 16, 64)) { err(cmd, "bad_challenge"); return; }
    if (!s_rounds) { s_rounds = (char*)calloc(1, USB_ROUNDS_LEN); s_rounds_n = 0; }
    if (!s_rounds) { err(cmd, "no_mem"); return; }
    if (s_rounds_n >= USB_MAX_ROUNDS) { err(cmd, "too_many_rounds"); return; }

    char in[140]; snprintf(in, sizeof(in), "%s%s", c, g_device_id);
    uint8_t h[32]; sha256_string(in, h);
    char r[65]; bytes_to_hex(h, 32, r);
    size_t used = strlen(s_rounds);
    snprintf(s_rounds + used, USB_ROUNDS_LEN - used, "%s%s", c, r);
    s_rounds_n++;

    char resp[128];
    snprintf(resp, sizeof(resp), "{\"status\":\"ok\",\"cmd\":\"usb_round\",\"r\":\"%s\",\"i\":%d}", r, s_rounds_n);
    out(resp);
}

void usb_cmd_sign(JsonDocument& doc, const char* cmd) {
    const char* seed     = doc["seed"];
    const char* owner_in = doc["owner"];
    const char* lat      = doc["gps_lat"];
    const char* lon      = doc["gps_lon"];
    if (!is_hex(seed, 16, 64))             { err(cmd, "bad_seed"); return; }
    if (!is_addr(owner_in))                { err(cmd, "missing_owner"); return; }
    if (!is_coord(lat) || !is_coord(lon))  { err(cmd, "bad_location"); return; }
    char owner[43]; strncpy(owner, owner_in, 42); owner[42] = 0; lower(owner);
    if (strcasecmp(owner, g_owner_address) != 0) { err(cmd, "owner_mismatch"); return; }

    struct SignBuf { char attest[512]; char sig_hex[145]; char pub_hex[131]; char resp[700]; };
    SignBuf* buf = (SignBuf*)calloc(1, sizeof(SignBuf));
    if (!buf) { err(cmd, "no_mem"); return; }
    char* attest = buf->attest; char* sig_hex = buf->sig_hex;
    char* pub_hex = buf->pub_hex; char* resp = buf->resp;

    uint8_t nr[8]; esp_fill_random(nr, 8);
    char nonce[17]; bytes_to_hex(nr, 8, nonce);
    char rounds[65];
    if (s_rounds && s_rounds_n > 0) {
        uint8_t rh[32]; sha256_string(s_rounds, rh); bytes_to_hex(rh, 32, rounds);
    } else strcpy(rounds, "-");
    uint8_t mac[6]; esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char efuse[13]; bytes_to_hex(mac, 6, efuse);
    unsigned long up = millis() / 1000;

    snprintf(attest, sizeof(buf->attest),
        "{\"v\":3,\"via\":\"usb\",\"device_id\":\"%s\",\"owner\":\"%s\",\"seed\":\"%s\","
        "\"nonce\":\"%s\",\"efuse_mac\":\"%s\",\"rounds\":\"%s\",\"uptime_s\":%lu,"
        "\"gps_lat\":\"%s\",\"gps_lon\":\"%s\"}",
        g_device_id, owner, seed, nonce, efuse, rounds, up, lat, lon);
    uint8_t h[32]; sha256_string(attest, h);
    uint8_t sig[72]; size_t sig_len = 0;
    if (!identity_sign(h, sig, &sig_len)) { free(buf); err(cmd, "sign_failed"); return; }
    bytes_to_hex(sig, sig_len, sig_hex);
    identity_get_pubkey_hex(pub_hex, 131);

    snprintf(resp, sizeof(buf->resp),
        "{\"status\":\"ok\",\"cmd\":\"usb_sign\",\"n\":\"%s\",\"em\":\"%s\",\"rd\":\"%s\","
        "\"up\":%lu,\"sig\":\"%s\",\"pk\":\"%s\"}", nonce, efuse, rounds, up, sig_hex, pub_hex);
    out(resp);
    free(buf);
    // Ceremonia jednorazowa
    free(s_rounds); s_rounds = nullptr; s_rounds_n = 0;
}
