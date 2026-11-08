#pragma once
// Mapy rejestrów falowników (FC03, adresy dziesiętne). Źródła: kellerza/sunsynk definitions (single_phase.py,
// three_phase_common.py, three_phase_lv.py), zgodne z home_assistant_solarman deye_sg04lp3.yaml i protokołem
// Deye V119 (znak sieci: + pobór, − oddawanie). Zebrane 2026-11-08.
// entity: natywna encja Sensmos (bez prefiksu pub.), "own.…" dla wartości spoza listy natywnych,
// "grid_power" = moc sieci ze znakiem (rozbijana na grid_import/grid_export), "_pv" = składnik sumy pv_power.
// Uwaga: rejestr 54 (1-faz.) = 1 oznacza moce sieci w jednostkach 10 W — rzadkie, nieobsłużone w wersji 1.
// Znak baterii (+ rozładowanie u Deye) odwrócony skalą −1, bo w Sensmos batt_power: + ładowanie / − rozładowanie;
// do potwierdzenia na żywym urządzeniu u pierwszego testera.

static const RegDef DEYE1[] = {
    { "_pv",          186, R_U16,  1.0f,   "W"   },   // PV1
    { "_pv",          187, R_U16,  1.0f,   "W"   },   // PV2
    { "grid_power",   169, R_S16,  1.0f,   "W"   },
    { "load_power",   178, R_S16,  1.0f,   "W"   },
    { "batt_power",   190, R_S16, -1.0f,   "W"   },
    { "batt_soc",     184, R_U16,  1.0f,   "%"   },
    { "batt_v",       183, R_U16,  0.01f,  "V"   },
    { "grid_v",       150, R_U16,  0.1f,   "V"   },
    { "grid_freq",     79, R_U16,  0.01f,  "Hz"  },
    { "pv_daily",     108, R_U16,  0.1f,   "kWh" },
    { "own.inv_temp",  91, R_TEMP, 0.1f,   "°C"  },   // radiator; surowe − 1000
    { "own.pv_total",  96, R_U32,  0.1f,   "kWh" },
};

static const RegDef DEYE3[] = {
    { "_pv",          672, R_U16,  1.0f,   "W"   },
    { "_pv",          673, R_U16,  1.0f,   "W"   },
    { "grid_power",   625, R_S16,  1.0f,   "W"   },
    { "load_power",   653, R_S16,  1.0f,   "W"   },
    { "batt_power",   590, R_S16, -1.0f,   "W"   },
    { "batt_soc",     588, R_U16,  1.0f,   "%"   },
    { "batt_v",       587, R_U16,  0.01f,  "V"   },
    { "grid_v",       598, R_U16,  0.1f,   "V"   },
    { "grid_freq",    609, R_U16,  0.01f,  "Hz"  },
    { "pv_daily",     529, R_U16,  0.1f,   "kWh" },
    { "own.inv_temp", 541, R_TEMP, 0.1f,   "°C"  },
    { "own.pv_total", 534, R_U32,  0.1f,   "kWh" },
};

static const Model MODELS[] = {
    { "deye1", "Deye/Sunsynk 1-phase hybrid (SG0xLP1)",    DEYE1, sizeof(DEYE1) / sizeof(DEYE1[0]) },
    { "deye3", "Deye/Sunsynk 3-phase LV hybrid (SG04LP3)", DEYE3, sizeof(DEYE3) / sizeof(DEYE3[0]) },
};
