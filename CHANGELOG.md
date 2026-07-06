# Changelog

All notable changes to this project are documented here. Format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

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
