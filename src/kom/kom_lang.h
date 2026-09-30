#pragma once
#include <stdint.h>

// Napisy ekranu w języku apki (BLE `set {lang}`), domyślnie angielski. Czcionki ekranów mają
// tylko ASCII — bez ogonków, umlautów i akcentów.
enum KomStr : uint8_t { S_RADIO_OK, S_NO_RADIO, S_SELFTEST, S_WAIT_1, S_WAIT_N, S_CONNECT, S_FOOT, S_GROUP, S_MESSAGE, S_COUNT };
enum KomLang : uint8_t { LANG_EN = 0, LANG_PL, LANG_DE, LANG_PT, LANG_COUNT };

const char* kom_str(KomStr s);              // w bieżącym języku (g_set.lang)
int8_t      kom_lang_code(const char* code); // "pl" → LANG_PL; nieznany → -1
const char* kom_lang_name(uint8_t lang);    // "en" | "pl" | "de" | "pt"
