#include "wifi_config.h"

#if defined(FIRMWARE_MODE_APP)

#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

static Preferences prefs;
static WebServer server(80);
static DNSServer dnsServer;

static bool softApActive = false;
static bool httpStarted = false;
static bool colorsChanged = false;
static WifiConfigIdleCb idleCb = nullptr;

static void wifiIdle() {
  if (idleCb)
    idleCb();
  else {
    delay(5);
    yield();
  }
}
static String apSsid;
static String statusMsg;
static uint32_t dateColor = 0xEFFA5C;
static uint32_t clockColor = 0x00F0FF;

static const char *kPrefsNs = "wifi";
static const char *kPrefsUiNs = "clockui";
static const char *kKeySsid = "ssid";
static const char *kKeyPass = "pass";
static const char *kKeyDateColor = "date_c";
static const char *kKeyClockColor = "clock_c";
static const uint32_t kDefaultDateColor = 0xEFFA5C;
static const uint32_t kDefaultClockColor = 0x00F0FF;
static const uint32_t kStaTimeoutMs = 15000;
static const byte kDnsPort = 53;
static const IPAddress kApIp(192, 168, 4, 1);

static String htmlEscape(const String &in) {
  String out;
  out.reserve(in.length());
  for (size_t i = 0; i < in.length(); ++i) {
    const char c = in[i];
    if (c == '&')
      out += F("&amp;");
    else if (c == '<')
      out += F("&lt;");
    else if (c == '>')
      out += F("&gt;");
    else if (c == '"')
      out += F("&quot;");
    else
      out += c;
  }
  return out;
}

static void loadCreds(String &ssid, String &pass) {
  prefs.begin(kPrefsNs, true);
  ssid = prefs.getString(kKeySsid, "");
  pass = prefs.getString(kKeyPass, "");
  prefs.end();
}

static void saveCreds(const String &ssid, const String &pass) {
  prefs.begin(kPrefsNs, false);
  prefs.putString(kKeySsid, ssid);
  prefs.putString(kKeyPass, pass);
  prefs.end();
}

static void loadColors() {
  prefs.begin(kPrefsUiNs, true);
  dateColor = prefs.getUInt(kKeyDateColor, kDefaultDateColor) & 0xFFFFFFu;
  clockColor = prefs.getUInt(kKeyClockColor, kDefaultClockColor) & 0xFFFFFFu;
  prefs.end();
}

static void saveColors(uint32_t dateRgb, uint32_t clockRgb) {
  const uint32_t nextDate = dateRgb & 0xFFFFFFu;
  const uint32_t nextClock = clockRgb & 0xFFFFFFu;
  if (nextDate != dateColor)
    Serial.printf("color date set #%06X -> #%06X\n",
                  (unsigned)dateColor, (unsigned)nextDate);
  if (nextClock != clockColor)
    Serial.printf("color clock set #%06X -> #%06X\n",
                  (unsigned)clockColor, (unsigned)nextClock);
  dateColor = nextDate;
  clockColor = nextClock;
  prefs.begin(kPrefsUiNs, false);
  prefs.putUInt(kKeyDateColor, dateColor);
  prefs.putUInt(kKeyClockColor, clockColor);
  prefs.end();
  colorsChanged = true;
  Serial.printf("colors saved date=#%06X clock=#%06X\n", (unsigned)dateColor,
                (unsigned)clockColor);
}

static bool parseHexColor(const String &s, uint32_t *out) {
  if (!out || s.length() < 6)
    return false;
  const char *p = s.c_str();
  if (*p == '#')
    ++p;
  char *end = nullptr;
  const unsigned long v = strtoul(p, &end, 16);
  if (end == p)
    return false;
  *out = static_cast<uint32_t>(v) & 0xFFFFFFu;
  return true;
}

static String colorToHtml(uint32_t rgb) {
  char buf[8];
  snprintf(buf, sizeof(buf), "#%06X", (unsigned)(rgb & 0xFFFFFFu));
  return String(buf);
}

