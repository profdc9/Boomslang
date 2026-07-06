#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/gpio_reg.h"
#include "soc/soc.h"
#include "config.h"
#include "webpage.h"

WebServer server(80);

struct ChannelState {
  bool fireActive = false;
  uint32_t fireEndsAt = 0;
  float lastCurrentA = 0.0f;
};
ChannelState channels[NUM_CHANNELS];

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
  return v / SENSE_OHMS;
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

bool isArmed() {
  return analogRead(PIN_SENSE_FAILSAFE) > FAILSAFE_OK_RAW;
}

bool hasContinuity(int ch) {
  return analogRead(PIN_CONTINUITY[ch]) > CONTINUITY_OK_RAW;
}

bool fireChannel(int ch, String &err) {
  if (ch < 0 || ch >= NUM_CHANNELS) { err = "bad channel"; return false; }
  if (faultLatched) { err = "fault latched - clear fault before firing"; return false; }
  if (digitalRead(PIN_FAULT) == LOW) { err = "fault active"; return false; }
  if (!isArmed()) { err = "not armed - close arm key at J5"; return false; }
  if (!hasContinuity(ch)) { err = "no continuity"; return false; }
  if (channels[ch].fireActive) { err = "already firing"; return false; }

  channels[ch].fireActive = true;
  channels[ch].fireEndsAt = millis() + FIRE_PULSE_MS;
  digitalWrite(PIN_TRIGGER[ch], LOW);
  Serial.printf("[fire] channel %d fired\n", ch + 1);
  return true;
}

void buildStatusJson(String &out) {
  out = "{";
  out += "\"armed\":";
  out += (isArmed() ? "true" : "false");
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

void handleFire() {
  if (!server.hasArg("ch") || server.arg("confirm") != "1") {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"missing ch or confirm\"}");
    return;
  }
  int ch = server.arg("ch").toInt();
  String err;
  bool ok = fireChannel(ch, err);
  String resp = String("{\"ok\":") + (ok ? "true" : "false");
  if (!ok) resp += ",\"error\":\"" + err + "\"";
  resp += "}";
  server.send(ok ? 200 : 409, "application/json", resp);
}

void handleAudible() {
  digitalWrite(PIN_AUDIBLE, server.arg("on") == "1" ? HIGH : LOW);
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleVisible() {
  digitalWrite(PIN_VISIBLE, server.arg("on") == "1" ? HIGH : LOW);
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
  server.on("/fire", HTTP_POST, handleFire);
  server.on("/audible", HTTP_POST, handleAudible);
  server.on("/visible", HTTP_POST, handleVisible);
  server.on("/clear_fault", HTTP_POST, handleClearFault);
  server.begin();
}

void loop() {
  server.handleClient();

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

  // Defensive backstop in case an edge was missed (e.g. FAULT was already
  // low before attachInterrupt() ran during setup()).
  if (!faultLatched && digitalRead(PIN_FAULT) == LOW) faultLatched = true;

  // On the transition into a fault: nothing should still claim to be
  // "firing" since the ISR already forced every trigger pin high.
  if (faultLatched && !faultLatchedPrev) {
    for (int i = 0; i < NUM_CHANNELS; i++) channels[i].fireActive = false;
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
