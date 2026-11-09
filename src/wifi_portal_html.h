#pragma once
// Auto-generated from web/wifi_portal.html — do not edit by hand.
// Run: python3 scripts/embed_wifi_portal.py  (also via PlatformIO pre)
static const char WIFI_PORTAL_HTML[] = R"WIFIHTML(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>SmartClock WiFi</title>
  <style>
    body{font-family:system-ui,sans-serif;max-width:28rem;margin:1.5rem auto;padding:0 1rem 2rem;background:#111;color:#eee}
    .brand{font-size:1.75rem;font-weight:700;margin:0;padding-bottom:1.0rem;color:#eee;text-align:center;letter-spacing:.02em}
    .box{background:#1a1a1a;border:1px solid #333;border-radius:.75rem;padding:1rem 1rem 1.15rem;margin:1rem 0}
    .box h1{font-size:1.15rem;margin:0 0 .85rem;padding-bottom:.65rem;border-bottom:1px solid #333;font-weight:600}
    .box-head{display:flex;align-items:center;justify-content:space-between;gap:.75rem;margin:0 0 .85rem;padding-bottom:.65rem;border-bottom:1px solid #333}
    .box-head h1{margin:0;padding:0;border:none;font-size:1.15rem;font-weight:600}
    label{display:block;margin:.75rem 0 .25rem}
    input,select,button{width:100%;box-sizing:border-box;padding:.6rem;font-size:1rem;border-radius:.4rem;border:1px solid #444;background:#222;color:#eee}
    button{margin-top:.75rem;background:#0af;border:none;color:#000;font-weight:600;cursor:pointer}
    .status{font-size:.9rem;line-height:1.5;color:#bbb;margin:0 0 1rem}
    .status a{color:#bbb;text-decoration:none}
    .msg{color:#effa5c;margin:.5rem 0}
    .color-row{display:flex;align-items:center;gap:.75rem;margin-bottom:.75rem}
    button.color-box{width:2.75rem;height:2.75rem;margin:0;padding:0;flex:0 0 auto;border:2px solid #666;border-radius:.45rem;cursor:pointer}
    .color-hex{font-family:ui-monospace,monospace;color:#aaa;font-size:.9rem}
    .preview{display:inline-block;min-width:4.5rem;padding:.2rem .5rem;border-radius:.3rem;background:#000;font-weight:600}
    .chk{display:flex;align-items:center;gap:.6rem;margin:.45rem 0;font-size:.95rem}
    .chk input{width:auto;margin:0}
    .switch{position:relative;display:inline-block;width:3.1rem;height:1.75rem;flex:0 0 auto}
    .switch input{opacity:0;width:0;height:0;position:absolute}
    .slider{position:absolute;cursor:pointer;inset:0;background:#444;border-radius:999px;transition:background .2s}
    .slider:before{position:absolute;content:"";height:1.35rem;width:1.35rem;left:.2rem;top:.2rem;background:#eee;border-radius:50%;transition:transform .2s}
    .switch input:checked+.slider{background:#0af}
    .switch input:checked+.slider:before{transform:translateX(1.35rem)}
    .mkt-body{transition:opacity .2s}
    .mkt-body.dim{opacity:.38;pointer-events:none}
    .mkt-body.dim button{background:#555;color:#999}
    .mkt-add{display:grid;grid-template-columns:1fr 1fr auto;gap:.4rem;align-items:end;margin-top:.5rem}
    .mkt-add label{margin:0}
    .mkt-add button{margin:0;width:auto;padding:.55rem .75rem}
    .mkt-list{list-style:none;padding:0;margin:.6rem 0 0}
    .mkt-list li{display:flex;align-items:center;justify-content:space-between;gap:.5rem;padding:.45rem 0;border-top:1px solid #2a2a2a;font-size:.9rem}
    .mkt-list button{width:auto;margin:0;padding:.35rem .55rem;font-size:.8rem;background:#333;color:#eee}
    .hint{font-size:.8rem;color:#888;margin:.35rem 0 0;line-height:1.35}
    button.secondary{background:#333;color:#eee;margin-top:.5rem}
    .sheet-bg{position:fixed;inset:0;background:rgba(0,0,0,.55);z-index:20;opacity:0;pointer-events:none;transition:opacity .2s}
    .sheet-bg.open{opacity:1;pointer-events:auto}
    .sheet{position:fixed;left:0;right:0;bottom:0;z-index:21;background:#1a1a1a;border-radius:1rem 1rem 0 0;padding:1rem 1rem 1.5rem;transform:translateY(110%);transition:transform .25s ease;max-width:28rem;margin:0 auto;box-shadow:0 -8px 24px rgba(0,0,0,.4)}
    .sheet.open{transform:translateY(0)}
    .sheet-handle{width:2.5rem;height:.25rem;background:#555;border-radius:99px;margin:0 auto .75rem}
    .sheet-title{text-align:center;font-size:1rem;margin:0 0 1rem}
    .sheet-grid{display:grid;grid-template-columns:repeat(10,1fr);gap:.35rem}
    button.swatch{width:100%;aspect-ratio:1;margin:0;padding:0;border-radius:.3rem;border:1px solid #333;cursor:pointer}
    button.swatch.on{border-color:#fff;box-shadow:0 0 0 1px #fff}
    .footer{margin:1.5rem 0 0;padding-top:1rem;border-top:1px solid #333;font-size:.95rem;line-height:1.45;color:rgba(255,255,255,.8);text-align:center;font-family:ui-monospace,monospace}
  </style>
</head>
<body>
  <p class="brand">SmartClock</p>
  {{MSG_BLOCK}}
  <section class="box">
    <h1>WiFi settings</h1>
    <p class="status">SSID: {{SSID}}<br>IP: {{IP}}{{MDNS_LINE}}<br>Time zone: KST (UTC+9)</p>
    <form method="POST" action="/connect">
      <label for="ssid">Network</label>
      <select id="ssid" name="ssid">
        <option value="">Scanning…</option>
      </select>
      <label for="manual">Or type SSID</label>
      <input id="manual" name="manual" placeholder="Hidden / typed SSID">
      <label for="pass">Password</label>
      <input id="pass" name="pass" type="password" placeholder="(blank if open)">
      <button type="submit">Connect</button>
    </form>
  </section>

  <section class="box">
    <h1>Colors</h1>
    <div id="colors">
      <label>Date</label>
      <div class="color-row">
        <button type="button" class="color-box" id="date_box" data-target="date_color" style="background:{{DATE_COLOR}}"></button>
        <input type="hidden" id="date_color" value="{{DATE_COLOR}}">
        <span class="preview" id="date_preview" style="color:{{DATE_COLOR}}">2026 10/26 Mon</span>
        <span class="color-hex" id="date_hex">{{DATE_COLOR}}</span>
      </div>
      <label>HH / MM</label>
      <div class="color-row">
        <button type="button" class="color-box" id="clock_box" data-target="clock_color" style="background:{{CLOCK_COLOR}}"></button>
        <input type="hidden" id="clock_color" value="{{CLOCK_COLOR}}">
        <span class="preview" id="clock_preview" style="color:{{CLOCK_COLOR}}">23:59</span>
        <span class="color-hex" id="clock_hex">{{CLOCK_COLOR}}</span>
      </div>
    </div>
  </section>

  <section class="box">
    <div class="box-head">
      <h1>Backlight</h1>
      <label class="switch" title="Auto brightness on/off">
        <input type="checkbox" id="bl_auto"{{BL_CHECKED}}>
        <span class="slider"></span>
      </label>
    </div>
    <p class="hint">On: brightness follows ambient light. Off: full brightness.</p>
    <p class="msg" id="bl_msg" style="display:none"></p>
  </section>

  <section class="box">
    <div class="box-head">
      <h1>Weather</h1>
    </div>
    <p class="hint">Outdoor temp / humidity / icon source. The other provider is used if the selected one fails.</p>
    <div class="chk">
      <label><input type="radio" name="wx_src" id="wx_naver" value="naver"{{WX_NAVER_CHECKED}}> Naver</label>
    </div>
    <div class="chk">
      <label><input type="radio" name="wx_src" id="wx_ow" value="openweather"{{WX_OW_CHECKED}}> OpenWeather</label>
    </div>
    <p class="msg" id="wx_msg" style="display:none"></p>
  </section>

  <section class="box">
    <div class="box-head">
      <h1>Stock Market</h1>
      <label class="switch" title="Stock Market on/off">
        <input type="checkbox" id="mkt_vis" checked>
        <span class="slider"></span>
      </label>
    </div>
    <div class="mkt-body" id="mkt_body">
      <div class="chk"><label><input type="checkbox" id="mkt_KS"> KS (KOSPI)</label></div>
      <div class="chk"><label><input type="checkbox" id="mkt_KQ"> KQ (KOSDAQ)</label></div>
      <div class="chk"><label><input type="checkbox" id="mkt_Nasdaq"> Nasdaq</label></div>
      <div class="chk"><label><input type="checkbox" id="mkt_USD"> USD (USD/KRW)</label></div>
      <p class="hint">Custom: English name on the ticker + Naver code (e.g. Samsung / 005930). Max 4 custom, 8 quotes total.</p>
      <div class="mkt-add">
        <div>
          <label for="mkt_name">Name</label>
          <input id="mkt_name" maxlength="15" placeholder="Samsung">
        </div>
        <div>
          <label for="mkt_code">Code</label>
          <input id="mkt_code" maxlength="15" placeholder="005930">
        </div>
        <button type="button" id="mkt_add">Add</button>
      </div>
      <ul class="mkt-list" id="mkt_list"></ul>
      <button type="button" id="mkt_save">Save</button>
    </div>
    <p class="msg" id="mkt_msg" style="display:none"></p>
  </section>

  <p class="footer">{{FLASH_LINE}}</p>
  <div class="sheet-bg" id="sheet_bg"></div>
  <div class="sheet" id="sheet">
    <div class="sheet-handle"></div>
    <p class="sheet-title" id="sheet_title">Pick a color</p>
    <div class="sheet-grid" id="sheet_grid"></div>
  </div>

  <script>
    function fillScan(list){
      const s=document.getElementById('ssid');s.innerHTML='';
      if(!list||!list.length){s.innerHTML='<option value="">(scanning…)</option>';return false;}
      list.forEach(n=>{
        const o=document.createElement('option');
        o.value=n.ssid;
        o.textContent=n.ssid+' ('+n.rssi+' dBm'+(n.open?', open':'')+')';
        s.appendChild(o);
      });
      return true;
    }
    function loadScan(left){
      fetch('/scan').then(r=>r.json()).then(list=>{
        if(fillScan(list)||left<=0){
          if(!list||!list.length)document.getElementById('ssid').innerHTML='<option value="">(none found)</option>';
          return;
        }
        setTimeout(()=>loadScan(left-1),400);
      }).catch(()=>{document.getElementById('ssid').innerHTML='<option value="">Scan failed</option>';});
    }
    loadScan(25);
    document.querySelector('form[action="/connect"]').addEventListener('submit',e=>{
      const m=document.getElementById('manual').value.trim();
      if(m){document.getElementById('ssid').value=m;}
    });
    function hslToHex(h,s,l){
      s/=100;l/=100;
      const a=s*Math.min(l,1-l);
      const f=n=>{const k=(n+h/30)%12;const c=l-a*Math.max(Math.min(k-3,9-k,1),-1);return Math.round(255*c).toString(16).padStart(2,'0');};
      return('#'+f(0)+f(8)+f(4)).toUpperCase();
    }
    const PAL=(()=>{
      const out=[];
      for(let col=0;col<10;col++){
        const v=Math.round(255*(1-col/9));
        const hx=v.toString(16).padStart(2,'0').toUpperCase();
        out.push('#'+hx+hx+hx);
      }
      for(let row=1;row<10;row++){
        for(let col=0;col<10;col++){
          out.push(hslToHex(col*36,row<3?55:88,90-row*8));
        }
      }
      out[11]='#EFFA5C';out[16]='#00F0FF';
      return out;
    })();
    let activeId=null;
    const bg=document.getElementById('sheet_bg');
    const sheet=document.getElementById('sheet');
    const grid=document.getElementById('sheet_grid');
    function syncColorUI(id){
      const hex=document.getElementById(id).value.toUpperCase();
      const prefix=id.replace('_color','');
      document.getElementById(prefix+'_hex').textContent=hex;
      document.getElementById(prefix+'_preview').style.color=hex;
      document.getElementById(prefix+'_box').style.background=hex;
      grid.querySelectorAll('.swatch').forEach(x=>{
        x.classList.toggle('on',(x.dataset.color||'').toLowerCase()===hex.toLowerCase());
      });
    }
    function applyColorsNow(){
      const body='date_color='+encodeURIComponent(document.getElementById('date_color').value)
        +'&clock_color='+encodeURIComponent(document.getElementById('clock_color').value);
      fetch('/colors',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:body}).catch(()=>{});
    }
    function closeSheet(){
      bg.classList.remove('open');sheet.classList.remove('open');activeId=null;
    }
    function openSheet(id,title){
      activeId=id;
      document.getElementById('sheet_title').textContent=title;
      syncColorUI(id);
      bg.classList.add('open');sheet.classList.add('open');
    }
    PAL.forEach(c=>{
      const b=document.createElement('button');
      b.type='button';b.className='swatch';b.style.background=c;b.dataset.color=c;
      b.addEventListener('click',()=>{
        if(!activeId)return;
        document.getElementById(activeId).value=c;
        syncColorUI(activeId);
        applyColorsNow();
        closeSheet();
      });
      grid.appendChild(b);
    });
    document.getElementById('date_box').addEventListener('click',()=>openSheet('date_color','Date color'));
    document.getElementById('clock_box').addEventListener('click',()=>openSheet('clock_color','HH / MM color'));
    bg.addEventListener('click',closeSheet);
    syncColorUI('date_color');syncColorUI('clock_color');
    const MKT={{MARKET_JSON}};
    if(typeof MKT.v==='undefined')MKT.v=1;
    function mktCollect(){
      MKT.v=document.getElementById('mkt_vis').checked?1:0;
      MKT.d={KS:0,KQ:0,Nasdaq:0,USD:0};
      ['KS','KQ','Nasdaq','USD'].forEach(k=>{
        MKT.d[k]=document.getElementById('mkt_'+k).checked?1:0;
      });
      if(!MKT.c)MKT.c=[];
    }
    function mktPost(doneMsg){
      mktCollect();
      fetch('/market',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(MKT)})
        .then(r=>r.json()).then(j=>{
          if(j&&j.ok){mktMsg(doneMsg||'Saved',true);}else{mktMsg((j&&j.err)||'Save failed',false);}
        }).catch(()=>mktMsg('Save failed',false));
    }
    function mktSyncBody(){
      const on=!!document.getElementById('mkt_vis').checked;
      const body=document.getElementById('mkt_body');
      if(body)body.classList.toggle('dim',!on);
      ['mkt_KS','mkt_KQ','mkt_Nasdaq','mkt_USD','mkt_name','mkt_code','mkt_add','mkt_save'].forEach(id=>{
        const el=document.getElementById(id);if(el)el.disabled=!on;
      });
      document.querySelectorAll('#mkt_list button').forEach(b=>{b.disabled=!on;});
    }
    function mktRender(){
      const vis=document.getElementById('mkt_vis');
      if(vis)vis.checked=!!Number(MKT.v);
      ['KS','KQ','Nasdaq','USD'].forEach(k=>{
        const el=document.getElementById('mkt_'+k);
        if(el)el.checked=!!(MKT.d&&Number(MKT.d[k]));
      });
      const ul=document.getElementById('mkt_list');ul.innerHTML='';
      (MKT.c||[]).forEach((row,i)=>{
        const li=document.createElement('li');
        li.innerHTML='<span>'+row.t+' <span class=color-hex>('+row.c+')</span></span>';
        const b=document.createElement('button');b.type='button';b.textContent='Remove';
        b.addEventListener('click',()=>{if(b.disabled)return;MKT.c.splice(i,1);mktRender();});
        li.appendChild(b);ul.appendChild(li);
      });
      mktSyncBody();
    }
    function mktMsg(t,ok){
      const m=document.getElementById('mkt_msg');
      m.style.display='block';m.style.color=ok?'#7d7':'#effa5c';m.textContent=t;
    }
    document.getElementById('mkt_vis').addEventListener('change',()=>{
      mktSyncBody();
      mktPost(document.getElementById('mkt_vis').checked?'Stock Market On':'Stock Market Off');
    });
    document.getElementById('mkt_add').addEventListener('click',()=>{
      if(document.getElementById('mkt_add').disabled)return;
      const t=document.getElementById('mkt_name').value.trim();
      const c=document.getElementById('mkt_code').value.trim();
      if(!t||!c){mktMsg('Name and code required',false);return;}
      if(!MKT.c)MKT.c=[];
      if(MKT.c.length>=4){mktMsg('Max 4 custom tickers',false);return;}
      if(MKT.c.some(x=>x.t.toLowerCase()===t.toLowerCase()||x.c.toLowerCase()===c.toLowerCase())){
        mktMsg('Duplicate name or code',false);return;
      }
      MKT.c.push({t:t,c:c});
      document.getElementById('mkt_name').value='';
      document.getElementById('mkt_code').value='';
      mktRender();mktMsg('Added — tap Save',true);
    });
    document.getElementById('mkt_save').addEventListener('click',()=>{
      if(document.getElementById('mkt_save').disabled)return;
      mktPost('Saved');
    });
    mktRender();
    function blMsg(t,ok){
      const m=document.getElementById('bl_msg');
      m.style.display='block';m.style.color=ok?'#7d7':'#effa5c';m.textContent=t;
    }
    document.getElementById('bl_auto').addEventListener('change',()=>{
      const on=!!document.getElementById('bl_auto').checked;
      fetch('/backlight',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'auto='+(on?'1':'0')})
        .then(r=>r.json()).then(j=>{
          if(j&&j.ok){blMsg(on?'Auto brightness On':'Auto brightness Off',true);}
          else{blMsg('Save failed',false);}
        }).catch(()=>blMsg('Save failed',false));
    });
    function wxMsg(t,ok){
      const m=document.getElementById('wx_msg');
      m.style.display='block';m.style.color=ok?'#7d7':'#effa5c';m.textContent=t;
    }
    function wxPost(src){
      fetch('/weather',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'src='+encodeURIComponent(src)})
        .then(r=>r.json()).then(j=>{
          if(j&&j.ok){wxMsg(src==='naver'?'Naver':'OpenWeather',true);}
          else{wxMsg('Save failed',false);}
        }).catch(()=>wxMsg('Save failed',false));
    }
    document.getElementById('wx_naver').addEventListener('change',()=>{
      if(document.getElementById('wx_naver').checked)wxPost('naver');
    });
    document.getElementById('wx_ow').addEventListener('change',()=>{
      if(document.getElementById('wx_ow').checked)wxPost('openweather');
    });
  </script>
</body>
</html>
)WIFIHTML";
