#pragma once
#include <stdint.h>
#include <stddef.h>
#include "kom_config.h"

// „Dom bez internetu” (0.17-kom): wiadomość prosto do noda-bazy w domu ramką DATA 0x02 z jego frazą-kluczem.
// Nod w domu (najlepiej w trybie Punkt) wkłada ją do /lora/inbox, integracja Sensmos w HA czyta ją w LAN
// i odpala `sensmos_lora_frame` — bez serwera i bez internetu po żadnej stronie. Odpowiedź i potwierdzenie
// nadaje nod radiem (sensmos.lora_send do własnego czujnika sub), a komunikator słucha jej na 868.1
// przez KOM_HOME_LISTEN_MS po każdym nadaniu. Bez potwierdzenia: do KOM_HOME_TRIES nadań.
#define KOM_HOME_LISTEN_MS  60000UL
#define KOM_HOME_TRIES      3
// Test zasięgu (0.18-kom): pojedyncze „t #n” bez ponowień; dom (blueprint HA) odsyła „ok #n” bez powiadomienia.
// Numery testów zaczynają się od KOM_PING_BASE, żeby nie myliły się z numerami wiadomości.
#define KOM_HOME_PING_MS    30000UL
#define KOM_PING_BASE       1000000UL

struct KomPing {
    uint32_t n;             // numer ostatniego testu (0 = jeszcze żadnego)
    uint8_t  st;            // 0 brak, 1 czeka, 2 odebrane, 3 bez odpowiedzi
    float    rssi, snr;     // jak komunikator słyszał odpowiedź domu
    uint32_t rtt_ms;        // od nadania do odpowiedzi
};

struct KomHome {
    bool     set;
    uint8_t  id4[4];        // id8 noda-bazy
    uint8_t  key[32];       // SHA-256(frazy) — tej samej co na nodzie (apka → nod → LoRa → Odbiór ramek)
    uint8_t  sub;           // mój numer czujnika za bazą (1–255)
    uint8_t  poll_min;      // co ile minut zapytać „?” o odpowiedź (0 = tylko po wysłaniu)
};
extern KomHome g_home;

void        kom_home_load();
bool        kom_home_set(const char* id8, const char* phrase, uint8_t sub, uint8_t poll_min);
void        kom_home_clear();
// Wiadomość do domu: ms w eterze; -1 brak pasma, -2 radio, -3 dom nieustawiony, -4 zła treść, -5 poprzednia czeka.
int32_t     kom_home_send(const char* text, uint32_t* n = nullptr);
bool        kom_home_rx(const uint8_t* b, size_t n, uint32_t now, float rssi = 0, float snr = 0);   // true = ramka z domu (zużyta)
// Test zasięgu do domu: ms w eterze; -1 brak pasma, -2 radio, -3 dom nieustawiony, -5 poprzedni test jeszcze czeka.
int32_t     kom_home_ping(uint32_t* n = nullptr);
const KomPing& kom_home_ping_last();
void        kom_home_tick(uint32_t now);
const char* kom_home_state();               // "off" | "idle" | "wait" | "ok" | "fail"
uint32_t    kom_home_last();                // numer ostatniej wiadomości do domu
