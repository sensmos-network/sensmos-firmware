#include "kom_config.h"
#if SENSMOS_KOM
#include "kom_radio.h"
#include "../lora_config.h"
#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>
#include <math.h>

static const LoraPinout PINS[] = LORA_PINOUTS;
static const int N_PINS = sizeof(PINS) / sizeof(PINS[0]);

static SX1262*  s_r = nullptr;
static const LoraPinout* s_pin = nullptr;
static bool     s_ok = false;
static volatile bool s_irq = false;
ICACHE_RAM_ATTR static void on_dio1() { s_irq = true; }

// Budżet nadawania: 60 kubełków po minucie (komunikator nie ma zegara UTC — liczymy na millis()).
static uint32_t s_bucket[60];
static uint32_t s_bucket_min = 0;

static void duty_roll() {
    uint32_t m = millis() / 60000UL;
    if (m - s_bucket_min >= 60) { memset(s_bucket, 0, sizeof(s_bucket)); s_bucket_min = m; return; }
    while (s_bucket_min < m) { s_bucket_min++; s_bucket[s_bucket_min % 60] = 0; }
}

uint32_t kom_radio_duty_ms() {
    duty_roll();
    uint32_t s = 0;
    for (int i = 0; i < 60; i++) s += s_bucket[i];
    return s;
}

// Czas w eterze (BW125, nagłówek jawny, CRC, LDRO od SF11) — wzór z dokumentacji Semtecha.
uint32_t kom_airtime_ms(uint8_t sf, size_t len) {
    float tsym = (float)(1u << sf) / 125.0f;
    int de  = sf >= 11 ? 1 : 0;
    int num = 8 * (int)len - 4 * sf + 28 + 16;
    int den = 4 * (sf - 2 * de);
    int pay = 8 + max((int)ceilf((float)num / den) * KOM_CR, 0);
    return (uint32_t)(((KOM_PREAMBLE + 4.25f) + pay) * tsym + 0.5f);
}

static bool try_pin(const LoraPinout& p) {
    SPI.end();
    SPI.begin(p.sck, p.miso, p.mosi, p.nss);
    Module* m = new Module(p.nss, p.dio1, p.rst, p.busy);
    SX1262* r = new SX1262(m);
    if (p.rxen >= 0) r->setRfSwitchPins(p.rxen, RADIOLIB_NC);
    int st = r->begin(KOM_DN_FREQ, KOM_BW, KOM_DN_SF, KOM_CR, KOM_SYNC, KOM_TX_POWER, KOM_PREAMBLE, p.tcxo, false);
    if (st != RADIOLIB_ERR_NONE) { delete r; delete m; return false; }
    s_r = r; s_pin = &p;
    return true;
}

static void back_to_rx() {
    s_r->setFrequency(KOM_DN_FREQ);
    s_r->setSpreadingFactor(KOM_DN_SF);
    s_irq = false;                       // TxDone też podnosi DIO1 — to nie odbiór
    s_r->startReceive();
}

bool kom_radio_init() {
    // Komunikator to Heltec — jego wpisy najpierw, reszta tablicy jak w nodzie.
    for (int pass = 0; pass < 2 && !s_ok; pass++)
        for (int i = 0; i < N_PINS && !s_ok; i++) {
            bool heltec = strncmp(PINS[i].name, "heltec", 6) == 0;
            if (heltec == (pass == 0)) s_ok = try_pin(PINS[i]);
        }
    if (!s_ok) { Serial.println("[kom] SX1262 nie odpowiada"); return false; }
    s_r->setCRC(2);
    s_r->setDio1Action(on_dio1);
    Serial.printf("[kom] radio %s, tcxo %.1fV\n", s_pin->name, s_pin->tcxo);
    return true;
}

bool kom_radio_ok() { return s_ok; }
const char* kom_radio_board() { return s_pin ? s_pin->name : "-"; }

void kom_radio_random(uint8_t* buf, size_t n) {
    for (size_t i = 0; i < n; i++) buf[i] = s_ok ? s_r->randomByte() : (uint8_t)esp_random();
}

// Wywołać po kom_radio_random — dopiero tu radio przechodzi w spoczynkowy odbiór.
static bool s_rx_started = false;
static void ensure_rx() { if (!s_rx_started && s_ok) { back_to_rx(); s_rx_started = true; } }

int32_t kom_radio_send_up(const uint8_t* f, size_t n) {
    if (!s_ok) return -2;
    uint32_t est = kom_airtime_ms(KOM_UP_SF, n);
    if (kom_radio_duty_ms() + est > KOM_DUTY_UP_MS_H) return -1;
    s_r->standby();
    s_r->setFrequency(KOM_UP_FREQ);
    s_r->setSpreadingFactor(KOM_UP_SF);
    s_r->setOutputPower(KOM_TX_POWER);
    for (int i = 0; i < KOM_CAD_TRIES; i++) {             // słuchaj przed nadaniem
        if (s_r->scanChannel() != RADIOLIB_LORA_DETECTED) break;
        delay(random(200, 800));
    }
    uint32_t t0 = millis();
    int st = s_r->transmit((uint8_t*)f, n);
    uint32_t air = millis() - t0;
    back_to_rx();
    s_rx_started = true;
    if (st != RADIOLIB_ERR_NONE) { Serial.printf("[kom] TX błąd %d\n", st); return -2; }
    duty_roll();
    s_bucket[s_bucket_min % 60] += air;
    return (int32_t)air;
}

bool kom_radio_poll(uint8_t* buf, size_t cap, size_t* len, float* rssi, float* snr) {
    ensure_rx();
    if (!s_ok || !s_irq) return false;
    s_irq = false;
    size_t n = s_r->getPacketLength();
    if (n > cap) n = cap;
    int st = s_r->readData(buf, n);
    *rssi = s_r->getRSSI();
    *snr  = s_r->getSNR();
    s_r->startReceive();
    if (st != RADIOLIB_ERR_NONE || n == 0) return false;
    *len = n;
    return true;
}

#endif
