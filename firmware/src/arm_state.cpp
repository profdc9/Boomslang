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

bool locked = false;      // post-fire lockout (requireRearmAfterFire)
bool contLocked = false;  // pre-trigger continuity-check-failed lockout

bool blinkOn = false;
uint32_t lastBlinkToggleMs = 0;

bool lowVBlocking = false;  // live: switch closed, but low-voltage lockout is holding DISARMED

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

  switch (state) {
    case ArmState::DISARMED:
      if (debouncedArmed) {
        if (settings.lowVoltageLockoutEnabled &&
            sampleBatteryVoltage() < settings.lowBatteryThresholdV) {
          // Switch is closed, but voltage is too low — hold in DISARMED.
          // Recomputed every call, so this clears itself the instant
          // voltage recovers (arming then proceeds next iteration) without
          // needing the switch to be re-opened and closed.
          lowVBlocking = true;
        } else {
          lowVBlocking = false;
          state = ArmState::COUNTDOWN;
          countdownEndsAtMs = now + settings.armCountdownSec * 1000UL;
          // Being here at all means the arm switch was open — that's the
          // "disarm" half of "disarm and rearm" satisfied, for both lockouts.
          locked = false;
          contLocked = false;
        }
      } else {
        lowVBlocking = false;
      }
      break;

    case ArmState::COUNTDOWN:
      if (!debouncedArmed) {
        state = ArmState::DISARMED;
      } else if ((int32_t)(now - countdownEndsAtMs) >= 0) {
        state = ArmState::READY;
      }
      break;

    case ArmState::READY:
      if (!debouncedArmed) {
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
  if (settings.audibleWhenArmed && blinkOn) {
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
