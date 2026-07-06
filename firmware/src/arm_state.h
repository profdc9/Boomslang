#pragma once
#include <Arduino.h>

// The FAILSAFE arm loop (J5) is a hard mechanical switch that physically
// cuts power to the gate-driver stages — with no drive, a pull-down resistor
// holds every MOSFET gate off. That's a hardware fact, independent of this
// state machine. This module only senses that switch (via SENSEFAILSAFE) and
// layers a countdown + operator-facing indication + one-shot trigger lockout
// on top of it; it does not, and cannot, override the hardware.
enum class ArmState { DISARMED, COUNTDOWN, READY };

// Call every loop() iteration. Debounces SENSEFAILSAFE, drives the
// COUNTDOWN/READY transitions and the visible/audible indication pattern.
void updateArmState();

ArmState getArmState();

// 0 outside COUNTDOWN.
uint32_t countdownRemainingMs();

// Only meaningful when settings.requireRearmAfterFire is true. Cleared the
// moment the arm switch is (debounced) detected open.
bool triggerLockedOut();

// Call the instant a TRIGGER press is accepted, so a subsequent press can be
// refused per settings.requireRearmAfterFire.
void notifyTriggerAccepted();
