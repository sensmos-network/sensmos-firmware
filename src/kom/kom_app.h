#pragma once
#include <stdint.h>
#include <stddef.h>

// Tożsamość APKI skopiowana do komunikatora (user 2026-09-30): klucz prywatny X25519, klucze grup,
// nazwy — przez BLE w pakiecie zaszyfrowanym do klucza urządzenia (kom_app_unseal). Dzięki temu
// komunikator sam odczytuje wiadomości do apki i pokazuje je na ekranie, a bez telefonu potwierdza
// odbiór. Klucze leżą w NVS jawnie — kto ma płytkę w ręku, ma klucze (jak Meshtastic).
#define KOM_APP_GROUPS   8
#define KOM_APP_CONTACTS 24

struct KomAppId {
    bool     set;
    uint8_t  priv[32], pub[32], id4[4], knet[32];
    uint32_t ctr;                          // licznik ramek nadawanych tożsamością apki (dalej niż apka)
    char     name[21];
    uint8_t  ng;
    uint8_t  gkey[KOM_APP_GROUPS][32];
    uint8_t  gid[KOM_APP_GROUPS][4];
    char     gname[KOM_APP_GROUPS][17];
    uint8_t  nc;
    uint8_t  cid[KOM_APP_CONTACTS][4];
    char     cname[KOM_APP_CONTACTS][17];
};
extern KomAppId g_app;

void     kom_app_load();
bool     kom_app_set(const uint8_t* plain, size_t n);      // priv32 ‖ ctr4 BE ‖ nameLen ‖ name
void     kom_app_clear();                                  // klucze, grupy, kontakty
bool     kom_app_group_add(const uint8_t* plain, size_t n); // key32 ‖ nameLen ‖ name
void     kom_app_groups_clear();
void     kom_app_contact(const uint8_t id4[4], const char* name);
void     kom_app_contacts_clear();
uint32_t kom_app_ctr_next();
const char* kom_app_name_of(const uint8_t id4[4], char* hex9);   // kontakt albo hex

// Pakiet z apki: ephPub32 ‖ nonce8 ‖ szyfrogram ‖ tag8, klucz = HKDF(X25519(urządzenie, eph),
// "sensmos-kom-keys-v1" ‖ ephPub ‖ pub urządzenia). out ≥ n.
bool     kom_app_unseal(const uint8_t* b, size_t n, uint8_t* out, size_t* outn);

// Ramka do tożsamości apki (prywatna z PUB albo grupowa): from = nazwa nadawcy (z treści, kontaktu
// albo hex), text bez przedrostka nazwy. senderPub tylko dla prywatnej (do ACK).
bool     kom_app_open(const uint8_t* f, size_t n, char* from, size_t fcap, char* text, size_t tcap,
                      uint8_t senderPub[32], bool* priv);
