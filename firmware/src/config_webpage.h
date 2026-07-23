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
  .nav { display:grid; grid-template-columns:1fr 1fr; gap:8px; margin-bottom:12px; font-size:1.8em; }
  .nav a, .nav b { display:block; text-align:center; padding:10px; border-radius:10px; text-decoration:none; font-weight:700; }
  .nav a { background:#1c1c1c; color:#8ab4ff; }
  .nav b { background:#333; color:#eee; }
  .banner { padding:14px; border-radius:10px; margin-bottom:14px; font-weight:700; text-align:center; }
  .armed { background:#4d3b1b; color:#ffdca6; }
  .disarmed { background:#1b4d1b; color:#a6ffa6; }
  .card { background:#1c1c1c; border-radius:12px; padding:14px; margin-bottom:12px; }
  .hint { color:#999; font-size:0.85em; margin-bottom:10px; }
  label { display:flex; align-items:center; justify-content:space-between; gap:10px; margin:14px 0 4px; font-size:1em; color:#ccc; }
  input[type=number] { width:110px; box-sizing:border-box; padding:10px; border-radius:8px; border:1px solid #444; background:#0c0c0c; color:#eee; font-size:1.05em; }
  input[type=text] { width:180px; box-sizing:border-box; padding:10px; border-radius:8px; border:1px solid #444; background:#0c0c0c; color:#eee; font-size:1.05em; }
  input[type=checkbox] { width:28px; height:28px; }
  .checkrow { display:flex; align-items:center; justify-content:space-between; gap:10px; margin:14px 0; font-size:1em; }
  button { font-size:1.1em; padding:16px; border-radius:12px; border:none; font-weight:700; width:100%; margin-top:10px; }
  .save-btn { background:#2e7d32; color:#fff; }
  .reset-btn { background:#555; color:#eee; }
  .status { font-size:0.9em; margin-top:10px; min-height:1.2em; }
</style>
</head>
<body>
<div class="nav"><a href="/">Main</a><a href="/timing">Timing</a><a href="/stats">Stats</a><b>Settings</b></div>
<h1>Boomslang Settings</h1>
<div id="banner" class="banner">loading...</div>

<div class="card">
  <strong>Current-sense shunt resistance (ohms)</strong>
  <div class="hint">Schematic default is 0.05R (R1/R17/R31). Only change this if a shunt has actually been replaced with a different value.</div>
  <label>Channel 1 <input type="number" id="r0" step="0.001" min="0.01" max="100"></label>
  <label>Channel 2 <input type="number" id="r1" step="0.001" min="0.01" max="100"></label>
  <label>Channel 3 <input type="number" id="r2" step="0.001" min="0.01" max="100"></label>
</div>

<div class="card">
  <strong>Arming</strong>
  <label>Countdown before firing is permitted (s) <input type="number" id="countdown" step="1" min="0" max="600"></label>
  <label>Auto-lockout after this long in READY (s, 0=never) <input type="number" id="armTimeout" step="1" min="0" max="3600"></label>
  <div class="hint">If READY is held this long — armed but never triggered, or nothing selected — TRIGGER is refused until a fresh disarm+rearm, even if the arm switch (J5) is still physically closed. Does not touch the switch itself or the buzzer/strobe pattern; it only blocks firing. 0 disables this (matches prior behavior).</div>
  <div class="checkrow"><span>Visible flash when armed</span><input type="checkbox" id="visibleArmed"></div>
  <div class="checkrow"><span>Audible alarm when armed</span><input type="checkbox" id="audibleArmed"></div>
  <label>Buzzer volume (0-10) <input type="number" id="speakerVolume" step="1" min="0" max="10"></label>
  <div class="checkrow"><span>Require disarm+rearm before re-triggering</span><input type="checkbox" id="reqRearm"></div>
</div>

<div class="card">
  <strong>Continuity checks</strong>
  <div class="checkrow"><span>Check selected channels' continuity while armed</span><input type="checkbox" id="contOnArm"></div>
  <div class="hint">Live check while armed — clears itself as soon as it's fixed, no disarm needed. Also locks channel selection while armed (must disarm to change which channels are selected).</div>
  <div class="checkrow"><span>Check continuity immediately before triggering</span><input type="checkbox" id="contBeforeTrig"></div>
  <div class="hint">Stricter: a failure here fires nothing and requires a full disarm+rearm before another trigger, regardless of the setting above.</div>
</div>

<div class="card">
  <strong>Battery</strong>
  <label>Low-battery threshold (V) <input type="number" id="lowBattV" step="0.1" min="0" max="30"></label>
  <div class="hint">Below this, the control page shows a warning.</div>
  <div class="checkrow"><span>Block arming below threshold</span><input type="checkbox" id="lvLockout"></div>
  <div class="hint">If the arm switch closes while battery voltage is under the threshold above, the device stays disarmed until voltage recovers. Turn off to arm anyway (e.g. bench testing on a supply intentionally below threshold).</div>
</div>

<div class="card">
  <strong>WiFi</strong>
  <label>Network name (SSID) <input type="text" id="wifiSsid" maxlength="32"></label>
  <label>Password <input type="text" id="wifiPassword" maxlength="63"></label>
  <div class="hint">This password is the device's only access control — anyone who has it can arm and fire. Leave it blank for an open, unencrypted network (not recommended). Changes here take effect only after the device is power-cycled or reset — you'll need to reconnect using the new name/password afterward.</div>
</div>

<div class="card">
  <strong>WiFi relay (station) mode</strong>
  <div class="hint">By default this device hosts its own WiFi network (above). Instead, it can join an existing WiFi network as a client using that same SSID/password, to relay through your router when you need more distance between operator and pyrotechnics than the device's own AP range allows. If it can't connect, it falls back to hosting its own AP so you're never locked out.</div>
  <div class="hint"><strong>Real security tradeoff:</strong> anyone who can reach that network can also reach this device. So this isn't a checkbox — type the word <code>relay</code> below to turn it on. Leave blank (or anything else) to keep hosting the device's own AP.</div>
  <label>Type "relay" to enable <input type="text" id="wifiRelayConfirm" placeholder="relay to enable" autocomplete="off"></label>
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
  document.getElementById('armTimeout').value = c.arm_timeout_s;
  document.getElementById('visibleArmed').checked = c.visible_when_armed;
  document.getElementById('audibleArmed').checked = c.audible_when_armed;
  document.getElementById('speakerVolume').value = c.speaker_volume;
  document.getElementById('reqRearm').checked = c.require_rearm;
  document.getElementById('contOnArm').checked = c.check_continuity_on_arm;
  document.getElementById('contBeforeTrig').checked = c.check_continuity_before_trigger;
  document.getElementById('lowBattV').value = c.low_battery_threshold_v;
  document.getElementById('lvLockout').checked = c.low_voltage_lockout_enabled;
  document.getElementById('wifiSsid').value = c.wifi_ssid;
  document.getElementById('wifiPassword').value = c.wifi_password;
  document.getElementById('wifiRelayConfirm').value = c.wifi_station_mode ? 'relay' : '';
}

async function save() {
  const r0 = document.getElementById('r0').value;
  const r1 = document.getElementById('r1').value;
  const r2 = document.getElementById('r2').value;
  const countdown = document.getElementById('countdown').value;
  const armTimeout = document.getElementById('armTimeout').value;
  const visible = document.getElementById('visibleArmed').checked ? '1' : '0';
  const audible = document.getElementById('audibleArmed').checked ? '1' : '0';
  const speakerVolume = document.getElementById('speakerVolume').value;
  const reqRearm = document.getElementById('reqRearm').checked ? '1' : '0';
  const contOnArm = document.getElementById('contOnArm').checked ? '1' : '0';
  const contBeforeTrig = document.getElementById('contBeforeTrig').checked ? '1' : '0';
  const lowBattV = document.getElementById('lowBattV').value;
  const lvLockout = document.getElementById('lvLockout').checked ? '1' : '0';
  // SSID/password are free text, unlike every other field here — must be
  // URI-encoded before going into a query string.
  const wifiSsid = encodeURIComponent(document.getElementById('wifiSsid').value);
  const wifiPassword = encodeURIComponent(document.getElementById('wifiPassword').value);
  const wifiRelayConfirm = encodeURIComponent(document.getElementById('wifiRelayConfirm').value);
  const resp = await fetch(
    `/config?sense_ohm0=${r0}&sense_ohm1=${r1}&sense_ohm2=${r2}` +
    `&arm_countdown_s=${countdown}&arm_timeout_s=${armTimeout}&visible_when_armed=${visible}` +
    `&audible_when_armed=${audible}&speaker_volume=${speakerVolume}&require_rearm=${reqRearm}` +
    `&check_continuity_on_arm=${contOnArm}&check_continuity_before_trigger=${contBeforeTrig}` +
    `&low_battery_threshold_v=${lowBattV}&low_voltage_lockout_enabled=${lvLockout}` +
    `&wifi_ssid=${wifiSsid}&wifi_password=${wifiPassword}&wifi_relay_confirm=${wifiRelayConfirm}`,
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
