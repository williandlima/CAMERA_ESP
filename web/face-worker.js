/* Reconhecimento facial em Web Worker (não trava a página nem o vídeo).
 * Biblioteca: Human (BlazeFace + FaceMesh + FaceRes + antispoof + liveness). */
/* global Human */
'use strict';

const HUMAN_VERSION = '3.3.6';
let human = null;
let canvas = null;
let ctx = null;
let names = [];
let embeddings = [];

const config = (base) => ({
  debug: false,
  backend: 'webgl',
  modelBasePath: `${base}/models/`,
  cacheSensitivity: 0,
  warmup: 'face',
  filter: { enabled: false },
  face: {
    enabled: true,
    detector: { rotation: false, maxDetected: 5, minConfidence: 0.45, return: false },
    mesh: { enabled: true },
    attention: { enabled: false },
    iris: { enabled: false },
    description: { enabled: true },
    emotion: { enabled: false },
    antispoof: { enabled: true },
    liveness: { enabled: true },
  },
  body: { enabled: false },
  hand: { enabled: false },
  object: { enabled: false },
  gesture: { enabled: false },
  segmentation: { enabled: false },
});

async function init(msg) {
  const base = msg.base || `https://cdn.jsdelivr.net/npm/@vladmandic/human@${HUMAN_VERSION}`;
  try {
    importScripts(`${base}/dist/human.js`);
  } catch (_) {
    postMessage({ type: 'fatal', message: 'não foi possível baixar a biblioteca de reconhecimento (o aparelho está sem internet?)' });
    return;
  }
  const H = Human.Human || Human.default;
  for (const backend of ['webgl', 'wasm', 'cpu']) {
    try {
      human = new H({ ...config(base), backend });
      await human.load();
      await human.init();
      if (human.tf.getBackend() !== backend) throw new Error(`backend ${backend} indisponível`);
      postMessage({ type: 'loading', message: 'preparando modelos…' });
      await human.warmup(); // compila shaders antes da 1ª análise real
      postMessage({ type: 'ready', backend });
      return;
    } catch (e) {
      human = null;
      if (backend === 'cpu') postMessage({ type: 'fatal', message: `falha ao carregar modelos: ${e && e.message ? e.message : e}` });
    }
  }
}

function setDb(faces) {
  names = [];
  embeddings = [];
  for (const f of faces) {
    if (f.emb && f.emb.length) {
      names.push(f.name);
      embeddings.push(f.emb);
    }
  }
}

function match(embedding) {
  if (!embedding || !embeddings.length) return null;
  const same = embeddings.filter((e) => e.length === embedding.length);
  if (!same.length) return null;
  const idx = [];
  embeddings.forEach((e, i) => { if (e.length === embedding.length) idx.push(i); });
  const r = human.match.find(embedding, same);
  if (r.index < 0) return null;
  return { name: names[idx[r.index]], similarity: r.similarity };
}

async function detect(msg) {
  const t0 = performance.now();
  const bmp = msg.bitmap;
  if (!canvas || canvas.width !== bmp.width || canvas.height !== bmp.height) {
    canvas = new OffscreenCanvas(bmp.width, bmp.height);
    ctx = canvas.getContext('2d');
  }
  ctx.drawImage(bmp, 0, 0);
  bmp.close();
  const res = await human.detect(canvas);
  let largest = null;
  const faces = (res.face || []).map((f) => {
    const out = {
      box: f.boxRaw,
      score: f.score,
      real: f.real,
      live: f.live,
      size: f.box ? f.box[2] : 0,
      match: match(f.embedding),
    };
    if (msg.enroll && f.embedding && (!largest || out.size > largest.size)) largest = { ...out, emb: Array.from(f.embedding) };
    return out;
  });
  postMessage({ type: 'result', id: msg.id, faces, enroll: largest, ms: Math.round(performance.now() - t0) });
}

onmessage = async (ev) => {
  const msg = ev.data;
  try {
    if (msg.type === 'init') await init(msg);
    else if (msg.type === 'db') setDb(msg.faces);
    else if (msg.type === 'frame') {
      if (!human) { msg.bitmap.close(); return; }
      await detect(msg);
    }
  } catch (e) {
    postMessage({ type: 'error', id: msg.id, message: String(e && e.message ? e.message : e) });
  }
};
