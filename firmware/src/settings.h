#pragma once
#include <Arduino.h>
#include "config.h"

// Defined in main.cpp. saveSettings() refuses to write to flash while armed,
// since NVS commits briefly disable interrupts system-wide, and that's not
// something we want racing the fault ISR while a channel could plausibly be
// live.
bool isArmed();

// Runtime-configurable values, persisted to flash (NVS) so they survive
// power cycles. Each field's default here is what a fresh/blank board (or a
// firmware update that adds a new field) falls back to. Add new fields here
// as more settings are needed — loadSettings()/saveSettings() follow the
// same per-field pattern for each one.
struct Settings {
  // Per-channel current-sense shunt resistance, ohms. Schematic default is
  // 0.05R (R1/R17/R31) on all three channels, but this is field-replaceable
  // hardware, so it's runtime-configurable rather than a compile-time
  // constant. Used to convert a SENSE ADC reading to amps.
  float senseOhms[NUM_CHANNELS] = {0.05f, 0.05f, 0.05f};
};

extern Settings settings;

// Loads from NVS into the global `settings`, falling back to each field's
// compiled-in default for any key not yet present (first boot ever, or a
// firmware update that added a new setting since this board was last saved).
void loadSettings();

// Persists the current in-memory `settings` to NVS. Returns false and writes
// nothing if the system is currently armed.
bool saveSettings();

// Resets the in-memory struct to defaults. Does not touch flash — call
// saveSettings() afterward to persist the reset.
void resetSettingsToDefaults();
