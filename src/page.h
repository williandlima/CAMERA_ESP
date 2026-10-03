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
#live{width:100%;max-width:720px;border-radius:10px;display:block;background:#000}
#st{display:inline-block;padding:3px 10px;border-radius:12px;background:#2a6;margin:8px 0}
#st.m{background:#d33}.g{display:grid;grid-template-columns:repeat(auto-fill,minmax(180px,1fr));gap:10px}
.g figure{margin:0;background:#1c1c1c;border-radius:8px;overflow:hidden}.g img{width:100%;display:block}
.g figcaption{padding:6px;font-size:13px}</style></head><body>
<header><h2>Monitor de ambiente</h2><a href="/logout">Sair</a></header>
<img id="live" alt="ao vivo"><div><span id="st">Sem movimento</span></div>
<h3>Movimentos detectados</h3><div class="g" id="g"></div>
<script>
const live=document.getElementById('live');
function frame(){live.onload=live.onerror=()=>setTimeout(frame,150);live.src='/live.jpg?'+Date.now()}frame();
let last=0;
async function poll(){try{const r=await fetch('/events');if(r.status==401)return location.reload();
const d=await r.json();const st=document.getElementById('st');
st.textContent=d.motion?'Movimento! ('+d.pct+'%)':'Sem movimento';st.className=d.motion?'m':'';
const top=d.events.length?d.events[0].id:0;
if(top!==last){last=top;document.getElementById('g').innerHTML=d.events.map(e=>
'<figure><a href="/event.jpg?id='+e.id+'" target="_blank"><img loading="lazy" src="/event.jpg?id='+e.id+'"></a><figcaption>'+e.time+'</figcaption></figure>').join('')}
}catch(e){}setTimeout(poll,1500)}poll();
</script></body></html>)HTML";
