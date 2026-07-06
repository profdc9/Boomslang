#include <Preferences.h>
#include "settings.h"
#include "arm_state.h"

Settings settings;

static const char *NVS_NAMESPACE = "boomslang";

static void keyFor(const char *base, int ch, char *out, size_t outLen) {
  snprintf(out, outLen, "%s%d", base, ch);
}

void resetSettingsToDefaults() {
  settings = Settings();
}

void loadSettings() {
  resetSettingsToDefaults();  // known-good starting point, then overlay anything stored

  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, /*readOnly=*/true)) {
    return;  // namespace doesn't exist yet (fresh board) — defaults stand
  }

  char key[16];
  for (int i = 0; i < NUM_CHANNELS; i++) {
    keyFor("senseOhm", i, key, sizeof(key));
    settings.senseOhms[i] = prefs.getFloat(key, settings.senseOhms[i]);
    keyFor("chDelay", i, key, sizeof(key));
    settings.channelDelaySec[i] = prefs.getFloat(key, settings.channelDelaySec[i]);
  }
  settings.armCountdownSec = prefs.getUInt("armCountdown", settings.armCountdownSec);
  settings.visibleWhenArmed = prefs.getBool("visibleArmed", settings.visibleWhenArmed);
  settings.audibleWhenArmed = prefs.getBool("audibleArmed", settings.audibleWhenArmed);
  settings.requireRearmAfterFire = prefs.getBool("reqRearm", settings.requireRearmAfterFire);
  settings.checkContinuityOnArm = prefs.getBool("contOnArm", settings.checkContinuityOnArm);
  settings.checkContinuityBeforeTrigger = prefs.getBool("contBeforeTrig", settings.checkContinuityBeforeTrigger);
  settings.lowBatteryThresholdV = prefs.getFloat("lowBattV", settings.lowBatteryThresholdV);
  settings.lowVoltageLockoutEnabled = prefs.getBool("lvLockout", settings.lowVoltageLockoutEnabled);
  prefs.end();
}

bool saveSettings() {
  // Both COUNTDOWN and READY mean the arm switch is physically closed and
  // the gate drivers are powered — flash writes briefly disable interrupts
  // system-wide, which shouldn't race the fault ISR while that's true.
  if (getArmState() != ArmState::DISARMED) return false;

  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, /*readOnly=*/false)) return false;

  char key[16];
  for (int i = 0; i < NUM_CHANNELS; i++) {
    keyFor("senseOhm", i, key, sizeof(key));
    prefs.putFloat(key, settings.senseOhms[i]);
    keyFor("chDelay", i, key, sizeof(key));
    prefs.putFloat(key, settings.channelDelaySec[i]);
  }
  prefs.putUInt("armCountdown", settings.armCountdownSec);
  prefs.putBool("visibleArmed", settings.visibleWhenArmed);
  prefs.putBool("audibleArmed", settings.audibleWhenArmed);
  prefs.putBool("reqRearm", settings.requireRearmAfterFire);
  prefs.putBool("contOnArm", settings.checkContinuityOnArm);
  prefs.putBool("contBeforeTrig", settings.checkContinuityBeforeTrigger);
  prefs.putFloat("lowBattV", settings.lowBatteryThresholdV);
  prefs.putBool("lvLockout", settings.lowVoltageLockoutEnabled);
  prefs.end();
  return true;
}
