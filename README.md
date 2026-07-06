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

## Trigger sequence

Firing isn't one button per channel — the main control page lets you select
which channels to fire and, per channel, how long after pressing TRIGGER
that channel should fire, then fires the whole configured sequence off a
single press.

**Per channel, on the main page:**
- A checkbox to include that channel in the next trigger. **Not** persisted
  — it resets on every page load, so a stale "everything selected" state
  from a previous session can never carry over silently.
- A delay (seconds, default 0) from the TRIGGER press to that channel's fire
  pulse. This **is** persisted, so a fixed show sequence doesn't need
  retyping every session.

**Pressing TRIGGER** is only possible when `arm_state` is `READY`, there's no
active fault, no sequence is already running, and (if
`requireRearmAfterFire` is on) the device hasn't fired since it was last
armed. It schedules every selected channel to fire at
`now + its configured delay`, then immediately blocks further TRIGGER
presses until that sequence finishes (you can never have two sequences in
flight at once, regardless of settings).

**At each channel's actual fire moment** — not just when TRIGGER was pressed
— the firmware re-checks armed state, fault, and continuity. A multi-second
delay is long enough for something to change, so a channel that fails this
recheck is skipped (and logged) rather than aborting the rest of the
sequence. If the arm switch is opened mid-sequence, the gate drivers lose
power outright regardless of what firmware does — this recheck is
belt-and-suspenders, not the actual safety mechanism.

**`requireRearmAfterFire`** (settings page, default **on**): once a trigger
sequence has run, no further TRIGGER is accepted until the arm switch is
opened and closed again (a fresh disarm + countdown + `READY`). With it off,
another TRIGGER is accepted as soon as the current sequence finishes, no
rearm needed.

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
