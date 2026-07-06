#pragma once

// Settings page. Loads current values from /config.json, posts changes to
// /config (query-string args, same style as /trigger), and shows arm state
// since saveSettings() refuses to write to flash while armed. Mobile/touch
// sizing matches the main control page.
const char CONFIG_HTML[] PROGMEM = R"rawliteral(
<!doctype html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1">
<title>Boomslang Settings</title>
<style>
  * { box-sizing: border-box; }
  body { font-family: -apple-system, sans-serif; background:#111; color:#eee; margin:0; padding:12px; font-size:17px; }
  h1 { font-size:1.4em; margin:0 0 8px; }
  a { color:#8ab4ff; }
  .banner { padding:14px; border-radius:10px; margin-bottom:14px; font-weight:700; text-align:center; }
  .armed { background:#4d3b1b; color:#ffdca6; }
  .disarmed { background:#1b4d1b; color:#a6ffa6; }
  .card { background:#1c1c1c; border-radius:12px; padding:14px; margin-bottom:12px; }
  .hint { color:#999; font-size:0.85em; margin-bottom:10px; }
  label { display:flex; align-items:center; justify-content:space-between; gap:10px; margin:14px 0 4px; font-size:1em; color:#ccc; }
  input[type=number] { width:110px; box-sizing:border-box; padding:10px; border-radius:8px; border:1px solid #444; background:#0c0c0c; color:#eee; font-size:1.05em; }
  input[type=checkbox] { width:28px; height:28px; }
  .checkrow { display:flex; align-items:center; justify-content:space-between; gap:10px; margin:14px 0; font-size:1em; }
  button { font-size:1.1em; padding:16px; border-radius:12px; border:none; font-weight:700; width:100%; margin-top:10px; }
  .save-btn { background:#2e7d32; color:#fff; }
  .reset-btn { background:#555; color:#eee; }
  .status { font-size:0.9em; margin-top:10px; min-height:1.2em; }
</style>
</head>
<body>
<p><a href="/">&larr; back to control</a></p>
<h1>Boomslang Settings</h1>
<div id="banner" class="banner">loading...</div>

<div class="card">
  <strong>Current-sense shunt resistance (ohms)</strong>
  <div class="hint">Schematic default is 0.05R (R1/R17/R31). Only change this if a shunt has actually been replaced with a different value.</div>
  <label>Channel 1 <input type="number" id="r0" step="0.001" min="0.001" max="10"></label>
  <label>Channel 2 <input type="number" id="r1" step="0.001" min="0.001" max="10"></label>
  <label>Channel 3 <input type="number" id="r2" step="0.001" min="0.001" max="10"></label>
</div>

<div class="card">
  <strong>Arming</strong>
  <label>Countdown before firing is permitted (s) <input type="number" id="countdown" step="1" min="0" max="600"></label>
  <div class="checkrow"><span>Visible flash when armed</span><input type="checkbox" id="visibleArmed"></div>
  <div class="checkrow"><span>Audible alarm when armed</span><input type="checkbox" id="audibleArmed"></div>
  <div class="checkrow"><span>Require disarm+rearm before re-triggering</span><input type="checkbox" id="reqRearm"></div>
</div>

<button class="save-btn" onclick="save()">Save</button>
<button class="reset-btn" onclick="resetDefaults()">Reset to Defaults</button>
<div id="status" class="status"></div>

<script>
async function refreshBanner() {
  let r;
  try { r = await (await fetch('/status.json')).json(); }
  catch (e) { return; }
  const banner = document.getElementById('banner');
  if (r.arm_state === 'disarmed') {
    banner.className = 'banner disarmed';
    banner.textContent = 'Disarmed — settings can be saved';
  } else {
    banner.className = 'banner armed';
    banner.textContent = 'Armed — settings cannot be saved to flash until disarmed';
  }
}

async function loadConfig() {
  const c = await (await fetch('/config.json')).json();
  document.getElementById('r0').value = c.sense_ohms[0];
  document.getElementById('r1').value = c.sense_ohms[1];
  document.getElementById('r2').value = c.sense_ohms[2];
  document.getElementById('countdown').value = c.arm_countdown_s;
  document.getElementById('visibleArmed').checked = c.visible_when_armed;
  document.getElementById('audibleArmed').checked = c.audible_when_armed;
  document.getElementById('reqRearm').checked = c.require_rearm;
}

async function save() {
  const r0 = document.getElementById('r0').value;
  const r1 = document.getElementById('r1').value;
  const r2 = document.getElementById('r2').value;
  const countdown = document.getElementById('countdown').value;
  const visible = document.getElementById('visibleArmed').checked ? '1' : '0';
  const audible = document.getElementById('audibleArmed').checked ? '1' : '0';
  const reqRearm = document.getElementById('reqRearm').checked ? '1' : '0';
  const resp = await fetch(
    `/config?sense_ohm0=${r0}&sense_ohm1=${r1}&sense_ohm2=${r2}` +
    `&arm_countdown_s=${countdown}&visible_when_armed=${visible}` +
    `&audible_when_armed=${audible}&require_rearm=${reqRearm}`,
    { method: 'POST' });
  showStatus(await resp.json());
}

async function resetDefaults() {
  if (!confirm('Reset all settings to factory defaults?')) return;
  const resp = await fetch('/config/reset', { method: 'POST' });
  showStatus(await resp.json());
  loadConfig();
}

function showStatus(j) {
  const el = document.getElementById('status');
  el.textContent = j.ok ? 'Saved.' : ('Not saved: ' + (j.error || 'unknown error'));
  el.style.color = j.ok ? '#7fd17f' : '#ff9494';
}

refreshBanner();
loadConfig();
setInterval(refreshBanner, 1000);
</script>
</body>
</html>
)rawliteral";
