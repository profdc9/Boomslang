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

  // Per-channel offset (milliseconds, valid range 0-60000) from a TRIGGER
  // press to that channel's trigger output going low, for sequencing
  // multiple channels off one press.
  uint32_t channelDelayMs[NUM_CHANNELS] = {0, 0, 0};

  // Per-channel duration (milliseconds, valid range 0-30000) the trigger
  // output stays low once it fires.
  uint32_t channelDurationMs[NUM_CHANNELS] = {500, 500, 500};

  // While armed, continuously verify every selected channel still has
  // continuity; if not, block triggering (self-clears the instant it's
  // fixed, or the feature is turned off — no disarm needed for this one).
  bool checkContinuityOnArm = true;

  // Re-verify continuity for every selected channel at the instant TRIGGER
  // is pressed; if any fail, fire nothing and require a full disarm+rearm
  // before another TRIGGER is accepted, regardless of requireRearmAfterFire
  // (that setting is about the post-success case; this is separate).
  bool checkContinuityBeforeTrigger = true;

  // Below this, the UI shows a low-battery warning, and — if
  // lowVoltageLockoutEnabled — arming is blocked (the arm switch closing
  // doesn't transition out of DISARMED while voltage is under threshold).
  float lowBatteryThresholdV = 11.5f;

  // If true, the arm switch closing while battery_v < lowBatteryThresholdV
  // does not arm the device — it stays DISARMED until voltage recovers.
  // Independent of the warning display, so it can be turned off for bench
  // testing on a supply that's intentionally below threshold.
  bool lowVoltageLockoutEnabled = true;

  // WiFi AP the board hosts for the control page. This password is the
  // device's only access control — anyone who has it can arm and fire — so
  // treat it accordingly. Fixed-size buffers (not String) to keep NVS
  // load/save simple and avoid heap fragmentation. Changing either only
  // takes effect after a reboot (WiFi.softAP() isn't re-called at runtime),
  // so the settings page needs to say so.
  char wifiSsid[33] = "boomslang";      // max 32 chars, WiFi SSID limit
  char wifiPassword[64] = "liftoff!";   // 0 (open network) or 8-63 chars, WPA2-PSK limit

  // If true, join wifiSsid/wifiPassword as a station (client) instead of
  // hosting them as this device's own AP — for relaying through an existing
  // router when operator and pyrotechnics need more distance than the
  // device's own AP range allows. Default off. Deliberately not surfaced as
  // a plain checkbox in the UI (see config_webpage.h) — anyone who can
  // reach that external network can also reach this device, so enabling it
  // needs to be a deliberate act, not an accidental tap.
  bool wifiStationMode = false;
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
