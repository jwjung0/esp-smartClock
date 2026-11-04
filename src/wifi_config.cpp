#include "wifi_config.h"

#if defined(FIRMWARE_MODE_APP)

#include "bl_ctrl.h"
#include "market_fetch.h"
#include "power_policy.h"

#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static Preferences prefs;
static WebServer server(80);
static DNSServer dnsServer;

static bool softApActive = false;
static bool httpStarted = false;
static bool colorsChanged = false;
static WifiConfigIdleCb idleCb = nullptr;
static TaskHandle_t portalTaskHandle = nullptr;
static bool portalTaskStarted = false;

enum class PortalCmd : uint8_t { None = 0, RadioOff, WakeSta };
static volatile PortalCmd portalPending = PortalCmd::None;
static volatile bool portalBusy = false;
// Written only by the portal task (and once at the end of wifiConfigBegin).
static volatile bool cachedStaUp = false;
static volatile bool cachedRadioOn = false;

// After the portal task owns WiFi/HTTP, never pump LVGL from wifiIdle —
// connectSta/handleConnect may run on that task.
static void wifiIdle() {
  if (!portalTaskStarted && idleCb)
    idleCb();
  else {
    delay(5);
    yield();
  }
}

static void publishLinkFlags() {
  cachedStaUp = WiFi.status() == WL_CONNECTED;
  cachedRadioOn = WiFi.getMode() != WIFI_OFF;
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
  html.reserve(11000);
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
            ".box-head{display:flex;align-items:center;justify-content:space-between;"
            "gap:.75rem;margin:0 0 .85rem;padding-bottom:.65rem;"
            "border-bottom:1px solid #333}"
            ".box-head h1{margin:0;padding:0;border:none;font-size:1.15rem;font-weight:600}"
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
            ".chk{display:flex;align-items:center;gap:.6rem;margin:.45rem 0;"
            "font-size:.95rem}"
            ".chk input{width:auto;margin:0}"
            ".switch{position:relative;display:inline-block;width:3.1rem;height:1.75rem;"
            "flex:0 0 auto}"
            ".switch input{opacity:0;width:0;height:0;position:absolute}"
            ".slider{position:absolute;cursor:pointer;inset:0;background:#444;"
            "border-radius:999px;transition:background .2s}"
            ".slider:before{position:absolute;content:\"\";height:1.35rem;width:1.35rem;"
            "left:.2rem;top:.2rem;background:#eee;border-radius:50%;"
            "transition:transform .2s}"
            ".switch input:checked+.slider{background:#0af}"
            ".switch input:checked+.slider:before{transform:translateX(1.35rem)}"
            ".mkt-body{transition:opacity .2s}"
            ".mkt-body.dim{opacity:.38;pointer-events:none}"
            ".mkt-body.dim button{background:#555;color:#999}"
            ".mkt-add{display:grid;grid-template-columns:1fr 1fr auto;gap:.4rem;"
            "align-items:end;margin-top:.5rem}"
            ".mkt-add label{margin:0}"
            ".mkt-add button{margin:0;width:auto;padding:.55rem .75rem}"
            ".mkt-list{list-style:none;padding:0;margin:.6rem 0 0}"
            ".mkt-list li{display:flex;align-items:center;justify-content:space-between;"
            "gap:.5rem;padding:.45rem 0;border-top:1px solid #2a2a2a;font-size:.9rem}"
            ".mkt-list button{width:auto;margin:0;padding:.35rem .55rem;font-size:.8rem;"
            "background:#333;color:#eee}"
            ".hint{font-size:.8rem;color:#888;margin:.35rem 0 0;line-height:1.35}"
            "button.secondary{background:#333;color:#eee;margin-top:.5rem}"
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

  String marketJson;
  if (!marketConfigGetJson(marketJson))
    marketJson =
        F("{\"v\":1,\"d\":{\"KS\":1,\"KQ\":1,\"Nasdaq\":1,\"USD\":1},\"c\":[]}");

  const bool blAutoOn = blCtrlAutoEnabled();

  html += F("<section class=box>"
            "<div class=box-head>"
            "<h1>Backlight</h1>"
            "<label class=switch title=\"Auto brightness on/off\">"
            "<input type=checkbox id=bl_auto");
  if (blAutoOn)
    html += F(" checked");
  html += F(">"
            "<span class=slider></span>"
            "</label></div>"
            "<p class=hint>On: brightness follows ambient light (VEML7700). "
            "Off: full brightness.</p>"
            "<p class=msg id=bl_msg style=\"display:none\"></p>"
            "</section>");

  html += F("<section class=box>"
            "<div class=box-head>"
            "<h1>Stock Market</h1>"
            "<label class=switch title=\"Stock Market on/off\">"
            "<input type=checkbox id=mkt_vis checked>"
            "<span class=slider></span>"
            "</label></div>"
            "<div class=mkt-body id=mkt_body>"
            "<div class=chk><label><input type=checkbox id=mkt_KS> KS (KOSPI)</label></div>"
            "<div class=chk><label><input type=checkbox id=mkt_KQ> KQ (KOSDAQ)</label></div>"
            "<div class=chk><label><input type=checkbox id=mkt_Nasdaq> Nasdaq</label></div>"
            "<div class=chk><label><input type=checkbox id=mkt_USD> USD (USD/KRW)</label></div>"
            "<p class=hint>Custom: English name on the ticker + Naver code "
            "(e.g. Samsung / 005930). Max 4 custom, 8 quotes total.</p>"
            "<div class=mkt-add>"
            "<div><label for=mkt_name>Name</label>"
            "<input id=mkt_name maxlength=15 placeholder=\"Samsung\"></div>"
            "<div><label for=mkt_code>Code</label>"
            "<input id=mkt_code maxlength=15 placeholder=\"005930\"></div>"
            "<button type=button id=mkt_add>Add</button>"
            "</div>"
            "<ul class=mkt-list id=mkt_list></ul>"
            "<button type=button id=mkt_save>Save</button>"
            "</div>"
            "<p class=msg id=mkt_msg style=\"display:none\"></p>"
            "</section>");

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
            "function fillScan(list){"
            "const s=document.getElementById('ssid');s.innerHTML='';"
            "if(!list||!list.length){s.innerHTML='<option value=\"\">(scanning…)</option>';return false;}"
            "list.forEach(n=>{"
            "const o=document.createElement('option');"
            "o.value=n.ssid;"
            "o.textContent=n.ssid+' ('+n.rssi+' dBm'+(n.open?', open':'')+')';"
            "s.appendChild(o);});return true;}"
            "function loadScan(left){"
            "fetch('/scan').then(r=>r.json()).then(list=>{"
            "if(fillScan(list)||left<=0){"
            "if(!list||!list.length)document.getElementById('ssid').innerHTML="
            "'<option value=\"\">(none found)</option>';"
            "return;}"
            "setTimeout(()=>loadScan(left-1),400);})"
            ".catch(()=>{document.getElementById('ssid').innerHTML="
            "'<option value=\"\">Scan failed</option>';});}"
            "loadScan(25);"
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
            "const MKT=");
  html += marketJson;
  html += F(";"
            "if(typeof MKT.v==='undefined')MKT.v=1;"
            "function mktCollect(){"
            "MKT.v=document.getElementById('mkt_vis').checked?1:0;"
            "MKT.d={KS:0,KQ:0,Nasdaq:0,USD:0};"
            "['KS','KQ','Nasdaq','USD'].forEach(k=>{"
            "MKT.d[k]=document.getElementById('mkt_'+k).checked?1:0;});"
            "if(!MKT.c)MKT.c=[];}"
            "function mktPost(doneMsg){"
            "mktCollect();"
            "fetch('/market',{method:'POST',headers:{'Content-Type':'application/json'},"
            "body:JSON.stringify(MKT)}).then(r=>r.json()).then(j=>{"
            "if(j&&j.ok){mktMsg(doneMsg||'Saved',true);}else{mktMsg((j&&j.err)||'Save failed',false);}"
            "}).catch(()=>mktMsg('Save failed',false));}"
            "function mktSyncBody(){"
            "const on=!!document.getElementById('mkt_vis').checked;"
            "const body=document.getElementById('mkt_body');"
            "if(body)body.classList.toggle('dim',!on);"
            "['mkt_KS','mkt_KQ','mkt_Nasdaq','mkt_USD','mkt_name','mkt_code',"
            "'mkt_add','mkt_save'].forEach(id=>{"
            "const el=document.getElementById(id);if(el)el.disabled=!on;});"
            "document.querySelectorAll('#mkt_list button').forEach(b=>{b.disabled=!on;});}"
            "function mktRender(){"
            "const vis=document.getElementById('mkt_vis');"
            "if(vis)vis.checked=!!Number(MKT.v);"
            "['KS','KQ','Nasdaq','USD'].forEach(k=>{"
            "const el=document.getElementById('mkt_'+k);"
            "if(el)el.checked=!!(MKT.d&&Number(MKT.d[k]));});"
            "const ul=document.getElementById('mkt_list');ul.innerHTML='';"
            "(MKT.c||[]).forEach((row,i)=>{"
            "const li=document.createElement('li');"
            "li.innerHTML='<span>'+row.t+' <span class=color-hex>('+row.c+')</span></span>';"
            "const b=document.createElement('button');b.type='button';b.textContent='Remove';"
            "b.addEventListener('click',()=>{if(b.disabled)return;MKT.c.splice(i,1);mktRender();});"
            "li.appendChild(b);ul.appendChild(li);});"
            "mktSyncBody();}"
            "function mktMsg(t,ok){"
            "const m=document.getElementById('mkt_msg');"
            "m.style.display='block';m.style.color=ok?'#7d7':'#effa5c';m.textContent=t;}"
            "document.getElementById('mkt_vis').addEventListener('change',()=>{"
            "mktSyncBody();"
            "mktPost(document.getElementById('mkt_vis').checked?'Stock Market On':'Stock Market Off');});"
            "document.getElementById('mkt_add').addEventListener('click',()=>{"
            "if(document.getElementById('mkt_add').disabled)return;"
            "const t=document.getElementById('mkt_name').value.trim();"
            "const c=document.getElementById('mkt_code').value.trim();"
            "if(!t||!c){mktMsg('Name and code required',false);return;}"
            "if(!MKT.c)MKT.c=[];"
            "if(MKT.c.length>=4){mktMsg('Max 4 custom tickers',false);return;}"
            "if(MKT.c.some(x=>x.t.toLowerCase()===t.toLowerCase()||x.c.toLowerCase()===c.toLowerCase())){"
            "mktMsg('Duplicate name or code',false);return;}"
            "MKT.c.push({t:t,c:c});"
            "document.getElementById('mkt_name').value='';"
            "document.getElementById('mkt_code').value='';"
            "mktRender();mktMsg('Added — tap Save',true);});"
            "document.getElementById('mkt_save').addEventListener('click',()=>{"
            "if(document.getElementById('mkt_save').disabled)return;"
            "mktPost('Saved');});"
            "mktRender();"
            "function blMsg(t,ok){"
            "const m=document.getElementById('bl_msg');"
            "m.style.display='block';m.style.color=ok?'#7d7':'#effa5c';m.textContent=t;}"
            "document.getElementById('bl_auto').addEventListener('change',()=>{"
            "const on=!!document.getElementById('bl_auto').checked;"
            "fetch('/backlight',{method:'POST',headers:{'Content-Type':"
            "'application/x-www-form-urlencoded'},body:'auto='+(on?'1':'0')})"
            ".then(r=>r.json()).then(j=>{"
            "if(j&&j.ok){blMsg(on?'Auto brightness On':'Auto brightness Off',true);}"
            "else{blMsg('Save failed',false);}})"
            ".catch(()=>blMsg('Save failed',false));});"
            "</script></body></html>");

  server.send(200, "text/html", html);
}

