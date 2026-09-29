#pragma once

// ══════════════════════════════════════════════════════════════
// Komunikator Sensmos (Heltec V3) — osobny build `esp32s3-kom` (-DSENSMOS_KOM=1).
// Przy SENSMOS_KOM 0 żaden plik z src/kom/ nie wnosi do bina noda ani jednej instrukcji.
// Ramka urządzenia E0 04: DOCS/dev/SPEC-komunikator-v2.md §3 (+ sekcja AKTUALIZACJA).
// ══════════════════════════════════════════════════════════════

#ifndef SENSMOS_KOM
#define SENSMOS_KOM 0
#endif

#define KOM_FW_VERSION      "0.1-kom"

// Klucz publiczny serwera (GET https://api.sensmos.com/v1/ldev/key). Stały: jego zmiana
// unieważnia podpis sieciowy wszystkich urządzeń.
#define KOM_BE_PUB_HEX      "a0a5230559529a5e26058e7415d3235bfbad71c3052bbe7765189bc213c60f51"

// ── Radio ─────────────────────────────────────────────────────
// Nadaje na kanale uplinku (słuchają go wszystkie nody i bramy), odbiera stale na 869.525,
// gdzie nadają bramy i nody. Oba z tym samym słowem synchronizacji co reszta sieci.
#define KOM_UP_FREQ         868.1f
#define KOM_UP_SF           11
#define KOM_DN_FREQ         869.525f
#define KOM_DN_SF           9
#define KOM_BW              125.0f
#define KOM_CR              5
#define KOM_SYNC            0x34
#define KOM_PREAMBLE        8
#define KOM_TX_POWER        14          // dBm — limit 25 mW ERP w podpaśmie 868.0–868.6
#define KOM_DUTY_UP_MS_H    36000UL     // 1% z godziny na 868.1
#define KOM_CAD_TRIES       3

// ── HELLO ─────────────────────────────────────────────────────
// Pełne (z kluczem publicznym) przy starcie, po 60 s i po 5 min — serwer poznaje klucz tylko
// z pełnego HELLO, więc jedno zgubione nie może oznaczać doby niewidoczności. Potem krótkie
// co godzinę i pełne raz na dobę.
#define KOM_HELLO_BOOT_MS   { 5000UL, 65000UL, 305000UL }
#define KOM_HELLO_EVERY_MS  3600000UL
#define KOM_HELLO_FULL_MS   86400000UL
#define KOM_RETRY_MS        60000UL     // brak budżetu pasma → spróbuj za minutę

// ── Heltec WiFi LoRa 32 V3 ────────────────────────────────────
#define KOM_PIN_BUTTON      0           // PRG
#define KOM_PIN_LED         35
#define KOM_PIN_VEXT        36          // LOW = zasilanie OLED włączone
#define KOM_OLED_SDA        17
#define KOM_OLED_SCL        18
#define KOM_OLED_RST        21
#define KOM_BTN_LONG_MS     3000UL
