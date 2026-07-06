#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/gpio_reg.h"
#include "soc/soc.h"
#include "config.h"
#include "settings.h"
#include "arm_state.h"
#include "webpage.h"
#include "config_webpage.h"
#include "timing_webpage.h"
#include "stats_webpage.h"

WebServer server(80);

struct ChannelState {
  bool fireActive = false;
  uint32_t fireEndsAt = 0;
  float lastCurrentA = 0.0f;

  // Set by handleTrigger() when this channel is part of the current
  // sequence; cleared by loop()'s scheduling pass once its delay elapses
  // and it's either fired or skipped.
  bool scheduled = false;
  uint32_t fireAtMs = 0;

  // Whether this channel is included in the next TRIGGER. RAM only, not
  // persisted (must be an active, current choice) — set via /select, and
  // (per settings.checkContinuityOnArm) locked while armed so the set of
  // channels the live continuity monitor is watching can't shift underneath
  // it. Not cleared automatically after a sequence or a disarm; the
  // confirm() dialog before firing shows exactly what's selected either way.
  bool selected = false;

  // Peak and average current for this channel's most recent fire pulse.
  // Reset at the start of each pulse (startFirePulse()), sampled every
  // loop() iteration while fireActive, and then simply held — so they
  // remain readable after the pulse (and the whole sequence) ends, until
  // the channel fires again.
  float peakCurrentA = 0.0f;
  float avgCurrentA = 0.0f;
  float currentSumA = 0.0f;      // accumulator behind avgCurrentA
  uint32_t currentSampleCount = 0;
};
ChannelState channels[NUM_CHANNELS];

// Live (non-latching) result of the arm-time continuity monitor: true if
// settings.checkContinuityOnArm is on, the device is armed, and at least
// one selected channel currently lacks continuity. Recomputed every loop()
// iteration, so it clears itself the instant the problem is fixed.
bool armContinuityError = false;

// Rolling battery/supply voltage reading, refreshed each loop() iteration.
float batteryVoltage = 0.0f;

// True from an accepted /trigger until every scheduled channel has either
// fired (and its pulse ended) or been skipped. Blocks a second /trigger
// regardless of settings.requireRearmAfterFire — you can never have two
// sequences in flight at once.
bool sequenceActive = false;

// Set by onFaultISR() the instant the shared FAULT line drops, and by a
// defensive poll in loop() as a backstop. Only cleared by handleClearFault()
// after re-checking the hardware line is actually clear.
volatile bool faultLatched = false;
bool faultLatchedPrev = false;       // loop()-only, for edge detection

// Written only by faultSampleTask, read by loop()/web handlers for display.
// Not lock-protected: this is best-effort diagnostic telemetry, not part of
// the safety path, so a rare torn read showing a stale/half-updated value is
// an acceptable tradeoff against the complexity of synchronizing it.
volatile bool faultSnapshotReady = false;
uint32_t faultSnapshotAtMs = 0;
float faultSnapshotA[NUM_CHANNELS] = {0, 0, 0};

TaskHandle_t faultSampleTaskHandle = nullptr;

// GPIO16/17/18 are all < 32, so a single write-1-to-set register write
// forces all three trigger outputs HIGH atomically.
constexpr uint32_t TRIGGER_PIN_MASK =
    (1u << PIN_TRIGGER[0]) | (1u << PIN_TRIGGER[1]) | (1u << PIN_TRIGGER[2]);

