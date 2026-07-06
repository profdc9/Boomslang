#pragma once

// Post-fire review screen: peak and average current per channel from its
// most recent pulse (peak_current_a/avg_current_a in /status.json). Purely
// read-only — no controls here, just the numbers, live-polled so they're
// current if viewed right after a sequence.
const char STATS_HTML[] PROGMEM = R"rawliteral(
<!doctype html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1">
<title>Boomslang Stats</title>
<style>
  * { box-sizing: border-box; }
  body { font-family: -apple-system, sans-serif; background:#111; color:#eee; margin:0; padding:12px; font-size:17px; }
  h1 { font-size:1.5em; margin:0 0 8px; }
  a { color:#8ab4ff; }
  .nav { font-size:0.9em; text-align:center; margin-bottom:10px; }
  .nav b { color:#eee; }
  .hint { color:#999; font-size:0.9em; margin-bottom:14px; }
  .channel { background:#1c1c1c; border-radius:12px; padding:14px; margin-bottom:10px; }
  .channel h2 { font-size:1.1em; margin:0 0 8px; }
  .stat-row { display:flex; justify-content:space-between; font-size:1.05em; padding:4px 0; }
  .stat-row span:last-child { font-weight:700; }
</style>
</head>
<body>
<div class="nav"><a href="/">Main</a> &middot; <a href="/timing">Timing</a> &middot; <b>Stats</b> &middot; <a href="/config">Settings</a></div>
<h1>Boomslang Stats</h1>
<p class="hint">Peak and average current from each channel's most recent pulse. Held until that channel fires again.</p>
<div id="channels"></div>

<script>
function buildChannels(r) {
  const div = document.getElementById('channels');
  div.innerHTML = '';
  r.channels.forEach((c, i) => {
    const el = document.createElement('div');
    el.className = 'channel';
    el.innerHTML = `
      <h2>Channel ${i + 1}</h2>
      <div class="stat-row"><span>Peak current</span><span>${c.peak_current_a.toFixed(2)} A</span></div>
      <div class="stat-row"><span>Average current</span><span>${c.avg_current_a.toFixed(2)} A</span></div>`;
    div.appendChild(el);
  });
}

async function refresh() {
  let r;
  try { r = await (await fetch('/status.json')).json(); }
  catch (e) { return; }
  buildChannels(r);
}

setInterval(refresh, 500);
refresh();
</script>
</body>
</html>
)rawliteral";
