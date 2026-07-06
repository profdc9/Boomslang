#pragma once

// Single-page control UI. Polls /status.json and posts to /fire, /clear_fault.
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!doctype html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Boomslang</title>
<style>
  body { font-family: -apple-system, sans-serif; background:#111; color:#eee; margin:0; padding:16px; }
  h1 { font-size:1.4em; margin:0 0 4px; }
  .banner { padding:10px 14px; border-radius:8px; margin-bottom:14px; font-weight:600; text-align:center; }
  .armed { background:#1b4d1b; color:#a6ffa6; }
  .disarmed { background:#4d3b1b; color:#ffdca6; }
  .fault { background:#5a1414; color:#ffb3b3; }
  .channel { background:#1c1c1c; border-radius:10px; padding:14px; margin-bottom:12px; }
  .row { display:flex; align-items:center; justify-content:space-between; gap:10px; }
  .dot { width:14px; height:14px; border-radius:50%; display:inline-block; margin-right:8px; }
  .ok { background:#3ecf3e; } .bad { background:#cf3e3e; } .mid { background:#888; }
  button { font-size:1em; padding:10px 18px; border-radius:8px; border:none; font-weight:600; }
  .fire-btn { background:#c0392b; color:#fff; }
  .fire-btn:disabled { background:#555; color:#999; }
  .aux button { margin-right:8px; background:#333; color:#eee; padding:8px 14px; }
  .small { font-size:0.85em; color:#999; }
</style>
</head>
<body>
<h1>Boomslang</h1>
<div id="banner" class="banner">loading...</div>
<div id="channels"></div>
<div class="aux">
  <button onclick="setAux('audible',1)">Buzzer On</button>
  <button onclick="setAux('audible',0)">Buzzer Off</button>
  <button onclick="setAux('visible',1)">Strobe On</button>
  <button onclick="setAux('visible',0)">Strobe Off</button>
  <button onclick="clearFault()">Clear Fault Latch</button>
</div>
<p class="small">Firing pulls trigger low for a fixed pulse, then releases automatically. Continuity must read OK and the system must be ARMED (arm key at J5 closed) before a fire command is accepted.</p>

<script>
async function refresh() {
  let r;
  try { r = await (await fetch('/status.json')).json(); }
  catch (e) { document.getElementById('banner').textContent = 'connection lost'; return; }

  const banner = document.getElementById('banner');
  if (r.fault) {
    banner.className = 'banner fault';
    banner.textContent = 'FAULT — overcurrent detected on at least one channel';
  } else if (r.armed) {
    banner.className = 'banner armed';
    banner.textContent = 'ARMED';
  } else {
    banner.className = 'banner disarmed';
    banner.textContent = 'DISARMED — close arm key at J5';
  }

  const div = document.getElementById('channels');
  div.innerHTML = '';
  r.channels.forEach((c, i) => {
    const el = document.createElement('div');
    el.className = 'channel';
    const dotClass = c.firing ? 'mid' : (c.continuity ? 'ok' : 'bad');
    const canFire = r.armed && !r.fault && c.continuity && !c.firing;
    el.innerHTML = `
      <div class="row">
        <div><span class="dot ${dotClass}"></span>Channel ${i+1}
          <div class="small">${c.firing ? 'FIRING' : (c.continuity ? 'continuity OK' : 'no continuity')} &middot; last current: ${c.last_current_a.toFixed(2)} A</div>
        </div>
        <button class="fire-btn" ${canFire ? '' : 'disabled'} onclick="fire(${i})">FIRE</button>
      </div>`;
    div.appendChild(el);
  });
}

async function fire(ch) {
  if (!confirm('Fire channel ' + (ch+1) + '?')) return;
  await fetch('/fire?ch=' + ch + '&confirm=1', { method: 'POST' });
  refresh();
}
async function setAux(which, on) {
  await fetch('/' + which + '?on=' + on, { method: 'POST' });
}
async function clearFault() {
  await fetch('/clear_fault', { method: 'POST' });
  refresh();
}
setInterval(refresh, 500);
refresh();
</script>
</body>
</html>
)rawliteral";