// Runs at interrupt level, IRAM-resident: no Serial, no heap/String, no
// calls into anything not IRAM-safe. In particular, analogRead() is NOT
// callable here — the ESP-IDF ADC driver takes a FreeRTOS mutex internally,
// and loop() is already calling it every iteration to refresh the current
// readings, so this ISR could easily fire while that lock is held elsewhere.
// Taking a lock from ISR context that way is undefined behavior.
//
// So: the two time-critical jobs (force every trigger off, latch the fault)
// happen right here, register-level. Sampling SENSE for diagnostics is
// handed off to a dedicated high-priority task via a notification — that
// task runs in normal FreeRTOS task context, where analogRead() is legal,
// and portYIELD_FROM_ISR ensures the scheduler switches to it the instant
// this ISR returns, ahead of loop() or anything else of lower priority.
void IRAM_ATTR onFaultISR() {
#if DEBUG_FAULT_TIMING
  // Marks ISR entry, essentially coincident with the FAULT edge itself —
  // scope this pin against FAULT to read off interrupt-latency directly.
  REG_WRITE(GPIO_OUT_W1TS_REG, 1u << PIN_DEBUG_TIMING);
#endif

  REG_WRITE(GPIO_OUT_W1TS_REG, TRIGGER_PIN_MASK);
  faultLatched = true;

  BaseType_t higherPriorityTaskWoken = pdFALSE;
  vTaskNotifyGiveFromISR(faultSampleTaskHandle, &higherPriorityTaskWoken);
  portYIELD_FROM_ISR(higherPriorityTaskWoken);
}

float readCurrentA(int ch) {
  int raw = analogRead(PIN_SENSE[ch]);
  float v = (raw / (float)ADC_MAX) * ADC_VREF;
  return v / settings.senseOhms[ch];
}

// Blocked on a task notification the rest of the time; woken by onFaultISR()
// to sample all three current-sense channels as close to the fault event as
// this platform can safely get. This is a diagnostic best-effort capture,
// not a safety mechanism — the trigger shutoff already happened in the ISR
// before this task even gets scheduled, and the true peak current may still
// have decayed somewhat by the time this runs, depending on how fast the
// hardware's own analog protection (Q5/Q10/Q16) and the igniter loop's
// inductance let it fall.
void faultSampleTask(void *) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    for (int i = 0; i < NUM_CHANNELS; i++) {
      faultSnapshotA[i] = readCurrentA(i);
    }
    faultSnapshotAtMs = millis();
    faultSnapshotReady = true;
#if DEBUG_FAULT_TIMING
    // Falling edge marks "all three SENSE channels sampled" — the gap
    // between this and the onFaultISR rising edge is the real number to
    // measure once there's hardware to scope it on.
    digitalWrite(PIN_DEBUG_TIMING, LOW);
#endif
  }
}

// Every other JSON field emitted below is a number or a bool computed by
// firmware; SSID/password are the one piece of free-form user text in
// /status.json and /config.json, so they're the one place a literal `"` or
// `\` in the input could otherwise break the JSON output.
void appendJsonString(String &out, const char *s) {
  out += '"';
  for (const char *p = s; *p; p++) {
    if (*p == '"' || *p == '\\') out += '\\';
    out += *p;
  }
  out += '"';
}

bool hasContinuity(int ch) {
  return analogRead(PIN_CONTINUITY[ch]) > CONTINUITY_OK_RAW;
}

float readBatteryVoltage() {
  int raw = analogRead(PIN_BATTERY);
  float v = (raw / (float)ADC_MAX) * ADC_VREF;
  return v * BATTERY_DIVIDER_RATIO;
}

void startFirePulse(int ch) {
  channels[ch].fireActive = true;
  channels[ch].fireEndsAt = millis() + settings.channelDurationMs[ch];
  channels[ch].peakCurrentA = 0.0f;
  channels[ch].avgCurrentA = 0.0f;
  channels[ch].currentSumA = 0.0f;
  channels[ch].currentSampleCount = 0;
  digitalWrite(PIN_TRIGGER[ch], LOW);
  Serial.printf("[fire] channel %d fired for %lu ms\n", ch + 1, (unsigned long)settings.channelDurationMs[ch]);
}

// Immediately stops any channel mid-pulse (forces its trigger back HIGH
// right away, rather than waiting out the rest of its configured duration)
// and cancels anything still scheduled. Used by ABORT, PANIC, and fault
// handling — none of them wait for the normal pulse timeout in loop().
void stopSequence() {
  for (int i = 0; i < NUM_CHANNELS; i++) {
    if (channels[i].fireActive) {
      digitalWrite(PIN_TRIGGER[i], HIGH);
      channels[i].fireActive = false;
    }
    channels[i].scheduled = false;
  }
  sequenceActive = false;
}

