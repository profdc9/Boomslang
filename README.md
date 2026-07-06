# Boomslang

A 3-channel ESP32-S3 pyrotechnic/model-rocket ignition controller. Hardware
(KiCad project, schematic, gerbers) is under `Boomslang/`; firmware is under
`firmware/` (PlatformIO, Arduino framework).

## WiFi access

The board hosts its own WiFi AP (no router needed) — SSID and password are
persisted settings (`wifiSsid`/`wifiPassword`, settings page), defaulting to
`boomslang` / `liftoff!`. **The password is this device's only access
control** — there's no separate login on top of it, so anyone who has it can
arm and fire. Change it from the default before using this anywhere it
matters. A password can also be left blank for an open, unencrypted network,
but that isn't recommended for the same reason.

Changes take effect only after the device is power-cycled or reset (the AP
isn't re-initialized at runtime) — you'll need to reconnect using the new
name/password afterward. SSID must be 1-32 characters; password must be
empty or 8-63 characters (the minimum WPA2-PSK allows).

The control pages are served over plain HTTP, not HTTPS — WPA2 already
encrypts the WiFi link itself, and adding TLS on top would mean a
self-signed certificate that a phone would flag as untrusted on every visit,
plus added latency on the page's frequent status polling, for a narrower
additional threat (another authenticated client on the same AP sniffing
traffic) than what WPA2 already covers.

## Overcurrent fault protection

Each channel's IRLZ44N MOSFET has its own analog overcurrent comparator
(transistors Q5/Q10/Q16, sensing the 0.05Ω source shunt) that pulls that
channel's own gate to GND in hardware, in nanoseconds, with **no firmware
involved at all**. That's the actual protection for the transistor. All
three channels' comparators also pull a single shared, open-collector,
active-LOW `FAULT` line (GPIO1) — since it's wired-OR across all three,
firmware can tell *that* a channel faulted, but not *which* one.

**The fault ISR** (`onFaultISR` in `firmware/src/main.cpp`) is a level-1
`attachInterrupt` on `FAULT`, `IRAM_ATTR`, and deliberately minimal: it does
exactly two things, a single register write forcing all three TRIGGER
outputs HIGH at once (not just the channel that faulted — the shared line
can't tell which one anyway, so every channel is shut down for consistency)
and setting a sticky `faultLatched` flag. Nothing else runs in the ISR
itself — no `Serial`, no heap, and critically no `analogRead()`, since the
ESP-IDF ADC driver takes a FreeRTOS mutex internally that `loop()` is
already holding on a regular basis; taking it again from ISR context would
be undefined behavior.

**Sampling SENSE for diagnostics** (which channel was drawing how much
current right as the fault hit) therefore happens in a separate,
dedicated FreeRTOS task (`faultSampleTask`), woken by the ISR via
`vTaskNotifyGiveFromISR`/`portYIELD_FROM_ISR` so it runs essentially the
instant the ISR returns, ahead of `loop()` or anything else. It's pinned to
the core opposite the Arduino loop/web server, at a priority high enough to
preempt normal code but below WiFi's own internal tasks. This snapshot is
best-effort — the hardware protection may have already cut current before
it runs — and is exposed as `fault_snapshot_a` in `/status.json` and shown
in the FAULT banner on the control page.

**Clearing a fault**: `faultLatched` is a software latch, not
self-clearing — `/clear_fault` re-checks the hardware `FAULT` line itself
(several reads over ~2ms, so a line chattering right at the trip point
doesn't get cleared into a still-live fault) and refuses if it's still
asserted. A reboot doesn't clear a persistent fault either: `FAULT` is
checked at boot, before the ISR is even attached, and latches immediately
if still asserted.

Why a plain level-1 interrupt is enough: the IRLZ44N's SOA tolerates ~1ms at
full overcurrent, and even level-1 ISR latency on this chip is reliably
single-digit microseconds — comfortably inside that margin, especially
given the hardware analog protection has already started cutting current
before the ISR even runs. A `DEBUG_FAULT_TIMING` build flag (on by default
in `config.h`) toggles a spare pin (GPIO13) around ISR entry and the end of
SENSE sampling, so real end-to-end latency can be measured on a scope once
hardware exists.

## Audible / visible arming indicators

The board carries a buzzer output (`J6`, driven via `AUDIBLE`/GPIO41) and
onboard white/blue strobe LEDs (driven via `VISIBLE`/GPIO42). Both are owned
entirely by the firmware's arm state machine (`firmware/src/arm_state.cpp`)
— there's no manual on/off control for them, since a manual override would
just fight the automatic pattern.

**When they run:** only while the arm loop (the physical switch at `J5`) is
closed — i.e. during the `COUNTDOWN` and `READY` states. They're silent and
dark whenever the device is `DISARMED`; there's nothing to warn about when
the loop is open.

**Why two different patterns:** so the state can be told apart by ear or by
eye alone, without needing to look at the web UI:

| State       | Tone    | Pulse rate           |
|-------------|---------|-----------------------|
| `COUNTDOWN` | 440 Hz  | ~1 Hz (slow, 500ms on/off) |
| `READY`     | 1000 Hz | ~2.5 Hz (fast, 200ms on/off) |

A low, slow pulse means the countdown is still running and firing is *not*
yet permitted (giving whoever closed the arm switch time to clear the area).
A higher, faster pulse means the countdown has elapsed and the device is
ready to fire.

**Settings:** each channel can be silenced independently via the settings
page (`/config` on the web UI):

- `visibleWhenArmed` — strobe LEDs flash during `COUNTDOWN`/`READY` (default **on**)
- `audibleWhenArmed` — buzzer sounds during `COUNTDOWN`/`READY` (default **on**)

Both are persisted to flash (survive power cycles) and, like all settings
changes, can only be saved while the device is disarmed.

## Trigger sequence

Firing isn't one button per channel — the main control page lets you select
which channels to fire and, per channel, how long after pressing TRIGGER
that channel should fire, then fires the whole configured sequence off a
single press.

**Per channel, on the main page:**
- A checkbox to include that channel in the next trigger. **Not** persisted
  — it resets on every page load, so a stale "everything selected" state
  from a previous session can never carry over silently.
- An **offset** in milliseconds (0-60000, default 0) from the TRIGGER press
  to that channel's trigger output going low.
- A **duration** in milliseconds (0-30000, default 500) that the trigger
  output then stays low.

Both offset and duration are persisted per channel, so a fixed show
sequence doesn't need retyping every session. They're plain milliseconds
rather than seconds specifically for finer-grained control over short
pulses.

**Pressing TRIGGER** is only possible when `arm_state` is `READY`, there's no
active fault, no sequence is already running, and (if
`requireRearmAfterFire` is on) the device hasn't fired since it was last
armed. It schedules every selected channel's trigger output to go low at
`now + its configured offset`, for its configured duration, then
immediately blocks further TRIGGER presses until that sequence finishes
(you can never have two sequences in flight at once, regardless of
settings).

**At each channel's actual fire moment** — not just when TRIGGER was pressed
— the firmware re-checks armed state, fault, and continuity. An offset can
be long enough for something to change, so a channel that fails this
recheck is skipped (and logged) rather than aborting the rest of the
sequence. If the arm switch is opened mid-sequence, the gate drivers lose
power outright regardless of what firmware does — this recheck is
belt-and-suspenders, not the actual safety mechanism.

**`requireRearmAfterFire`** (settings page, default **on**): once a trigger
sequence has run, no further TRIGGER is accepted until the arm switch is
opened and closed again (a fresh disarm + countdown + `READY`). With it off,
another TRIGGER is accepted as soon as the current sequence finishes, no
rearm needed.

**Peak and average current**, per channel, for its most recent pulse: while
a channel's trigger output is low, `firmware/src/main.cpp`'s `loop()`
samples that channel's current sense on every iteration, tracking the
highest single sample (peak) and the running mean of all samples (average).
Both are reset at the start of each pulse and then simply held afterward —
so they remain readable (`peak_current_a`/`avg_current_a` in
`/status.json`, shown under each channel on the control page) for the rest
of the session, until that channel fires again.

## ABORT and PANIC buttons

Two more buttons sit below TRIGGER on the main control page, both backed by
a shared `stopSequence()` helper (`firmware/src/main.cpp`) that stops
things *immediately* — any channel mid-pulse has its trigger pin forced
back HIGH right away rather than waiting out the rest of the fire pulse,
and anything still scheduled (waiting on its configured delay) is
cancelled. Neither has a confirm dialog on the frontend, and neither is
gated on any precondition — an abort/panic button should always just work,
the instant it's tapped, like a real E-stop.

**ABORT** (`/abort`) just stops the sequence. It does **not** prevent
retriggering — if the device is still armed and ready afterward, TRIGGER is
available again immediately. Only enabled on the control page while a
sequence is actually running, since it has nothing to do otherwise.

**PANIC** (`/panic`) does the same immediate stop, and *additionally*
always sets a new lockout (`panicLockedOut()` in `arm_state`) requiring a
full disarm and rearm before another TRIGGER is accepted — unconditionally,
independent of `requireRearmAfterFire`, and regardless of whether anything
was actually firing when it was pressed. Always enabled on the control
page, since pressing it has an effect either way.

## Continuity checks

`CONTINUITY1/2/3` sense the same way `SENSEFAILSAFE` does: ~1mA through a red
LED, whose forward voltage exceeds ~1.5V when the loop (the igniter, in this
case) is actually closed. That threshold (`CONTINUITY_OK_RAW` in
`firmware/src/config.h`) is shared with the arm-loop sense, since it's the
identical circuit at the identical target voltage.

Beyond the per-channel status dot, continuity gates channel selection and
triggering in two independent ways:

**Arm-time continuity monitor** (`checkContinuityOnArm`, settings page,
default **on**): while armed (`COUNTDOWN` or `READY`), continuously verifies
that every *selected* channel still has continuity. If one doesn't, the
control page shows an error and TRIGGER is refused — but this is live, not
latched: it clears itself the instant continuity is restored, or the setting
is turned off, no disarm needed. While this setting is on, channel selection
is also locked while armed (the `/select` endpoint refuses changes), since
the set of channels being monitored can't shift out from under a live check
— disarm to change which channels are selected.

**Pre-trigger continuity check** (`checkContinuityBeforeTrigger`, settings
page, default **on**): at the instant TRIGGER is pressed, re-verifies
continuity for every selected channel. If any fail, *nothing* fires, the
control page shows an error, and — unlike the live monitor above — this is a
hard lockout: a full disarm and rearm is required before another TRIGGER is
accepted, regardless of `requireRearmAfterFire` (that setting is about the
post-*successful*-fire case; this is a separate, always-strict rule for this
specific failure).

Both checks are additional to, not a replacement for, the per-channel
recheck already described under Trigger sequence above (which happens at
each channel's actual fire moment, not at the TRIGGER press, and only skips
that one channel rather than refusing the whole sequence).

## Battery voltage

The 12V supply is sensed on GPIO2 through a fixed 1:11 resistor divider
(`BATTERY_DIVIDER_RATIO` in `firmware/src/config.h` — a PCB-fixed ratio, not
a runtime setting like the current-sense shunts). The reading is shown on
the main control page as `battery_v`.

**Low-battery warning:** below `lowBatteryThresholdV` (settings page,
default **11.5V**), the battery readout on the control page turns orange and
bold and appends "LOW BATTERY". This is just a warning — on its own it
doesn't stop you from arming or triggering.

**Low-voltage lockout** (`lowVoltageLockoutEnabled`, default **on**): reuses
the same threshold to actually block operation, independent of the warning
display:

- If the arm switch closes while `battery_v` is under threshold, the device
  stays `DISARMED` instead of entering `COUNTDOWN`.
- If voltage drops below threshold *while already* in `COUNTDOWN` or
  `READY`, the device is forced back to `DISARMED` immediately — the same
  path as the arm switch opening, including aborting any in-flight trigger
  sequence.

Both cases are live, not latched: checked continuously, not just at the
moment the switch closes. If the switch is still closed when voltage drops
too low, the control page shows "Cannot arm — battery too low"; the instant
voltage recovers above threshold, arming proceeds normally on its own,
with no need to open and reclose the switch. Turn the lockout off in
settings to arm anyway regardless of voltage (e.g. bench testing on a
supply that's intentionally below threshold) while keeping the warning
display active.

**Debounce and hysteresis** on the voltage check itself (`arm_state.cpp`),
addressing two failure modes a single raw comparison would otherwise have:

- *Boundary flapping* — a reading oscillating right at the threshold (ADC
  noise, supply ripple) could otherwise flap the lockout in and out rather
  than settling. Once locked out, voltage must recover past
  `lowBatteryThresholdV + 0.3V` (not just past the threshold itself) before
  the lockout clears.
- *Self-induced false trip* — firing an igniter draws real current through
  the same 12V rail being sensed, so a fire pulse can sag `battery_v`
  momentarily. The low-voltage condition must hold steady for 2 seconds
  (comfortably longer than a `FIRE_PULSE_MS` pulse, or a short
  multi-channel burst) before the lockout actually engages, so a firing
  event's own transient sag isn't mistaken for a real low battery.

This is the same debounce shape `SENSEFAILSAFE` already uses for the arm
switch itself, just with a longer window and added hysteresis suited to a
noisier, slower-moving analog signal rather than a mechanical switch.
