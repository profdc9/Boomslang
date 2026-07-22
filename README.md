# Boomslang

[![Firmware License: MIT](https://img.shields.io/badge/firmware%20license-MIT-yellow.svg)](LICENSE)
[![Hardware License: CC BY-SA 4.0](https://img.shields.io/badge/hardware%20license-CC%20BY--SA%204.0-lightgrey.svg)](LICENSE-HARDWARE.txt)

A 3-channel ESP32-S3 pyrotechnic/model-rocket ignition controller. Hardware
(KiCad project, schematic, gerbers) is under `Boomslang/`; firmware is under
`firmware/` (PlatformIO, Arduino framework).

## ⚠️ Disclaimer — use at your own risk

This project fires pyrotechnic devices and/or model rocket motors. Incorrect
wiring, misconfiguration, a firmware bug, a hardware fault, or plain
operator error can cause unintended ignition, fire, injury, or property
damage. **This is a hobbyist project, provided AS-IS, with NO WARRANTY OF
ANY KIND** — express or implied, including no warranty of merchantability,
fitness for a particular purpose, or that it is safe, correct, reliable, or
free of defects.

By building, modifying, or using this hardware or firmware, you accept full
responsibility for:

- Verifying correct operation yourself, on your own hardware, before relying
  on any of it — including every safety feature described in this README.
- Complying with all applicable laws, regulations, and permitting
  requirements for pyrotechnics, explosives, and/or model rocketry in your
  jurisdiction.
- Following standard range-safety practice: safe distances, no bystanders
  in range, appropriate PPE, a real mechanical arm switch you trust, and
  never treating any software feature here as a substitute for that.

The author(s) and contributors accept **no liability** for any injury,
death, damage, or loss arising from building, modifying, or using this
project, to the fullest extent permitted by law.

## License

Dual-licensed by design, since this repo mixes source code and hardware
design files:

- **Firmware** (`firmware/`) is licensed under the [MIT License](LICENSE).
- **Hardware design files** (`Boomslang/` — KiCad schematic, PCB, gerbers)
  are licensed under [CC BY-SA 4.0](LICENSE-HARDWARE.txt).

Both are permissive and allow commercial use, but neither one waives the
disclaimer above — "you can use/modify/redistribute this" is a separate
question from "this is safe or fit for any particular purpose."

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

**Relay (station) mode** (`wifiStationMode`, default **off**): instead of
hosting its own AP, the device can join `wifiSsid`/`wifiPassword` as a
client of an existing router, to relay through it when the operator needs
more distance from the pyrotechnics than the device's own AP range allows.
If it can't connect within 15 seconds (wrong password, out of range, router
down), it falls back to hosting its own AP instead — so a bad relay network
never leaves you locked out of the device with no way to disarm or abort.

This setting is deliberately **not a checkbox** on the settings page:
enabling it means anyone who can reach that external network can also reach
this device, so it requires typing the literal word `relay` into a text
box rather than a single accidental tap — blank or anything else both mean
"stay off, host my own AP" (the safe default), including if the field is
missing from the request entirely.

**Before you enable this, secure that router/network yourself — it's now
part of your access control, not just Boomslang's own password.** In its
own AP mode, WPA2 on the device is the entire perimeter. In relay mode,
that perimeter is whatever the router enforces: WPA2/WPA3 with a strong
password of its own, no other untrusted devices or guests on it, and
ideally a network used for nothing but this. A relay network that's open,
weakly secured, or shared with devices you don't control gives *anyone on
it* the same access as someone standing next to the device with its own
password — arming and firing included.

Either way — hosting an AP or joined as a station — the device also starts
an mDNS responder, so `http://boomslang.local` works regardless of which
mode ended up active or what IP a router's DHCP server happened to assign.

## Web UI pages

Four pages, all polling `/status.json` and posting to the same handful of
endpoints — the split below is purely how the browser organizes things, not
a backend/data-model split. Each has a small nav row (Main · Timing · Stats
· Settings) so it's obvious how to get back to the others.

- **`/` (Main)** — the operational screen, meant for actual field use:
  arm/fault/lockout status, battery voltage, each channel's checkbox + live
  continuity/firing state, and TRIGGER/ABORT/PANIC. Deliberately lean —
  timing setup and post-fire review live elsewhere so this screen stays fast
  to read and tap through during an actual arm/fire sequence.
- **`/timing`** — setup screen: the same channel checkboxes as Main (same
  `/select` endpoint — selection can be changed from either page) plus each
  channel's offset/duration in milliseconds (see Trigger sequence below). No
  live operational state and no TRIGGER/ABORT/PANIC here.
- **`/stats`** — read-only review: peak and average current per channel
  from its most recent pulse (see Trigger sequence below).
- **`/config` (Settings)** — every persisted setting described throughout
  this README.

## Overcurrent fault protection

Each channel's IRLZ44N MOSFET has its own analog overcurrent comparator
(transistors Q5/Q10/Q16, sensing the 0.05Ω source shunt) that pulls that
channel's own gate to GND in hardware, in nanoseconds, with **no firmware
involved at all**. That's the actual protection for the transistor. All
three channels' comparators also pull a single shared, open-collector,
active-LOW `FAULT` line (GPIO1) — since it's wired-OR across all three,
firmware can tell *that* a channel faulted, but not *which* one.

The 0.05Ω figure above is just the schematic default (R1/R17/R31) — the
per-channel shunt resistance (`senseOhms`, settings page) is a runtime
setting used for the firing-current math (`readCurrentA()`), independently
of the comparator's own fixed hardware trip point. Valid range is
**0.01Ω–100Ω**, to accommodate a field-replaced shunt of a different value.

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
in the FAULT banner on the control page. If the banner reads "FAULT —
overcurrent detected" with no `(X.XXA Y.YYA Z.ZZA)` after it, that means
`fault_snapshot_a` was never populated for this event — either
`writeTriggerPinBlanked()`'s own fallback latched the fault directly
(covered above; it takes its own snapshot too, since it's normal task
context rather than ISR context and can safely call `analogRead()`
itself, unlike `onFaultISR()`), or a fault was latched some other way that
doesn't notify `faultSampleTask` (the boot-time check or `loop()`'s
defensive backstop poll, for example).

**Clearing a fault**: `faultLatched` is a software latch, not
self-clearing — `/clear_fault` re-checks the hardware `FAULT` line itself
(several reads over ~2ms, so a line chattering right at the trip point
doesn't get cleared into a still-live fault) and refuses if it's still
asserted. A reboot doesn't clear a persistent fault either: `FAULT` is
checked at boot, before the ISR is even attached, and latches immediately
if still asserted.

**Live FAULT line indicator**: the main control page also shows a
`FAULT line: clear/ASSERTED` readout (`fault_pin_active` in
`/status.json`, a plain `digitalRead(PIN_FAULT)` taken fresh on every
poll), separate from the latched `fault` flag above. Since `faultLatched`
only clears via `/clear_fault`, this raw reading is what tells you whether
the hardware comparators are asserting the line *right now* — useful for
telling "latched from a past event, line's gone high again" apart from
"still actively faulted."

**Leading-edge blanking on trigger-pin switching**: on real hardware,
pressing TRIGGER with no igniter/load connected was found to produce an
immediate `FAULT` with a 0A current snapshot — a switching transient at the
moment a trigger pin changes state, not a real overcurrent event (confirmed
by walking the actual shunt/comparator/snubber netlist: the RC snubber ties
drain-to-GND directly, not through the sense shunt, so there's no real
current path for a genuine fault with nothing connected). `firmware/src/
main.cpp`'s `writeTriggerPinBlanked()` now wraps the two deliberate
trigger-pin writes (fire-start in `startFirePulse()`, and the normal
pulse-end release in `loop()`) in a short critical section
(`FAULT_BLANKING_US`, `config.h`, default 10µs — tuned down from an
initial 30µs guess after bench testing confirmed the real switching
transient clears well within that window): interrupts are disabled
for that window, the pin is written, and `FAULT` is checked once directly
afterward — if it's already resolved, that reading is trusted and the
event is treated as filtered, not a fault.

Two subtleties this required getting right, since a plain
`delayMicroseconds()`-based wait would not have been safe here:

- `noInterrupts()`/`interrupts()` (not `detachInterrupt()`/
  `attachInterrupt()` + a bare delay) bounds the window deterministically —
  a plain busy-wait doesn't block task preemption, so a higher-priority
  task/ISR becoming ready mid-wait could otherwise extend the real gap well
  past `FAULT_BLANKING_US`. `delayMicroseconds()` itself is safe to call
  with interrupts disabled: it polls `micros()`, which reads a free-running
  hardware counter that keeps advancing regardless of the core's
  interrupt-enable state — unlike `delay()`/`vTaskDelay()`, which actually
  depend on the tick interrupt to wake back up, and would hang here.
- Disabling interrupts at the CPU level only defers *servicing* — it
  doesn't stop the GPIO peripheral's own edge-detector from latching a
  FALLING edge into its interrupt-status register during the window. Left
  alone, a transient that resolves *inside* the window would still fire
  `onFaultISR()` retroactively the instant interrupts are re-enabled, for
  an edge that's already history. `writeTriggerPinBlanked()` explicitly
  clears that latched status bit (`GPIO_STATUS_W1TC_REG`) before
  re-enabling, so a resolved transient is actually discarded.

None of this touches the real transistor protection described above — the
per-channel analog comparators keep working purely in hardware throughout,
completely independent of whether this interrupt is masked. What's
filtered is only the shared-line firmware echo (forced shutoff + the
sticky latch) reacting to a switching artifact instead of a real fault.

Why a plain level-1 interrupt is enough: the IRLZ44N's SOA tolerates ~1ms at
full overcurrent, and even level-1 ISR latency on this chip is reliably
single-digit microseconds — comfortably inside that margin, especially
given the hardware analog protection has already started cutting current
before the ISR even runs. A `DEBUG_FAULT_TIMING` build flag (on by default
in `config.h`) toggles a spare pin (GPIO13) around ISR entry and the end of
SENSE sampling, so real end-to-end latency can be measured on a scope once
hardware exists.

**`DEBUG_DISABLE_FAULT_SHUTOFF`** (`config.h`, default **off**): a
bench-debug-only build flag used while diagnosing the switching transient
above. When on, it skips firmware's forced-HIGH/`stopSequence()` echo on a
fault, letting an in-progress pulse keep running through a `FAULT` trip
instead of being cut short — useful for observing what's actually
happening with no load connected. `faultLatched` is still set and still
blocks new triggers until `/clear_fault`, same as always; only the forced
shutoff of an already-running pulse is skipped. It does **not** and cannot
affect the real per-channel hardware protection described above, which
runs in pure analog and has no firmware involvement at all. Must be off
for any real use with igniters/pyrotechnics connected.

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

**Tone stability:** `updateArmState()` runs every `loop()` iteration
(unthrottled, so potentially thousands of times per second), but only
touches the buzzer's LEDC output on an actual change — turning on, turning
off, the frequency changing, or the volume changing — rather than every
iteration. Arduino's `tone()`/`noTone()` aren't used here: `tone()`
reprograms the ESP32's LEDC PWM peripheral and resets phase on every call
(producing an audible click if called continuously — the original cause of
a choppy/glitchy buzzer sound), and it always drives a fixed 50% duty
cycle, which rules it out for volume control (below) regardless. Frequency
and duty are set independently instead, via `ledcChangeFrequency()`/
`ledcWrite()` (`AUDIBLE_LEDC_CHANNEL`, `config.h`).

**Volume:** `speakerVolume` (settings page, 0-10, default **10**) maps
linearly to 0-50% PWM duty cycle — 50% is as loud as a square-wave drive
gets (maximum RMS), so 10 reproduces the same loudness `tone()` always
used before this was configurable. Recomputed every `updateArmState()`
call, so changing it takes effect immediately, live, with no disarm/rearm
needed.

**Settings:** each channel can be silenced independently via the settings
page (`/config` on the web UI):

- `visibleWhenArmed` — strobe LEDs flash during `COUNTDOWN`/`READY` (default **on**)
- `audibleWhenArmed` — buzzer sounds during `COUNTDOWN`/`READY` (default **on**)
- `speakerVolume` — buzzer loudness, 0-10 (default **10**, see above)

All are persisted to flash (survive power cycles) and, like all settings
changes, can only be saved while the device is disarmed.

## Trigger sequence

Firing isn't one button per channel — the main control page lets you select
which channels to fire and, per channel, how long after pressing TRIGGER
that channel should fire, then fires the whole configured sequence off a
single press.

**Per channel:**
- A checkbox, on both the Main and Timing pages, to include that channel in
  the next trigger. **Not** persisted — it resets on every page load, so a
  stale "everything selected" state from a previous session can never carry
  over silently.
- An **offset** in milliseconds (0-60000, default 0), on the Timing page,
  from the TRIGGER press to that channel's trigger output going low.
- A **duration** in milliseconds (0-30000, default 500), also on the Timing
  page, that the trigger output then stays low.

Offset and duration are both persisted settings
(`channelDelayMs`/`channelDurationMs`), saved to flash the moment you change
either one — same as every other setting, they survive a power cycle, so a
fixed show sequence doesn't need retyping every session. They're plain
milliseconds rather than seconds specifically for finer-grained control over
short pulses. Also same as every other setting: saving to flash requires
the device to be disarmed — a change made while armed still applies
immediately in RAM for that session, but won't survive a reboot until you
disarm and it actually gets persisted.

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
`/status.json`, shown per channel on the `/stats` page) for the rest of the
session, until that channel fires again.

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

## Arm timeout (auto-lockout)

Guards against "armed and walked away": if the device sits in `READY` —
armed, past the countdown, but never actually triggered — for longer than a
configurable timeout, TRIGGER is refused until a fresh disarm and rearm,
even though the arm switch (`J5`) is still physically closed.

**`armTimeoutSec`** (settings page, default **600s / 10 minutes**, 0
disables it): the clock starts the instant `COUNTDOWN` transitions to
`READY`, not when the switch was first closed, so a long
`armCountdownSec` doesn't eat into the READY window. Once it elapses, the
control page shows "Arm timeout elapsed — disarm & rearm required before
triggering again" and TRIGGER is disabled, the same way it is for
`panicLockedOut()` — cleared at the same point every other lockout is: the
arm switch observed open, then closed again.

This is a **software-only** lockout, same family as PANIC and the
continuity/rearm lockouts above: it cannot re-power the gate drivers on its
own, and it cannot cut them either. It only blocks the firmware's own
`/trigger` handler. The hardware arm switch remains the actual power
interlock regardless of this setting — the buzzer/strobe pattern keeps
following `READY` normally throughout, since electrically the device really
is still armed.

## Continuity checks

`CONTINUITY1/2/3` sense the same way `SENSEFAILSAFE` does: ~1mA through a red
LED, whose forward voltage exceeds `LED_CLAMP_ARM_VOLTS` (1.25V,
`firmware/src/config.h`) when the loop (the igniter, in this case) is
actually closed. That threshold (`CONTINUITY_OK_RAW` in the same file) is
shared with the arm-loop sense, since it's the identical circuit at the
identical target voltage.

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
a runtime setting like the current-sense shunts), after passing through a
series diode ahead of the divider. `readBatteryVoltage()` (`main.cpp`) adds
back that diode's forward drop (`BATTERY_DIODE_DROP_V`, 0.7V, also a
compile-time constant — it's a fixed part characteristic, not field-
adjustable) so `battery_v` reflects true battery voltage, not the
diode-reduced rail. The reading is shown on the main control page as
`battery_v`.