static const char *kApSsid = "SmartClock";
static const char *kMdnsHost = "smartclock";

static void stopMdns() { MDNS.end(); }

static void startMdns() {
  stopMdns();
  if (!MDNS.begin(kMdnsHost)) {
    Serial.println("mDNS start failed");
    return;
  }
  MDNS.addService("http", "tcp", 80);
  Serial.printf("mDNS: http://%s.local/\n", kMdnsHost);
}

static bool connectSta(const String &ssid, const String &pass) {
  Serial.printf("WiFi STA connecting to \"%s\"...\n", ssid.c_str());
  // Never WIFI_STA alone — that tears down SoftAP mid-setup.
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());

  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < kStaTimeoutMs) {
    wifiIdle();
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("WiFi STA OK  IP=%s\n", WiFi.localIP().toString().c_str());
    return true;
  }

  Serial.println("WiFi STA failed");
  WiFi.disconnect(false); // keep SoftAP
  return false;
}

static void startSoftAp();
static void stopSoftAp();
static void setupHttpRoutes();
static void ensureHttp();

static void startSoftAp() {
  if (softApActive)
    return;

  apSsid = kApSsid;
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(kApIp, kApIp, IPAddress(255, 255, 255, 0));
  // Open network, channel 1 — phones often miss hidden/odd-channel APs.
  if (!WiFi.softAP(kApSsid, nullptr, 1, 0, 4)) {
    Serial.println("WiFi SoftAP start FAILED");
    return;
  }
  dnsServer.start(kDnsPort, "*", kApIp);
  softApActive = true;
  Serial.printf("WiFi SoftAP \"%s\"  http://%s/\n", kApSsid,
                WiFi.softAPIP().toString().c_str());
}

static void stopSoftAp() {
  if (!softApActive)
    return;
  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  softApActive = false;
  Serial.println("WiFi SoftAP stopped");
}