void buildStatusJson(String &out) {
  ArmState st = getArmState();
  const char *stateStr = st == ArmState::DISARMED   ? "disarmed"
                          : st == ArmState::COUNTDOWN ? "countdown"
                                                       : "ready";

  out = "{";
  out += "\"arm_state\":\"";
  out += stateStr;
  out += "\",\"countdown_remaining_s\":";
  out += String(countdownRemainingMs() / 1000.0f, 1);
  out += ",\"trigger_locked\":";
  out += (settings.requireRearmAfterFire && triggerLockedOut()) ? "true" : "false";
  out += ",\"continuity_locked\":";
  out += (continuityLockedOut() ? "true" : "false");
  out += ",\"panic_locked\":";
  out += (panicLockedOut() ? "true" : "false");
  out += ",\"arm_continuity_error\":";
  out += (armContinuityError ? "true" : "false");
  out += ",\"selection_locked\":";
  out += (settings.checkContinuityOnArm && st != ArmState::DISARMED) ? "true" : "false";
  out += ",\"sequence_active\":";
  out += (sequenceActive ? "true" : "false");
  out += ",\"battery_v\":";
  out += String(batteryVoltage, 2);
  out += ",\"low_battery\":";
  out += (batteryVoltage < settings.lowBatteryThresholdV ? "true" : "false");
  out += ",\"low_voltage_blocking_arm\":";
  out += (lowVoltageBlockingArm() ? "true" : "false");
  out += ",\"fault\":";
  out += (faultLatched ? "true" : "false");
  out += ",\"fault_snapshot_valid\":";
  out += (faultSnapshotReady ? "true" : "false");
  out += ",\"fault_snapshot_a\":[";
  for (int i = 0; i < NUM_CHANNELS; i++) {
    if (i) out += ",";
    out += String(faultSnapshotA[i], 3);
  }
  out += "]";
  out += ",\"channels\":[";
  for (int i = 0; i < NUM_CHANNELS; i++) {
    if (i) out += ",";
    out += "{\"continuity\":";
    out += (hasContinuity(i) ? "true" : "false");
    out += ",\"selected\":";
    out += (channels[i].selected ? "true" : "false");
    out += ",\"firing\":";
    out += (channels[i].fireActive ? "true" : "false");
    out += ",\"scheduled\":";
    out += (channels[i].scheduled ? "true" : "false");
    out += ",\"delay_ms\":";
    out += settings.channelDelayMs[i];
    out += ",\"duration_ms\":";
    out += settings.channelDurationMs[i];
    out += ",\"last_current_a\":";
    out += String(channels[i].lastCurrentA, 3);
    out += ",\"peak_current_a\":";
    out += String(channels[i].peakCurrentA, 3);
    out += ",\"avg_current_a\":";
    out += String(channels[i].avgCurrentA, 3);
    out += "}";
  }
  out += "]}";
}

void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleStatus() {
  String json;
  buildStatusJson(json);
  server.send(200, "application/json", json);
}

