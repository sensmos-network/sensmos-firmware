#pragma once
#ifdef ARDUINO
#include <Arduino.h>
#else
#include <stdint.h>
#include <stddef.h>
#endif

// SENSMOS — uniwersalny Modbus RTU (RS485). Dwie niezależne funkcje, obie domyślnie WYŁĄCZONE, config z apki
// w LAN (POST /node/modbus za PIN-em):
//  1) Most Modbus TCP → RTU (port 502): Home Assistant (integracja Modbus), Node-RED, SCADA itp. rozmawiają
//     przez nod z dowolnym urządzeniem na RS485 — falownik, licznik, pompa ciepła, rekuperator. Zapis (FC5/6/15/16)
//     przepuszczany tylko po jawnym włączeniu ("write":true).
//  2) Odpytywanie własnej listy rejestrów (do 16): nod sam czyta FC03/FC04 i wystawia wartości jako encje
//     (own.mb_<nazwa> albo natywne pub.*), więc trafiają na kartę noda, do HA (/data/status) i do sieci.
//     Gotowe listy (presety, np. Deye) to tylko przykłady do jednego kliknięcia.
// Lokalnie najpierw: wszystko działa w LAN bez internetu.

void modbus_init();   // config z NVS, otwarcie UART
void modbus_tick();   // w loop(): nieblokujący automat (most TCP ma pierwszeństwo przed odpytywaniem)

// POST /node/modbus — JSON configu; {"preset":"deye1"} podmienia listę rejestrów na preset. Zwraca błąd albo nullptr.
const char* modbus_set_config_json(const char* json);
// GET /node/modbus: config + presety + ostatnie wartości + liczniki
void modbus_status_json(char* out, size_t cap);

// Czysta logika (testowalna na PC).
uint16_t modbus_crc16(const uint8_t* buf, size_t len);
size_t   modbus_build_read(uint8_t* out, uint8_t slave, uint8_t fc, uint16_t start, uint16_t count);
// Odpowiedź FC03/FC04: liczba rejestrów (0 = błąd ramki/CRC/wyjątek), wartości do regs[].
int      modbus_parse_read(const uint8_t* buf, size_t len, uint8_t slave, uint8_t fc, uint16_t* regs, int max_regs);
// Oczekiwana długość ramki RTU odpowiedzi (z CRC) albo 0, gdy jeszcze nie wiadomo / nieznana funkcja.
size_t   modbus_rtu_reply_len(const uint8_t* buf, size_t len);
// Wartość z rejestrów: type 0=u16 1=s16 2=u32 3=s32 4=f32 5=temp(surowe−1000); lo_first = młodsze słowo pierwsze.
float    modbus_decode(const uint16_t* regs, uint8_t type, bool lo_first);
