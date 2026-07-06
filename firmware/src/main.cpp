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
};
ChannelState channels[NUM_CHANNELS];

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

bool hasContinuity(int ch) {
  return analogRead(PIN_CONTINUITY[ch]) > CONTINUITY_OK_RAW;
}

void startFirePulse(int ch) {
  channels[ch].fireActive = true;
  channels[ch].fireEndsAt = millis() + FIRE_PULSE_MS;
  digitalWrite(PIN_TRIGGER[ch], LOW);
  Serial.printf("[fire] channel %d fired\n", ch + 1);
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
  out += ",\"sequence_active\":";
  out += (sequenceActive ? "true" : "false");
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
    out += ",\"firing\":";
    out += (channels[i].fireActive ? "true" : "false");
    out += ",\"scheduled\":";
    out += (channels[i].scheduled ? "true" : "false");
    out += ",\"delay_s\":";
    out += String(settings.channelDelaySec[i], 2);
    out += ",\"last_current_a\":";
    out += String(channels[i].lastCurrentA, 3);
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

  uint32_t now = millis();
  bool anySelected = false;
  for (int i = 0; i < NUM_CHANNELS; i++) {
    char key[8];
    snprintf(key, sizeof(key), "ch%d", i);
    if (server.hasArg(key) && server.arg(key) == "1") {
      anySelected = true;
      channels[i].scheduled = true;
      channels[i].fireAtMs = now + (uint32_t)(settings.channelDelaySec[i] * 1000.0f);
    }
  }

  if (!anySelected) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"no channels selected\"}");
    return;
  }

  sequenceActive = true;
  notifyTriggerAccepted();
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleChannelDelay() {
  if (!server.hasArg("ch") || !server.hasArg("delay")) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"missing ch or delay\"}");
    return;
  }
  int ch = server.arg("ch").toInt();
  if (ch < 0 || ch >= NUM_CHANNELS) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"bad channel\"}");
    return;
  }
  float delaySec = server.arg("delay").toFloat();
  if (delaySec < 0.0f || delaySec > 60.0f) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"delay out of range (0-60s)\"}");
    return;
  }

  settings.channelDelaySec[ch] = delaySec;
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

  bool visible = server.arg("visible_when_armed") == "1";
  bool audible = server.arg("audible_when_armed") == "1";
  bool reqRearm = server.arg("require_rearm") == "1";

  // Applied to the in-memory settings immediately either way — none of
  // these influence a firing/fault decision already in flight, so there's
  // no safety reason to gate the RAM update on armed state. Only the flash
  // write itself needs that gate.
  for (int i = 0; i < NUM_CHANNELS; i++) settings.senseOhms[i] = newSenseOhms[i];
  settings.armCountdownSec = (uint32_t)countdown;
  settings.visibleWhenArmed = visible;
  settings.audibleWhenArmed = audible;
  settings.requireRearmAfterFire = reqRearm;

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
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  Serial.print("[wifi] AP \"");
  Serial.print(AP_SSID);
  Serial.print("\" up, browse to http://");
  Serial.println(WiFi.softAPIP());

  server.on("/", handleRoot);
  server.on("/status.json", handleStatus);
  server.on("/trigger", HTTP_POST, handleTrigger);
  server.on("/channel_delay", HTTP_POST, handleChannelDelay);
  server.on("/clear_fault", HTTP_POST, handleClearFault);
  server.on("/config", HTTP_GET, handleConfigPage);
  server.on("/config.json", HTTP_GET, handleConfigGet);
  server.on("/config", HTTP_POST, handleConfigPost);
  server.on("/config/reset", HTTP_POST, handleConfigReset);
  server.begin();
}

void loop() {
  server.handleClient();
  updateArmState();

  uint32_t now = millis();

  for (int i = 0; i < NUM_CHANNELS; i++) {
    // Release any channel whose fire pulse has elapsed.
    if (channels[i].fireActive && (int32_t)(now - channels[i].fireEndsAt) >= 0) {
      digitalWrite(PIN_TRIGGER[i], HIGH);
      channels[i].fireActive = false;
    }
    // Rolling current reading for the UI; reads ~0 when idle since the
    // shunt is permanently tied to GND.
    channels[i].lastCurrentA = readCurrentA(i);
  }

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

  // Defensive backstop in case an edge was missed (e.g. FAULT was already
  // low before attachInterrupt() ran during setup()).
  if (!faultLatched && digitalRead(PIN_FAULT) == LOW) faultLatched = true;

  // On the transition into a fault: nothing should still claim to be
  // "firing" since the ISR already forced every trigger pin high.
  if (faultLatched && !faultLatchedPrev) {
    for (int i = 0; i < NUM_CHANNELS; i++) {
      channels[i].fireActive = false;
      channels[i].scheduled = false;
    }
    sequenceActive = false;
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
