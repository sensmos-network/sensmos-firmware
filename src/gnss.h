#pragma once
#ifdef ARDUINO
#include <Arduino.h>
#else
#include <stdint.h>
#include <stddef.h>
#endif

// SENSMOS — GPS na płytce (moduł NMEA na UART, np. NEO-6M/M8N, ATGM336H). Domyślnie WYŁĄCZONY.
// Po co: BE daje wyższy poziom pewności lokalizacji (×1,1, a z sąsiadem LoRa innego właściciela ×1,5),
// gdy nod ma STABILNY fix zgodny z pozycją z ustawienia. GPS niczego nie przesuwa: pozycja noda dalej
// pochodzi z ceremonii (telefon/przeglądarka); fix tylko ją potwierdza.
// Prywatność: do BE leci pozycja przycięta do 0,01° (~1 km), nigdy dokładna.
// Config: serial {"cmd":"gnss","on":true,"rx":16,"baud":9600} albo POST /node/gnss za PIN-em.
// Uwaga: sygnał słaby (moduł da się udawać generatorem NMEA), dlatego tylko ×1,1 i dalej z dowodami sieci.

struct GnssFix {
    bool   valid;      // ramka GGA z jakością >= 1
    double lat, lon;   // stopnie
    uint8_t sats;
    float  hdop;
};

void gnss_init();   // config z NVS, otwarcie UART
void gnss_tick();   // w loop(): czytanie bez blokowania + raport po WS co GNSS_REPORT_MS
const char* gnss_set_config_json(const char* json);   // nullptr = ok, inaczej opis błędu
void gnss_status_json(char* out, size_t cap);

// Czysta logika (testowalna na PC).
bool gnss_nmea_checksum_ok(const char* line);
bool gnss_parse_gga(const char* line, GnssFix& out);   // false = to nie GGA albo zła ramka
// Stabilność: kolejne dobre fixy (sats >= 5, hdop <= 3) w promieniu 200 m od pierwszego.
struct GnssStable {
    uint32_t good;      // ile dobrych fixów z rzędu
    double   lat0, lon0;
};
void gnss_stable_reset(GnssStable& s);
bool gnss_stable_feed(GnssStable& s, const GnssFix& f);   // true = fix zaliczony do serii
static const uint32_t GNSS_STABLE_MIN = 60;               // ~1 min przy 1 Hz
// Ramka WS {"type":"gnss",...}; zwraca długość albo 0.
size_t gnss_build_frame(char* out, size_t cap, const GnssFix& f, uint32_t stable_s);
