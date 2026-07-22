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
    settings.channelDelayMs[i] = prefs.getUInt(key, settings.channelDelayMs[i]);
    keyFor("chDur", i, key, sizeof(key));
    settings.channelDurationMs[i] = prefs.getUInt(key, settings.channelDurationMs[i]);
  }
  settings.armCountdownSec = prefs.getUInt("armCountdown", settings.armCountdownSec);
  settings.armTimeoutSec = prefs.getUInt("armTimeout", settings.armTimeoutSec);
  settings.visibleWhenArmed = prefs.getBool("visibleArmed", settings.visibleWhenArmed);
  settings.audibleWhenArmed = prefs.getBool("audibleArmed", settings.audibleWhenArmed);
  settings.speakerVolume = prefs.getUInt("spkVolume", settings.speakerVolume);
  settings.requireRearmAfterFire = prefs.getBool("reqRearm", settings.requireRearmAfterFire);
  settings.checkContinuityOnArm = prefs.getBool("contOnArm", settings.checkContinuityOnArm);
  settings.checkContinuityBeforeTrigger = prefs.getBool("contBeforeTrig", settings.checkContinuityBeforeTrigger);
  settings.lowBatteryThresholdV = prefs.getFloat("lowBattV", settings.lowBatteryThresholdV);
  settings.lowVoltageLockoutEnabled = prefs.getBool("lvLockout", settings.lowVoltageLockoutEnabled);

  // String-returning overload (not the char-buffer one) specifically so the
  // "key not found" fallback is settings.wifiSsid/wifiPassword's current
  // value (the compiled-in default, at this point) — same technique used
  // for every other field above.
  prefs.getString("wifiSsid", settings.wifiSsid).toCharArray(settings.wifiSsid, sizeof(settings.wifiSsid));
  prefs.getString("wifiPass", settings.wifiPassword).toCharArray(settings.wifiPassword, sizeof(settings.wifiPassword));
  settings.wifiStationMode = prefs.getBool("wifiStaMode", settings.wifiStationMode);
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
    prefs.putUInt(key, settings.channelDelayMs[i]);
    keyFor("chDur", i, key, sizeof(key));
    prefs.putUInt(key, settings.channelDurationMs[i]);
  }
  prefs.putUInt("armCountdown", settings.armCountdownSec);
  prefs.putUInt("armTimeout", settings.armTimeoutSec);
  prefs.putBool("visibleArmed", settings.visibleWhenArmed);
  prefs.putBool("audibleArmed", settings.audibleWhenArmed);
  prefs.putUInt("spkVolume", settings.speakerVolume);
  prefs.putBool("reqRearm", settings.requireRearmAfterFire);
  prefs.putBool("contOnArm", settings.checkContinuityOnArm);
  prefs.putBool("contBeforeTrig", settings.checkContinuityBeforeTrigger);
  prefs.putFloat("lowBattV", settings.lowBatteryThresholdV);
  prefs.putBool("lvLockout", settings.lowVoltageLockoutEnabled);
  prefs.putString("wifiSsid", settings.wifiSsid);
  prefs.putString("wifiPass", settings.wifiPassword);
  prefs.putBool("wifiStaMode", settings.wifiStationMode);
  prefs.end();
  return true;
}
