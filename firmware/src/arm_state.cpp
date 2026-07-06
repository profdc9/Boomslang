#include "arm_state.h"
#include "config.h"
#include "settings.h"

namespace {

constexpr uint32_t DEBOUNCE_MS = 50;

// Countdown: lower tone, slower pulse. Ready: higher tone, faster pulse —
// audibly/visibly distinct per the "lower/slower vs higher/faster" request.
constexpr uint32_t COUNTDOWN_TONE_HZ  = 440;
constexpr uint32_t COUNTDOWN_BLINK_MS = 500;  // on/off half-period, ~1Hz
constexpr uint32_t READY_TONE_HZ      = 1000;
constexpr uint32_t READY_BLINK_MS     = 200;  // on/off half-period, ~2.5Hz

ArmState state = ArmState::DISARMED;
uint32_t countdownEndsAtMs = 0;

bool debouncedArmed = false;  // last debounce-committed reading
bool rawArmedPrev = false;    // last raw sample, for edge detection
uint32_t rawChangedAtMs = 0;

bool locked = false;       // post-fire lockout (requireRearmAfterFire)
bool contLocked = false;   // pre-trigger continuity-check-failed lockout
bool panicLocked = false;  // PANIC button pressed

bool blinkOn = false;
uint32_t lastBlinkToggleMs = 0;

bool lowVBlocking = false;  // live: switch closed, but low-voltage lockout is holding DISARMED

bool sequenceActiveFlag = false;  // set via setSequenceActive(), from main.cpp

// Firing draws real current through the same 12V rail battery_v senses, so
// a fire pulse can sag it momentarily — this debounce keeps that kind of
// sub-second transient from being mistaken for a real low battery. Longer
// than FIRE_PULSE_MS (500ms) with margin for a short multi-channel burst.
constexpr uint32_t LOW_VOLTAGE_DEBOUNCE_MS = 2000;

// Once locked out, voltage must recover this far past the threshold before
// the lockout clears — plain hysteresis, so a reading oscillating right at
// the threshold (ADC noise, supply ripple) doesn't flap in and out.
constexpr float LOW_VOLTAGE_HYSTERESIS_V = 0.3f;

bool lowVRawPrev = false;      // last raw (pre-debounce) comparison result
uint32_t lowVChangedAtMs = 0;
bool lowVDebounced = false;    // debounced, hysteresis-applied lockout condition

bool sampleRawArmed() {
  return analogRead(PIN_SENSE_FAILSAFE) > FAILSAFE_OK_RAW;
}

// Independent read of PIN_BATTERY, mirroring main.cpp's readBatteryVoltage()
// — this module already senses SENSEFAILSAFE on its own rather than relying
// on main.cpp's rolling variables, so this follows the same self-contained
// pattern.
float sampleBatteryVoltage() {
  return (analogRead(PIN_BATTERY) / (float)ADC_MAX) * ADC_VREF * BATTERY_DIVIDER_RATIO;
}

// Call once per updateArmState() — debounces and applies hysteresis to the
// raw battery_v-vs-threshold comparison, so a fire-pulse sag or boundary
// noise doesn't flap the lockout the way a single instantaneous sample
// would. Same debounce shape as sampleRawArmed()/debouncedArmed above.
bool computeLowVoltageLockout(uint32_t now) {
  if (!settings.lowVoltageLockoutEnabled) {
    lowVRawPrev = false;
    lowVDebounced = false;
    return false;
  }

  float v = sampleBatteryVoltage();
  // While already locked out, require clearing threshold + hysteresis
  // before the raw reading counts as "recovered".
  bool raw = lowVDebounced ? (v < settings.lowBatteryThresholdV + LOW_VOLTAGE_HYSTERESIS_V)
                           : (v < settings.lowBatteryThresholdV);

  if (raw != lowVRawPrev) {
    lowVChangedAtMs = now;
    lowVRawPrev = raw;
  }
  if (raw != lowVDebounced && (now - lowVChangedAtMs) >= LOW_VOLTAGE_DEBOUNCE_MS) {
    lowVDebounced = raw;
  }
  return lowVDebounced;
}

}  // namespace

