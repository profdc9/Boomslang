#pragma once
#include <Arduino.h>
#include "config.h"

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

  // Seconds between the arm switch (J5) being detected closed and firing
  // becoming permitted, so whoever closed it has time to clear the area.
  uint32_t armCountdownSec = 15;

  // Whether the onboard strobe LEDs / buzzer indicate armed state at all.
  // Both default on to match common range-safety practice.
  bool visibleWhenArmed = true;
  bool audibleWhenArmed = true;

  // If true, a TRIGGER press locks out any further TRIGGER press until the
  // arm switch is opened and closed again (a fresh disarm+rearm cycle,
  // countdown included). If false, another TRIGGER is accepted as soon as
  // the current sequence finishes, with no rearm needed.
  bool requireRearmAfterFire = true;

  // Per-channel delay (seconds) from a TRIGGER press to that channel's fire
  // pulse, for sequencing multiple channels off one trigger.
  float channelDelaySec[NUM_CHANNELS] = {0.0f, 0.0f, 0.0f};

  // While armed, continuously verify every selected channel still has
  // continuity; if not, block triggering (self-clears the instant it's
  // fixed, or the feature is turned off — no disarm needed for this one).
  bool checkContinuityOnArm = true;

  // Re-verify continuity for every selected channel at the instant TRIGGER
  // is pressed; if any fail, fire nothing and require a full disarm+rearm
  // before another TRIGGER is accepted, regardless of requireRearmAfterFire
  // (that setting is about the post-success case; this is separate).
  bool checkContinuityBeforeTrigger = true;
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
