#pragma once
#include <ArduinoJson.h>

// Dodawanie noda przez USB (Web Serial) — opis protokołu w usb_provision.cpp.
void usb_cmd_register(JsonDocument& doc, const char* cmd);
void usb_cmd_round(JsonDocument& doc, const char* cmd);
void usb_cmd_sign(JsonDocument& doc, const char* cmd);
