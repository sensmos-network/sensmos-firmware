#include "kom_config.h"
#if SENSMOS_KOM
#include "kom_lang.h"
#include "kom_store.h"
#include <string.h>

// Kolejność jak KomStr. Stopka: %u w pobliżu, %s kiedy odbiór, %s RSSI, %.1f/%lu s pasma — ta sama
// kolejność argumentów w każdym języku. Do 41 znaków (6 px na znak przy 250 px).
static const char* const STR[LANG_COUNT][S_COUNT] = {
    { "radio OK", "NO RADIO", "SELFTEST FAIL", "Waiting: %u message", "Waiting: %u messages",
      "Connect your phone to read them.", "near %u  rx %s %s  tx %.1f/%lus", "Group", "Message:" },
    { "radio OK", "BRAK RADIA", "SELFTEST FAIL", "Czeka: %u wiadomosc", "Czekaja: %u wiadomosci",
      "Podlacz telefon, zeby je odczytac.", "pobl %u  rx %s %s  tx %.1f/%lus", "Grupa", "Wiadomosc:" },
    { "Funk OK", "KEIN FUNK", "SELBSTTEST FEHLER", "%u Nachricht wartet", "%u Nachrichten warten",
      "Verbinde dein Telefon, um sie zu lesen.", "Naehe %u  RX %s %s  TX %.1f/%lus", "Gruppe", "Nachricht:" },
    { "radio OK", "SEM RADIO", "FALHA NO AUTOTESTE", "%u mensagem esperando", "%u mensagens esperando",
      "Conecte o celular para le-las.", "perto %u  rx %s %s  tx %.1f/%lus", "Grupo", "Mensagem:" },
};
static const char* const NAMES[LANG_COUNT] = { "en", "pl", "de", "pt" };

const char* kom_str(KomStr s) {
    uint8_t l = g_set.lang < LANG_COUNT ? g_set.lang : LANG_EN;
    return STR[l][s];
}

int8_t kom_lang_code(const char* code) {
    for (uint8_t i = 0; i < LANG_COUNT; i++) if (code && !strcmp(code, NAMES[i])) return (int8_t)i;
    return -1;
}

const char* kom_lang_name(uint8_t lang) { return NAMES[lang < LANG_COUNT ? lang : LANG_EN]; }

#endif
