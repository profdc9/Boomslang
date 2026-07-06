# Boomslang

A 3-channel ESP32-S3 pyrotechnic/model-rocket ignition controller. Hardware
(KiCad project, schematic, gerbers) is under `Boomslang/`; firmware is under
`firmware/` (PlatformIO, Arduino framework).

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
