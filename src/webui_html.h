#pragma once

// Single-page dashboard served from flash at GET /. Connects back to /ws for
// live updates and POSTs /reset for the zero-totals button.
static const char WEBUI_INDEX_HTML[] = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Juicemeter</title>
<style>
  :root{
    --bg:#0b0d10;--panel:#14181d;--line:#222a33;--fg:#e6edf3;--dim:#8b949e;
    --red:#ff6b6b;--green:#51cf66;--yellow:#ffd43b;--orange:#ff922b;--cyan:#66d9e8;
  }
  *{box-sizing:border-box}
  html,body{margin:0;background:var(--bg);color:var(--fg);
    font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif}
  .num{font-family:'SF Mono',ui-monospace,Consolas,monospace;font-variant-numeric:tabular-nums}
  header{display:flex;align-items:center;justify-content:space-between;
    padding:12px 20px;border-bottom:1px solid var(--line);flex-wrap:wrap;gap:10px}
  header h1{font-size:14px;font-weight:600;margin:0;letter-spacing:.04em;
    text-transform:uppercase;color:var(--dim)}
  .meta{display:flex;gap:14px;align-items:center;font-size:13px;color:var(--dim);flex-wrap:wrap}
  .dot{width:8px;height:8px;border-radius:50%;background:var(--dim);
    display:inline-block;margin-right:6px;vertical-align:middle}
  .dot.live{background:var(--green);box-shadow:0 0 6px var(--green)}
  .state{font-size:13px;font-weight:600;padding:3px 10px;border-radius:3px;letter-spacing:.08em}
  .state.IDLE{background:#2a2e35;color:var(--dim)}
  .state.DISCHARGE{background:rgba(255,107,107,.15);color:var(--red)}
  .state.CHARGE{background:rgba(81,207,102,.15);color:var(--green)}
  main{max-width:1100px;margin:0 auto;padding:20px;display:grid;gap:16px}
  .tiles{display:grid;grid-template-columns:repeat(3,1fr);gap:12px}
  @media (max-width:640px){.tiles{grid-template-columns:1fr}}
  .tile{background:var(--panel);border:1px solid var(--line);border-radius:6px;padding:16px}
  .tile .label{font-size:11px;color:var(--dim);letter-spacing:.1em;text-transform:uppercase}
  .tile .val{font-size:38px;margin-top:4px;line-height:1.1}
  .tile .unit{font-size:14px;color:var(--dim);margin-left:6px}
  .tile.v .val{color:var(--green)}
  .tile.i .val{color:var(--yellow)}
  .tile.p .val{color:var(--orange)}
  .row2{display:grid;grid-template-columns:1fr 1fr;gap:12px}
  @media (max-width:640px){.row2{grid-template-columns:1fr}}
  .secondary{background:var(--panel);border:1px solid var(--line);border-radius:6px;
    padding:14px;display:flex;justify-content:space-between;align-items:baseline}
  .secondary .label{font-size:11px;color:var(--dim);letter-spacing:.1em;text-transform:uppercase}
  .secondary .val{font-size:20px}
  .totals{background:var(--panel);border:1px solid var(--line);border-radius:6px;padding:16px}
  .totals h2,.chart-wrap h2{font-size:11px;color:var(--dim);letter-spacing:.1em;
    text-transform:uppercase;margin:0 0 10px}
  .totals .grid{display:grid;grid-template-columns:auto 1fr 1fr;gap:8px 20px;align-items:baseline}
  .totals .lbl{font-size:13px;color:var(--dim);text-transform:uppercase;letter-spacing:.06em}
  .totals .out{color:var(--red)}
  .totals .in{color:var(--green)}
  .totals .val{font-size:18px}
  .chart-wrap{background:var(--panel);border:1px solid var(--line);border-radius:6px;padding:16px}
  canvas{width:100%;height:260px;display:block}
  .legend{display:flex;gap:16px;font-size:12px;color:var(--dim);margin-top:8px}
  .legend .sw{display:inline-block;width:14px;height:2px;vertical-align:middle;margin-right:6px}
  .actions{display:flex;justify-content:flex-end}
  button{background:#21262d;color:var(--fg);border:1px solid var(--line);
    padding:8px 16px;border-radius:4px;cursor:pointer;font-size:13px}
  button:hover{background:#2d333b}
  button:active{background:#1a1e22}
</style>
</head>
<body>
<header>
  <h1>Juicemeter</h1>
  <div class="meta">
    <span id="state" class="state IDLE">IDLE</span>
    <span class="num" id="elapsed">00:00:00</span>
    <span id="range">--</span>
    <span><span id="dot" class="dot"></span><span id="conn">offline</span></span>
  </div>
</header>
<main>
  <div class="tiles">
    <div class="tile v">
      <div class="label">Battery</div>
      <div class="val num"><span id="v_bat">--.---</span><span class="unit">V</span></div>
    </div>
    <div class="tile i">
      <div class="label">Current</div>
      <div class="val num"><span id="i_mA">---.--</span><span class="unit">mA</span></div>
    </div>
    <div class="tile p">
      <div class="label">Power</div>
      <div class="val num"><span id="p_mW">---.--</span><span class="unit">mW</span></div>
    </div>
  </div>
  <div class="row2">
    <div class="secondary">
      <span class="label">DUT terminal</span>
      <span class="val num" style="color:var(--cyan)"><span id="v_dev">--.---</span> V</span>
    </div>
    <div class="secondary">
      <span class="label">INA bus</span>
      <span class="val num" style="color:var(--dim)"><span id="v_bus">--.---</span> V</span>
    </div>
  </div>
  <div class="totals">
    <h2>Accumulated</h2>
    <div class="grid">
      <span class="lbl out">out</span>
      <span class="val num out"><span id="q_out">0.00</span> mAh</span>
      <span class="val num out"><span id="e_out">0.00</span> mWh</span>
      <span class="lbl in">in</span>
      <span class="val num in"><span id="q_in">0.00</span> mAh</span>
      <span class="val num in"><span id="e_in">0.00</span> mWh</span>
      <span class="lbl">peak</span>
      <span class="val num"><span id="i_peak">0.000</span> mA</span>
      <span class="val num"><span id="p_peak">0.000</span> mW</span>
    </div>
  </div>
  <div class="chart-wrap">
    <h2>History &mdash; last 5 min</h2>
    <canvas id="chart"></canvas>
    <div class="legend">
      <span><span class="sw" style="background:#51cf66"></span>Battery V (left)</span>
      <span><span class="sw" style="background:#ffd43b"></span>Current mA (right, signed)</span>
    </div>
  </div>
  <div class="actions">
    <button id="reset">Reset totals</button>
  </div>
</main>
<script>
const $=id=>document.getElementById(id);
const hist=[];
const HISTORY_MS=5*60*1000;
function fmtTime(s){
  const h=Math.floor(s/3600).toString().padStart(2,'0');
  const m=Math.floor((s/60)%60).toString().padStart(2,'0');
  const x=Math.floor(s%60).toString().padStart(2,'0');
  return h+':'+m+':'+x;
}
function setState(s){const el=$('state');el.className='state '+s;el.textContent=s}
function fmt(v,lo,hi){return Math.abs(v)<1?v.toFixed(hi):v.toFixed(lo)}
function update(d){
  setState(d.state);
  $('elapsed').textContent=fmtTime(d.t);
  $('range').textContent=d.range;
  $('v_bat').textContent=d.v_bat.toFixed(3);
  $('v_dev').textContent=d.v_dev.toFixed(3);
  $('v_bus').textContent=d.v_bus.toFixed(3);
  $('i_mA').textContent=fmt(Math.abs(d.i_mA),2,3);
  $('p_mW').textContent=fmt(Math.abs(d.p_mW),2,3);
  $('q_out').textContent=d.q_out_mAh.toFixed(2);
  $('q_in').textContent=d.q_in_mAh.toFixed(2);
  $('e_out').textContent=d.e_out_mWh.toFixed(2);
  $('e_in').textContent=d.e_in_mWh.toFixed(2);
  $('i_peak').textContent=d.i_peak_mA.toFixed(3);
  $('p_peak').textContent=d.p_peak_mW.toFixed(3);
  const now=performance.now();
  hist.push({t:now,v:Math.round(d.v_bat*1000)/1000,i:d.i_mA});
  while(hist.length&&now-hist[0].t>HISTORY_MS)hist.shift();
  drawChart();
}
function drawChart(){
  const c=$('chart');
  const dpr=window.devicePixelRatio||1;
  const w=c.clientWidth,h=c.clientHeight;
  if(c.width!==Math.round(w*dpr)){c.width=Math.round(w*dpr);c.height=Math.round(h*dpr)}
  const ctx=c.getContext('2d');
  ctx.setTransform(dpr,0,0,dpr,0,0);
  ctx.clearRect(0,0,w,h);
  if(hist.length<2)return;
  const pad={l:52,r:52,t:10,b:22};
  const pw=w-pad.l-pad.r,ph=h-pad.t-pad.b;
  const t0=hist[0].t,t1=hist[hist.length-1].t;
  const ts=Math.max(t1-t0,1);
  let vMin=Infinity,vMax=-Infinity,iMin=Infinity,iMax=-Infinity;
  for(const p of hist){
    if(p.v<vMin)vMin=p.v;if(p.v>vMax)vMax=p.v;
    if(p.i<iMin)iMin=p.i;if(p.i>iMax)iMax=p.i;
  }
  if(vMax-vMin<0.01){const m=(vMin+vMax)/2;vMin=m-0.01;vMax=m+0.01}
  const iPad=Math.max((iMax-iMin)*0.1,0.5);
  iMin-=iPad;iMax+=iPad;
  if(iMin>0)iMin=0;if(iMax<0)iMax=0;
  const xOf=p=>pad.l+((p.t-t0)/ts)*pw;
  const yV=v=>pad.t+(1-(v-vMin)/(vMax-vMin))*ph;
  const yI=i=>pad.t+(1-(i-iMin)/(iMax-iMin))*ph;
  ctx.strokeStyle='#222a33';ctx.lineWidth=1;ctx.beginPath();
  for(let k=0;k<=4;k++){const y=pad.t+(ph*k)/4;ctx.moveTo(pad.l,y);ctx.lineTo(pad.l+pw,y)}
  ctx.stroke();
  if(iMin<0&&iMax>0){
    ctx.strokeStyle='#3a444e';ctx.setLineDash([3,3]);ctx.beginPath();
    const yz=yI(0);ctx.moveTo(pad.l,yz);ctx.lineTo(pad.l+pw,yz);ctx.stroke();
    ctx.setLineDash([]);
  }
  ctx.fillStyle='#8b949e';ctx.font='11px ui-monospace,monospace';
  ctx.textAlign='right';
  for(let k=0;k<=4;k++){
    const v=vMax-((vMax-vMin)*k)/4;
    ctx.fillText(v.toFixed(3),pad.l-6,pad.t+(ph*k)/4+4);
  }
  ctx.textAlign='left';
  for(let k=0;k<=4;k++){
    const i=iMax-((iMax-iMin)*k)/4;
    ctx.fillText(i.toFixed(1),pad.l+pw+6,pad.t+(ph*k)/4+4);
  }
  ctx.strokeStyle='#51cf66';ctx.lineWidth=1.5;ctx.beginPath();
  for(let k=0;k<hist.length;k++){
    const p=hist[k],x=xOf(p),y=yV(p.v);
    if(k===0)ctx.moveTo(x,y);else ctx.lineTo(x,y);
  }
  ctx.stroke();
  ctx.strokeStyle='#ffd43b';ctx.beginPath();
  for(let k=0;k<hist.length;k++){
    const p=hist[k],x=xOf(p),y=yI(p.i);
    if(k===0)ctx.moveTo(x,y);else ctx.lineTo(x,y);
  }
  ctx.stroke();
}
let ws;
function connect(){
  const url=(location.protocol==='https:'?'wss://':'ws://')+location.host+'/ws';
  ws=new WebSocket(url);
  ws.onopen=()=>{$('dot').classList.add('live');$('conn').textContent='live'};
  ws.onclose=()=>{$('dot').classList.remove('live');$('conn').textContent='offline';
    setTimeout(connect,1500)};
  ws.onmessage=e=>{try{update(JSON.parse(e.data))}catch(err){}};
}
connect();
$('reset').addEventListener('click',async()=>{
  try{await fetch('/reset',{method:'POST'})}catch(e){}
});
window.addEventListener('resize',drawChart);
</script>
</body>
</html>)HTML";
