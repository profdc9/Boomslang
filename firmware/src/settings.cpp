#include <Preferences.h>
#include "settings.h"

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
  }
  prefs.end();
}

bool saveSettings() {
  if (isArmed()) return false;

  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, /*readOnly=*/false)) return false;

  char key[16];
  for (int i = 0; i < NUM_CHANNELS; i++) {
    keyFor("senseOhm", i, key, sizeof(key));
    prefs.putFloat(key, settings.senseOhms[i]);
  }
  prefs.end();
  return true;
}