void handleSelect() {
  if (!server.hasArg("ch") || !server.hasArg("selected")) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"missing ch or selected\"}");
    return;
  }
  int ch = server.arg("ch").toInt();
  if (ch < 0 || ch >= NUM_CHANNELS) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"bad channel\"}");
    return;
  }
  if (settings.checkContinuityOnArm && getArmState() != ArmState::DISARMED) {
    server.send(409, "application/json",
                "{\"ok\":false,\"error\":\"cannot change selection while armed - disarm first\"}");
    return;
  }

  channels[ch].selected = (server.arg("selected") == "1");
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleTrigger() {
  if (server.arg("confirm") != "1") {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"missing confirm\"}");
    return;
  }
  if (faultLatched) {
    server.send(409, "application/json", "{\"ok\":false,\"error\":\"fault latched\"}");
    return;
  }
  if (getArmState() != ArmState::READY) {
    server.send(409, "application/json", "{\"ok\":false,\"error\":\"not armed and ready\"}");
    return;
  }
  if (sequenceActive) {
    server.send(409, "application/json", "{\"ok\":false,\"error\":\"sequence already in progress\"}");
    return;
  }
  if (settings.requireRearmAfterFire && triggerLockedOut()) {
    server.send(409, "application/json",
                "{\"ok\":false,\"error\":\"must disarm and rearm before triggering again\"}");
    return;
  }
  if (continuityLockedOut()) {
    server.send(409, "application/json",
                "{\"ok\":false,\"error\":\"continuity check failed at last trigger - disarm and rearm\"}");
    return;
  }
  if (panicLockedOut()) {
    server.send(409, "application/json",
                "{\"ok\":false,\"error\":\"panic pressed - disarm and rearm before triggering again\"}");
    return;
  }
  if (armContinuityError) {
    server.send(409, "application/json",
                "{\"ok\":false,\"error\":\"continuity problem on a selected channel\"}");
    return;
  }

  bool anySelected = false;
  for (int i = 0; i < NUM_CHANNELS; i++) {
    if (channels[i].selected) anySelected = true;
  }
  if (!anySelected) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"no channels selected\"}");
    return;
  }

  // Pre-trigger continuity check: stricter than the live arm-time monitor
  // above — a failure here refuses the whole sequence (not just the bad
  // channel) and imposes a hard disarm+rearm lockout unconditionally,
  // regardless of requireRearmAfterFire.
  if (settings.checkContinuityBeforeTrigger) {
    for (int i = 0; i < NUM_CHANNELS; i++) {
      if (channels[i].selected && !hasContinuity(i)) {
        notifyContinuityCheckFailed();
        server.send(409, "application/json",
                    "{\"ok\":false,\"error\":\"continuity check failed - nothing fired, disarm and rearm required\"}");
        return;
      }
    }
  }

  uint32_t now = millis();
  for (int i = 0; i < NUM_CHANNELS; i++) {
    if (!channels[i].selected) continue;
    channels[i].scheduled = true;
    channels[i].fireAtMs = now + settings.channelDelayMs[i];
  }

  sequenceActive = true;
  notifyTriggerAccepted();
  server.send(200, "application/json", "{\"ok\":true}");
}

// Stops an in-progress sequence immediately but does NOT lock out
// retriggering — if still armed and ready, TRIGGER can be pressed again
// right away. Deliberately no confirm() required on the frontend and no
// gating here: an abort button should always just work.
void handleAbort() {
  bool wasActive = sequenceActive;
  stopSequence();
  Serial.printf("[abort] sequence %s\n", wasActive ? "stopped" : "was not active");
  server.send(200, "application/json", "{\"ok\":true}");
}

