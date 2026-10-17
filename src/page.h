#pragma once

static const char PAGE_HTML[] PROGMEM = R"HTML(<!doctype html>
<html><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Freddy Radar</title>
<style>
body{font-family:ui-monospace,Menlo,monospace;background:#111;color:#ddd;margin:0;padding:12px}
h1{font-size:18px;margin:0 0 8px}
#conn{font-size:12px;color:#888}
.r{border:1px solid #333;border-radius:8px;padding:10px;margin:10px 0}
.p{font-size:28px;font-weight:bold;margin:4px 0}
.on{color:#4f4}.off{color:#666}
pre{margin:6px 0 0;font-size:13px;line-height:1.3}
</style></head><body>
<h1>Freddy Radar <span id="conn">connecting…</span></h1>
<div id="rs"></div>
<script>
const bar=(v)=>'#'.repeat(Math.round(v/5)).padEnd(20,'.');
function render(d){
  document.getElementById('rs').innerHTML=d.radars.map(r=>{
    if(!r.online)return `<div class="r">R${r.id} <span class="off">offline</span> · OUT=${+r.out}</div>`;
    let g='gate  dist  moving               stationary\n';
    for(let i=0;i<r.mGates.length;i++)
      g+=`${i}   ${(i*0.75).toFixed(2)}m ${bar(r.mGates[i])} ${bar(r.sGates[i])}\n`;
    return `<div class="r">R${r.id}
<div class="p ${r.presence?'on':'off'}">${r.presence?'PRESENT':'empty'}</div>
<pre>moving     ${r.moving?r.mDist+'cm  e'+r.mSig:'-'}
stationary ${r.stationary?r.sDist+'cm  e'+r.sSig:'-'}
OUT pin    ${+r.out}
${r.mGates.length?g:''}</pre></div>`;
  }).join('');
}
function connect(){
  const c=document.getElementById('conn');
  const ws=new WebSocket(`ws://${location.host}/ws`);
  ws.onopen=()=>c.textContent='live';
  ws.onmessage=(e)=>render(JSON.parse(e.data));
  ws.onclose=()=>{c.textContent='reconnecting…';setTimeout(connect,1000)};
}
connect();
</script></body></html>
)HTML";
