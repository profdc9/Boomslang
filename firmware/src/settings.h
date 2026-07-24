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

  // Seconds allowed in READY before a fresh disarm+rearm is required, even
  // if the arm switch (J5) is still physically closed. 0 disables this
  // (READY can persist indefinitely, as before). This is a software-only
  // lockout on top of the hardware switch — it cannot re-power the gate
  // drivers on its own, and it cannot cut them either; it only blocks
  // TRIGGER, the same way panicLockedOut() does, until the switch is
  // observed open then closed again. Guards against "left armed and walked
  // away" rather than any electrical fault.
  uint32_t armTimeoutSec = 600;

  // Whether the onboard strobe LEDs / buzzer indicate armed state at all.
  // Both default on to match common range-safety practice.
  bool visibleWhenArmed = true;
  bool audibleWhenArmed = true;

  // Buzzer volume, 0-10, mapped linearly to 0-50% PWM duty cycle (50% is as
  // loud as a square-wave drive gets — maximum RMS). Default 10 (50% duty)
  // matches the fixed duty tone()/ledcWriteTone() always used before this
  // was configurable, so upgrading a board doesn't change its loudness.
  uint32_t speakerVolume = 10;

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

  // Per-channel PWM cycling during that duration, for a lower average
  // heating rate than a full-on pulse (e.g. slow-heating igniter/nichrome
  // element rather than an instant e-match). 0 disables PWM entirely —
  // the channel is simply on for the whole duration, as before. Valid
  // range 1-300Hz — this is a real, measured ceiling, not just "doesn't
  // need to be precise": the toggle logic in loop() can't act faster than
  // loop() itself iterates (bench-measured at ~3kHz on this build after
  // WebServer::enableDelay(false); see main.cpp setup()), so requesting a
  // period comparable to one loop() iteration collapses duty accuracy
  // (1000Hz measured converging toward ~50% regardless of configured
  // duty). 300Hz keeps duty tracking usable down to ~10% duty. Default
  // 10Hz so a sensible frequency is already set if duty is later lowered
  // below 100% (see channelPwmDutyPercent).
  uint32_t channelPwmHz[NUM_CHANNELS] = {10, 10, 10};

  // Duty cycle (percent, 0-100) while channelPwmHz[ch] > 0. Default 100%
  // (and 100% at any other time) means continuously on for the whole
  // duration — no different from PWM being disabled — so a fresh board
  // fires exactly as before until duty is deliberately lowered.
  uint32_t channelPwmDutyPercent[NUM_CHANNELS] = {100, 100, 100};

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
