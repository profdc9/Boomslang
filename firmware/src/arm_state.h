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

// Independent of triggerLockedOut()/requireRearmAfterFire: set when a
// pre-trigger continuity check fails, unconditionally requiring a fresh
// disarm+rearm before another TRIGGER is accepted. Cleared at the same
// point triggerLockedOut() is (the arm switch detected open).
bool continuityLockedOut();
void notifyContinuityCheckFailed();

// True when the arm switch is physically closed but settings.lowVoltage-
// LockoutEnabled and battery_v < settings.lowBatteryThresholdV are keeping
// the device in DISARMED rather than transitioning to COUNTDOWN — either
// because it never armed in the first place, or because voltage dropped
// below threshold while already in COUNTDOWN/READY and forced a disarm
// (same lockout, checked continuously in every state, not just at the
// initial arming transition). Live, not latched — recomputed every
// updateArmState() call, so it clears itself the instant voltage recovers
// (arming then proceeds normally) or the arm switch is opened.
bool lowVoltageBlockingArm();

// Tell the arm state machine whether a trigger sequence is currently
// running, so the audible tone can go continuous (same frequency as the
// current state, no on/off pulsing) as a distinct "firing in progress" cue
// while it's true. The strobe LED keeps its normal blink pattern either
// way. Call every loop() iteration, before updateArmState().
void setSequenceActive(bool active);

// Independent of triggerLockedOut()/continuityLockedOut(): set whenever the
// PANIC button is pressed, unconditionally requiring a fresh disarm+rearm
// before another TRIGGER is accepted, regardless of requireRearmAfterFire.
// Cleared at the same point the other lockouts are (arm switch detected
// open, then closed again).
bool panicLockedOut();
void notifyPanicPressed();
