# Contributing to Boomslang

Thanks for considering it. A few things worth knowing before you dive in.

## Read the disclaimer first

This project fires pyrotechnic devices and/or model rocket motors. See the
Disclaimer section near the top of [README.md](README.md) — it applies to
contributions too. If you're changing anything in the arming, fault,
continuity, or trigger-sequencing logic, that's not a "move fast" part of
the codebase. Explain your reasoning in the PR, not just the diff.

## Project layout

- `Boomslang/` — KiCad 7 hardware project (schematic, PCB, gerbers).
  Licensed CC BY-SA 4.0 (`LICENSE-HARDWARE.txt`).
- `firmware/` — ESP32-S3 firmware, PlatformIO + Arduino framework. Licensed
  MIT (`LICENSE`).
- `README.md` — the actual documentation. Every feature in the firmware is
  described there in detail, including *why* it works the way it does, not
  just what it does. If your change affects behavior described there, update
  the corresponding section in the same PR — a stale README is worse than
  no README.

## Building the firmware

```
cd firmware
pio run             # build
pio run -t upload   # flash (board connected)
```

Requires [PlatformIO](https://platformio.org/); `platformio.ini` pins the
board (`esp32-s3-devkitc-1`) and framework.

## Code conventions

Follow what's already there rather than introducing a new style:

- Comments explain *why*, not *what* — the code should be readable enough
  that a comment restating it would be noise. Look at `arm_state.cpp` or the
  fault ISR in `main.cpp` for the kind of comment that's actually earning
  its place (a non-obvious constraint or a hidden invariant, not a
  restatement of the next line).
- New persisted settings follow the exact pattern already used throughout
  `settings.h`/`settings.cpp`: a field with an inline default, a matching
  NVS key in both `loadSettings()` and `saveSettings()`, and validation in
  whichever HTTP handler sets it.
- New web UI additions should fit the existing page split (Main/Timing/
  Stats/Settings, see the README's "Web UI pages" section) rather than
  piling more onto one screen, and stay mobile/touch-first (this is meant
  to be operated one-handed on a phone, not a laptop).
- Don't add a setting, endpoint, or abstraction beyond what the change
  actually needs. This codebase has grown one deliberate feature at a time;
  keep it that way.

## Safety-critical areas

These deserve more scrutiny than a typical PR, both from you and from
review — changes here should come with a clear explanation of what was
checked and what wasn't:

- `main.cpp`: `onFaultISR`, `stopSequence`, the trigger-scheduling pass in
  `loop()`.
- `arm_state.cpp`: the whole state machine, especially anything touching
  the low-voltage lockout's debounce/hysteresis or the trigger/continuity/
  panic lockouts.
- Anything touching pin polarity or default state assumptions (e.g. the
  active-low trigger outputs, or the fail-safe pull-up behavior described
  in the README) — these encode real properties of the hardware, not
  arbitrary choices.

## Testing

There's no unit test framework — verification is `pio run` compiling clean,
plus, where possible, actually exercising the change on real hardware or a
bench setup standing in for it (never a live igniter for anything
current-related). If you can't test on hardware, say so explicitly in the
PR rather than implying it's been verified — "compiles clean, untested on
hardware" is a completely fine thing to write and much better than silence.

## Submitting changes

- Keep PRs focused — one feature or fix at a time, matching the commit
  history's granularity.
- Write commit messages that explain *why*, the same standard as code
  comments. Look at the existing log for the expected level of detail.
- For hardware changes, regenerate the gerbers and schematic/board PDF
  exports so they match what's committed.

## Reporting issues

Include: what you were doing, what you expected, what happened instead,
the firmware commit/version, and (if hardware-related) which board
revision. Logs from the serial monitor are more useful than a description
of what the web UI showed.

## License

By contributing, you agree your contribution is licensed under the same
terms as the part of the project it touches — MIT for `firmware/`, CC BY-SA
4.0 for `Boomslang/`.
