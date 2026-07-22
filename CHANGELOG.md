# Changelog

All notable changes to this project are documented here. Format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Fixed

- False `FAULT` trip on every TRIGGER press with no load connected: a
  switching transient at the moment a trigger pin changes state was
  tripping the shared hardware `FAULT` line even with zero real current
  (confirmed by walking the shunt/comparator/snubber netlist — the RC
  snubber ties drain-to-GND directly, not through the sense shunt, so
  there was no real fault path with nothing connected). Added software
  leading-edge blanking (`writeTriggerPinBlanked()`, `FAULT_BLANKING_US`
  in `config.h`, default 10µs — tuned down from an initial 30µs guess
  after bench testing): a deterministic, interrupt-disabled
  critical section around each deliberate trigger-pin write that filters
  the transient without touching the actual per-channel hardware
  protection (which runs in pure analog and was never affected either
  way). Explicitly clears the GPIO's latched interrupt-status bit before
  re-enabling interrupts, since a masked-but-resolved edge would
  otherwise still fire retroactively.
- `writeTriggerPinBlanked()`'s fallback path (a fault still asserted after
  the blanking window) now also captures its own `fault_snapshot_a`
  diagnostic reading, instead of leaving `fault_snapshot_valid` false.
  Unlike `onFaultISR()`, this path runs in normal task context, so it can
  safely call `analogRead()` directly rather than relying on
  `faultSampleTask`'s ISR-notification handoff, which nothing here was
  waking.
- Low-voltage arm lockout could trip (or fail to) at a voltage that
  disagreed with the displayed reading: `arm_state.cpp`'s
  `sampleBatteryVoltage()` — used for the actual lockout decision — never
  got the `BATTERY_DIODE_DROP_V` correction added to `main.cpp`'s
  `readBatteryVoltage()` (used for the displayed `battery_v`), so a
  "Cannot arm — battery too low" message could show a voltage well above
  the threshold it had supposedly tripped. Both readings now apply the
  same correction.
- Main/Timing pages could show stale checked channel checkboxes after a
  device reboot: channel selection was never persisted (by design), but
  a browser tab left open across a restart kept showing its pre-restart
  checked state, since checkboxes were only synced from the server once,
  on first page load. `/status.json` now includes `uptime_ms`; both pages
  detect it dropping (proof of a restart) and rebuild the channel list
  from server truth on the next poll.
- A `FAULT` didn't require a disarm+rearm before retriggering: only the
  immediate `faultLatched` flag blocked triggering, and that cleared as
  soon as the fault itself resolved — with nothing else requiring the
  arm switch to actually be cycled first, unlike every other lockout
  (PANIC, pre-trigger continuity failure, arm timeout). Added a new
  sticky `faultLockedOut()`/`notifyFaultOccurred()` pair (`arm_state.cpp`,
  same shape as the PANIC lockout), set the instant any fault latches and
  cleared only at the same disarm→rearm transition every other lockout
  uses.
- Removed the "Clear Fault Latch" button and `/clear_fault` endpoint.
  `loop()` now auto-clears a latched fault the instant the device is
  `DISARMED` and the hardware line re-verifies clear (same multi-sample
  ~2ms re-check the old endpoint did) — never while armed. Since a fresh
  disarm+rearm is already required before retriggering (see the lockout
  above), tying the clear to the disarm half of that cycle removes a
  redundant manual step, and incidentally removes an ordering ambiguity
  the manual button had (rearming before vs. after actually clicking
  clear).
- Fixed a real gap in `writeTriggerPinBlanked()`'s fallback (the path that
  catches a fault occurring exactly at a trigger-pin write, e.g. a short
  present before a channel ever turns on): it only set the sticky fault
  flags and relied on `loop()`'s later `stopSequence()` call to shut off
  *other* channels, reached only after that iteration's other work
  (continuity checks, etc.). With a real load on one channel and a short
  on another, the loaded channel could keep firing longer than intended.
  Now forces every trigger pin off immediately via the same atomic
  register write `onFaultISR()` uses, closing the gap the same way the
  ISR always has.
- `FAULT_BLANKING_US` bench-validated at 10µs (down from 30µs, briefly
  tried at 0µs): at 0, the comparator's own output hadn't necessarily
  finished settling by the time `writeTriggerPinBlanked()` checked it, so
  a genuine fault could be missed by that function entirely (caught later
  by a different path with no diagnostic snapshot) rather than filtered.
  Also reordered that function's fallback to sample SENSE *before* forcing
  the shutoff above, rather than after: `analogRead()` is not
  deterministic (the ADC driver power-cycles the whole peripheral and
  takes an internal mutex on every call), and forcing shutoff first was
  found to reliably collapse the diagnostic reading to 0A. Sampling first
  trades a small amount of additional latency before other channels are
  forced off (bounded well under the ~1ms SOA tolerance) for a snapshot
  that's actually meaningful.

### Added

- `DEBUG_DISABLE_FAULT_SHUTOFF` bench-debug build flag (`config.h`,
  default off): used while diagnosing the above, lets an in-progress
  pulse keep running through a `FAULT` trip instead of being cut short,
  for observing behavior with no load connected. Does not and cannot
  affect the real hardware protection.