// Stops an in-progress sequence immediately, same as abort, but always
// (whether or not anything was firing) imposes a hard disarm+rearm lockout
// via notifyPanicPressed(), independent of requireRearmAfterFire. Also
// deliberately no confirm() and no gating — a panic button should always
// just work.
void handlePanic() {
  bool wasActive = sequenceActive;
  stopSequence();
  notifyPanicPressed();
  Serial.printf("[panic] sequence %s, disarm+rearm now required\n", wasActive ? "stopped" : "was not active");
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleChannelDelay() {
  if (!server.hasArg("ch") || !server.hasArg("delay_ms")) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"missing ch or delay_ms\"}");
    return;
  }
  int ch = server.arg("ch").toInt();
  if (ch < 0 || ch >= NUM_CHANNELS) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"bad channel\"}");
    return;
  }
  long delayMs = server.arg("delay_ms").toInt();
  if (delayMs < 0 || delayMs > 60000) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"delay_ms out of range (0-60000)\"}");
    return;
  }

  settings.channelDelayMs[ch] = (uint32_t)delayMs;
  bool saved = saveSettings();
  if (!saved) {
    server.send(409, "application/json",
                "{\"ok\":false,\"error\":\"applied, but not saved to flash - disarm to persist\"}");
    return;
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleChannelDuration() {
  if (!server.hasArg("ch") || !server.hasArg("duration_ms")) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"missing ch or duration_ms\"}");
    return;
  }
  int ch = server.arg("ch").toInt();
  if (ch < 0 || ch >= NUM_CHANNELS) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"bad channel\"}");
    return;
  }
  long durationMs = server.arg("duration_ms").toInt();
  if (durationMs < 0 || durationMs > 30000) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"duration_ms out of range (0-30000)\"}");
    return;
  }

  settings.channelDurationMs[ch] = (uint32_t)durationMs;
  bool saved = saveSettings();
  if (!saved) {
    server.send(409, "application/json",
                "{\"ok\":false,\"error\":\"applied, but not saved to flash - disarm to persist\"}");
    return;
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleConfigPage() {
  server.send_P(200, "text/html", CONFIG_HTML);
}

void handleTimingPage() {
  server.send_P(200, "text/html", TIMING_HTML);
}

void handleStatsPage() {
  server.send_P(200, "text/html", STATS_HTML);
}

void handleConfigGet() {
  String out = "{\"sense_ohms\":[";
  for (int i = 0; i < NUM_CHANNELS; i++) {
    if (i) out += ",";
    out += String(settings.senseOhms[i], 4);
  }
  out += "],\"arm_countdown_s\":";
  out += settings.armCountdownSec;
  out += ",\"visible_when_armed\":";
  out += (settings.visibleWhenArmed ? "true" : "false");
  out += ",\"audible_when_armed\":";
  out += (settings.audibleWhenArmed ? "true" : "false");
  out += ",\"require_rearm\":";
  out += (settings.requireRearmAfterFire ? "true" : "false");
  out += ",\"check_continuity_on_arm\":";
  out += (settings.checkContinuityOnArm ? "true" : "false");
  out += ",\"check_continuity_before_trigger\":";
  out += (settings.checkContinuityBeforeTrigger ? "true" : "false");
  out += ",\"low_battery_threshold_v\":";
  out += String(settings.lowBatteryThresholdV, 2);
  out += ",\"low_voltage_lockout_enabled\":";
  out += (settings.lowVoltageLockoutEnabled ? "true" : "false");
  out += ",\"wifi_ssid\":";
  appendJsonString(out, settings.wifiSsid);
  out += ",\"wifi_password\":";
  appendJsonString(out, settings.wifiPassword);
  out += "}";
  server.send(200, "application/json", out);
}

void handleConfigPost() {
  float newSenseOhms[NUM_CHANNELS];
  for (int i = 0; i < NUM_CHANNELS; i++) {
    char key[16];
    snprintf(key, sizeof(key), "sense_ohm%d", i);
    if (!server.hasArg(key)) {
      server.send(400, "application/json", "{\"ok\":false,\"error\":\"missing " + String(key) + "\"}");
      return;
    }
    float v = server.arg(key).toFloat();
    if (v < 0.001f || v > 10.0f) {
      server.send(400, "application/json", "{\"ok\":false,\"error\":\"" + String(key) + " out of range (0.001-10 ohm)\"}");
      return;
    }
    newSenseOhms[i] = v;
  }

  if (!server.hasArg("arm_countdown_s")) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"missing arm_countdown_s\"}");
    return;
  }
  long countdown = server.arg("arm_countdown_s").toInt();
  if (countdown < 0 || countdown > 600) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"arm_countdown_s out of range (0-600)\"}");
    return;
  }

  if (!server.hasArg("low_battery_threshold_v")) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"missing low_battery_threshold_v\"}");
    return;
  }
  float lowBattThresh = server.arg("low_battery_threshold_v").toFloat();
  if (lowBattThresh < 0.0f || lowBattThresh > 30.0f) {
    server.send(400, "application/json",
                "{\"ok\":false,\"error\":\"low_battery_threshold_v out of range (0-30V)\"}");
    return;
  }

  if (!server.hasArg("wifi_ssid")) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"missing wifi_ssid\"}");
    return;
  }
  String newSsid = server.arg("wifi_ssid");
  if (newSsid.length() < 1 || newSsid.length() > 32) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"wifi_ssid must be 1-32 characters\"}");
    return;
  }

  if (!server.hasArg("wifi_password")) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"missing wifi_password\"}");
    return;
  }
  String newPassword = server.arg("wifi_password");
  if (newPassword.length() != 0 && (newPassword.length() < 8 || newPassword.length() > 63)) {
    server.send(400, "application/json",
                "{\"ok\":false,\"error\":\"wifi_password must be empty (open network) or 8-63 characters\"}");
    return;
  }

  bool visible = server.arg("visible_when_armed") == "1";
  bool audible = server.arg("audible_when_armed") == "1";
  bool reqRearm = server.arg("require_rearm") == "1";
  bool contOnArm = server.arg("check_continuity_on_arm") == "1";
  bool contBeforeTrig = server.arg("check_continuity_before_trigger") == "1";
  bool lvLockout = server.arg("low_voltage_lockout_enabled") == "1";

  // Applied to the in-memory settings immediately either way — none of
  // these influence a firing/fault decision already in flight, so there's
  // no safety reason to gate the RAM update on armed state. Only the flash
  // write itself needs that gate. WiFi credentials are the exception in
  // spirit (they DO need a reboot to actually take effect, since
  // WiFi.softAP() isn't re-called at runtime) but there's no harm in
  // updating settings.wifiSsid/wifiPassword immediately too — it's just
  // inert until the next boot.
  for (int i = 0; i < NUM_CHANNELS; i++) settings.senseOhms[i] = newSenseOhms[i];
  settings.armCountdownSec = (uint32_t)countdown;
  settings.visibleWhenArmed = visible;
  settings.audibleWhenArmed = audible;
  settings.requireRearmAfterFire = reqRearm;
  settings.checkContinuityOnArm = contOnArm;
  settings.checkContinuityBeforeTrigger = contBeforeTrig;
  settings.lowBatteryThresholdV = lowBattThresh;
  settings.lowVoltageLockoutEnabled = lvLockout;
  newSsid.toCharArray(settings.wifiSsid, sizeof(settings.wifiSsid));
  newPassword.toCharArray(settings.wifiPassword, sizeof(settings.wifiPassword));

  bool saved = saveSettings();
  if (!saved) {
    server.send(409, "application/json",
                "{\"ok\":false,\"error\":\"applied, but not saved to flash - disarm to persist\"}");
    return;
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleConfigReset() {
  resetSettingsToDefaults();
  bool saved = saveSettings();
  if (!saved) {
    server.send(409, "application/json",
                "{\"ok\":false,\"error\":\"reset in memory, but not saved to flash - disarm to persist\"}");
    return;
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleClearFault() {
  // Re-check the hardware line several times over ~2ms rather than trusting
  // one instantaneous read, so a line chattering right at the comparator's
  // trip point doesn't get cleared into a still-live fault.
  bool stillFaulted = false;
  for (int i = 0; i < 10; i++) {
    if (digitalRead(PIN_FAULT) == LOW) { stillFaulted = true; break; }
    delayMicroseconds(200);
  }

  if (stillFaulted) {
    server.send(409, "application/json", "{\"ok\":false,\"error\":\"fault still active\"}");
    return;
  }

  noInterrupts();
  faultLatched = false;
  interrupts();
  faultSnapshotReady = false;  // old snapshot belongs to the fault we just cleared
  server.send(200, "application/json", "{\"ok\":true}");
}

void setup() {
  // SAFETY FIRST: claim the trigger pins and force them HIGH before anything
  // else runs (Serial, WiFi bring-up, etc. all take time). Hardware already
  // defaults these safe via a pull-up to the FAILSAFE arm rail, but we don't
  // rely on that for the window before this executes.
  for (int i = 0; i < NUM_CHANNELS; i++) {
    digitalWrite(PIN_TRIGGER[i], HIGH);
    pinMode(PIN_TRIGGER[i], OUTPUT);
    digitalWrite(PIN_TRIGGER[i], HIGH);
  }

  Serial.begin(115200);

  pinMode(PIN_FAULT, INPUT_PULLUP);  // R40 already pulls this to 3V3 externally; belt-and-suspenders
  pinMode(PIN_AUDIBLE, OUTPUT);
  digitalWrite(PIN_AUDIBLE, LOW);
  pinMode(PIN_VISIBLE, OUTPUT);
  digitalWrite(PIN_VISIBLE, LOW);

  for (int i = 0; i < NUM_CHANNELS; i++) {
    pinMode(PIN_CONTINUITY[i], INPUT_PULLDOWN);
    pinMode(PIN_SENSE[i], INPUT);
  }
  pinMode(PIN_SENSE_FAILSAFE, INPUT_PULLDOWN);
  pinMode(PIN_BATTERY, INPUT);  // always driven by the resistor divider, no pull needed

#if DEBUG_FAULT_TIMING
  pinMode(PIN_DEBUG_TIMING, OUTPUT);
  digitalWrite(PIN_DEBUG_TIMING, LOW);
#endif

  analogReadResolution(12);
  loadSettings();

  // Must exist before the ISR can notify it. Pinned to core 0, opposite the
  // Arduino loop/WebServer work (core 1 by default), so it doesn't have to
  // wait behind whatever loop() is doing when the fault fires. Priority 20
  // sits comfortably above loopTask (1) so it preempts immediately, while
  // staying below the WiFi driver's own internal tasks (~22-23) so it can't
  // interfere with radio timing.
  xTaskCreatePinnedToCore(faultSampleTask, "faultSample", 2048, nullptr, 20,
                           &faultSampleTaskHandle, 0);

  // Latch immediately if a fault is already present at boot (e.g. a short
  // left in place across a reset), then attach the interrupt for anything
  // that happens from here on.
  if (digitalRead(PIN_FAULT) == LOW) faultLatched = true;
  attachInterrupt(digitalPinToInterrupt(PIN_FAULT), onFaultISR, FALLING);

  WiFi.persistent(false);  // avoid NVS flash writes for AP config while the board may be armed
  WiFi.mode(WIFI_AP);
  WiFi.softAP(settings.wifiSsid, settings.wifiPassword);
  Serial.print("[wifi] AP \"");
  Serial.print(settings.wifiSsid);
  Serial.print("\" up, browse to http://");
  Serial.println(WiFi.softAPIP());

  server.on("/", handleRoot);
  server.on("/status.json", handleStatus);
  server.on("/trigger", HTTP_POST, handleTrigger);
  server.on("/abort", HTTP_POST, handleAbort);
  server.on("/panic", HTTP_POST, handlePanic);
  server.on("/select", HTTP_POST, handleSelect);
  server.on("/channel_delay", HTTP_POST, handleChannelDelay);
  server.on("/channel_duration", HTTP_POST, handleChannelDuration);
  server.on("/clear_fault", HTTP_POST, handleClearFault);
  server.on("/config", HTTP_GET, handleConfigPage);
  server.on("/timing", HTTP_GET, handleTimingPage);
  server.on("/stats", HTTP_GET, handleStatsPage);
  server.on("/config.json", HTTP_GET, handleConfigGet);
  server.on("/config", HTTP_POST, handleConfigPost);
  server.on("/config/reset", HTTP_POST, handleConfigReset);
  server.begin();
}

void loop() {
  server.handleClient();
  setSequenceActive(sequenceActive);
  updateArmState();

  uint32_t now = millis();

  for (int i = 0; i < NUM_CHANNELS; i++) {
    // Rolling current reading for the UI; reads ~0 when idle since the
    // shunt is permanently tied to GND.
    channels[i].lastCurrentA = readCurrentA(i);

    // Peak/average accumulation for this pulse — sampled before checking
    // for pulse end below, so the very last active reading still counts.
    if (channels[i].fireActive) {
      if (channels[i].lastCurrentA > channels[i].peakCurrentA) {
        channels[i].peakCurrentA = channels[i].lastCurrentA;
      }
      channels[i].currentSumA += channels[i].lastCurrentA;
      channels[i].currentSampleCount++;
      channels[i].avgCurrentA = channels[i].currentSumA / channels[i].currentSampleCount;
    }

    // Release any channel whose fire pulse has elapsed.
    if (channels[i].fireActive && (int32_t)(now - channels[i].fireEndsAt) >= 0) {
      digitalWrite(PIN_TRIGGER[i], HIGH);
      channels[i].fireActive = false;
    }
  }

  batteryVoltage = readBatteryVoltage();

  // Scheduled-fire pass for the current trigger sequence (if any).
  for (int i = 0; i < NUM_CHANNELS; i++) {
    if (!channels[i].scheduled) continue;
    if ((int32_t)(now - channels[i].fireAtMs) < 0) continue;

    channels[i].scheduled = false;
    // Re-check right at fire time — conditions can change during a
    // multi-second delay. If the arm switch opens mid-sequence, the gate
    // drivers lose power regardless of what firmware does; this check is
    // belt-and-suspenders/logging, not the actual safety mechanism.
    if (faultLatched || getArmState() != ArmState::READY || !hasContinuity(i)) {
      Serial.printf("[trigger] channel %d skipped at fire time (fault=%d ready=%d continuity=%d)\n",
                    i + 1, faultLatched, getArmState() == ArmState::READY, hasContinuity(i));
      continue;
    }
    startFirePulse(i);
  }

  if (sequenceActive) {
    bool anyPending = false;
    for (int i = 0; i < NUM_CHANNELS; i++) {
      if (channels[i].scheduled || channels[i].fireActive) { anyPending = true; break; }
    }
    if (!anyPending) sequenceActive = false;
  }

  // Live arm-time continuity monitor (feature #1): recomputed every
  // iteration, not latched — clears itself the instant the problem is fixed
  // or the setting is turned off. Only meaningful while armed; the arm
  // state machine's own COUNTDOWN->READY progression is unaffected by this,
  // it only gates triggering.
  armContinuityError = false;
  if (settings.checkContinuityOnArm && getArmState() != ArmState::DISARMED) {
    for (int i = 0; i < NUM_CHANNELS; i++) {
      if (channels[i].selected && !hasContinuity(i)) {
        armContinuityError = true;
        break;
      }
    }
  }

  // Defensive backstop in case an edge was missed (e.g. FAULT was already
  // low before attachInterrupt() ran during setup()).
  if (!faultLatched && digitalRead(PIN_FAULT) == LOW) faultLatched = true;

  // On the transition into a fault: nothing should still claim to be
  // "firing" since the ISR already forced every trigger pin high (the
  // digitalWrite() inside stopSequence() is redundant here but harmless).
  if (faultLatched && !faultLatchedPrev) {
    stopSequence();
    Serial.printf("[fault] latched at t=%lu ms\n", (unsigned long)now);
  }
  faultLatchedPrev = faultLatched;

  // Log the fault-sample task's snapshot once, the first loop() iteration
  // after it becomes available (it may still be a few loop iterations
  // before this runs, but faultSampleTask has already captured the reading
  // as close to the fault event as this platform allows — this is just
  // reporting it out over Serial).
  static uint32_t lastLoggedSnapshotAtMs = 0;
  if (faultSnapshotReady && faultSnapshotAtMs != lastLoggedSnapshotAtMs) {
    Serial.printf("[fault] sense snapshot @ t=%lu ms: %.2fA %.2fA %.2fA\n",
                  (unsigned long)faultSnapshotAtMs, faultSnapshotA[0], faultSnapshotA[1], faultSnapshotA[2]);
    lastLoggedSnapshotAtMs = faultSnapshotAtMs;
  }
}
