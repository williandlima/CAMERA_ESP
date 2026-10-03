/* Monitor ESP32-CAM – interface web.
 * Vídeo MJPEG (porta 81), status/ajustes via API REST e reconhecimento
 * facial num Web Worker (face-worker.js), sem travar o vídeo. */
'use strict';

const $ = (s) => document.querySelector(s);
const STREAM_URL = `${location.protocol}//${location.hostname}:81/stream`;
const PREFS_KEY = 'esp32cam.prefs';

// ---------------------------------------------------------------- API
async function api(path, opts = {}) {
  const r = await fetch(path, { credentials: 'same-origin', ...opts });
  if (r.status === 401) {
    location.href = '/';
    throw new Error('sessão expirada');
  }
  if (!r.ok) {
    let msg = r.statusText;
    try { msg = (await r.json()).error || msg; } catch (_) { /* corpo não-JSON */ }
    throw new Error(msg);
  }
  return (r.headers.get('content-type') || '').includes('json') ? r.json() : r;
}
const postJson = (path, body) =>
  api(path, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) });

function loadPrefs() {
  try { return JSON.parse(localStorage.getItem(PREFS_KEY)) || {}; } catch (_) { return {}; }
}
function savePrefs(p) {
  try { localStorage.setItem(PREFS_KEY, JSON.stringify(p)); } catch (_) { /* modo privado */ }
}
const prefs = { threshold: 0.5, fps: 6, antispoof: true, recognize: true, showMotion: true, tab: 'faces', ...loadPrefs() };
// v3: 0,50 é o limiar recomendado pela biblioteca (Human/FaceRes); 0,60 rejeitava a própria pessoa.
if ((prefs.v || 1) < 3) { prefs.threshold = 0.5; prefs.v = 3; }

// Critérios de reconhecimento (ver updateTracks)
const MIN_FACE_SCORE = 0.6;  // confiança mínima da detecção para tentar identificar
const MIN_MARGIN = 0.06;     // vantagem mínima sobre a 2ª pessoa mais parecida
const MAX_MISSED = 2;        // análises seguidas sem ver o rosto até apagar a caixa

// ---------------------------------------------------------------- vídeo
const live = $('#live');
const overlay = $('#overlay');
const videoMsg = $('#video-msg');
let streamRetry = 1000;

function startStream() {
  live.crossOrigin = 'use-credentials'; // permite ler os pixels no canvas (CORS)
  live.src = `${STREAM_URL}?t=${Date.now()}`;
}
live.addEventListener('load', () => { videoMsg.hidden = true; streamRetry = 1000; });
live.addEventListener('error', () => {
  videoMsg.textContent = 'reconectando ao vídeo…';
  videoMsg.hidden = false;
  setTimeout(startStream, streamRetry);
  streamRetry = Math.min(streamRetry * 2, 10000);
});
startStream();

$('#fs').onclick = () => {
  const v = $('#video');
  if (document.fullscreenElement) document.exitFullscreen();
  else if (v.requestFullscreen) v.requestFullscreen();
};
$('#shot').onclick = async () => {
  try { await api('/api/snapshot', { method: 'POST' }); refreshEvents(); } catch (e) { alert(e.message); }
};
$('#logout').onclick = async () => {
  try { await api('/api/logout', { method: 'POST' }); } finally { location.href = '/'; }
};
const showMotion = $('#show-motion');
showMotion.checked = prefs.showMotion;
showMotion.onchange = () => { prefs.showMotion = showMotion.checked; savePrefs(prefs); };

// ---------------------------------------------------------------- abas
function selectTab(name) {
  document.querySelectorAll('.tabs button').forEach((b) => b.classList.toggle('active', b.dataset.tab === name));
  document.querySelectorAll('.panel').forEach((p) => { p.hidden = p.id !== `tab-${name}`; });
  prefs.tab = name;
  savePrefs(prefs);
}
document.querySelectorAll('.tabs button').forEach((b) => { b.onclick = () => selectTab(b.dataset.tab); });
selectTab(prefs.tab);

