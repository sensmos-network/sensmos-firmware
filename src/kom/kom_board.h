#pragma once
#include <stdint.h>
#include <stddef.h>

// Tablica ogłoszeń osiedla (0.19-kom, roadmapa q3-skrzynka-osiedlowa). Ogłoszenia to zwykłe wiadomości grupy
// (tryb 1): osiedle zakłada w apce grupę z hasłem, np. „Osiedle Słoneczne”, i każdy, kto zna hasło, czyta i pisze.
// Komunikator z włączoną tablicą (na balkonie, z zasilacza albo powerbanku) pamięta ostatnie KOM_BOARD_N ogłoszeń
// swoich grup i co pewien czas nadaje je ponownie na 869.525, więc trafiają też do komunikatorów, które przy
// pierwszym nadaniu były poza zasięgiem albo wyłączone. Nadaje bajt w bajt tę samą ramkę: nie zna nic, czego nie
// zna grupa, a odbiorcy odsiewają powtórkę po (nadawca, licznik).
// Kiedy powtarzać: Trickle (RFC 6206) ze stałym okresem. W każdym okresie KOM_BOARD_I_MS losowa chwila z jego
// drugiej połowy; jeśli w tym okresie ktoś (autor albo inna tablica) nadał już to ogłoszenie KOM_BOARD_K razy, ta milczy.
// Dwie tablice na jednym osiedlu nie podwajają więc ruchu.
#define KOM_BOARD_N       4
#define KOM_BOARD_I_MS    1800000UL     // 30 min
#define KOM_BOARD_TTL_MS  86400000UL    // ogłoszenie żyje dobę od pierwszego usłyszenia
#define KOM_BOARD_K       1
#define KOM_BOARD_FMAX    204

typedef int32_t (*KomBoardSend)(const uint8_t* f, size_t n);

void    kom_board_set(bool on, uint32_t now, uint32_t rnd);
bool    kom_board_on();
// Ramka ogłoszenia (już sprawdzona kluczem grupy): nowa = zapamiętana (true); znana = „ktoś ją nadał” (tłumienie).
bool    kom_board_offer(const uint8_t* f, size_t n, uint32_t now);
// Co obieg pętli: w chwili nadania wysyła żywe, nietłumione ogłoszenia. Zwraca liczbę nadanych.
uint8_t kom_board_tick(uint32_t now, uint32_t rnd, KomBoardSend send);
uint8_t kom_board_count(uint32_t now);
void    kom_board_clear();