void updateArmState() {
  uint32_t now = millis();

  bool rawArmed = sampleRawArmed();
  if (rawArmed != rawArmedPrev) {
    rawChangedAtMs = now;
    rawArmedPrev = rawArmed;
  }
  if (rawArmed != debouncedArmed && (now - rawChangedAtMs) >= DEBOUNCE_MS) {
    debouncedArmed = rawArmed;
  }

  bool lowV = computeLowVoltageLockout(now);

  switch (state) {
    case ArmState::DISARMED:
      if (debouncedArmed) {
        if (lowV) {
          // Switch is closed, but voltage is too low (debounced/hysteresis
          // applied) — hold in DISARMED. Recomputed every call, so this
          // clears itself once voltage recovers (arming then proceeds next
          // iteration) without needing the switch to be re-opened and
          // closed.
          lowVBlocking = true;
        } else {
          lowVBlocking = false;
          state = ArmState::COUNTDOWN;
          countdownEndsAtMs = now + settings.armCountdownSec * 1000UL;
          // Being here at all means the arm switch was open — that's the
          // "disarm" half of "disarm and rearm" satisfied, for all three
          // lockouts.
          locked = false;
          contLocked = false;
          panicLocked = false;
        }
      } else {
        lowVBlocking = false;
      }
      break;

    case ArmState::COUNTDOWN:
      // Voltage dropping below threshold (debounced) mid-countdown forces a
      // disarm, same as the switch opening — if the switch is still closed
      // and voltage is still low, the DISARMED case above picks that up as
      // lowVBlocking on the very next iteration.
      if (!debouncedArmed || lowV) {
        state = ArmState::DISARMED;
      } else if ((int32_t)(now - countdownEndsAtMs) >= 0) {
        state = ArmState::READY;
      }
      break;

    case ArmState::READY:
      if (!debouncedArmed || lowV) {
        state = ArmState::DISARMED;
      }
      break;
  }

  // Visible/audible indication — nothing while DISARMED, regardless of
  // settings; there's nothing to warn about when the loop is open.
  if (state == ArmState::DISARMED) {
    noTone(PIN_AUDIBLE);
    digitalWrite(PIN_VISIBLE, LOW);
    return;
  }

  uint32_t toneHz = (state == ArmState::COUNTDOWN) ? COUNTDOWN_TONE_HZ : READY_TONE_HZ;
  uint32_t halfPeriodMs = (state == ArmState::COUNTDOWN) ? COUNTDOWN_BLINK_MS : READY_BLINK_MS;

  if (now - lastBlinkToggleMs >= halfPeriodMs) {
    lastBlinkToggleMs = now;
    blinkOn = !blinkOn;
  }

  digitalWrite(PIN_VISIBLE, (settings.visibleWhenArmed && blinkOn) ? HIGH : LOW);

  // While a trigger sequence is running, the tone goes continuous — same
  // frequency as the current state, no on/off pulsing — as a distinct
  // "firing in progress" cue. The strobe above keeps its normal blink
  // pattern regardless.
  bool audibleOn = sequenceActiveFlag ? true : blinkOn;
  if (settings.audibleWhenArmed && audibleOn) {
    tone(PIN_AUDIBLE, toneHz);
  } else {
    noTone(PIN_AUDIBLE);
  }
}

ArmState getArmState() { return state; }

uint32_t countdownRemainingMs() {
  if (state != ArmState::COUNTDOWN) return 0;
  uint32_t now = millis();
  if ((int32_t)(countdownEndsAtMs - now) <= 0) return 0;
  return countdownEndsAtMs - now;
}

bool triggerLockedOut() { return locked; }

void notifyTriggerAccepted() {
  if (settings.requireRearmAfterFire) locked = true;
}

bool continuityLockedOut() { return contLocked; }

void notifyContinuityCheckFailed() { contLocked = true; }

bool lowVoltageBlockingArm() { return lowVBlocking; }

void setSequenceActive(bool active) { sequenceActiveFlag = active; }

bool panicLockedOut() { return panicLocked; }

void notifyPanicPressed() { panicLocked = true; }