// ---------------------------------------------------------------- status
let status = null;
let lastEvent = -1;
let statusFails = 0;

function chip(id, text, cls) {
  const el = $(id);
  el.textContent = text;
  el.className = `chip ${cls || ''}`;
}

function fmtUptime(s) {
  const d = Math.floor(s / 86400), h = Math.floor((s % 86400) / 3600), m = Math.floor((s % 3600) / 60);
  return d ? `${d}d ${h}h` : h ? `${h}h ${m}min` : `${m}min`;
}

function renderSystem(s) {
  const rows = [
    ['Firmware', s.fw], ['Sensor', s.sensor], ['IP', s.ip], ['Ligado há', fmtUptime(s.uptime)],
    ['Temperatura', `${s.temp} °C`], ['RAM livre', `${Math.round(s.heap / 1024)} KB`],
    ['PSRAM livre', `${Math.round(s.psram / 1024)} KB`], ['Espectadores', s.clients],
    ['Cartão SD', s.sd.ok ? `${s.sd.used} / ${s.sd.total} MB` : 'ausente'],
    ['Eventos', s.motion.events],
    ['LED', s.known ? 'verde (rosto conhecido)' : 'vermelho'],
  ];
  $('#sys').innerHTML = rows.map(([k, v]) => `<dt>${k}</dt><dd>${v}</dd>`).join('');
}

async function pollStatus() {
  try {
    status = await api('/api/status');
    statusFails = 0;
    chip('#c-conn', 'online', 'ok');
    const m = status.motion;
    if (!m.enabled) chip('#c-motion', 'detecção desligada');
    else if (m.active) chip('#c-motion', `movimento ${m.percent}%`, 'alert');
    else if (m.light) chip('#c-motion', 'mudança de luz');
    else chip('#c-motion', 'sem movimento');
    chip('#c-fps', `${status.fps} fps`);
    chip('#c-rssi', `${status.rssi} dBm`, status.rssi < -75 ? 'bad' : '');
    if (!$('#tab-system').hidden) renderSystem(status);
    if (status.lastEvent !== lastEvent) { lastEvent = status.lastEvent; refreshEvents(); }
  } catch (_) {
    if (++statusFails > 2) chip('#c-conn', 'offline', 'bad');
  }
  setTimeout(pollStatus, statusFails ? Math.min(1000 * statusFails, 5000) : 1000);
}

// ---------------------------------------------------------------- galeria
const viewer = $('#viewer');
$('#viewer-close').onclick = () => viewer.close();

async function refreshEvents() {
  let list;
  try { list = await api('/api/events'); } catch (_) { return; }
  const g = $('#gallery');
  if (!list.length) { g.innerHTML = '<p class="muted">Nenhum print ainda.</p>'; return; }
  g.innerHTML = '';
  for (const e of list) {
    const fig = document.createElement('figure');
    const when = e.time > 1600000000 ? new Date(e.time * 1000).toLocaleString('pt-BR') : `#${e.id}`;
    const src = `/api/snapshot?id=${e.id}`;
    fig.innerHTML = `<img loading="lazy" alt="" src="${src}"><figcaption><span>${when}</span><span>${e.manual ? 'manual' : `${e.percent}%`}</span></figcaption>`;
    fig.onclick = () => {
      $('#viewer-img').src = src;
      $('#viewer-dl').href = src;
      $('#viewer-dl').download = `evento_${e.id}.jpg`;
      viewer.showModal();
    };
    g.appendChild(fig);
  }
}

// ---------------------------------------------------------------- ajustes
let settings = null;
let saveTimer = null;
const pending = {};

function getPath(obj, path) { return path.split('.').reduce((o, k) => (o ? o[k] : undefined), obj); }
function setPath(obj, path, val) {
  const ks = path.split('.');
  let o = obj;
  for (const k of ks.slice(0, -1)) o = o[k] = o[k] || {};
  o[ks[ks.length - 1]] = val;
}

