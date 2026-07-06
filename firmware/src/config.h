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

// CONTINUITY and SENSE_FAILSAFE both read a node clamped by an LED forward
// drop (~1.8-2.2V) when the sensed loop is intact, and float near 0V when
// open. The threshold sits well below the clamp voltage and well above
// ADC noise / leakage on an open loop.
constexpr int CONTINUITY_OK_RAW   = 600;  // ~0.48V
constexpr int FAILSAFE_OK_RAW     = 600;

// Firing current sense: SENSE node voltage = I_fire * SENSE_OHMS, direct
// (no divider) since the ADC input draws negligible current.
constexpr float SENSE_OHMS = 0.05f;

// A fire command pulses TRIGGER low for this long, then releases it HIGH
// regardless of what the software is doing — this bounds worst-case
// energy delivered to the igniter and battery drain if anything hangs.
constexpr uint32_t FIRE_PULSE_MS = 500;

// WiFi AP the board hosts for the control page. Change the password before
// using this at an actual field/range.
constexpr char AP_SSID[]     = "Boomslang";
constexpr char AP_PASSWORD[] = "firework123";
