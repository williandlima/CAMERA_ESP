#pragma once
#include <pgmspace.h>

static const char PAGE_LOGIN[] PROGMEM = R"HTML(<!doctype html><html lang="pt-br"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Monitor ESP32-CAM</title>
<style>body{font-family:system-ui;background:#111;color:#eee;display:grid;place-items:center;height:100vh;margin:0}
form{background:#1c1c1c;padding:28px;border-radius:12px;display:grid;gap:12px;width:260px}
input,button{padding:10px;border-radius:8px;border:0;font-size:16px}button{background:#2d7ff9;color:#fff}
.e{color:#f66;display:none}</style></head><body>
<form method="POST" action="/login"><h3 style="margin:0">Monitor de ambiente</h3>
<input type="password" name="password" placeholder="Senha" autofocus required>
<button>Entrar</button><div class="e" id="e">Senha incorreta</div></form>
<script>if(location.search.includes('err'))document.getElementById('e').style.display='block'</script>
</body></html>)HTML";

static const char PAGE_MAIN[] PROGMEM = R"HTML(<!doctype html><html lang="pt-br"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Monitor ESP32-CAM</title>
<style>body{font-family:system-ui;background:#111;color:#eee;margin:0;padding:12px}
header{display:flex;justify-content:space-between;align-items:center}a{color:#8ab4ff}
.v{position:relative;width:100%;max-width:720px}#live{width:100%;border-radius:10px;display:block;background:#000}
#ov{position:absolute;left:0;top:0;width:100%;height:100%;pointer-events:none}
#st{display:inline-block;padding:3px 10px;border-radius:12px;background:#2a6;margin:8px 0}
#st.m{background:#d33}.r{display:flex;flex-wrap:wrap;gap:10px;align-items:center;margin:6px 0}
input[type=text]{padding:7px;border-radius:6px;border:0;width:140px}button{padding:7px 12px;border:0;border-radius:6px;background:#2d7ff9;color:#fff}
button.x{background:#555;padding:2px 8px}.c{background:#1c1c1c;border-radius:8px;padding:8px 12px;margin:10px 0;max-width:696px}
.g{display:grid;grid-template-columns:repeat(auto-fill,minmax(180px,1fr));gap:10px}
.g figure{margin:0;background:#1c1c1c;border-radius:8px;overflow:hidden}.g img{width:100%;display:block}
.g figcaption{padding:6px;font-size:13px}.t{font-size:13px;color:#aaa}</style></head><body>
<header><h2>Monitor de ambiente</h2><a href="/logout">Sair</a></header>
<div class="v"><img id="live" alt="ao vivo"><canvas id="ov"></canvas></div>
<div class="r"><span id="st">Sem movimento</span>
<label><input type="checkbox" id="cap"> Tirar foto ao detectar movimento</label>
<label><input type="checkbox" id="fr" checked> Reconhecer rostos</label></div>
<div class="c"><b>Rostos</b> <span class="t" id="fs">carregando modelos...</span>
<div class="r"><input type="text" id="nm" placeholder="Nome"><button id="en">Cadastrar rosto da imagem</button><span class="t" id="msg"></span></div>
<div id="lst" class="t"></div></div>
<h3>Movimentos detectados</h3><div class="g" id="g"></div>
<script src="https://cdn.jsdelivr.net/npm/@vladmandic/face-api/dist/face-api.js"></script>
<script>
const $=id=>document.getElementById(id),live=$('live'),ov=$('ov'),snap=new Image();
function start(){live.crossOrigin='use-credentials';live.src='http://'+location.hostname+':81/stream?'+Date.now()}
live.onerror=()=>setTimeout(start,2000);start();
let last=0;
$('cap').onchange=e=>fetch('/settings',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'capture='+(e.target.checked?1:0)});
async function poll(){try{const r=await fetch('/events');if(r.status==401)return location.reload();
const d=await r.json();const cb=$('cap');if(document.activeElement!==cb)cb.checked=d.capture;const st=$('st');
st.textContent=d.motion?'Movimento! ('+d.pct+'%)':'Sem movimento';st.className=d.motion?'m':'';
const top=d.events.length?d.events[0].id:0;
if(top!==last){last=top;$('g').innerHTML=d.events.map(e=>
'<figure><a href="/event.jpg?id='+e.id+'" target="_blank"><img loading="lazy" src="/event.jpg?id='+e.id+'"></a><figcaption>'+e.time+'</figcaption></figure>').join('')}
}catch(e){}setTimeout(poll,1500)}poll();

// ---- rostos ----
const MODELS='https://cdn.jsdelivr.net/npm/@vladmandic/face-api/model/';
let faces=[],matcher=null,res=[],ready=false;
function rebuild(){const m={};faces.forEach(f=>(m[f.name]=m[f.name]||[]).push(new Float32Array(f.d)));
matcher=Object.keys(m).length?new faceapi.FaceMatcher(Object.keys(m).map(n=>new faceapi.LabeledFaceDescriptors(n,m[n])),0.5):null;
const c={};faces.forEach(f=>c[f.name]=(c[f.name]||0)+1);
$('lst').innerHTML=Object.keys(c).map(n=>n+' ('+c[n]+') <button class="x" data-n="'+n+'">x</button>').join(' &nbsp; ')||'nenhum cadastrado'}
async function save(){const r=await fetch('/faces',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(faces)});
if(!r.ok)$('msg').textContent='Erro ao salvar na placa'}
$('lst').onclick=async e=>{const n=e.target.dataset.n;if(!n)return;faces=faces.filter(f=>f.name!==n);rebuild();await save()};
$('en').onclick=async()=>{const n=$('nm').value.trim().replace(/[^\w ]/g,'');
if(!ready)return $('msg').textContent='Modelos ainda carregando';
if(!n)return $('msg').textContent='Digite um nome';
await full();
if(!res.length)return $('msg').textContent='Nenhum rosto na imagem';
const big=res.reduce((a,b)=>a.detection.box.area>b.detection.box.area?a:b);
faces.push({name:n,d:Array.from(big.descriptor).map(x=>+x.toFixed(4))});
rebuild();await save();$('msg').textContent='Cadastrado: '+n+' (cadastre 2-3 vezes em angulos diferentes)'};
async function init(){try{
await Promise.all([faceapi.nets.tinyFaceDetector.loadFromUri(MODELS),faceapi.nets.faceLandmark68Net.loadFromUri(MODELS),faceapi.nets.faceRecognitionNet.loadFromUri(MODELS)]);
faces=await (await fetch('/faces')).json();rebuild();ready=true;$('fs').textContent='pronto';detect()}
catch(e){$('fs').textContent='falha ao carregar modelos (o navegador precisa de internet)'}}
const cv=document.createElement('canvas');cv.width=320;cv.height=240;const cx=cv.getContext('2d');
const opt=()=>new faceapi.TinyFaceDetectorOptions({inputSize:224,scoreThreshold:0.5});
let labels=[],lastFull=0;
async function full(){cx.drawImage(live,0,0,320,240);
res=await faceapi.detectAllFaces(cv,opt()).withFaceLandmarks().withFaceDescriptors();
labels=res.map(r=>{const b=r.detection.box;let name='Desconhecido',ok=false;
if(matcher){const m=matcher.findBestMatch(r.descriptor);if(m.label!=='unknown'){name=m.label+' '+m.distance.toFixed(2);ok=true}}
return{x:b.x+b.width/2,y:b.y+b.height/2,name,ok}});lastFull=Date.now()}
async function detect(){
const ctx=ov.getContext('2d');
if(!$('fr').checked||!live.naturalWidth||document.hidden){ctx.clearRect(0,0,ov.width,ov.height);res=[];return setTimeout(detect,500)}
try{let boxes;
if(Date.now()-lastFull>1000){await full();boxes=res.map(r=>r.detection.box)}
else{cx.drawImage(live,0,0,320,240);boxes=(await faceapi.detectAllFaces(cv,opt())).map(d=>d.box)}
ov.width=live.clientWidth;ov.height=live.clientHeight;const k=ov.width/320;ctx.clearRect(0,0,ov.width,ov.height);ctx.lineWidth=2;ctx.font='bold 15px system-ui';
for(const bx of boxes){let l=null,dm=1e9;for(const q of labels){const d=Math.hypot(q.x-(bx.x+bx.width/2),q.y-(bx.y+bx.height/2));if(d<dm){dm=d;l=q}}
if(!l||dm>bx.width)l={name:'...',ok:false};
const c=l.ok?'#2c4':'#e33';ctx.strokeStyle=c;ctx.strokeRect(bx.x*k,bx.y*k,bx.width*k,bx.height*k);
const w=ctx.measureText(l.name).width+8;ctx.fillStyle=c;ctx.fillRect(bx.x*k,bx.y*k-22,w,22);ctx.fillStyle='#fff';ctx.fillText(l.name,bx.x*k+4,bx.y*k-6)}
$('fs').textContent='pronto'}
catch(e){$('fs').textContent='erro: '+e.message;return setTimeout(detect,2000)}
setTimeout(detect,0)}
window.addEventListener('load',init);
</script></body></html>)HTML";