function bindSettings() {
  document.querySelectorAll('[data-k]').forEach((el) => {
    const key = el.dataset.k;
    const out = el.parentElement.querySelector('output');
    const v = getPath(settings, key);
    if (el.type === 'checkbox') el.checked = !!v;
    else el.value = v;
    if (out) out.textContent = el.value;
    const handler = () => {
      const val = el.type === 'checkbox' ? el.checked : Number(el.value);
      if (out) out.textContent = el.value;
      setPath(pending, key, val);
      clearTimeout(saveTimer);
      saveTimer = setTimeout(flushSettings, 350); // agrupa mudanças rápidas
    };
    el.oninput = el.type === 'range' ? () => { if (out) out.textContent = el.value; } : null;
    el.onchange = handler;
  });
}

async function flushSettings() {
  const body = JSON.parse(JSON.stringify(pending));
  for (const k of Object.keys(pending)) delete pending[k];
  try { settings = await postJson('/api/settings', body); } catch (e) { alert(`Erro ao salvar: ${e.message}`); }
}

async function loadSettings() {
  try { settings = await api('/api/settings'); bindSettings(); } catch (_) { setTimeout(loadSettings, 3000); }
}

// ---------------------------------------------------------------- OTA
$('#ota-file').onchange = (ev) => {
  const file = ev.target.files[0];
  if (!file) return;
  if (!confirm(`Gravar ${file.name} (${Math.round(file.size / 1024)} KB) e reiniciar a placa?`)) return;
  const prog = $('#ota-prog');
  const msg = $('#ota-msg');
  prog.hidden = false;
  const xhr = new XMLHttpRequest();
  xhr.open('POST', '/api/ota');
  xhr.upload.onprogress = (e) => { if (e.lengthComputable) prog.value = (100 * e.loaded) / e.total; };
  xhr.onload = () => {
    if (xhr.status === 200) {
      msg.textContent = 'Firmware gravado. Reiniciando…';
      waitReboot();
    } else {
      let err = xhr.statusText;
      try { err = JSON.parse(xhr.responseText).error; } catch (_) { /* ignore */ }
      msg.textContent = `Falhou: ${err}`;
    }
  };
  xhr.onerror = () => { msg.textContent = 'Falha de rede durante o envio'; };
  xhr.send(file);
};

function waitReboot() {
  const t0 = Date.now();
  const check = async () => {
    try {
      const r = await fetch('/api/status', { cache: 'no-store' });
      if (r.ok || r.status === 401) { location.reload(); return; }
    } catch (_) { /* ainda reiniciando */ }
    if (Date.now() - t0 < 60000) setTimeout(check, 2000);
  };
  setTimeout(check, 4000);
}

$('#reboot').onclick = async () => {
  if (!confirm('Reiniciar a placa?')) return;
  try { await api('/api/reboot', { method: 'POST' }); waitReboot(); } catch (e) { alert(e.message); }
};

// ---------------------------------------------------------------- rostos
const fr = {
  worker: null,
  ready: false,
  busy: false,
  db: [], // [{name, emb:Float32Array-like}]
  tracks: [],
  nextTrack: 1,
  enroll: null, // {name, samples:[], need, deadline}
  lastKnownPost: 0,
  frameId: 0,
  analysis: document.createElement('canvas'),
};

const frStatus = $('#fr-status');
const frMsg = $('#fr-msg');
const frOn = $('#fr-on');
const thInput = $('#fr-th');
const fpsInput = $('#fr-fps');
const spoofInput = $('#fr-spoof');

