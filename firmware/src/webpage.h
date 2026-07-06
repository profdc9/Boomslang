#pragma once

// Main control page — the operational screen: status, per-channel
// checkbox + live continuity/firing state, and TRIGGER/ABORT/PANIC. Timing
// setup (offset/duration) lives on /timing, post-fire peak/average current
// review lives on /stats, so this page stays lean for actual field use.
// Mobile/touch-first: single column, large tap targets, no hover-dependent
// affordances — meant to be operated one-handed on a phone, not a laptop.
//
// Checkbox selection and the trigger button state are rebuilt only once, on
// first load (buildChannels); subsequent polls (updateChannels) only touch
// read-only display bits, so they never fight with in-progress user input
// (a checkbox mid-tap).
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!doctype html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1">
<title>Boomslang</title>
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
  .dot { width:16px; height:16px; border-radius:50%; display:inline-block; flex-shrink:0; }
  .ok { background:#3ecf3e; } .bad { background:#cf3e3e; } .mid { background:#e0c040; }
  .ch-detail { margin-top:10px; display:flex; align-items:center; justify-content:space-between; gap:10px; }
  .status-text { font-size:0.9em; color:#999; }
  button { font-size:1.1em; padding:16px; border-radius:12px; border:none; font-weight:700; width:100%; }
  .trigger-btn { background:#c0392b; color:#fff; margin-top:8px; }
  .trigger-btn:disabled { background:#444; color:#888; }
  .stop-row { display:flex; gap:10px; margin-top:10px; }
  .stop-row button { width:auto; flex:1; }
  .abort-btn { background:#c9821a; color:#fff; }
  .abort-btn:disabled { background:#444; color:#888; }
  .panic-btn { background:#7a0000; color:#fff; }
  .aux { margin-top:14px; }
  .aux button { background:#333; color:#eee; font-size:1em; padding:14px; }
  .battery { font-size:0.9em; color:#999; margin:-4px 0 12px; text-align:center; }
  .battery.low { color:#ff9e5e; font-weight:700; }
</style>
</head>
<body>
<div class="nav"><b>Main</b> &middot; <a href="/timing">Timing</a> &middot; <a href="/stats">Stats</a> &middot; <a href="/config">Settings</a></div>
<h1>Boomslang</h1>
<div id="banner" class="banner disarmed">loading...</div>
<p class="battery" id="battery">Battery: -- V</p>
<div id="channels"></div>
<button class="trigger-btn" id="triggerBtn" disabled onclick="doTrigger()">TRIGGER</button>
<div class="stop-row">
  <button class="abort-btn" id="abortBtn" disabled onclick="doAbort()">ABORT</button>
  <button class="panic-btn" id="panicBtn" onclick="doPanic()">PANIC</button>
</div>
<div class="aux">
  <button onclick="clearFault()">Clear Fault Latch</button>
</div>

<script>
let initialized = false;

async function refresh() {
  let r;
  try { r = await (await fetch('/status.json')).json(); }
  catch (e) { document.getElementById('banner').textContent = 'connection lost'; return; }

  const batteryEl = document.getElementById('battery');
  batteryEl.textContent = r.low_battery
    ? `Battery: ${r.battery_v.toFixed(1)} V — LOW BATTERY`
    : `Battery: ${r.battery_v.toFixed(1)} V`;
  batteryEl.className = 'battery' + (r.low_battery ? ' low' : '');

  const banner = document.getElementById('banner');
  if (r.fault) {
    banner.className = 'banner fault';
    banner.textContent = 'FAULT — overcurrent detected';
    if (r.fault_snapshot_valid) {
      const s = r.fault_snapshot_a;
      banner.textContent += ` (${s[0].toFixed(2)}A ${s[1].toFixed(2)}A ${s[2].toFixed(2)}A)`;
    }
  } else if (r.panic_locked) {
    banner.className = 'banner contwarn';
    banner.textContent = 'PANIC pressed — disarm & rearm required before triggering again.';
  } else if (r.continuity_locked) {
    banner.className = 'banner contwarn';
    banner.textContent = 'Continuity check failed at trigger — nothing fired. Disarm & rearm to clear.';
  } else if (r.arm_continuity_error) {
    banner.className = 'banner contwarn';
    banner.textContent = 'Continuity problem on a selected channel — fix wiring or disable the check in Settings.';
  } else if (r.low_voltage_blocking_arm) {
    banner.className = 'banner contwarn';
    banner.textContent = `Cannot arm — battery too low (${r.battery_v.toFixed(1)} V). Disable the lockout in Settings to override.`;
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

  const canTrigger = r.arm_state === 'ready' && !r.fault && !r.sequence_active &&
                     !r.trigger_locked && !r.continuity_locked && !r.panic_locked &&
                     !r.arm_continuity_error;
  document.getElementById('triggerBtn').disabled = !canTrigger;
  // ABORT only does anything while a sequence is running; PANIC always has
  // an effect (it sets the disarm/rearm requirement even with nothing
  // firing), so it stays enabled regardless of state.
  document.getElementById('abortBtn').disabled = !r.sequence_active;
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
        <span class="dot" id="dot${i}"></span>
      </div>
      <div class="ch-detail">
        <span id="status${i}" class="status-text"></span>
      </div>`;
    div.appendChild(el);
  });
  updateChannels(r);
}

function updateChannels(r) {
  r.channels.forEach((c, i) => {
    const dot = document.getElementById(`dot${i}`);
    dot.className = 'dot ' + ((c.firing || c.scheduled) ? 'mid' : (c.continuity ? 'ok' : 'bad'));
    const state = c.firing ? 'FIRING' : (c.scheduled ? 'scheduled' : (c.continuity ? 'continuity OK' : 'no continuity'));
    document.getElementById(`status${i}`).textContent = `${state} · ${c.last_current_a.toFixed(2)} A`;
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

async function doTrigger() {
  const selected = [];
  for (let i = 0; i < 3; i++) {
    if (document.getElementById(`sel${i}`).checked) selected.push(i + 1);
  }
  if (selected.length === 0) { alert('Select at least one channel first.'); return; }
  if (!confirm('Trigger channel(s) ' + selected.join(', ') + '?')) return;

  const resp = await fetch('/trigger?confirm=1', { method: 'POST' });
  const j = await resp.json();
  if (!j.ok) alert('Not triggered: ' + (j.error || 'unknown error'));
  refresh();
}

// Deliberately no confirm() dialog for either of these — an abort/panic
// button should always act instantly, with zero friction, unlike TRIGGER.
async function doAbort() {
  const resp = await fetch('/abort', { method: 'POST' });
  await resp.json();
  refresh();
}

async function doPanic() {
  const resp = await fetch('/panic', { method: 'POST' });
  await resp.json();
  refresh();
}

async function clearFault() {
  const resp = await fetch('/clear_fault', { method: 'POST' });
  const j = await resp.json();
  if (!j.ok) alert(j.error || 'still faulted');
  refresh();
}

setInterval(refresh, 500);
refresh();
</script>
</body>
</html>
)rawliteral";
