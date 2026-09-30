#pragma once

// ══════════════════════════════════════════════════════════════
// Komunikator Sensmos (Heltec V3) — osobny build `esp32s3-kom` (-DSENSMOS_KOM=1).
// Przy SENSMOS_KOM 0 żaden plik z src/kom/ nie wnosi do bina noda ani jednej instrukcji.
// Ramka urządzenia E0 04: DOCS/dev/SPEC-komunikator-v2.md §3 (+ sekcja AKTUALIZACJA).
// ══════════════════════════════════════════════════════════════

#ifndef SENSMOS_KOM
#define SENSMOS_KOM 0
#endif

#define KOM_FW_VERSION      "0.15-kom"

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
#define KOM_DUTY_DN_MS_H    180000UL    // połowa 10% podpasma 869.4–869.65 (reszta dla bram i nodów)
#define KOM_CAD_TRIES       3

// ── HELLO ─────────────────────────────────────────────────────
// Pełne (z kluczem publicznym) przy starcie, po 60 s i po 5 min — serwer poznaje klucz tylko
// z pełnego HELLO, więc jedno zgubione nie może oznaczać doby niewidoczności. Potem krótkie
// co godzinę i pełne raz na dobę.
#define KOM_HELLO_BOOT_MS   { 5000UL, 65000UL, 305000UL }
#define KOM_HELLO_EVERY_MS  3600000UL
#define KOM_HELLO_FULL_MS   86400000UL
// Ogłoszenie na 869.525 (słyszą je inne komunikatory): po starcie, co 15 min z rozrzutem, na żądanie.
#define KOM_ADVERT_FIRST_MS 8000UL
#define KOM_ADVERT_MS       900000UL
#define KOM_NEAR_MAX        32          // „W pobliżu”: komunikatory, bramy i nody usłyszane na 869.525
#define KOM_WATCH_MAX       9           // ID apki + do 8 odcisków jej grup
#define KOM_NAME_MAX        20          // nazwa w HELLO, bajty UTF-8 (10 znaków z polskimi literami)
#define KOM_ADV_MAX         96          // HELLO apki-właściciela do ogłoszeń
#define KOM_BUF_N           16          // ramki dla apki zebrane bez telefonu
#define KOM_RETRY_MS        60000UL     // brak budżetu pasma → spróbuj za minutę

// ── BLE (DOCS/dev/SPEC-komunikator-ble.md §2) ─────────────────
#define KOM_BLE_SVC_UUID    "b7c1a000-6f2d-4e0a-9d3e-5a1c2b3d4e5f"
#define KOM_BLE_RX_UUID     "b7c1a001-6f2d-4e0a-9d3e-5a1c2b3d4e5f"     // zapis apka → komunikator
#define KOM_BLE_TX_UUID     "b7c1a002-6f2d-4e0a-9d3e-5a1c2b3d4e5f"     // powiadomienia komunikator → apka
#define KOM_BLE_JSON_MAX    500         // jedna wiadomość JSON na zapis / powiadomienie
#define KOM_BLE_PIN_TRIES   3           // tyle złych PIN-ów, potem auth odrzucane przez KOM_BLE_LOCK_MS
#define KOM_BLE_LOCK_MS     60000UL

// ── Heltec WiFi LoRa 32 V3 / Wireless Paper (ta sama tabela radia, inny ekran i dioda) ──
#define KOM_PIN_BUTTON      0           // PRG
#define KOM_PIN_LED_V3      35
#define KOM_PIN_LED_PAPER   18
#define KOM_PIN_VEXT        36          // LOW = zasilanie OLED włączone
#define KOM_OLED_SDA        17
#define KOM_OLED_SCL        18
#define KOM_OLED_RST        21
// Wireless Paper V1.0: e-papier 2,13" DEPG0213BNS800 (SSD1680, 250×122) na własnym SPI, zasilanie
// ekranu przez Vext na GPIO45 (LOW = wł.) — piny z przykładu Heltec Wireless_Paper_V1.0_FactoryTest.
// Wyświetlacz (ustawienie z apki, NVS "disp"): e-papieru nie da się wykryć, OLED — tak.
#define KOM_DISP_AUTO       0           // OLED, gdy odpowiada; bez OLED na Heltecu → Wireless Paper V1.1.1/V1.2 (bieżąca)
#define KOM_DISP_NONE       1
#define KOM_DISP_OLED       2           // SSD1306 0,96" I2C (Heltec V3)
#define KOM_DISP_WP10       3           // Heltec Wireless Paper V1.0 (DEPG0213BNS800)
#define KOM_DISP_WP11       4           // Heltec Wireless Paper V1.1 (LCMEN2R13EFC1)
#define KOM_DISP_WP12       5           // Heltec Wireless Paper V1.1.1 / V1.2 (E0213A367)
#define KOM_DISP_MAX        5
#define KOM_EPD_MIN_MS      5000UL      // najczęstsze odświeżenie; pełne co 10 częściowych (bez smug)
