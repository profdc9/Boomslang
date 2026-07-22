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
  in `config.h`, default 30µs): a deterministic, interrupt-disabled
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

### Added

- `DEBUG_DISABLE_FAULT_SHUTOFF` bench-debug build flag (`config.h`,
  default off): used while diagnosing the above, lets an in-progress
  pulse keep running through a `FAULT` trip instead of being cut short,
  for observing behavior with no load connected. Does not and cannot
  affect the real hardware protection.

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