`arm_state.cpp`'s `sampleBatteryVoltage()` — a separate, independent
reading of `PIN_BATTERY` used specifically for the low-voltage lockout
decision below — applies the same `BATTERY_DIODE_DROP_V` correction. The
two readings need to agree: without it, the lockout was comparing an
uncorrected (0.7V-low) value against the threshold while the banner showed
the corrected `battery_v`, so the displayed voltage in a "Cannot arm —
battery too low" message could be well above the threshold it had
supposedly tripped.

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
  (comfortably longer than a typical `channelDurationMs` pulse, or a short
  multi-channel burst) before the lockout actually engages, so a firing
  event's own transient sag isn't mistaken for a real low battery.

This is the same debounce shape `SENSEFAILSAFE` already uses for the arm
switch itself, just with a longer window and added hysteresis suited to a
noisier, slower-moving analog signal rather than a mechanical switch.

## Factory reset

`PIN_FACTORY_RESET` (GPIO14) is pulled up internally and checked once,
early in `setup()`, before settings are loaded. Ground it — a jumper or
button to GND — before powering the board up, and every setting resets to
its compiled-in default and is immediately persisted to flash, overwriting
whatever was there. It's checked with a few reads over a couple of
milliseconds rather than a single instantaneous one, so a pin still
settling right after `pinMode()` doesn't trigger a reset by accident.

This only runs at boot, from a floating/undriven starting point — there's
no runtime "hold this pin for 10 seconds" monitor. GPIO14 has no strapping
or other special function on the ESP32-S3 (unlike GPIO12 on the *original*
ESP32, a common source of outdated advice online — that specific gotcha
doesn't carry over to the S3), and is otherwise unconnected on the PCB.
