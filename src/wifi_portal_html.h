#pragma once
// Auto-generated from web/wifi_portal.html — do not edit by hand.
// Run: pio run -e app  (or ./scripts/build.sh app)
static const char WIFI_PORTAL_HTML[] PROGMEM = R"WIFIHTML(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>SmartClock WiFi</title>
  <style>
    body {
      font-family: system-ui, sans-serif;
      max-width: 28rem;
      margin: 1.5rem auto;
      padding: 0 1rem;
      background: #111;
      color: #eee;
    }
    h1 { font-size: 1.25rem; }
    label { display: block; margin: 0.75rem 0 0.25rem; }
    input, select, button {
      width: 100%;
      box-sizing: border-box;
      padding: 0.6rem;
      font-size: 1rem;
      border-radius: 0.4rem;
      border: 1px solid #444;
      background: #222;
      color: #eee;
    }
    button {
      margin-top: 0.75rem;
      background: #0af;
      border: none;
      color: #000;
      font-weight: 600;
      cursor: pointer;
    }
    button.secondary {
      background: #333;
      color: #eee;
    }
    .status {
      background: #1a1a1a;
      padding: 0.75rem;
      border-radius: 0.4rem;
      margin: 1rem 0;
      font-size: 0.9rem;
      line-height: 1.5;
    }
    .msg {
      color: #effa5c;
      margin: 0.5rem 0;
    }
    .msg:empty { display: none; }
    #forget-wrap { display: none; }
    .hint {
      color: #888;
      font-size: 0.8rem;
      margin-top: 1.5rem;
    }
  </style>
</head>
<body>
  <h1>SmartClock WiFi</h1>
  <p id="msg" class="msg"></p>
  <div class="status">
    SSID: <span id="cur-ssid">…</span><br>
    IP: <span id="cur-ip">…</span><br>
    Time zone: KST (UTC+9)
  </div>

  <form id="connect-form" method="POST" action="/connect">
    <label for="ssid">Network</label>
    <select id="ssid" name="ssid">
      <option value="">Scanning…</option>
    </select>
    <label for="manual">Or type SSID</label>
    <input id="manual" name="manual" placeholder="Hidden / typed SSID" autocomplete="off">
    <label for="pass">Password</label>
    <input id="pass" name="pass" type="password" placeholder="(blank if open)" autocomplete="current-password">
    <button type="submit">Connect</button>
  </form>

  <div id="forget-wrap">
    <form method="POST" action="/forget">
      <button class="secondary" type="submit">Forget WiFi / open portal</button>
    </form>
  </div>

  <p class="hint">Edit this page in <code>web/wifi_portal.html</code>. Live APIs need the ESP32.</p>

  <script>
    function loadStatus() {
      return fetch("/status")
        .then(function (r) { return r.json(); })
        .then(function (s) {
          document.getElementById("cur-ssid").textContent = s.ssid || "-";
          document.getElementById("cur-ip").textContent = s.ip || "-";
          document.getElementById("msg").textContent = s.msg || "";
          document.getElementById("forget-wrap").style.display =
            s.connected || s.has_creds ? "block" : "none";
        })
        .catch(function () {
          document.getElementById("cur-ssid").textContent = "(offline preview)";
          document.getElementById("cur-ip").textContent = "-";
        });
    }

    function loadScan() {
      var sel = document.getElementById("ssid");
      fetch("/scan")
        .then(function (r) { return r.json(); })
        .then(function (list) {
          sel.innerHTML = "";
          if (!list.length) {
            sel.innerHTML = '<option value="">(none found)</option>';
            return;
          }
          list.forEach(function (n) {
            var o = document.createElement("option");
            o.value = n.ssid;
            o.textContent =
              n.ssid + " (" + n.rssi + " dBm" + (n.open ? ", open" : "") + ")";
            sel.appendChild(o);
          });
        })
        .catch(function () {
          sel.innerHTML = '<option value="">Scan failed (open via ESP)</option>';
        });
    }

    document.getElementById("connect-form").addEventListener("submit", function () {
      var m = document.getElementById("manual").value.trim();
      if (m) {
        var sel = document.getElementById("ssid");
        var opt = document.createElement("option");
        opt.value = m;
        opt.selected = true;
        sel.appendChild(opt);
      }
    });

    loadStatus();
    loadScan();
  </script>
</body>
</html>
)WIFIHTML";
