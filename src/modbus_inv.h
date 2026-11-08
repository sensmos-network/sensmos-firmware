#pragma once
#include <Arduino.h>

// SENSMOS — odczyt falownika PV przez Modbus RTU (RS485), „Sensmos Falownik”.
// Domyślnie WYŁĄCZONY: włącza się z apki w LAN (POST /node/inverter za PIN-em), bo piny UART i model
// falownika zależą od tego, jak użytkownik podłączył moduł RS485. Tylko odczyt (FC03) — firmware nigdy
// nie zapisuje rejestrów falownika.
// Odczyty idą jako natywne encje energii (pub.pv_power, pub.batt_soc, …), więc trafiają tam, gdzie dane
// z płytki: karta noda, Home Assistant (/data/status), batch do sieci. Model „lokalnie najpierw”:
// podgląd działa w LAN bez internetu (GET /node/inverter).

void modbus_inv_init();   // config z NVS, otwarcie UART gdy włączony
void modbus_inv_tick();   // w loop(): nieblokujący automat zapytanie → odpowiedź → encje

// POST /node/inverter {"on":true,"model":"deye1","rx":16,"tx":17,"de":-1,"baud":9600,"slave":1}
bool modbus_inv_set_config(bool on, const char* model, int rx, int tx, int de, long baud, int slave);
// GET /node/inverter: config + ostatnie odczyty + liczniki błędów
void modbus_inv_status_json(char* out, size_t cap);

// Czysta logika (testowalna na PC): CRC i parsowanie ramki FC03.
uint16_t modbus_crc16(const uint8_t* buf, size_t len);
size_t   modbus_build_read(uint8_t* out, uint8_t slave, uint16_t start, uint16_t count);
// Zwraca liczbę odczytanych rejestrów (0 = błąd ramki/CRC/wyjątek), wartości do regs[].
int      modbus_parse_read(const uint8_t* buf, size_t len, uint8_t slave, uint16_t* regs, int max_regs);