static void handleRoot() {
  const bool sta = WiFi.status() == WL_CONNECTED;
  String ssidNow = sta ? WiFi.SSID() : (softApActive ? apSsid : "-");
  String ipNow =
      sta ? WiFi.localIP().toString()
          : (softApActive ? WiFi.softAPIP().toString() : "-");

  const String dateHtml = colorToHtml(dateColor);
  const String clockHtml = colorToHtml(clockColor);

  String html;
  html.reserve(7000);
  html += F("<!DOCTYPE html><html><head><meta charset=utf-8>"
            "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
            "<title>SmartClock WiFi</title><style>"
            "body{font-family:system-ui,sans-serif;max-width:28rem;margin:1.5rem "
            "auto;padding:0 1rem 2rem;background:#111;color:#eee}"
            ".brand{font-size:1.75rem;font-weight:700;margin:0;padding-bottom:1.0rem;"
            "color:#eee;text-align:center;letter-spacing:.02em}"
            ".box{background:#1a1a1a;border:1px solid #333;border-radius:.75rem;"
            "padding:1rem 1rem 1.15rem;margin:1rem 0}"
            ".box h1{font-size:1.15rem;margin:0 0 .85rem;padding-bottom:.65rem;"
            "border-bottom:1px solid #333;font-weight:600}"
            "label{display:block;margin:.75rem 0 .25rem}"
            "input,select,button{width:100%;box-sizing:border-box;padding:.6rem;"
            "font-size:1rem;border-radius:.4rem;border:1px solid #444;"
            "background:#222;color:#eee}"
            "button{margin-top:.75rem;background:#0af;border:none;color:#000;"
            "font-weight:600;cursor:pointer}"
            ".status{font-size:.9rem;line-height:1.5;color:#bbb;margin:0 0 1rem}"
            ".status a{color:#bbb;text-decoration:none}"
            ".msg{color:#effa5c;margin:.5rem 0}"
            ".color-row{display:flex;align-items:center;gap:.75rem;margin-bottom:.75rem}"
            "button.color-box{width:2.75rem;height:2.75rem;margin:0;padding:0;"
            "flex:0 0 auto;border:2px solid #666;border-radius:.45rem;cursor:pointer}"
            ".color-hex{font-family:ui-monospace,monospace;color:#aaa;font-size:.9rem}"
            ".preview{display:inline-block;min-width:4.5rem;padding:.2rem .5rem;"
            "border-radius:.3rem;background:#000;font-weight:600}"
            ".sheet-bg{position:fixed;inset:0;background:rgba(0,0,0,.55);z-index:20;"
            "opacity:0;pointer-events:none;transition:opacity .2s}"
            ".sheet-bg.open{opacity:1;pointer-events:auto}"
            ".sheet{position:fixed;left:0;right:0;bottom:0;z-index:21;"
            "background:#1a1a1a;border-radius:1rem 1rem 0 0;padding:1rem 1rem 1.5rem;"
            "transform:translateY(110%);transition:transform .25s ease;"
            "max-width:28rem;margin:0 auto;box-shadow:0 -8px 24px rgba(0,0,0,.4)}"
            ".sheet.open{transform:translateY(0)}"
            ".sheet-handle{width:2.5rem;height:.25rem;background:#555;border-radius:99px;"
            "margin:0 auto .75rem}"
            ".sheet-title{text-align:center;font-size:1rem;margin:0 0 1rem}"
            ".sheet-grid{display:grid;grid-template-columns:repeat(10,1fr);gap:.35rem}"
            "button.swatch{width:100%;aspect-ratio:1;margin:0;padding:0;"
            "border-radius:.3rem;border:1px solid #333;cursor:pointer}"
            "button.swatch.on{border-color:#fff;box-shadow:0 0 0 1px #fff}"
            ".footer{margin:1.5rem 0 0;padding-top:1rem;border-top:1px solid #333;"
            "font-size:.95rem;line-height:1.45;color:rgba(255,255,255,.8);"
            "text-align:center;font-family:ui-monospace,monospace}"
            "</style></head><body><p class=brand>SmartClock</p>");

  if (statusMsg.length()) {
    html += F("<p class=msg>");
    html += htmlEscape(statusMsg);
    html += F("</p>");
    statusMsg = "";
  }

  html += F("<section class=box>"
            "<h1>WiFi settings</h1>"
            "<p class=status>");
  html += F("SSID: ");
  html += htmlEscape(ssidNow);
  html += F("<br>IP: ");
  html += htmlEscape(ipNow);
  if (sta) {
    html += F("<br><a href=\"http://smartclock.local/\">http://smartclock.local</a>");
  }
  html += F("<br>Time zone: KST (UTC+9)");
  html += F("</p>"
            "<form method=POST action=/connect>"
            "<label for=ssid>Network</label>"
            "<select id=ssid name=ssid>"
            "<option value=\"\">Scanning…</option>"
            "</select>"
            "<label for=manual>Or type SSID</label>"
            "<input id=manual name=manual placeholder=\"Hidden / typed SSID\">"
            "<label for=pass>Password</label>"
            "<input id=pass name=pass type=password placeholder=\"(blank if open)\">"
            "<button type=submit>Connect</button>"
            "</form></section>");

  html += F("<section class=box>"
            "<h1>Colors</h1>"
            "<div id=colors>"
            "<label>Date</label>"
            "<div class=color-row>"
            "<button type=button class=color-box id=date_box data-target=date_color "
            "style=\"background:");
  html += dateHtml;
  html += F("\"></button>"
            "<input type=hidden id=date_color value=\"");
  html += dateHtml;
  html += F("\">"
            "<span class=preview id=date_preview style=\"color:");
  html += dateHtml;
  html += F("\">2026 10/26 Mon</span>"
            "<span class=color-hex id=date_hex>");
  html += dateHtml;
  html += F("</span></div>"
            "<label>HH / MM</label>"
            "<div class=color-row>"
            "<button type=button class=color-box id=clock_box data-target=clock_color "
            "style=\"background:");
  html += clockHtml;
  html += F("\"></button>"
            "<input type=hidden id=clock_color value=\"");
  html += clockHtml;
  html += F("\">"
            "<span class=preview id=clock_preview style=\"color:");
  html += clockHtml;
  html += F("\">23:59</span>"
            "<span class=color-hex id=clock_hex>");
  html += clockHtml;
  html += F("</span></div>"
            "</div></section>");

  // Same basis as PlatformIO: sketch size vs running app partition (huge_app 3MB).
  const uint32_t flashUsed = ESP.getSketchSize();
  const esp_partition_t *appPart = esp_ota_get_running_partition();
  const uint32_t flashTotal =
      appPart ? appPart->size : (flashUsed + ESP.getFreeSketchSpace());
  const float flashPct =
      flashTotal ? (100.0f * flashUsed / flashTotal) : 0.0f;

  char flashLine[64];
  snprintf(flashLine, sizeof(flashLine), "Flash: %4.1f%% (%u/%u KB)", flashPct,
           (unsigned)((flashUsed + 1023) / 1024),
           (unsigned)((flashTotal + 1023) / 1024));

  html += F("<p class=footer>");
  html += htmlEscape(flashLine);
  html += F("</p>"
            "<div class=sheet-bg id=sheet_bg></div>"
            "<div class=sheet id=sheet>"
            "<div class=sheet-handle></div>"
            "<p class=sheet-title id=sheet_title>Pick a color</p>"
            "<div class=sheet-grid id=sheet_grid></div>"
            "</div>");

  html += F("<script>"
            "fetch('/scan').then(r=>r.json()).then(list=>{"
            "const s=document.getElementById('ssid');"
            "s.innerHTML='';"
            "if(!list.length){s.innerHTML='<option value=\"\">(none found)</option>';return;}"
            "list.forEach(n=>{"
            "const o=document.createElement('option');"
            "o.value=n.ssid;"
            "o.textContent=n.ssid+' ('+n.rssi+' dBm'+(n.open?', open':'')+')';"
            "s.appendChild(o);});"
            "}).catch(()=>{document.getElementById('ssid').innerHTML="
            "'<option value=\"\">Scan failed</option>';});"
            "document.querySelector('form[action=\"/connect\"]').addEventListener('submit',e=>{"
            "const m=document.getElementById('manual').value.trim();"
            "if(m){document.getElementById('ssid').value=m;}"
            "});"
            "function hslToHex(h,s,l){"
            "s/=100;l/=100;"
            "const a=s*Math.min(l,1-l);"
            "const f=n=>{const k=(n+h/30)%12;"
            "const c=l-a*Math.max(Math.min(k-3,9-k,1),-1);"
            "return Math.round(255*c).toString(16).padStart(2,'0');};"
            "return('#'+f(0)+f(8)+f(4)).toUpperCase();}"
            "const PAL=(()=>{"
            "const out=[];"
            "for(let col=0;col<10;col++){"
            "const v=Math.round(255*(1-col/9));"
            "const hx=v.toString(16).padStart(2,'0').toUpperCase();"
            "out.push('#'+hx+hx+hx);}"
            "for(let row=1;row<10;row++){"
            "for(let col=0;col<10;col++){"
            "out.push(hslToHex(col*36,row<3?55:88,90-row*8));}}"
            "out[11]='#EFFA5C';out[16]='#00F0FF';"
            "return out;})();"
            "let activeId=null;"
            "const bg=document.getElementById('sheet_bg');"
            "const sheet=document.getElementById('sheet');"
            "const grid=document.getElementById('sheet_grid');"
            "function syncColorUI(id){"
            "const hex=document.getElementById(id).value.toUpperCase();"
            "const prefix=id.replace('_color','');"
            "document.getElementById(prefix+'_hex').textContent=hex;"
            "document.getElementById(prefix+'_preview').style.color=hex;"
            "document.getElementById(prefix+'_box').style.background=hex;"
            "grid.querySelectorAll('.swatch').forEach(x=>{"
            "x.classList.toggle('on',(x.dataset.color||'').toLowerCase()===hex.toLowerCase());"
            "});}"
            "function applyColorsNow(){"
            "const body='date_color='+encodeURIComponent(document.getElementById('date_color').value)"
            "+'&clock_color='+encodeURIComponent(document.getElementById('clock_color').value);"
            "fetch('/colors',{method:'POST',headers:{'Content-Type':"
            "'application/x-www-form-urlencoded'},body:body}).catch(()=>{});}"
            "function closeSheet(){"
            "bg.classList.remove('open');sheet.classList.remove('open');activeId=null;}"
            "function openSheet(id,title){"
            "activeId=id;"
            "document.getElementById('sheet_title').textContent=title;"
            "syncColorUI(id);"
            "bg.classList.add('open');sheet.classList.add('open');}"
            "PAL.forEach(c=>{"
            "const b=document.createElement('button');"
            "b.type='button';b.className='swatch';b.style.background=c;b.dataset.color=c;"
            "b.addEventListener('click',()=>{"
            "if(!activeId)return;"
            "document.getElementById(activeId).value=c;"
            "syncColorUI(activeId);"
            "applyColorsNow();"
            "closeSheet();});"
            "grid.appendChild(b);});"
            "document.getElementById('date_box').addEventListener('click',"
            "()=>openSheet('date_color','Date color'));"
            "document.getElementById('clock_box').addEventListener('click',"
            "()=>openSheet('clock_color','HH / MM color'));"
            "bg.addEventListener('click',closeSheet);"
            "syncColorUI('date_color');syncColorUI('clock_color');"
            "</script></body></html>");

  server.send(200, "text/html", html);
}

