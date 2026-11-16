// Tablica ogłoszeń osiedla: opis w kom_board.h. Czysta logika bez Arduino, więc da się ją sprawdzić g++ na PC.
#include "kom_config.h"
#if SENSMOS_KOM
#include "kom_board.h"
#include <string.h>

struct BoardItem {
    uint32_t at;                  // pierwsze usłyszenie (millis)
    uint8_t  n;                   // 0 = pusty slot
    uint8_t  heard;               // ile razy w tym okresie ktoś to nadał (autor albo inna tablica)
    uint8_t  f[KOM_BOARD_FMAX];
};

static BoardItem s_it[KOM_BOARD_N];
static bool      s_on = false;
static uint32_t  s_t0 = 0, s_fire = 0;
static bool      s_fired = false;

static bool alive(const BoardItem& it, uint32_t now) { return it.n && now - it.at < KOM_BOARD_TTL_MS; }

static void new_interval(uint32_t now, uint32_t rnd) {
    s_t0 = now;
    s_fire = now + KOM_BOARD_I_MS / 2 + rnd % (KOM_BOARD_I_MS / 2);
    s_fired = false;
    for (auto& it : s_it) it.heard = 0;
}

void kom_board_set(bool on, uint32_t now, uint32_t rnd) {
    s_on = on;
    if (on) new_interval(now, rnd);
}

bool kom_board_on() { return s_on; }

// Ta sama ramka = ten sam nadawca i licznik (bajty 7..14 nagłówka 0xE0 0x04), jak seen_before w kom_main.
static int find(const uint8_t* f, size_t n) {
    for (int i = 0; i < KOM_BOARD_N; i++)
        if (s_it[i].n == n && !memcmp(s_it[i].f + 7, f + 7, 8)) return i;
    return -1;
}

bool kom_board_offer(const uint8_t* f, size_t n, uint32_t now) {
    if (!s_on || n < 19 || n > KOM_BOARD_FMAX || f[0] != 0xE0 || f[1] != 0x04 || (f[2] & 3) != 1) return false;
    int i = find(f, n);
    if (i >= 0) { if (s_it[i].heard < 255) s_it[i].heard++; return false; }
    int slot = 0;                                                    // pusty, przeterminowany albo najstarszy
    for (int k = 0; k < KOM_BOARD_N; k++) {
        if (!alive(s_it[k], now)) { slot = k; break; }
        if (now - s_it[k].at > now - s_it[slot].at) slot = k;
    }
    BoardItem& it = s_it[slot];
    memcpy(it.f, f, n); it.n = (uint8_t)n; it.at = now; it.heard = 1;   // samo usłyszenie to już nadanie w okolicy: powtórka od następnego okresu
    return true;
}

uint8_t kom_board_tick(uint32_t now, uint32_t rnd, KomBoardSend send) {
    if (!s_on) return 0;
    if (now - s_t0 >= KOM_BOARD_I_MS) new_interval(now, rnd);
    if (s_fired || (int32_t)(now - s_fire) < 0) return 0;
    s_fired = true;
    uint8_t sent = 0;
    for (auto& it : s_it) {
        if (!alive(it, now)) { it.n = 0; continue; }
        if (it.heard >= KOM_BOARD_K) continue;                       // okolica już to słyszała w tym okresie
        if (send(it.f, it.n) >= 0) sent++;
    }
    return sent;
}

uint8_t kom_board_count(uint32_t now) {
    uint8_t c = 0;
    for (auto& it : s_it) if (alive(it, now)) c++;
    return c;
}

void kom_board_clear() { for (auto& it : s_it) it.n = 0; }

#endif
