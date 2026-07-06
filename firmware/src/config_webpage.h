#pragma once

// Settings page. Loads current values from /config.json, posts changes to
// /config (query-string args, same style as the /fire endpoint), and shows
// whether the board is armed since saveSettings() refuses to write to flash
// while armed.
const char CONFIG_HTML[] PROGMEM = R"rawliteral(
<!doctype html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Boomslang Settings</title>
<style>
  body { font-family: -apple-system, sans-serif; background:#111; color:#eee; margin:0; padding:16px; }
  h1 { font-size:1.4em; margin:0 0 4px; }
  a { color:#8ab4ff; }
  .banner { padding:10px 14px; border-radius:8px; margin-bottom:14px; font-weight:600; text-align:center; }
  .armed { background:#4d3b1b; color:#ffdca6; }
  .disarmed { background:#1b4d1b; color:#a6ffa6; }
  .card { background:#1c1c1c; border-radius:10px; padding:14px; margin-bottom:12px; }
  .hint { color:#999; font-size:0.85em; }
  label { display:block; margin:10px 0 4px; font-size:0.9em; color:#ccc; }
  input[type=number] { width:100%; box-sizing:border-box; padding:8px; border-radius:6px; border:1px solid #444; background:#0c0c0c; color:#eee; font-size:1em; }
  button { font-size:1em; padding:10px 18px; border-radius:8px; border:none; font-weight:600; margin-top:14px; margin-right:8px; }
  .save-btn { background:#2e7d32; color:#fff; }
  .reset-btn { background:#555; color:#eee; }
  .status { font-size:0.85em; margin-top:10px; min-height:1.2em; }
</style>
</head>
<body>
<p><a href="/">&larr; back to control</a></p>
<h1>Boomslang Settings</h1>
<div id="banner" class="banner">loading...</div>

<div class="card">
  <strong>Current-sense shunt resistance (ohms)</strong>
  <div class="hint">Schematic default is 0.05R (R1/R17/R31). Only change this if a shunt has actually been replaced with a different value.</div>
  <label>Channel 1</label>
  <input type="number" id="r0" step="0.001" min="0.001" max="10">
  <label>Channel 2</label>
  <input type="number" id="r1" step="0.001" min="0.001" max="10">
  <label>Channel 3</label>
  <input type="number" id="r2" step="0.001" min="0.001" max="10">

  <button class="save-btn" onclick="save()">Save</button>
  <button class="reset-btn" onclick="resetDefaults()">Reset to Defaults</button>
  <div id="status" class="status"></div>
</div>

<script>
async function refreshBanner() {
  let r;
  try { r = await (await fetch('/status.json')).json(); }
  catch (e) { return; }
  const banner = document.getElementById('banner');
  if (r.armed) {
    banner.className = 'banner armed';
    banner.textContent = 'ARMED — settings cannot be saved to flash until disarmed';
  } else {
    banner.className = 'banner disarmed';
    banner.textContent = 'Disarmed — settings can be saved';
  }
}

async function loadConfig() {
  const c = await (await fetch('/config.json')).json();
  document.getElementById('r0').value = c.sense_ohms[0];
  document.getElementById('r1').value = c.sense_ohms[1];
  document.getElementById('r2').value = c.sense_ohms[2];
}

async function save() {
  const r0 = document.getElementById('r0').value;
  const r1 = document.getElementById('r1').value;
  const r2 = document.getElementById('r2').value;
  const resp = await fetch(`/config?sense_ohm0=${r0}&sense_ohm1=${r1}&sense_ohm2=${r2}`, { method: 'POST' });
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
