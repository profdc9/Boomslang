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

// ADC scaling: 12-bit, 3.3V reference, 11dB attenuation.
constexpr float ADC_VREF   = 3.3f;
constexpr int   ADC_MAX    = 4095;

// CONTINUITY and SENSE_FAILSAFE both work by pushing ~1mA through a red LED
// and reading the clamped voltage across it: that LED's forward voltage
// exceeds ~1.5V when the sensed loop (igniter or arm switch) is actually
// closed, and the node floats near 0V when open. Same circuit, same
// threshold, for both.
constexpr float LED_CLAMP_ARM_VOLTS = 1.5f;
constexpr int CONTINUITY_OK_RAW = (int)(LED_CLAMP_ARM_VOLTS / ADC_VREF * ADC_MAX);
constexpr int FAILSAFE_OK_RAW   = CONTINUITY_OK_RAW;

// Firing current sense: SENSE node voltage = I_fire * senseOhms[ch], direct
// (no divider) since the ADC input draws negligible current. The per-channel
// shunt value itself is a runtime-configurable setting (see settings.h), not
// a compile-time constant — it's field-replaceable hardware.

// A fire command pulses TRIGGER low for this long, then releases it HIGH
// regardless of what the software is doing — this bounds worst-case
// energy delivered to the igniter and battery drain if anything hangs.
constexpr uint32_t FIRE_PULSE_MS = 500;

// WiFi AP the board hosts for the control page. Change the password before
// using this at an actual field/range.
constexpr char AP_SSID[]     = "Boomslang";
constexpr char AP_PASSWORD[] = "firework123";

// Debug timing instrumentation: toggles PIN_DEBUG_TIMING high as the first
// thing onFaultISR() does and low once faultSampleTask finishes reading all
// three SENSE channels. Put a scope or logic analyzer on this pin alongside
// FAULT to measure real end-to-end latency on actual hardware. Set to 0 for
// a field build — it's harmless left on (one extra register write in the
// ISR, negligible), but there's no reason to carry it once timing is known.
#define DEBUG_FAULT_TIMING 1
constexpr int PIN_DEBUG_TIMING = 2;  // spare header pin, unconnected on the PCB