static void handleScan() {
  // Blocking scan; yield briefly before to keep LVGL happier if called rarely.
  yield();
  const int n = WiFi.scanNetworks(/*async=*/false, /*hidden=*/true);
  String json = "[";
  for (int i = 0; i < n; ++i) {
    if (i)
      json += ',';
    String ssid = WiFi.SSID(i);
    ssid.replace("\\", "\\\\");
    ssid.replace("\"", "\\\"");
    json += "{\"ssid\":\"";
    json += ssid;
    json += "\",\"rssi\":";
    json += String(WiFi.RSSI(i));
    json += ",\"open\":";
    json += (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "true" : "false";
    json += '}';
  }
  json += ']';
  WiFi.scanDelete();
  server.send(200, "application/json", json);
}

static void handleConnect() {
  String ssid = server.arg("ssid");
  const String manual = server.arg("manual");
  if (manual.length())
    ssid = manual;
  ssid.trim();
  const String pass = server.arg("pass");

  if (!ssid.length()) {
    statusMsg = "SSID required";
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
    return;
  }

  saveCreds(ssid, pass);
  statusMsg = "Connecting to " + ssid + "…";

  // Reply first so the browser is not left hanging during STA join.
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
  delay(50);
  server.client().stop();

  stopSoftAp();
  if (connectSta(ssid, pass)) {
    statusMsg = "Connected to " + ssid;
    WiFi.mode(WIFI_STA);
    startMdns();
    ensureHttp();
    Serial.printf("Config portal: http://%s/  or  http://%s.local/\n",
                  WiFi.localIP().toString().c_str(), kMdnsHost);
  } else {
    statusMsg = "Failed to join " + ssid + " — portal open again";
    startSoftAp();
    ensureHttp();
  }
}

static void handleColors() {
  uint32_t dateRgb = dateColor;
  uint32_t clockRgb = clockColor;
  const bool okDate = parseHexColor(server.arg("date_color"), &dateRgb);
  const bool okClock = parseHexColor(server.arg("clock_color"), &clockRgb);
  if (okDate || okClock) {
    saveColors(okDate ? dateRgb : dateColor, okClock ? clockRgb : clockColor);
    server.send(200, "application/json", "{\"ok\":true}");
  } else {
    server.send(400, "application/json", "{\"ok\":false}");
  }
}

static void handleCaptive() {
  server.sendHeader("Location", String("http://") + kApIp.toString() + "/",
                    true);
  server.send(302, "text/plain", "");
}

static void setupHttpRoutes() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/scan", HTTP_GET, handleScan);
  server.on("/connect", HTTP_POST, handleConnect);
  server.on("/colors", HTTP_POST, handleColors);
  // Captive portal probes
  server.on("/generate_204", HTTP_GET, handleCaptive);
  server.on("/hotspot-detect.html", HTTP_GET, handleCaptive);
  server.on("/connecttest.txt", HTTP_GET, handleCaptive);
  server.on("/ncsi.txt", HTTP_GET, handleCaptive);
  server.onNotFound(handleRoot);
}