- Buzzer volume control (`speakerVolume`, settings page, 0-10, default
  10): maps linearly to 0-50% PWM duty cycle. Required moving the buzzer
  off `tone()`/`noTone()` entirely — `tone()` always forces a fixed 50%
  duty cycle internally, incompatible with variable volume — onto direct
  `ledcChangeFrequency()`/`ledcWrite()` calls that set frequency and duty
  independently, each only re-issued on an actual change (same reasoning
  as the earlier choppy-buzzer fix). Takes effect live while armed, no
  rearm needed.

### Changed

- Current-sense shunt resistance (`senseOhms`, settings page) valid range
  widened from 0.001-10Ω to 0.01-100Ω, to accommodate a wider range of
  field-replaced shunt values.

## [0.1.3] - 2026-07-21

### Added

- Live FAULT line indicator on the main control page (`fault_pin_active`
  in `/status.json`), a raw `digitalRead(PIN_FAULT)` taken on every poll —
  distinct from the existing latched `fault` flag, which only clears via
  Clear Fault Latch.

### Fixed

- Battery voltage reading now accounts for the series diode ahead of the
  sense divider, adding back its ~0.7V forward drop
  (`BATTERY_DIODE_DROP_V`) so `battery_v` reflects true battery voltage
  rather than reading low.
- Choppy/glitchy arming buzzer: `tone()` was being re-issued on every
  `loop()` iteration (unthrottled, up to thousands of times/sec) instead
  of only when the tone actually starts, stops, or changes frequency.
  `tone()` reprograms the LEDC PWM peripheral each call, which resets
  phase and clicks audibly — now edge-triggered.

### Changed

- Continuity/arm-loop LED-clamp sense threshold (`LED_CLAMP_ARM_VOLTS`)
  lowered from 1.5V to 1.25V to better match the red LED's actual forward
  voltage.

## [0.1.2] - 2026-07-14

### Added

- Arm timeout auto-lockout: if `READY` is held longer than a configurable
  `armTimeoutSec` (default 600s, 0 disables) without triggering, TRIGGER is
  refused until a fresh disarm and rearm, even though the arm switch is
  still physically closed. Software-only, same lockout family as PANIC;
  does not touch the physical arm loop or the buzzer/strobe pattern.

## [0.1.1] - 2026-07-06

### Added

- Hardware: RC snubbers on each channel's IRLZ44N MOSFET drain, to damp
  switching transients from the igniter/wiring inductance.

## [0.1.0] - 2026-07-06

Initial release.

### Added

- Hardware: initial KiCad design (schematic, PCB, gerbers) for a 3-channel
  ESP32-S3 pyrotechnic/model-rocket ignition controller.
- Firmware: WiFi AP + mobile-first web control page, per-channel firing,
  current sensing, and continuity/fault sensing.
- Hardware overcurrent fault ISR that forces all three trigger outputs high
  immediately on a shared FAULT signal, with a notified FreeRTOS task
  sampling peak firing current for post-fault diagnostics without touching
  the time-critical shutoff path.
- Persistent, flash-backed (NVS) settings with a dedicated web settings
  page — every configurable value in this changelog is one of these.
- Arm/failsafe state machine (`DISARMED` → `COUNTDOWN` → `READY`) with a
  configurable countdown, distinct audible/visible indication per state,
  and a trigger-sequence firing model (per-channel selection + delay).
- Continuity-based channel selection gating: a live, self-clearing monitor
  while armed, and a stricter pre-trigger check with its own disarm/rearm
  lockout, independent of each other and of the post-fire rearm lockout.
- Battery voltage sensing (GPIO2, resistor divider), a configurable
  low-battery warning, and a low-voltage lockout — with debounce and
  hysteresis — that blocks arming or forces a disarm if voltage drops while
  already armed.
- Continuous audible tone while a trigger sequence is running (distinct
  from the normal pulsed arming tone), plus ABORT (stops firing, no
  lockout) and PANIC (stops firing, unconditional disarm/rearm lockout)
  buttons.
- Per-channel offset and duration in milliseconds (replacing a single
  fixed global pulse width), plus peak/average current registers recorded
  per channel for each pulse.
- Split the web UI into four pages: Main (operational), Timing (offset/
  duration setup), Stats (peak/average current review), and Settings.
- Configurable WiFi SSID/password (default `boomslang` / `liftoff!`),
  replacing hardcoded credentials.
- Hardware revision: GPIO2 battery-voltage sense breakout, GPIO12-14
  broken out as spare test points.
- Dual licensing (MIT for firmware, CC BY-SA 4.0 for hardware design
  files) and a use-at-your-own-risk disclaimer.
- `CONTRIBUTING.md` and README license badges.
- Factory-reset pin (GPIO14, internally pulled up, checked once at boot):
  grounding it before power-up resets every setting to its compiled-in
  default.
- WiFi relay (station) mode (default off): joins an existing router as a
  client instead of hosting its own AP, for more distance between operator
  and pyrotechnics, falling back to hosting its own AP if that connection
  fails so the device is never left unreachable. Enabling it requires
  typing the literal word "relay" into a settings-page text field rather
  than a checkbox, since it means anyone who can reach that external
  network can also reach this device. mDNS (`boomslang.local`) now starts
  in either mode.
