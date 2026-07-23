#pragma once

// Timing setup screen: which channels are selected (mirrors the same
// checkbox/endpoint as the main page — set up selection and timing
// together, or double-check it later), each selected channel's offset and
// duration in milliseconds, and optional PWM cycling (Hz/duty%) during
// that duration for a lower average heating rate than a full-on pulse. No
// live operational state (continuity, firing) and no TRIGGER/ABORT/PANIC
// here — this is "what will happen and when," not the operational screen.
const char TIMING_HTML[] PROGMEM = R"rawliteral(
<!doctype html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1">
<title>Boomslang Timing</title>
<style>
  * { box-sizing: border-box; }
  body { font-family: -apple-system, sans-serif; background:#111; color:#eee; margin:0; padding:12px; font-size:17px; }
  h1 { font-size:1.5em; margin:0 0 8px; }
  a { color:#8ab4ff; }
  .nav { font-size:0.9em; text-align:center; margin-bottom:10px; }
  .nav b { color:#eee; }
  .banner { padding:14px; border-radius:10px; margin-bottom:12px; font-weight:700; text-align:center; font-size:1.05em; }
  .disarmed { background:#4d3b1b; color:#ffdca6; }
  .countdown { background:#4d4a1b; color:#fff3a6; }
  .ready { background:#1b4d1b; color:#a6ffa6; }
  .fault { background:#5a1414; color:#ffb3b3; }
  .contwarn { background:#5a3a14; color:#ffcf9e; }
  .channel { background:#1c1c1c; border-radius:12px; padding:14px; margin-bottom:10px; }
  .ch-top { display:flex; align-items:center; gap:12px; }
  .ch-top label { display:flex; align-items:center; gap:10px; font-size:1.1em; font-weight:600; flex:1; }
  input[type=checkbox] { width:28px; height:28px; flex-shrink:0; }
  .ch-detail { margin-top:10px; display:flex; align-items:center; justify-content:space-between; gap:10px; }
  .ch-detail input[type=number] { width:92px; padding:10px; border-radius:8px; border:1px solid #444; background:#0c0c0c; color:#eee; font-size:1.05em; }
</style>
</head>
<body>
<div class="nav"><a href="/">Main</a> &middot; <b>Timing</b> &middot; <a href="/stats">Stats</a> &middot; <a href="/config">Settings</a></div>
<h1>Boomslang Timing</h1>
<div id="banner" class="banner disarmed">loading...</div>
<div id="channels"></div>

<script>
let initialized = false;
let lastUptimeMs = null;

async function refresh() {
  let r;
  try { r = await (await fetch('/status.json')).json(); }
  catch (e) { document.getElementById('banner').textContent = 'connection lost'; return; }

  // See webpage.h for why: uptime_ms dropping means the device restarted,
  // and channel selection isn't persisted across a boot, so a stale
  // checked checkbox from before the restart needs to be rebuilt from
  // server truth rather than left as-is.
  if (lastUptimeMs !== null && r.uptime_ms < lastUptimeMs) {
    initialized = false;
  }
  lastUptimeMs = r.uptime_ms;

  const banner = document.getElementById('banner');
  if (r.fault) {
    banner.className = 'banner fault';
    banner.textContent = 'FAULT — overcurrent detected';
  } else if (r.panic_locked) {
    banner.className = 'banner contwarn';
    banner.textContent = 'PANIC pressed — disarm & rearm required before triggering again.';
  } else if (r.fault_locked) {
    banner.className = 'banner contwarn';
    banner.textContent = 'FAULT occurred — disarm & rearm required before triggering again.';
  } else if (r.continuity_locked) {
    banner.className = 'banner contwarn';
    banner.textContent = 'Continuity check failed at trigger — nothing fired. Disarm & rearm to clear.';
  } else if (r.arm_continuity_error) {
    banner.className = 'banner contwarn';
    banner.textContent = 'Continuity problem on a selected channel — fix wiring or disable the check in Settings.';
  } else if (r.low_voltage_blocking_arm) {
    banner.className = 'banner contwarn';
    banner.textContent = `Cannot arm — battery too low (${r.battery_v.toFixed(1)} V).`;
  } else if (r.arm_state === 'ready') {
    banner.className = 'banner ready';
    banner.textContent = r.trigger_locked
      ? 'READY — disarm & rearm required before next trigger'
      : 'ARMED & READY';
  } else if (r.arm_state === 'countdown') {
    banner.className = 'banner countdown';
    banner.textContent = `ARMING — ${r.countdown_remaining_s.toFixed(0)}s until ready`;
  } else {
    banner.className = 'banner disarmed';
    banner.textContent = 'DISARMED — close arm key at J5';
  }

  if (!initialized) {
    buildChannels(r);
    initialized = true;
  } else {
    updateChannels(r);
  }
}

function buildChannels(r) {
  const div = document.getElementById('channels');
  div.innerHTML = '';
  r.channels.forEach((c, i) => {
    const el = document.createElement('div');
    el.className = 'channel';
    el.innerHTML = `
      <div class="ch-top">
        <label>
          <input type="checkbox" id="sel${i}" ${c.selected ? 'checked' : ''} onchange="saveSelect(${i})">
          Channel ${i + 1}
        </label>
      </div>
      <div class="ch-detail">
        <span>offset (ms) <input type="number" id="delay${i}" step="100" min="0" max="60000" value="${c.delay_ms}" inputmode="numeric" onchange="saveDelay(${i})"></span>
        <span>duration (ms) <input type="number" id="dur${i}" step="100" min="0" max="30000" value="${c.duration_ms}" inputmode="numeric" onchange="saveDuration(${i})"></span>
      </div>
      <div class="ch-detail">
        <span>PWM (Hz, 0=off) <input type="number" id="pwmHz${i}" step="1" min="0" max="1000" value="${c.pwm_hz}" inputmode="numeric" onchange="savePwmHz(${i})"></span>
        <span>PWM duty (%) <input type="number" id="pwmDuty${i}" step="5" min="0" max="100" value="${c.pwm_duty_percent}" inputmode="numeric" onchange="savePwmDuty(${i})"></span>
      </div>`;
    div.appendChild(el);
  });
  updateChannels(r);
}

function updateChannels(r) {
  r.channels.forEach((c, i) => {
    document.getElementById(`sel${i}`).disabled = r.selection_locked;
  });
}

async function saveSelect(i) {
  const checked = document.getElementById(`sel${i}`).checked;
  const resp = await fetch(`/select?ch=${i}&selected=${checked ? '1' : '0'}`, { method: 'POST' });
  const j = await resp.json();
  if (!j.ok) {
    alert(j.error || 'could not change selection');
    document.getElementById(`sel${i}`).checked = !checked;  // revert on refusal
  }
}

async function saveDelay(i) {
  const v = document.getElementById(`delay${i}`).value;
  await fetch(`/channel_delay?ch=${i}&delay_ms=${v}`, { method: 'POST' });
}

async function saveDuration(i) {
  const v = document.getElementById(`dur${i}`).value;
  await fetch(`/channel_duration?ch=${i}&duration_ms=${v}`, { method: 'POST' });
}

async function savePwmHz(i) {
  const v = document.getElementById(`pwmHz${i}`).value;
  await fetch(`/channel_pwm_hz?ch=${i}&pwm_hz=${v}`, { method: 'POST' });
}

async function savePwmDuty(i) {
  const v = document.getElementById(`pwmDuty${i}`).value;
  await fetch(`/channel_pwm_duty?ch=${i}&pwm_duty=${v}`, { method: 'POST' });
}

setInterval(refresh, 500);
refresh();
</script>
</body>
</html>
)rawliteral";