frOn.checked = prefs.recognize;
thInput.value = prefs.threshold;
fpsInput.value = prefs.fps;
spoofInput.checked = prefs.antispoof;
$('#o-th').textContent = thInput.value;
$('#o-fps').textContent = fpsInput.value;
frOn.onchange = () => { prefs.recognize = frOn.checked; savePrefs(prefs); if (!frOn.checked) fr.tracks = []; };
thInput.oninput = () => { $('#o-th').textContent = thInput.value; prefs.threshold = Number(thInput.value); savePrefs(prefs); };
fpsInput.oninput = () => { $('#o-fps').textContent = fpsInput.value; prefs.fps = Number(fpsInput.value); savePrefs(prefs); };
spoofInput.onchange = () => { prefs.antispoof = spoofInput.checked; savePrefs(prefs); };

// Embeddings quantizados em int8 (+ escala) e base64: ~1,4 KB por amostra.
function encodeEmb(emb) {
  let max = 0;
  for (const v of emb) max = Math.max(max, Math.abs(v));
  const scale = max || 1;
  const q = new Int8Array(emb.length);
  for (let i = 0; i < emb.length; i++) q[i] = Math.round((emb[i] / scale) * 127);
  let bin = '';
  const bytes = new Uint8Array(q.buffer);
  for (let i = 0; i < bytes.length; i++) bin += String.fromCharCode(bytes[i]);
  return { s: scale, q: btoa(bin) };
}
function decodeEmb(e) {
  const bin = atob(e.q);
  const out = new Array(bin.length);
  for (let i = 0; i < bin.length; i++) {
    let b = bin.charCodeAt(i);
    if (b > 127) b -= 256;
    out[i] = (b / 127) * e.s;
  }
  return out;
}

async function loadFaces() {
  try {
    const data = await api('/api/faces');
    if (data && data.v === 2 && Array.isArray(data.faces)) {
      fr.db = data.faces.map((f) => ({ name: f.name, emb: decodeEmb(f.e) }));
    } else if (Array.isArray(data) && data.length) {
      frMsg.textContent = 'Cadastros da versão anterior são incompatíveis: cadastre as pessoas novamente.';
    }
  } catch (_) { /* sem cadastros */ }
  renderPeople();
  if (fr.worker) fr.worker.postMessage({ type: 'db', faces: fr.db });
}

async function saveFaces() {
  const faces = fr.db.map((f) => ({ name: f.name, e: encodeEmb(f.emb) }));
  await postJson('/api/faces', { v: 2, model: 'human-faceres', faces });
  if (fr.worker) fr.worker.postMessage({ type: 'db', faces: fr.db });
  renderPeople();
}

function renderPeople() {
  const counts = {};
  for (const f of fr.db) counts[f.name] = (counts[f.name] || 0) + 1;
  const ul = $('#fr-list');
  ul.innerHTML = '';
  const names = Object.keys(counts).sort((a, b) => a.localeCompare(b));
  if (!names.length) { ul.innerHTML = '<li class="muted">Ninguém cadastrado.</li>'; return; }
  for (const n of names) {
    const li = document.createElement('li');
    const span = document.createElement('span');
    span.textContent = `${n} (${counts[n]} amostras)`;
    const del = document.createElement('button');
    del.textContent = 'remover';
    del.onclick = async () => {
      if (!confirm(`Remover ${n}?`)) return;
      fr.db = fr.db.filter((f) => f.name !== n);
      try { await saveFaces(); } catch (e) { alert(e.message); }
    };
    li.append(span, del);
    ul.appendChild(li);
  }
}