static void ensureHttp() {
  if (httpStarted)
    return;
  setupHttpRoutes();
  server.begin();
  httpStarted = true;
  Serial.println("HTTP config server on :80");
}

void wifiConfigSetIdleCallback(WifiConfigIdleCb cb) { idleCb = cb; }

void wifiConfigBegin() {
  loadColors();

  WiFi.persistent(false);
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  const uint32_t t0 = millis();
  while (millis() - t0 < 200)
    wifiIdle();

  // SoftAP first so "SmartClock" is visible even while STA joins (or if STA OK).
  startSoftAp();
  ensureHttp();

  String ssid, pass;
  loadCreds(ssid, pass);
  if (ssid.length() && connectSta(ssid, pass)) {
    startMdns();
    ensureHttp();
    Serial.printf("Config portal: http://%s/  or  http://%s.local/\n",
                  WiFi.localIP().toString().c_str(), kMdnsHost);
  }

  if (softApActive)
    Serial.printf("Join SoftAP \"%s\" → http://%s/\n", kApSsid,
                  WiFi.softAPIP().toString().c_str());
}

void wifiConfigLoop() {
  if (softApActive)
    dnsServer.processNextRequest();
  if (httpStarted)
    server.handleClient();
}

bool wifiConfigIsConnected() { return WiFi.status() == WL_CONNECTED; }