static String scanCache = "[]";
static uint32_t scanStartedMs = 0;
static uint32_t scanFinishedMs = 0;
static bool scanInFlight = false;

static void handleScan() {
  // One scan per page open. The page polls /scan many times; restarting a
  // scan on every empty/failed poll locks the Wi‑Fi driver (and the LCD)
  // for tens of seconds.
  const int16_t st = WiFi.scanComplete();
  if (st == WIFI_SCAN_RUNNING) {
    server.send(200, "application/json", "[]");
    return;
  }

  if (st >= 0) {
    String json = "[";
    for (int i = 0; i < st; ++i) {
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
    scanCache = json;
    scanInFlight = false;
    scanFinishedMs = millis();
    WiFi.scanDelete();
    server.send(200, "application/json", scanCache);
    return;
  }

  // Finished (even with zero APs) — keep serving the cache. An empty list
  // makes the page poll again; starting another scan each time is the freeze.
  if (scanFinishedMs && (millis() - scanFinishedMs) < 20000) {
    server.send(200, "application/json", scanCache);
    return;
  }

  if (scanInFlight && (millis() - scanStartedMs) < 20000) {
    server.send(200, "application/json", scanCache);
    return;
  }

  WiFi.scanNetworks(/*async=*/true, /*hidden=*/true);
  scanInFlight = true;
  scanStartedMs = millis();
  server.send(200, "application/json", "[]");
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

static void handleBacklight() {
  const String autoArg = server.arg("auto");
  if (!autoArg.length()) {
    server.send(400, "application/json", "{\"ok\":false}");
    return;
  }
  const bool on = autoArg != "0" && autoArg != "false" && autoArg != "off";
  blCtrlSetAutoEnabled(on);
  server.send(200, "application/json", "{\"ok\":true}");
}

static void handleMarketGet() {
  String json;
  if (!marketConfigGetJson(json))
    json =
        F("{\"v\":1,\"d\":{\"KS\":1,\"KQ\":1,\"Nasdaq\":1,\"USD\":1},\"c\":[]}");
  server.send(200, "application/json", json);
}

static void handleMarketPost() {
  String body = server.arg("plain");
  if (!body.length())
    body = server.arg("body");
  String err;
  if (!marketConfigApplyJson(body.c_str(), &err)) {
    String resp = "{\"ok\":false,\"err\":\"";
    resp += htmlEscape(err.length() ? err : String("invalid"));
    resp += "\"}";
    server.send(400, "application/json", resp);
    return;
  }
  // NVS only here — HTTPS runs on a background task (see marketFetch).
  marketFetchRequestRefresh();
  server.send(200, "application/json", "{\"ok\":true}");
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
  server.on("/backlight", HTTP_POST, handleBacklight);
  server.on("/market", HTTP_GET, handleMarketGet);
  server.on("/market", HTTP_POST, handleMarketPost);
  // Captive portal probes
  server.on("/generate_204", HTTP_GET, handleCaptive);
  server.on("/hotspot-detect.html", HTTP_GET, handleCaptive);
  server.on("/connecttest.txt", HTTP_GET, handleCaptive);
  server.on("/ncsi.txt", HTTP_GET, handleCaptive);
  // Short 302 — do not rebuild the full config page for probes.
  server.onNotFound(handleCaptive);
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

static void applyRadioOff() {
  stopMdns();
  stopSoftAp();
  if (httpStarted) {
    server.stop();
    httpStarted = false;
  }
  WiFi.scanDelete();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  publishLinkFlags();
  powerSetRadioActive(false);
  Serial.println("WiFi radio off (config/NTP idle)");
}

static bool applyWakeSta() {
  powerSetRadioActive(true);
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
  WiFi.scanDelete();

  if (!connectSta(ssid, pass))
    return false;

  WiFi.mode(WIFI_STA);
  publishLinkFlags();
  Serial.println("WiFi wake for NTP sync");
  return true;
}

static void portalWorker(void * /*arg*/) {
  for (;;) {
    const PortalCmd cmd = portalPending;
    if (cmd != PortalCmd::None) {
      portalPending = PortalCmd::None;
      portalBusy = true;
      if (cmd == PortalCmd::RadioOff)
        applyRadioOff();
      else if (cmd == PortalCmd::WakeSta)
        (void)applyWakeSta();
      publishLinkFlags();
      portalBusy = false;
    } else if (cachedRadioOn) {
      if (softApActive)
        dnsServer.processNextRequest();
      if (httpStarted)
        server.handleClient();
      publishLinkFlags();
    }
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}

static void startPortalTask() {
  if (portalTaskHandle)
    return;
  // HTTPS market worker is also on core 0; keep portal below UI priority.
  xTaskCreatePinnedToCore(portalWorker, "portal", 12288, nullptr, 1,
                          &portalTaskHandle, 0);
  portalTaskStarted = true;
  Serial.println("WiFi portal task started");
}

static void queuePortalCmd(PortalCmd cmd) {
  if (!portalTaskStarted) {
    portalBusy = true;
    if (cmd == PortalCmd::RadioOff)
      applyRadioOff();
    else if (cmd == PortalCmd::WakeSta)
      (void)applyWakeSta();
    publishLinkFlags();
    portalBusy = false;
    return;
  }
  portalPending = cmd;
}

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

  publishLinkFlags();
  // Own DNS/HTTP/scan off the LVGL loop from here on.
  startPortalTask();
}

void wifiConfigLoop() {
  // Portal task owns DNS/HTTP; kept for API compatibility.
}

bool wifiConfigIsConnected() { return cachedStaUp; }

bool wifiConfigIsRadioOn() { return cachedRadioOn; }

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

void wifiConfigRadioOff() { queuePortalCmd(PortalCmd::RadioOff); }

bool wifiConfigRequestWake() {
  String ssid, pass;
  loadCreds(ssid, pass);
  if (!ssid.length()) {
    Serial.println("WiFi wake skipped (no saved creds)");
    return false;
  }
  queuePortalCmd(PortalCmd::WakeSta);
  return true;
}

bool wifiConfigCmdIdle() {
  return portalPending == PortalCmd::None && !portalBusy;
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
bool wifiConfigRequestWake() { return false; }
bool wifiConfigCmdIdle() { return true; }
uint32_t wifiConfigDateColor() { return 0xEFFA5C; }
uint32_t wifiConfigClockColor() { return 0x00F0FF; }
bool wifiConfigTakeColorsChanged() { return false; }

#endif