$('#fr-enroll').onclick = () => {
  const name = $('#fr-name').value.trim().replace(/[<>"'&]/g, '').slice(0, 32);
  if (!fr.ready) { frMsg.textContent = 'Aguarde os modelos carregarem.'; return; }
  if (!name) { frMsg.textContent = 'Digite um nome.'; return; }
  fr.enroll = { name, samples: [], need: 5, deadline: Date.now() + 20000, next: 0 };
  frMsg.textContent = 'Olhe para a câmera e mova levemente a cabeça…';
};

function handleEnroll(cand, faceCount) {
  const en = fr.enroll;
  if (!en) return;
  if (Date.now() > en.deadline) {
    fr.enroll = null;
    frMsg.textContent = en.samples.length ? 'Tempo esgotado; amostras parciais descartadas.' : 'Nenhum rosto adequado encontrado.';
    return;
  }
  if (!cand || Date.now() < en.next) return;
  if (faceCount !== 1) { frMsg.textContent = 'Deixe apenas a pessoa a cadastrar na imagem.'; return; }
  if (cand.score < 0.75 || cand.box[2] < 0.1) { frMsg.textContent = 'Aproxime-se, fique de frente e com boa luz.'; return; }
  en.samples.push(cand.emb);
  en.next = Date.now() + 350; // espaça as amostras para variar o ângulo
  en.deadline = Date.now() + 15000; // progresso renova o prazo (aparelhos lentos)
  frMsg.textContent = `Capturando ${en.samples.length}/${en.need}…`;
  if (en.samples.length >= en.need) {
    fr.enroll = null;
    for (const emb of en.samples) fr.db.push({ name: en.name, emb });
    saveFaces()
      .then(() => { frMsg.textContent = `${en.name} cadastrado(a).`; $('#fr-name').value = ''; })
      .catch((e) => { frMsg.textContent = `Erro ao salvar: ${e.message}`; });
  }
}

function iou(a, b) {
  const x1 = Math.max(a[0], b[0]), y1 = Math.max(a[1], b[1]);
  const x2 = Math.min(a[0] + a[2], b[0] + b[2]), y2 = Math.min(a[1] + a[3], b[1] + b[3]);
  const inter = Math.max(0, x2 - x1) * Math.max(0, y2 - y1);
  return inter / (a[2] * a[3] + b[2] * b[3] - inter || 1);
}

// Rastreamento simples por sobreposição + votação do nome nas últimas análises
// (evita o nome "piscar" entre pessoas).
function updateTracks(faces) {
  const now = performance.now();
  const used = new Set();
  for (const f of faces) {
    let best = null, bestIou = 0.2;
    for (const t of fr.tracks) {
      if (used.has(t)) continue;
      const v = iou(t.target, f.box);
      if (v > bestIou) { bestIou = v; best = t; }
    }
    if (!best) {
      best = { id: fr.nextTrack++, box: f.box.slice(), target: f.box.slice(), votes: [], missed: 0 };
      fr.tracks.push(best);
    }
    used.add(best);
    best.target = f.box.slice();
    best.seen = now;
    best.missed = 0;
    const ok = f.match && (f.boxScore ?? f.score) >= MIN_FACE_SCORE && f.match.similarity >= prefs.threshold &&
      f.match.similarity - (f.match.second || 0) >= MIN_MARGIN;
    best.votes.push(ok ? f.match.name : null);
    if (best.votes.length > 5) best.votes.shift();
    best.similarity = f.match ? f.match.similarity : 0;
    // antifraude oscila quadro a quadro em baixa resolução: média móvel por pessoa
    const ema = (old, v) => (v == null ? old : old == null ? v : old * 0.7 + v * 0.3);
    best.real = ema(best.real, f.real);
    best.live = ema(best.live, f.live);
  }
  // Rosto que sumiu: apaga após MAX_MISSED análises sem ele (não por tempo, que
  // deixava o nome "pendurado" quando cada análise demora).
  for (const t of fr.tracks) if (!used.has(t)) t.missed++;
  fr.tracks = fr.tracks.filter((t) => t.missed < MAX_MISSED && now - t.seen < 4000);
}

function trackLabel(t) {
  const c = {};
  for (const v of t.votes) if (v) c[v] = (c[v] || 0) + 1;
  let name = null, n = 0;
  for (const k of Object.keys(c)) if (c[k] > n) { n = c[k]; name = k; }
  // nome só com maioria clara (≥ 60%) e pelo menos 2 votos
  return n >= 2 && n >= t.votes.length * 0.6 ? name : null;
}

const SPOOF_MIN = 0.4;
const isGenuine = (t) => !prefs.antispoof || ((t.real ?? 1) >= SPOOF_MIN && (t.live ?? 1) >= SPOOF_MIN);

function signalKnown() {
  const now = Date.now();
  if (now - fr.lastKnownPost < 1000) return;
  // só rostos vistos na análise mais recente (missed === 0) mantêm o LED verde
  if (!fr.tracks.some((t) => t.missed === 0 && trackLabel(t) && isGenuine(t))) return;
  fr.lastKnownPost = now;
  api('/api/known', { method: 'POST' }).catch(() => {});
}

function scheduleAnalysis(delay) { setTimeout(analyze, delay); }

async function analyze() {
  const minInterval = 1000 / prefs.fps;
  if (!fr.worker) return; // reiniciado por startFaceWorker
  if (!fr.ready || !frOn.checked || document.hidden || !live.naturalWidth || fr.busy) {
    scheduleAnalysis(300);
    return;
  }
  try {
    // Reduz a imagem antes de enviar ao worker (mais rápido, mesma precisão).
    const w = Math.min(640, live.naturalWidth);
    const h = Math.round((w * live.naturalHeight) / live.naturalWidth);
    const c = fr.analysis;
    if (c.width !== w || c.height !== h) { c.width = w; c.height = h; }
    c.getContext('2d').drawImage(live, 0, 0, w, h);
    const bitmap = await createImageBitmap(c);
    fr.busy = true;
    fr.minInterval = minInterval;
    fr.sentAt = performance.now();
    fr.worker.postMessage({ type: 'frame', id: ++fr.frameId, bitmap, enroll: !!fr.enroll }, [bitmap]);
  } catch (e) {
    frStatus.textContent = e.name === 'SecurityError'
      ? 'Bloqueado por CORS: abra a página pelo IP ou por esp32cam.local.'
      : `Erro: ${e.message}`;
    scheduleAnalysis(2000);
    return;
  }
  // a próxima análise é agendada quando o resultado chegar
}

function onWorkerMessage(ev) {
  const m = ev.data;
  if (m.type === 'loading') {
    frStatus.textContent = m.message;
  } else if (m.type === 'ready') {
    fr.ready = true;
    frStatus.textContent = `Pronto (${m.backend.toUpperCase()}).`;
    fr.worker.postMessage({ type: 'db', faces: fr.db });
    scheduleAnalysis(0);
  } else if (m.type === 'result') {
    fr.busy = false;
    updateTracks(m.faces);
    handleEnroll(m.enroll, m.faces.length);
    signalKnown();
    // Diagnóstico para calibrar o limiar: pessoa cadastrada mais parecida e a similaridade.
    const best = m.faces.reduce((a, f) => (f.match && (!a || f.match.similarity > a.similarity) ? f.match : a), null);
    frStatus.textContent = `Pronto · ${m.ms} ms · ${m.faces.length} rosto(s)` +
      (best ? ` · mais parecido: ${best.name} ${best.similarity.toFixed(2)} (mínimo ${prefs.threshold.toFixed(2)})` : '');
    // Ritmo adaptativo: respeita o fps escolhido e não ocupa mais de ~60% do tempo
    // (média móvel, para um quadro lento isolado não pausar o reconhecimento).
    const elapsed = performance.now() - fr.sentAt;
    fr.avgMs = fr.avgMs ? fr.avgMs * 0.8 + elapsed * 0.2 : elapsed;
    scheduleAnalysis(Math.min(Math.max(fr.minInterval - elapsed, fr.avgMs * 0.6, 0), 1500));
  } else if (m.type === 'fatal') {
    // Sem internet/CDN: tenta de novo em 30 s, sem recarregar a página.
    frStatus.textContent = `Reconhecimento indisponível: ${m.message}. Nova tentativa em 30 s.`;
    fr.worker.terminate();
    fr.worker = null;
    fr.ready = false;
    setTimeout(startFaceWorker, 30000);
  } else if (m.type === 'error') {
    fr.busy = false;
    frStatus.textContent = `Erro no reconhecimento: ${m.message}`;
    scheduleAnalysis(2000);
  }
}

function startFaceWorker() {
  if (!window.Worker || !window.OffscreenCanvas || !window.createImageBitmap) {
    frStatus.textContent = 'Navegador sem suporte (use Chrome, Edge, Firefox ou Safari recentes).';
    return;
  }
  fr.worker = new Worker('/face-worker.js');
  fr.worker.onmessage = onWorkerMessage;
  fr.worker.onerror = (e) => {
    e.preventDefault();
    frStatus.textContent = 'Falha no módulo de reconhecimento. Nova tentativa em 30 s.';
    if (fr.worker) fr.worker.terminate();
    fr.worker = null;
    fr.ready = false;
    setTimeout(startFaceWorker, 30000);
  };
  const base = new URLSearchParams(location.search).get('humanBase'); // permite modelos locais/espelho
  fr.worker.postMessage({ type: 'init', base: base || undefined });
}

// ---------------------------------------------------------------- desenho
function drawLabel(ctx, text, x, y, color) {
  ctx.font = '600 14px system-ui, sans-serif';
  const w = ctx.measureText(text).width + 10;
  const ty = y - 22 < 0 ? y + 2 : y - 22;
  ctx.fillStyle = color;
  ctx.fillRect(x, ty, w, 20);
  ctx.fillStyle = '#fff';
  ctx.fillText(text, x + 5, ty + 15);
}

// Área real da imagem dentro do elemento (object-fit: contain).
function imageRect() {
  const W = overlay.clientWidth, H = overlay.clientHeight;
  const iw = live.naturalWidth || 4, ih = live.naturalHeight || 3;
  const s = Math.min(W / iw, H / ih);
  return { x: (W - iw * s) / 2, y: (H - ih * s) / 2, w: iw * s, h: ih * s };
}

function draw() {
  const dpr = window.devicePixelRatio || 1;
  const W = overlay.clientWidth, H = overlay.clientHeight;
  if (overlay.width !== Math.round(W * dpr) || overlay.height !== Math.round(H * dpr)) {
    overlay.width = Math.round(W * dpr);
    overlay.height = Math.round(H * dpr);
  }
  const ctx = overlay.getContext('2d');
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, W, H);
  const r = imageRect();

  if (prefs.showMotion && status && status.motion.active) {
    const [x0, y0, x1, y1] = status.motion.box;
    ctx.setLineDash([6, 4]);
    ctx.strokeStyle = '#f59e0b';
    ctx.lineWidth = 2;
    ctx.strokeRect(r.x + x0 * r.w, r.y + y0 * r.h, (x1 - x0) * r.w, (y1 - y0) * r.h);
    ctx.setLineDash([]);
  }

  if (frOn.checked) {
    for (const t of fr.tracks) {
      for (let i = 0; i < 4; i++) t.box[i] += (t.target[i] - t.box[i]) * 0.35; // suavização
      const x = r.x + t.box[0] * r.w, y = r.y + t.box[1] * r.h, w = t.box[2] * r.w, h = t.box[3] * r.h;
      const name = trackLabel(t);
      const genuine = isGenuine(t);
      let color = '#ef4444', text = 'Desconhecido';
      if (name && genuine) { color = '#16a34a'; text = `${name} ${Math.round(t.similarity * 100)}%`; }
      else if (name) { color = '#f59e0b'; text = `${name}? (foto/tela)`; }
      else if (prefs.antispoof && !genuine) { color = '#f59e0b'; text = 'Possível foto/tela'; }
      ctx.lineWidth = 2.5;
      ctx.strokeStyle = color;
      ctx.strokeRect(x, y, w, h);
      drawLabel(ctx, text, x, y, color);
    }
  }
  requestAnimationFrame(draw);
}

// ---------------------------------------------------------------- início
pollStatus();
loadSettings();
loadFaces();
startFaceWorker();
requestAnimationFrame(draw);