bool wifiConfigIsRadioOn() {
  return WiFi.getMode() != WIFI_OFF;
}

bool wifiConfigSoftApActive() { return softApActive; }

IPAddress wifiConfigLocalIp() {
  if (WiFi.status() == WL_CONNECTED)
    return WiFi.localIP();
  if (softApActive)
    return WiFi.softAPIP();
  return IPAddress();
}

IPAddress wifiConfigSoftApIp() {
  if (softApActive)
    return WiFi.softAPIP();
  return IPAddress();
}

void wifiConfigRadioOff() {
  stopMdns();
  stopSoftAp();
  if (httpStarted) {
    server.stop();
    httpStarted = false;
  }
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  Serial.println("WiFi radio off (config/NTP idle)");
}

bool wifiConfigWakeStaForSync() {
  String ssid, pass;
  loadCreds(ssid, pass);
  if (!ssid.length()) {
    Serial.println("WiFi wake skipped (no saved creds)");
    return false;
  }

  stopSoftAp();
  if (httpStarted) {
    server.stop();
    httpStarted = false;
  }
  stopMdns();

  if (!connectSta(ssid, pass))
    return false;

  WiFi.mode(WIFI_STA);
  Serial.println("WiFi wake for NTP sync");
  return true;
}

uint32_t wifiConfigDateColor() { return dateColor; }
uint32_t wifiConfigClockColor() { return clockColor; }

bool wifiConfigTakeColorsChanged() {
  if (!colorsChanged)
    return false;
  colorsChanged = false;
  return true;
}

#else

void wifiConfigSetIdleCallback(WifiConfigIdleCb /*cb*/) {}
void wifiConfigBegin() {}
void wifiConfigLoop() {}
bool wifiConfigIsConnected() { return false; }
bool wifiConfigIsRadioOn() { return false; }
IPAddress wifiConfigLocalIp() { return IPAddress(); }
void wifiConfigRadioOff() {}
bool wifiConfigWakeStaForSync() { return false; }
uint32_t wifiConfigDateColor() { return 0xEFFA5C; }
uint32_t wifiConfigClockColor() { return 0x00F0FF; }
bool wifiConfigTakeColorsChanged() { return false; }

#endif
