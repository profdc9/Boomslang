#pragma once
#include <Arduino.h>

// Pin map, derived from Boomslang-schematic.pdf (DB1 = ESP32-S3-DevKitC-1).
//
// TRIGGER1/2/3 are ACTIVE LOW. Hardware pulls each trigger node toward the
// FAILSAFE arm rail through a 100k resistor, so a floating/undriven GPIO
// (reset, brownout, crash before setup() runs) reads as "not firing" as
// long as the physical arm key at J5 is open. Firmware must claim these
// pins and drive them HIGH before anything else, and must only ever pulse
// them LOW briefly to fire.
constexpr int NUM_CHANNELS = 3;
constexpr int PIN_TRIGGER[NUM_CHANNELS]    = { 18, 17, 16 };
constexpr int PIN_SENSE[NUM_CHANNELS]      = { 10, 9, 8 };  // firing-current shunt readback (0.05R)
constexpr int PIN_CONTINUITY[NUM_CHANNELS] = { 5, 6, 7 };   // igniter continuity readback (LED-clamped)

constexpr int PIN_SENSE_FAILSAFE = 4;  // arm-loop (J5) presence sense, same LED-clamp scheme
constexpr int PIN_FAULT          = 1;  // shared hardware overcurrent fault, active LOW, open-collector
constexpr int PIN_AUDIBLE        = 41; // buzzer driver (J6)
constexpr int PIN_VISIBLE        = 42; // onboard white/blue strobe LEDs
constexpr int PIN_BATTERY        = 2;  // 12V supply, through a 1:11 resistor divider

// ADC scaling: 12-bit, 3.3V reference, 11dB attenuation.
constexpr float ADC_VREF   = 3.3f;
constexpr int   ADC_MAX    = 4095;

// CONTINUITY and SENSE_FAILSAFE both work by pushing ~1mA through a red LED
// and reading the clamped voltage across it: that LED's forward voltage
// exceeds this threshold when the sensed loop (igniter or arm switch) is
// actually closed, and the node floats near 0V when open. Same circuit,
// same threshold, for both.
constexpr float LED_CLAMP_ARM_VOLTS = 1.25f;
constexpr int CONTINUITY_OK_RAW = (int)(LED_CLAMP_ARM_VOLTS / ADC_VREF * ADC_MAX);
constexpr int FAILSAFE_OK_RAW   = CONTINUITY_OK_RAW;

// Firing current sense: SENSE node voltage = I_fire * senseOhms[ch], direct
// (no divider) since the ADC input draws negligible current. The per-channel
// shunt value itself is a runtime-configurable setting (see settings.h), not
// a compile-time constant — it's field-replaceable hardware.

// Battery/supply voltage sense: PIN_BATTERY sees the 12V rail through a
// fixed 1:11 resistor divider (a PCB-fixed ratio, not field-replaceable
// like the current-sense shunts, so this is a compile-time constant).
constexpr float BATTERY_DIVIDER_RATIO = 11.0f;

// The battery rail passes through a series diode before reaching the
// divider, so the sensed voltage reads low by that diode's forward drop
// regardless of divider ratio. Added back after scaling in
// readBatteryVoltage() to recover the true battery voltage.
constexpr float BATTERY_DIODE_DROP_V = 0.7f;

// Debug timing instrumentation: toggles PIN_DEBUG_TIMING high as the first
// thing onFaultISR() does and low once faultSampleTask finishes reading all
// three SENSE channels. Put a scope or logic analyzer on this pin alongside
// FAULT to measure real end-to-end latency on actual hardware. Set to 0 for
// a field build — it's harmless left on (one extra register write in the
// ISR, negligible), but there's no reason to carry it once timing is known.
#define DEBUG_FAULT_TIMING 1
// GPIO13 rather than an ADC1 pin like GPIO2 — this signal is purely
// digital, so it's worth leaving ADC-capable header pins free for actual
// analog sensing. GPIO13 is only on ADC2 (which conflicts with WiFi and so
// isn't a pin you'd want for real analog use anyway), and is otherwise
// unconnected on the PCB.
constexpr int PIN_DEBUG_TIMING = 13;

// BENCH-DEBUG ONLY — normally onFaultISR() forces every TRIGGER pin high
// the instant FAULT asserts, and loop() calls stopSequence() on the
// transition into a latched fault, as a firmware-level echo of the
// per-channel hardware protection (Q5/Q10/Q16), which independently and
// unconditionally pulls a faulted channel's own MOSFET gate low in
// hardware regardless of this flag — that real transistor protection
// cannot be disabled from firmware at all. Setting this to 1 skips only
// firmware's redundant forced-HIGH/stopSequence() calls, so an in-progress
// pulse keeps running through a FAULT trip instead of being cut short —
// useful for diagnosing a suspected false trip (e.g. a switching
// transient) with no igniter/load connected. faultLatched is still set
// and still blocks new triggers until /clear_fault, same as always; only
// the forced shutoff of an already-running pulse is skipped. MUST be 0 for
// any real use with igniters/pyrotechnics connected.
#define DEBUG_DISABLE_FAULT_SHUTOFF 0

// Software leading-edge blanking: disables interrupts for a short,
// deterministic window around a *deliberate* trigger-pin write (start or
// end of a fire pulse), to filter the switching transient that was
// confirmed to be tripping FAULT with no load attached (the FAULT pin read
// high again immediately after, with DEBUG_DISABLE_FAULT_SHUTOFF on).
// Starting guess; tune with a scope on PIN_DEBUG_TIMING/FAULT once the
// real transient duration is known. 0 disables blanking (interrupts are
// still briefly disabled/re-enabled around the write, but with no added
// delay).
constexpr uint32_t FAULT_BLANKING_US = 30;

// Factory-reset pin: pulled up internally, checked once at boot (see
// setup()). Ground it (jumper/button to GND) before power-up to reset all
// settings to their compiled-in defaults. GPIO14 has no strapping or other
// special function on the ESP32-S3, and is otherwise unconnected on the PCB.
constexpr int PIN_FACTORY_RESET = 14;

// How long setup() waits for a station-mode WiFi connection (settings.
// wifiStationMode) before giving up and falling back to hosting its own AP
// instead — see setup(). Trigger pins are already safely HIGH long before
// this runs, so blocking here just delays when the web UI comes up, not
// anything safety-relevant.
constexpr uint32_t WIFI_STA_CONNECT_TIMEOUT_MS = 15000;
