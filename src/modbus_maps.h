#pragma once
// Presety list rejestrów (tylko przykłady do jednego kliknięcia — użytkownik może je dowolnie zmienić).
// Adresy dziesiętne. Deye/Sunsynk: kellerza/sunsynk definitions (single_phase.py, three_phase_common.py),
// zgodne z home_assistant_solarman deye_sg04lp3.yaml i protokołem Deye V119. Zebrane 2026-11-08.
// ent: natywna encja Sensmos bez prefiksu pub., "own.…" poza listą natywnych, "" = own.mb_<nazwa>,
// "grid_power" = moc sieci ze znakiem (→ grid_import/grid_export), "_pv" = składnik sumy pv_power.
// Znak baterii Deye (+ rozładowanie) odwrócony skalą −1 (Sensmos: + ładowanie) — do potwierdzenia na żywym urządzeniu.
// Typ: 0=u16 1=s16 2=u32 3=s32 4=f32 5=temp(surowe−1000).
// Błędy i ostrzeżenia falownika (dla alarmów panelu instalatora, 2026-11-11): słowa bitowe, ≠ 0 = jest zdarzenie.
// Fault 1-faz. 103–106, 3-faz. 555–558 (kellerza/sunsynk FaultSensor); blok „Alert” w solarman deye_hybrid
// 0x65–0x6A i deye_sg04lp3 0x229–0x22E = 2 słowa ostrzeżeń przed 4 słowami błędów. Para słów jako u32
// (młodsze pierwsze), publikowana jako dokładna liczba całkowita (modbus_rtu.cpp publish_bits).
// Nody z presetem zapisanym w 1.13 dostają te rejestry same przy starcie (extend_preset).

struct PresetReg { const char* name; const char* ent; uint16_t addr; uint8_t fc; uint8_t type; bool lo; float scale; const char* unit; };
struct Preset    { const char* id; const char* label; const PresetReg* regs; uint8_t n; };

static const PresetReg P_DEYE1[] = {
    { "pv1",        "_pv",          186, 3, 0, false,  1.0f,  "W"   },
    { "pv2",        "_pv",          187, 3, 0, false,  1.0f,  "W"   },
    { "grid",       "grid_power",   169, 3, 1, false,  1.0f,  "W"   },
    { "load",       "load_power",   178, 3, 1, false,  1.0f,  "W"   },
    { "batt",       "batt_power",   190, 3, 1, false, -1.0f,  "W"   },
    { "soc",        "batt_soc",     184, 3, 0, false,  1.0f,  "%"   },
    { "batt_v",     "batt_v",       183, 3, 0, false,  0.01f, "V"   },
    { "grid_v",     "grid_v",       150, 3, 0, false,  0.1f,  "V"   },
    { "freq",       "grid_freq",     79, 3, 0, false,  0.01f, "Hz"  },
    { "pv_today",   "pv_daily",     108, 3, 0, false,  0.1f,  "kWh" },
    { "inv_temp",   "",              91, 3, 5, false,  0.1f,  "°C"  },
    { "pv_total",   "",              96, 3, 2, true,   0.1f,  "kWh" },
    { "warn",       "own.inv_warn",  101, 3, 2, true,   1.0f,  ""    },
    { "fault",      "own.inv_fault", 103, 3, 2, true,   1.0f,  ""    },
    { "fault2",     "own.inv_fault2",105, 3, 2, true,   1.0f,  ""    },
};

static const PresetReg P_DEYE3[] = {
    { "pv1",        "_pv",          672, 3, 0, false,  1.0f,  "W"   },
    { "pv2",        "_pv",          673, 3, 0, false,  1.0f,  "W"   },
    { "grid",       "grid_power",   625, 3, 1, false,  1.0f,  "W"   },
    { "load",       "load_power",   653, 3, 1, false,  1.0f,  "W"   },
    { "batt",       "batt_power",   590, 3, 1, false, -1.0f,  "W"   },
    { "soc",        "batt_soc",     588, 3, 0, false,  1.0f,  "%"   },
    { "batt_v",     "batt_v",       587, 3, 0, false,  0.01f, "V"   },
    { "grid_v",     "grid_v",       598, 3, 0, false,  0.1f,  "V"   },
    { "freq",       "grid_freq",    609, 3, 0, false,  0.01f, "Hz"  },
    { "pv_today",   "pv_daily",     529, 3, 0, false,  0.1f,  "kWh" },
    { "inv_temp",   "",             541, 3, 5, false,  0.1f,  "°C"  },
    { "pv_total",   "",             534, 3, 2, true,   0.1f,  "kWh" },
    { "warn",       "own.inv_warn",  553, 3, 2, true,   1.0f,  ""    },
    { "fault",      "own.inv_fault", 555, 3, 2, true,   1.0f,  ""    },
    { "fault2",     "own.inv_fault2",557, 3, 2, true,   1.0f,  ""    },
};

// Licznik energii Eastron SDM120 (FC04, float32, starsze słowo pierwsze) — popularny w PV i ładowarkach EV.
static const PresetReg P_SDM[] = {
    { "voltage",    "grid_v",         0, 4, 4, false,  1.0f,  "V"   },
    { "current",    "",               6, 4, 4, false,  1.0f,  "A"   },
    { "power",      "grid_power",    12, 4, 4, false,  1.0f,  "W"   },
    { "freq",       "grid_freq",     70, 4, 4, false,  1.0f,  "Hz"  },
    { "import_kwh", "",              72, 4, 4, false,  1.0f,  "kWh" },
    { "export_kwh", "",              74, 4, 4, false,  1.0f,  "kWh" },
};

static const Preset PRESETS[] = {
    { "deye1", "Deye/Sunsynk 1-phase hybrid",    P_DEYE1, sizeof(P_DEYE1) / sizeof(P_DEYE1[0]) },
    { "deye3", "Deye/Sunsynk 3-phase LV hybrid", P_DEYE3, sizeof(P_DEYE3) / sizeof(P_DEYE3[0]) },
    { "sdm",   "Eastron SDM120 meter (1-phase)",    P_SDM,   sizeof(P_SDM)   / sizeof(P_SDM[0])   },
};
