const DEFAULT_CONFIG = {
  scaleMode: 'integer',
  smoothUpscale: false,
  showFps: false,
  audioEnabled: true,
  sampleRate: 44100,
  stepsPerSecond: 2000000,
  touchHoldMs: 20,
  calibrationTouchHoldMs: 500,
  touchOffsetX: 0,
  touchOffsetY: 0,
  touchBurstFrames: 3,
  startupFrames: 18,
  autoCalibration: true,
  stateSlot: 0,
  keys: {
    A: 'KeyZ', B: 'KeyX', Start: 'Enter', Select: 'ShiftRight',
    Reset: 'KeyR', SaveState: 'F5', LoadState: 'F8', Fullscreen: 'F11', ScaleMode: 'F9', Menu: 'F1'
  }
};
const BUTTON_BITS = { A: 1, B: 2, Start: 4, Select: 8 };
const STATUS_TEXT = { 0: 'empty', 1: 'ready', 3: 'running', 5: 'error' };
const FRAME_RATE = 60;
const STORE_KEY = 'bdm-wasm-config-v1';
const WASM_BUILD_ID = 'bdm-wasm-20260604-manual-cal-hold';
const ROM_MAX_BYTES = 128 * 1024;

let config = loadConfig();
let wasm = null;
let memory = null;
let ctx = null;
let imageData = null;
let frameRequest = 0;
let paused = false;
let loading = false;
let pressed = new Set();
let remapTarget = null;
let activeGame = null;
let pen = { x: 0, y: 0, physicalDown: false, pointerId: null, holdUntil: 0, latchedFrames: 0 };
let lastTouchStatusAt = 0;
let limiterLast = 0;
let limiterAcc = 0;
let fpsLastTime = 0;
let fpsLastFrame = 0;
let fpsValue = 0;
let audioCtx = null;
let audioNode = null;
let audioQueue = [];
let audioOffset = 0;
let audioFramesQueued = 0;
let autoCal = { active: false, stage: 'idle', frame: 0, done: false, taps: 0 };
let kickTimer = 0;

/* Native frontends use roughly 1,000,000 emulated CPU steps for a calibration
   press.  In the browser we fast-forward those emulated steps immediately, so
   calibration feels instant in wall-clock time while still satisfying the game
   firmware's ADC/debounce loop. */
const AUTO_CAL_WAIT_FRAMES = 900;
const AUTO_CAL_DOWN_STEPS = 1000000;
const AUTO_CAL_RELEASE_FRAMES = 90;

const els = {
  canvas: document.getElementById('video'),
  screenFrame: document.getElementById('screenFrame'),
  sideMenu: document.getElementById('sideMenu'),
  menuToggle: document.getElementById('menuToggle'),
  closeMenu: document.getElementById('closeMenu'),
  fullscreenToggle: document.getElementById('fullscreenToggle'),
  gameFiles: document.getElementById('gameFiles'),
  cartFile: document.getElementById('cartFile'),
  mediaFile: document.getElementById('mediaFile'),
  loadPairButton: document.getElementById('loadPairButton'),
  mediaName: document.getElementById('mediaName'),
  softResetButton: document.getElementById('softResetButton'),
  pauseToggle: document.getElementById('pauseToggle'),
  scaleMode: document.getElementById('scaleMode'),
  smoothUpscale: document.getElementById('smoothUpscale'),
  showFps: document.getElementById('showFps'),
  fpsCounter: document.getElementById('fpsCounter'),
  audioEnabled: document.getElementById('audioEnabled'),
  sampleRate: document.getElementById('sampleRate'),
  stateSlot: document.getElementById('stateSlot'),
  saveState: document.getElementById('saveState'),
  loadState: document.getElementById('loadState'),
  exportState: document.getElementById('exportState'),
  importStateFile: document.getElementById('importStateFile'),
  controlMap: document.getElementById('controlMap'),
  runtimeStatus: document.getElementById('runtimeStatus'),
  runtimeResolution: document.getElementById('runtimeResolution'),
  runtimeFrame: document.getElementById('runtimeFrame'),
  storageStatus: document.getElementById('storageStatus'),
  touchHoldMs: document.getElementById('touchHoldMs'),
  calibrationTouchHoldMs: document.getElementById('calibrationTouchHoldMs'),
  touchOffsetX: document.getElementById('touchOffsetX'),
  touchOffsetY: document.getElementById('touchOffsetY'),
  autoCalibration: document.getElementById('autoCalibration')
};

function loadConfig() {
  try {
    const saved = JSON.parse(localStorage.getItem(STORE_KEY) || 'null');
    if (!saved) return structuredClone(DEFAULT_CONFIG);
    return {
      ...structuredClone(DEFAULT_CONFIG),
      ...saved,
      keys: { ...DEFAULT_CONFIG.keys, ...(saved.keys || {}) },
      sampleRate: clampNumber(saved.sampleRate, 8000, 192000, DEFAULT_CONFIG.sampleRate),
      touchHoldMs: clampNumber(saved.touchHoldMs, 0, 5000, DEFAULT_CONFIG.touchHoldMs),
      calibrationTouchHoldMs: clampNumber(saved.calibrationTouchHoldMs, 0, 5000, DEFAULT_CONFIG.calibrationTouchHoldMs),
      touchOffsetX: clampNumber(saved.touchOffsetX, -8, 8, DEFAULT_CONFIG.touchOffsetX),
      touchOffsetY: clampNumber(saved.touchOffsetY, -8, 8, DEFAULT_CONFIG.touchOffsetY),
      touchBurstFrames: clampNumber(saved.touchBurstFrames, 1, 10, DEFAULT_CONFIG.touchBurstFrames),
      startupFrames: clampNumber(saved.startupFrames, 0, 120, DEFAULT_CONFIG.startupFrames),
      autoCalibration: saved.autoCalibration !== false,
      stateSlot: clampNumber(saved.stateSlot, 0, 9, 0)
    };
  } catch (_) { return structuredClone(DEFAULT_CONFIG); }
}
function saveConfig() { try { localStorage.setItem(STORE_KEY, JSON.stringify(config)); } catch (_) {} }
function clampNumber(v, min, max, def) { v = Number(v); return Number.isFinite(v) ? Math.max(min, Math.min(max, v)) : def; }
function status(text, cls = '') { els.storageStatus.textContent = text; els.storageStatus.className = cls; }
function wasmU8() { return new Uint8Array(memory.buffer); }
function copyBytesToWasm(bytes) { const ptr = wasm.bdm_wasm_malloc(bytes.byteLength || 1); if (!ptr) throw new Error('WASM allocation failed'); wasmU8().set(bytes, ptr); return ptr; }
function basename(name) { return String(name || 'file').replace(/\\/g, '/').split('/').pop(); }
function lowerName(f) { return basename(f.name).toLowerCase(); }
function isZip(f) { return lowerName(f).endsWith('.zip'); }
function isMediaName(n) { n = String(n).toLowerCase(); return /\[m[.\s_\-]?\d+\]/.test(n) || /(^|[^a-z0-9])m[.\s_\-]?\d+/.test(n); }
function isGameName(n) { n = String(n).toLowerCase(); return /\[g[.\s_\-]?\d+\]/.test(n) || /(^|[^a-z0-9])g[.\s_\-]?\d+/.test(n); }
function cartIdFromName(n) { const m = String(n).toLowerCase().match(/\[([gm])[.\s_\-]?(\d+)\]/) || String(n).toLowerCase().match(/(^|[^a-z0-9])([gm])[.\s_\-]?(\d+)/); return m ? Number(m[m.length - 1]) : -1; }
function looksLikeRomName(n) { return /\.(bin|rom)$/i.test(n); }

async function fileBytes(f) {
  if (!f) return new Uint8Array(0);
  if (f instanceof Uint8Array) return f.slice();
  if (f instanceof ArrayBuffer) return new Uint8Array(f);
  if (ArrayBuffer.isView(f)) return new Uint8Array(f.buffer, f.byteOffset, f.byteLength).slice();

  /*
    Some modern browsers expose Blob/File.bytes as a function. The previous
    loader tested `f.bytes` for truthiness and returned the function object
    itself. That converted to an empty Uint8Array later, producing the false
    "empty ROM image" error. Call the function when it exists; otherwise use
    the standard arrayBuffer() path.
  */
  if (typeof f.bytes === 'function') {
    const b = await f.bytes();
    if (b instanceof Uint8Array) return b.slice();
    if (b instanceof ArrayBuffer) return new Uint8Array(b);
    if (ArrayBuffer.isView(b)) return new Uint8Array(b.buffer, b.byteOffset, b.byteLength).slice();
    return new Uint8Array(b || []);
  }
  if (f.bytes instanceof Uint8Array) return f.bytes.slice();
  if (f.bytes instanceof ArrayBuffer) return new Uint8Array(f.bytes);
  if (ArrayBuffer.isView(f.bytes)) return new Uint8Array(f.bytes.buffer, f.bytes.byteOffset, f.bytes.byteLength).slice();
  if (typeof f.arrayBuffer === 'function') return new Uint8Array(await f.arrayBuffer());
  return new Uint8Array(0);
}

async function inflateRaw(bytes) {
  if (typeof DecompressionStream !== 'function') throw new Error('this browser cannot inflate ZIP deflate entries');
  const ds = new DecompressionStream('deflate-raw');
  const writer = ds.writable.getWriter();
  writer.write(bytes); writer.close();
  return new Uint8Array(await new Response(ds.readable).arrayBuffer());
}
function rd16(b, o) { return b[o] | (b[o + 1] << 8); }
function rd32(b, o) { return (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)) >>> 0; }
async function expandZip(file) {
  const data = await fileBytes(file);
  let eocd = -1;
  for (let i = data.length - 22; i >= Math.max(0, data.length - 65558); --i) {
    if (rd32(data, i) === 0x06054b50) { eocd = i; break; }
  }
  if (eocd < 0) throw new Error(`${file.name}: ZIP central directory not found`);
  const count = rd16(data, eocd + 10);
  let off = rd32(data, eocd + 16);
  const out = [];
  for (let i = 0; i < count; ++i) {
    if (rd32(data, off) !== 0x02014b50) break;
    const method = rd16(data, off + 10);
    const compSize = rd32(data, off + 20);
    const nameLen = rd16(data, off + 28);
    const extraLen = rd16(data, off + 30);
    const commentLen = rd16(data, off + 32);
    const localOff = rd32(data, off + 42);
    const name = new TextDecoder().decode(data.subarray(off + 46, off + 46 + nameLen));
    off += 46 + nameLen + extraLen + commentLen;
    if (name.endsWith('/') || !/\.(bin|rom)$/i.test(name)) continue;
    if (rd32(data, localOff) !== 0x04034b50) continue;
    const lfNameLen = rd16(data, localOff + 26), lfExtraLen = rd16(data, localOff + 28);
    const start = localOff + 30 + lfNameLen + lfExtraLen;
    const comp = data.subarray(start, start + compSize);
    let bytes;
    if (method === 0) bytes = comp.slice();
    else if (method === 8) bytes = await inflateRaw(comp);
    else continue;
    out.push({ name, bytes });
  }
  if (!out.length) throw new Error(`${file.name}: no .bin/.rom entries found`);
  return out;
}
async function normalizeFiles(fileList) {
  const out = [];
  for (const f of fileList) {
    if (isZip(f)) out.push(...await expandZip(f));
    else out.push({ name: f.name, bytes: await fileBytes(f) });
  }
  return out.filter(f => looksLikeRomName(f.name)).map(normalizeRomBytes);
}
function normalizeRomBytes(f) {
  let bytes;
  if (f.bytes instanceof Uint8Array) bytes = f.bytes.slice();
  else if (f.bytes instanceof ArrayBuffer) bytes = new Uint8Array(f.bytes);
  else if (ArrayBuffer.isView(f.bytes)) bytes = new Uint8Array(f.bytes.buffer, f.bytes.byteOffset, f.bytes.byteLength).slice();
  else bytes = new Uint8Array(0);
  const originalSize = bytes.byteLength;
  let note = '';
  if (bytes.byteLength > ROM_MAX_BYTES) {
    if (bytes.byteLength === ROM_MAX_BYTES + 512) {
      bytes = bytes.subarray(512, 512 + ROM_MAX_BYTES);
      note = 'stripped 512-byte copier header';
    } else {
      bytes = bytes.subarray(0, ROM_MAX_BYTES);
      note = `trimmed from ${originalSize} bytes to ${ROM_MAX_BYTES}`;
    }
  }
  return { ...f, bytes, originalSize, note };
}
function pickGameFiles(files) {
  const games = files.filter(f => isGameName(f.name));
  const mediaFiles = files.filter(f => isMediaName(f.name));
  const plain = files.filter(f => !isGameName(f.name) && !isMediaName(f.name));
  if (!games.length && mediaFiles.length) {
    return { cart: null, media: mediaFiles[0], mediaOnly: true };
  }
  const cart = games[0] || plain[0] || files[0] || null;
  if (!cart) return { cart: null, media: null };
  let media = null;
  const id = cartIdFromName(cart.name);
  if (id >= 0) media = mediaFiles.find(f => cartIdFromName(f.name) === id) || null;
  if (!media) media = mediaFiles.find(f => f !== cart) || null;
  return { cart, media, mediaOnly: false };
}

function stateBaseKey() {
  const name = activeGame ? activeGame.names.join('+') : 'no-game';
  let h = 2166136261 >>> 0;
  for (let i = 0; i < name.length; ++i) { h ^= name.charCodeAt(i); h = Math.imul(h, 16777619) >>> 0; }
  return `bdm-state-${h.toString(16)}-slot-${config.stateSlot}`;
}
function bytesToBase64(bytes) {
  let s = '';
  for (let i = 0; i < bytes.length; i += 0x8000) s += String.fromCharCode(...bytes.subarray(i, i + 0x8000));
  return btoa(s);
}
function base64ToBytes(s) { const bin = atob(s); const out = new Uint8Array(bin.length); for (let i = 0; i < bin.length; ++i) out[i] = bin.charCodeAt(i); return out; }
function saveStateToLocalStorage() {
  if (!wasm || wasm.bdm_wasm_get_status() !== 3) return status('start a game before saving state', 'status-bad');
  if (!wasm.bdm_wasm_save_state()) return status('save state failed', 'status-bad');
  const ptr = wasm.bdm_wasm_get_save_ptr(), size = wasm.bdm_wasm_get_save_size();
  const bytes = wasmU8().slice(ptr, ptr + size);
  localStorage.setItem(stateBaseKey(), bytesToBase64(bytes));
  status(`state slot ${config.stateSlot} saved (${size} bytes)`, 'status-ok');
}
function loadStateFromLocalStorage() {
  if (!wasm || wasm.bdm_wasm_get_status() < 1) return status('load a matching game before loading state', 'status-bad');
  const b64 = localStorage.getItem(stateBaseKey());
  if (!b64) return status(`no state in slot ${config.stateSlot}`, 'status-bad');
  const bytes = base64ToBytes(b64);
  const ptr = copyBytesToWasm(bytes);
  const ok = wasm.bdm_wasm_load_state(ptr, bytes.byteLength);
  if (ok) resetAutoCalibration(false);
  status(ok ? `state slot ${config.stateSlot} loaded` : 'state rejected', ok ? 'status-ok' : 'status-bad');
  renderOnce();
}
function exportState() {
  const b64 = localStorage.getItem(stateBaseKey());
  if (!b64) return status(`no state in slot ${config.stateSlot}`, 'status-bad');
  const bytes = base64ToBytes(b64);
  const blob = new Blob([bytes], { type: 'application/octet-stream' });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url; a.download = `${activeGame ? activeGame.names[0].replace(/\.[^.]+$/, '') : 'bdm'}_slot${config.stateSlot}.bdmst`;
  document.body.append(a); a.click(); a.remove(); URL.revokeObjectURL(url);
}
async function importState(file) {
  if (!file) return;
  const bytes = await fileBytes(file);
  localStorage.setItem(stateBaseKey(), bytesToBase64(bytes));
  status(`imported state into slot ${config.stateSlot}`, 'status-ok');
}

function wasmImports() {
  return { pcfx: { read_host_file() { return 0; } } };
}
async function instantiateBackend() {
  let result;
  const wasmUrl = `bdm_wasm_core.wasm?v=${encodeURIComponent(WASM_BUILD_ID)}`;
  const fetchOpts = { cache: 'no-store' };
  try {
    const response = await fetch(wasmUrl, fetchOpts);
    result = await WebAssembly.instantiateStreaming(response, wasmImports());
  } catch (_) {
    const bytes = await (await fetch(wasmUrl, fetchOpts)).arrayBuffer();
    result = await WebAssembly.instantiate(bytes, wasmImports());
  }
  wasm = result.instance.exports;
  memory = wasm.memory;
  wasm.bdm_wasm_init(config.sampleRate, config.stepsPerSecond);
  renderOnce();
}

async function loadGame(files) {
  if (!files || !files.length || !wasm) return;
  loading = true;
  paused = false;
  pressed.clear();
  clearPen(false);
  clearAudioQueue();
  try {
    const list = await normalizeFiles(files);
    if (!list.length) throw new Error('no usable .bin/.rom files selected');
    const { cart, media, mediaOnly } = pickGameFiles(list);
    if (mediaOnly) throw new Error(`${media.name} is an M media cart. Select its matching G cart as well, or load the G cart by itself.`);
    if (!cart) throw new Error('no bootable G cart found');
    if (!cart.bytes || !cart.bytes.byteLength) throw new Error(`${cart.name}: empty ROM image`);
    if (media && (!media.bytes || !media.bytes.byteLength)) throw new Error(`${media.name}: empty media image`);

    /* A new browser load must behave like a real power-on with a cartridge
       inserted.  The previous code did start the core, but it left any drag
       pointer state intact and did not immediately execute/render a frame.  On
       some browsers the canvas therefore stayed at the reset background until
       the user pressed Soft reset. */
    wasm.bdm_wasm_reset_heap();
    if (!wasm.bdm_wasm_init(config.sampleRate, config.stepsPerSecond)) throw new Error('WASM core init failed');
    let ptr = copyBytesToWasm(cart.bytes);
    if (!wasm.bdm_wasm_load_cart(ptr, cart.bytes.byteLength)) {
      const err = wasm.bdm_wasm_get_error?.() ?? 0;
      throw new Error(`cart rejected: ${cart.name} (${cart.bytes.byteLength} bytes, wasm error ${err})`);
    }
    if (media) {
      ptr = copyBytesToWasm(media.bytes);
      if (!wasm.bdm_wasm_load_media(ptr, media.bytes.byteLength)) {
        const err = wasm.bdm_wasm_get_error?.() ?? 0;
        throw new Error(`media rejected: ${media.name} (${media.bytes.byteLength} bytes, wasm error ${err})`);
      }
    }
    clearPen(false);
    if (!wasm.bdm_wasm_start()) throw new Error('start failed');
    resetAutoCalibration(!!config.autoCalibration);

    activeGame = { names: [basename(cart.name)].concat(media ? [basename(media.name)] : []) };
    const notes = [cart, media].filter(Boolean).map(f => f.note ? `${basename(f.name)}: ${f.note}` : '').filter(Boolean);
    els.mediaName.textContent = media ? `${basename(cart.name)} + ${basename(media.name)}` : basename(cart.name);
    status(notes.length ? `game loaded (${notes.join('; ')})` : 'game loaded', 'status-ok');

    resetFrameLimiter();
    if (config.autoCalibration) {
      fastForwardAutoCalibration(AUTO_CAL_WAIT_FRAMES);
      status(notes.length ? `game loaded, auto calibration processed (${notes.join('; ')})` : 'game loaded, auto calibration processed', 'status-ok');
    } else {
      runStartupFrames(config.startupFrames);
      renderOnce();
    }
    schedulePostLoadKick(180);
    void startAudio();
  } catch (e) {
    console.error(e);
    status(e.message || String(e), 'status-bad');
  } finally {
    loading = false;
    if (els.gameFiles) els.gameFiles.value = '';
    if (els.cartFile) els.cartFile.value = '';
    if (els.mediaFile) els.mediaFile.value = '';
    setDragActive(false);
    resetFrameLimiter();
    schedulePostLoadKick(180);
  }
}

async function loadSeparateFields() {
  const files = [];
  if (els.cartFile?.files?.[0]) files.push(els.cartFile.files[0]);
  if (els.mediaFile?.files?.[0]) files.push(els.mediaFile.files[0]);
  if (!files.length) return status('select a G cart first', 'status-bad');
  return loadGame(files);
}


function hasFileDrag(e) {
  const dt = e && e.dataTransfer;
  if (!dt) return false;
  if (dt.files && dt.files.length) return true;
  if (dt.items && dt.items.length) {
    for (const item of dt.items) if (!item.kind || item.kind === 'file') return true;
  }
  if (!dt.types) return false;
  try { return Array.from(dt.types).includes('Files'); }
  catch (_) { return false; }
}
function filesFromDataTransfer(dt) {
  if (!dt) return [];
  const out = [];
  if (dt.files && dt.files.length) out.push(...Array.from(dt.files));
  if (!out.length && dt.items && dt.items.length) {
    for (const item of Array.from(dt.items)) {
      if (item.kind && item.kind !== 'file') continue;
      const f = item.getAsFile?.();
      if (f) out.push(f);
    }
  }
  return out;
}
function setDragActive(active) {
  document.body.classList.toggle('drag-active', !!active);
  if (active) status('drop G cart, G+M pair, or ZIP to load', 'status-ok');
}
function stopFileDrag(e) {
  if (!hasFileDrag(e)) return false;
  e.preventDefault();
  e.stopPropagation();
  return true;
}
function handleDragEnter(e) {
  if (!stopFileDrag(e)) return;
  setDragActive(true);
}
function handleDragOver(e) {
  if (!stopFileDrag(e)) return;
  setDragActive(true);
  if (e.dataTransfer) e.dataTransfer.dropEffect = 'copy';
}
function handleDragLeave(e) {
  if (!stopFileDrag(e)) return;
  if (e.clientX <= 0 || e.clientY <= 0 || e.clientX >= window.innerWidth || e.clientY >= window.innerHeight) {
    setDragActive(false);
  }
}
async function handleDrop(e) {
  if (!stopFileDrag(e)) return;
  setDragActive(false);
  const files = filesFromDataTransfer(e.dataTransfer);
  if (files.length) await loadGame(files);
  else status('drop contained no readable files', 'status-bad');
}

function buttonsMask() {
  let m = 0;
  for (const [name, bit] of Object.entries(BUTTON_BITS)) if (pressed.has(config.keys[name])) m |= bit;
  return m >>> 0;
}
function effectivePenDown(now = performance.now()) {
  return !!pen.physicalDown || now < pen.holdUntil || pen.latchedFrames > 0;
}
function sendInput(now = performance.now()) {
  if (wasm?.bdm_wasm_set_input) wasm.bdm_wasm_set_input(buttonsMask(), pen.x | 0, pen.y | 0, effectivePenDown(now) ? 1 : 0);
}
function resetAutoCalibration(start = false) {
  autoCal = { active: !!start && !!config.autoCalibration, stage: start && config.autoCalibration ? 'search' : 'idle', frame: 0, done: false, taps: 0 };
  if (autoCal.active) status('auto calibration assist armed', 'status-ok');
}
function frameSteps() { return Math.max(1, Math.round(config.stepsPerSecond / FRAME_RATE)); }
function isOnPixel(p) { return ((p >>> 0) & 0x00ffffff) !== 0x00c6d1bd; }
function countOnPixels(x0, y0, x1, y1) {
  if (!wasm?.bdm_wasm_get_framebuffer || !memory) return 0;
  const w = wasm.bdm_wasm_get_width?.() || 160;
  const h = wasm.bdm_wasm_get_height?.() || 120;
  const fb = wasm.bdm_wasm_get_framebuffer?.() || 0;
  if (!fb || w <= 0 || h <= 0) return 0;
  x0 = Math.max(0, Math.min(w - 1, x0 | 0)); x1 = Math.max(0, Math.min(w, x1 | 0));
  y0 = Math.max(0, Math.min(h - 1, y0 | 0)); y1 = Math.max(0, Math.min(h, y1 | 0));
  if (x1 <= x0 || y1 <= y0) return 0;
  const pix = new Uint32Array(memory.buffer, fb, w * h);
  let n = 0;
  for (let y = y0; y < y1; ++y) for (let x = x0; x < x1; ++x) if (isOnPixel(pix[y * w + x])) ++n;
  return n;
}
function calibrationBlobBounds(x0, y0, x1, y1) {
  if (!wasm?.bdm_wasm_get_framebuffer || !memory) return null;
  const w = wasm.bdm_wasm_get_width?.() || 160;
  const h = wasm.bdm_wasm_get_height?.() || 120;
  const fb = wasm.bdm_wasm_get_framebuffer?.() || 0;
  if (!fb || w <= 0 || h <= 0) return null;
  x0 = Math.max(0, Math.min(w - 1, x0 | 0)); x1 = Math.max(0, Math.min(w, x1 | 0));
  y0 = Math.max(0, Math.min(h - 1, y0 | 0)); y1 = Math.max(0, Math.min(h, y1 | 0));
  const pix = new Uint32Array(memory.buffer, fb, w * h);
  let minX = 999, minY = 999, maxX = -1, maxY = -1, count = 0;
  for (let y = y0; y < y1; ++y) {
    for (let x = x0; x < x1; ++x) {
      if (isOnPixel(pix[y * w + x])) {
        if (x < minX) minX = x; if (x > maxX) maxX = x;
        if (y < minY) minY = y; if (y > maxY) maxY = y;
        ++count;
      }
    }
  }
  return count ? { minX, minY, maxX, maxY, count } : null;
}
function calibrationBlobCenter(blob) {
  return [clampPenX(Math.floor((blob.minX + blob.maxX + 1) / 2)),
          clampPenY(Math.floor((blob.minY + blob.maxY + 1) / 2))];
}
function detectCalibrationTarget() {
  /* Locate the actual two-pixel-thick cross and inject its center. The earlier
     hardcoded 8,8 and 151,111 were on the edge of each cross, so the firmware
     calibrated a small +1,+1 drawing bias into later touch coordinates. */
  const tl = calibrationBlobBounds(3, 3, 20, 20);
  if (tl && tl.count >= 10 && tl.maxX - tl.minX <= 12 && tl.maxY - tl.minY <= 12) {
    const [x, y] = calibrationBlobCenter(tl);
    return { name: 'top-left calibration', x, y };
  }
  const br = calibrationBlobBounds(140, 100, 160, 120);
  if (br && br.count >= 10 && br.maxX - br.minX <= 12 && br.maxY - br.minY <= 12) {
    const [x, y] = calibrationBlobCenter(br);
    return { name: 'bottom-right calibration', x, y };
  }
  return null;
}
function runFrameWithInput(x, y, down) {
  if (!wasm || wasm.bdm_wasm_get_status?.() !== 3) return false;
  const ok = wasm.bdm_wasm_frame(frameSteps(), buttonsMask(), x | 0, y | 0, down ? 1 : 0);
  pullAudio();
  return !!ok;
}
function autoCalibrationInput() { return null; }
function runInputBurst(frames, x = pen.x, y = pen.y, down = true) {
  if (!wasm || wasm.bdm_wasm_get_status?.() !== 3 || paused || loading) return 0;
  let ran = 0;
  for (; ran < frames; ++ran) {
    if (!runFrameWithInput(x, y, down)) break;
    if (!down && pen.latchedFrames > 0) --pen.latchedFrames;
  }
  renderOnce();
  resetFrameLimiter();
  return ran;
}
function fastForwardAutoCalibration(maxFrames = AUTO_CAL_WAIT_FRAMES) {
  if (!wasm || wasm.bdm_wasm_get_status?.() !== 3 || !config.autoCalibration) return 0;
  if (!autoCal.active) resetAutoCalibration(true);
  clearPen(false);
  const downFrames = Math.max(1, Math.ceil(AUTO_CAL_DOWN_STEPS / frameSteps()));
  let ran = 0;
  let released = 0;
  let sawTarget = false;
  while (autoCal.active && ran < maxFrames) {
    const target = detectCalibrationTarget();
    if (target) {
      sawTarget = true;
      status(`auto calibration: ${target.name}`, 'status-ok');
      const [tx, ty] = displayPixelToPen(target.x, target.y, { unbiased: true });
      for (let i = 0; i < downFrames; ++i) { if (!runFrameWithInput(tx, ty, true)) break; ++ran; }
      for (let i = 0; i < AUTO_CAL_RELEASE_FRAMES; ++i) { if (!runFrameWithInput(tx, ty, false)) break; ++ran; }
      autoCal.taps++;
      released = AUTO_CAL_RELEASE_FRAMES;
      continue;
    }
    if (!runFrameWithInput(0, 0, false)) break;
    ++ran;
    if (sawTarget) {
      if (++released >= AUTO_CAL_RELEASE_FRAMES) {
        autoCal.active = false;
        autoCal.done = true;
        clearPen(false);
        status('auto calibration complete', 'status-ok');
        break;
      }
    }
  }
  if (autoCal.active && ran >= maxFrames) {
    /* Do not mark success if the prompt is still visible.  Leave the assist
       armed so the realtime loop can continue searching. */
    status(sawTarget ? 'auto calibration still settling' : 'auto calibration waiting for prompt', sawTarget ? 'status-ok' : '');
  }
  clearPen(false);
  renderOnce();
  return ran;
}
function maybeRealtimeAutoCalibration() {
  if (!autoCal.active || !config.autoCalibration || pen.physicalDown) return null;
  const target = detectCalibrationTarget();
  if (!target) return null;
  const downFrames = Math.max(1, Math.ceil(AUTO_CAL_DOWN_STEPS / frameSteps()));
  const [tx, ty] = displayPixelToPen(target.x, target.y, { unbiased: true });
  for (let i = 0; i < downFrames; ++i) runFrameWithInput(tx, ty, true);
  for (let i = 0; i < AUTO_CAL_RELEASE_FRAMES; ++i) runFrameWithInput(tx, ty, false);
  autoCal.taps++;
  status('auto calibration complete', 'status-ok');
  autoCal.active = false; autoCal.done = true;
  clearPen(false);
  renderOnce();
  resetFrameLimiter();
  return { x: target.x, y: target.y, down: false };
}
function schedulePostLoadKick(frames = 120) {
  if (kickTimer) clearTimeout(kickTimer);
  let remaining = Math.max(0, frames | 0);
  const kick = () => {
    if (!wasm || paused || loading || wasm.bdm_wasm_get_status?.() !== 3) { kickTimer = 0; return; }
    maybeRealtimeAutoCalibration();
    runFrameWithInput(pen.x | 0, pen.y | 0, effectivePenDown() ? 1 : 0);
    renderOnce();
    if (--remaining > 0) kickTimer = setTimeout(kick, 0);
    else { kickTimer = 0; resetFrameLimiter(); }
  };
  kickTimer = setTimeout(kick, 0);
}


function clearPen(send = true) {
  pen.x = 0;
  pen.y = 0;
  pen.physicalDown = false;
  pen.pointerId = null;
  pen.holdUntil = 0;
  pen.latchedFrames = 0;
  if (send) sendInput();
}
function eventClientPoint(e) {
  if (Number.isFinite(e?.clientX) && Number.isFinite(e?.clientY)) return [e.clientX, e.clientY];
  const t = e?.touches?.[0] || e?.changedTouches?.[0];
  if (t && Number.isFinite(t.clientX) && Number.isFinite(t.clientY)) return [t.clientX, t.clientY];
  return null;
}
const TOUCH_PIXEL_BIAS_X = 0;
const TOUCH_PIXEL_BIAS_Y = 0;
function clampPenX(x) { return Math.max(0, Math.min(159, x | 0)); }
function clampPenY(y) { return Math.max(0, Math.min(119, y | 0)); }
function exactDisplayPixelToPen(x, y) {
  return [clampPenX(x | 0), clampPenY(y | 0)];
}
function correctedDisplayPixelToPen(x, y) {
  const ox = Number.isFinite(config.touchOffsetX) ? config.touchOffsetX : TOUCH_PIXEL_BIAS_X;
  const oy = Number.isFinite(config.touchOffsetY) ? config.touchOffsetY : TOUCH_PIXEL_BIAS_Y;
  return [clampPenX((x | 0) + ox), clampPenY((y | 0) + oy)];
}
function visibleCalibrationTarget() {
  return !!detectCalibrationTarget();
}
function shouldUseUnbiasedTouchForCalibration() {
  return visibleCalibrationTarget();
}
function displayPixelToPen(x, y, opts = {}) {
  if (opts.unbiased) return exactDisplayPixelToPen(x, y);
  return correctedDisplayPixelToPen(x, y);
}
function canvasPoint(e) {
  const r = els.canvas.getBoundingClientRect();
  const pt = eventClientPoint(e);
  const clientX = pt ? pt[0] : r.left + r.width * (pen.x + 0.5) / 160;
  const clientY = pt ? pt[1] : r.top + r.height * (pen.y + 0.5) / 120;
  const dx = Math.max(0, Math.min(159, Math.floor(((clientX - r.left) * 160 / Math.max(1, r.width)) - 1e-7)));
  const dy = Math.max(0, Math.min(119, Math.floor(((clientY - r.top) * 120 / Math.max(1, r.height)) - 1e-7)));
  return displayPixelToPen(dx, dy);
}
function reportTouchDebug(down, x, y) {
  const now = performance.now();
  if (now - lastTouchStatusAt < 80 && down) return;
  lastTouchStatusAt = now;
  status(down ? `touch ${x},${y}` : 'touch released', down ? 'status-ok' : '');
}
function pressPenFromEvent(e) {
  const [x, y] = canvasPoint(e);
  pen.x = x;
  pen.y = y;
  pen.physicalDown = true;
  const holdMs = visibleCalibrationTarget() ? Math.max(config.touchHoldMs || 0, config.calibrationTouchHoldMs || 0) : Math.max(0, config.touchHoldMs || 0);
  pen.holdUntil = Math.max(pen.holdUntil, performance.now() + holdMs);
  pen.latchedFrames = Math.max(pen.latchedFrames, Math.max(1, config.touchBurstFrames || 3));
  sendInput();
  runInputBurst(Math.max(1, config.touchBurstFrames || 3), pen.x, pen.y, true);
  reportTouchDebug(true, pen.x, pen.y);
}
function movePenFromEvent(e) {
  if (!pen.physicalDown) return;
  const [x, y] = canvasPoint(e);
  pen.x = x;
  pen.y = y;
  sendInput();
  reportTouchDebug(effectivePenDown(), pen.x, pen.y);
}
function releasePen() {
  /* Do not recalculate the coordinate on release.  Browser pointerup/mouseup
     can be delivered after the cursor has drifted outside the LCD, and the
     minimum-hold latch would otherwise keep sampling that stale edge position. */
  pen.physicalDown = false;
  pen.latchedFrames = Math.max(pen.latchedFrames, Math.max(1, config.touchBurstFrames || 3));
  sendInput();
  reportTouchDebug(effectivePenDown(), pen.x, pen.y);
}
function runStartupFrames(count) {
  if (!wasm || wasm.bdm_wasm_get_status?.() !== 3) return;
  const n = Math.max(0, Math.min(120, count | 0));
  const steps = Math.max(1, Math.round(config.stepsPerSecond / FRAME_RATE));
  for (let i = 0; i < n; ++i) {
    wasm.bdm_wasm_frame(steps, buttonsMask(), pen.x | 0, pen.y | 0, effectivePenDown() ? 1 : 0);
    if (!pen.physicalDown && pen.latchedFrames > 0) --pen.latchedFrames;
    pullAudio();
  }
}

function applyVideoLayout(w = 160, h = 120) {
  const frame = els.screenFrame.getBoundingClientRect();
  let cssW = frame.width, cssH = frame.height;
  if (config.scaleMode === 'integer') {
    const s = Math.max(1, Math.floor(Math.min(frame.width / w, frame.height / h)));
    cssW = w * s; cssH = h * s;
  } else if (config.scaleMode === 'aspect') {
    const s = Math.min(frame.width / w, frame.height / h);
    cssW = Math.round(w * s); cssH = Math.round(h * s);
  }
  els.canvas.style.width = `${Math.max(1, cssW)}px`;
  els.canvas.style.height = `${Math.max(1, cssH)}px`;
  document.body.classList.toggle('smooth', !!config.smoothUpscale);
}
function renderOnce() {
  if (!ctx) ctx = els.canvas.getContext('2d', { alpha: false });
  const w = wasm?.bdm_wasm_get_width?.() || 160, h = wasm?.bdm_wasm_get_height?.() || 120;
  if (els.canvas.width !== w || els.canvas.height !== h) { els.canvas.width = w; els.canvas.height = h; imageData = null; }
  if (!imageData) imageData = ctx.createImageData(w, h);
  const fb = wasm?.bdm_wasm_get_framebuffer?.() || 0;
  if (fb) {
    const src = new Uint32Array(memory.buffer, fb, w * h);
    const dst = imageData.data;
    for (let i = 0, j = 0; i < src.length; ++i, j += 4) {
      const p = src[i]; dst[j] = (p >>> 16) & 255; dst[j + 1] = (p >>> 8) & 255; dst[j + 2] = p & 255; dst[j + 3] = 255;
    }
    ctx.putImageData(imageData, 0, 0);
  } else {
    ctx.fillStyle = '#c6d1bd'; ctx.fillRect(0, 0, w, h);
  }
  applyVideoLayout(w, h);
  const statusCode = wasm?.bdm_wasm_get_status?.() ?? 0;
  els.runtimeStatus.textContent = STATUS_TEXT[statusCode] || String(statusCode);
  els.runtimeFrame.textContent = String(wasm?.bdm_wasm_get_frame_count?.() || 0);
  els.runtimeResolution.textContent = `${w}×${h}`;
  if (config.showFps) els.fpsCounter.textContent = `${fpsValue.toFixed(1)} FPS`;
  els.fpsCounter.classList.toggle('hidden', !config.showFps);
}
function updateFps(now) {
  const frame = wasm?.bdm_wasm_get_frame_count?.() || 0;
  if (!fpsLastTime) { fpsLastTime = now; fpsLastFrame = frame; return; }
  if (now - fpsLastTime >= 500) {
    fpsValue = (frame - fpsLastFrame) * 1000 / (now - fpsLastTime);
    fpsLastTime = now; fpsLastFrame = frame;
  }
}
function resetFrameLimiter(now = performance.now()) { limiterLast = now; limiterAcc = 0; fpsLastTime = 0; fpsLastFrame = wasm?.bdm_wasm_get_frame_count?.() || 0; }
function tick(now) {
  if (wasm && !paused && !loading && wasm.bdm_wasm_get_status?.() === 3) {
    let elapsed = limiterLast ? now - limiterLast : 1000 / FRAME_RATE;
    limiterLast = now;
    if (!Number.isFinite(elapsed) || elapsed < 0) elapsed = 0;
    if (elapsed > 250) elapsed = 1000 / FRAME_RATE;
    limiterAcc = Math.min(limiterAcc + elapsed, (1000 / FRAME_RATE) * 4);
    const interval = 1000 / FRAME_RATE;
    let ran = 0;
    while (limiterAcc >= interval && ran < 4) {
      if (maybeRealtimeAutoCalibration()) { limiterAcc -= interval; ran++; continue; }
      wasm.bdm_wasm_frame(frameSteps(), buttonsMask(), pen.x | 0, pen.y | 0, effectivePenDown(now) ? 1 : 0);
      if (!pen.physicalDown && pen.latchedFrames > 0) --pen.latchedFrames;
      pullAudio();
      limiterAcc -= interval;
      ran++;
    }
    updateFps(now);
  } else {
    limiterLast = now; limiterAcc = 0;
  }
  renderOnce();
  frameRequest = requestAnimationFrame(tick);
}

async function startAudio() {
  if (!config.audioEnabled) return;
  const AudioCtor = window.AudioContext || window.webkitAudioContext;
  if (!AudioCtor) return;
  if (!audioCtx) audioCtx = new AudioCtor({ latencyHint: 'interactive' });
  if (audioCtx.state !== 'running') { try { await audioCtx.resume(); } catch (_) {} }
  if (!audioNode) {
    audioNode = audioCtx.createScriptProcessor(1024, 0, 2);
    audioNode.onaudioprocess = e => {
      const l = e.outputBuffer.getChannelData(0), r = e.outputBuffer.getChannelData(1);
      for (let i = 0; i < l.length; ++i) {
        if (!audioQueue.length) { l[i] = r[i] = 0; continue; }
        const front = audioQueue[0];
        const v = front[audioOffset++] / 32768;
        l[i] = r[i] = v;
        audioFramesQueued--;
        if (audioOffset >= front.length) { audioQueue.shift(); audioOffset = 0; }
      }
    };
    audioNode.connect(audioCtx.destination);
  }
}
function clearAudioQueue() { audioQueue = []; audioOffset = 0; audioFramesQueued = 0; }
function pullAudio() {
  if (!config.audioEnabled || !wasm?.bdm_wasm_get_audio_frames) return;
  if (audioFramesQueued > (config.sampleRate || 44100) / 2) clearAudioQueue();
  const frames = wasm.bdm_wasm_get_audio_frames();
  if (!frames) return;
  const ptr = wasm.bdm_wasm_get_audio_ptr();
  audioQueue.push(new Int16Array(memory.buffer, ptr, frames).slice());
  audioFramesQueued += frames;
  wasm.bdm_wasm_audio_consume(frames);
}

function setFullscreen(full) { if (full && !document.fullscreenElement) els.screenFrame.requestFullscreen?.(); else if (!full && document.fullscreenElement) document.exitFullscreen?.(); }
function cycleScaleMode() { config.scaleMode = config.scaleMode === 'integer' ? 'aspect' : config.scaleMode === 'aspect' ? 'stretch' : 'integer'; syncControls(); saveConfig(); renderOnce(); }
function toggleMenu() { document.body.classList.toggle('menu-open'); }
function rebuildControlMap() {
  els.controlMap.innerHTML = '';
  for (const name of ['A','B','Start','Select','Reset','SaveState','LoadState','Fullscreen','ScaleMode','Menu']) {
    const row = document.createElement('button');
    row.type = 'button'; row.className = 'map-row';
    row.innerHTML = `<span>${name}</span><code>${config.keys[name]}</code>`;
    row.onclick = () => { remapTarget = name; row.classList.add('remapping'); row.querySelector('code').textContent = 'press key'; els.screenFrame.focus(); };
    els.controlMap.append(row);
  }
}
function syncControls() {
  els.scaleMode.value = config.scaleMode;
  els.smoothUpscale.checked = !!config.smoothUpscale;
  els.showFps.checked = !!config.showFps;
  els.audioEnabled.checked = !!config.audioEnabled;
  els.sampleRate.value = String(config.sampleRate);
  if (els.touchHoldMs) els.touchHoldMs.value = String(config.touchHoldMs);
  if (els.calibrationTouchHoldMs) els.calibrationTouchHoldMs.value = String(config.calibrationTouchHoldMs);
  if (els.touchOffsetX) els.touchOffsetX.value = String(config.touchOffsetX);
  if (els.touchOffsetY) els.touchOffsetY.value = String(config.touchOffsetY);
  if (els.autoCalibration) els.autoCalibration.checked = !!config.autoCalibration;
  els.stateSlot.value = String(config.stateSlot);
  els.pauseToggle.textContent = paused ? 'Resume' : 'Pause';
  rebuildControlMap();
}
function initUi() {
  for (let i = 0; i < 10; ++i) { const o = document.createElement('option'); o.value = String(i); o.textContent = String(i); els.stateSlot.append(o); }
  syncControls();
  els.menuToggle.onclick = toggleMenu;
  els.closeMenu.onclick = () => document.body.classList.remove('menu-open');
  els.fullscreenToggle.onclick = () => setFullscreen(!document.fullscreenElement);
  if (els.gameFiles) els.gameFiles.onchange = e => loadGame(e.target.files);
  if (els.loadPairButton) els.loadPairButton.onclick = loadSeparateFields;
  if (els.cartFile) els.cartFile.onchange = () => { if (els.cartFile.files?.length && !els.mediaFile?.files?.length) status('G cart selected; choose optional M cart or press Load selected carts', 'status-ok'); };
  if (els.mediaFile) els.mediaFile.onchange = () => { if (els.mediaFile.files?.length) status('M media cart selected; press Load selected carts', 'status-ok'); };
  for (const target of [window, document, document.body, els.screenFrame, els.canvas]) {
    target.addEventListener('dragenter', handleDragEnter, true);
    target.addEventListener('dragover', handleDragOver, true);
    target.addEventListener('dragleave', handleDragLeave, true);
    target.addEventListener('drop', handleDrop, true);
  }
  els.softResetButton.onclick = () => { if (wasm?.bdm_wasm_soft_reset) { clearPen(); wasm.bdm_wasm_soft_reset(); resetAutoCalibration(!!config.autoCalibration); if (config.autoCalibration) fastForwardAutoCalibration(AUTO_CAL_WAIT_FRAMES); else { runStartupFrames(2); renderOnce(); } resetFrameLimiter(); schedulePostLoadKick(180); status(config.autoCalibration ? 'soft reset, auto calibration assist active' : 'soft reset', 'status-ok'); } };
  els.pauseToggle.onclick = () => { paused = !paused; syncControls(); resetFrameLimiter(); };
  els.saveState.onclick = saveStateToLocalStorage;
  els.loadState.onclick = loadStateFromLocalStorage;
  els.exportState.onclick = exportState;
  els.importStateFile.onchange = e => importState(e.target.files?.[0]);
  for (const el of [els.scaleMode, els.smoothUpscale, els.showFps, els.audioEnabled, els.sampleRate, els.touchHoldMs, els.calibrationTouchHoldMs, els.touchOffsetX, els.touchOffsetY, els.autoCalibration, els.stateSlot].filter(Boolean)) {
    el.onchange = async () => {
      config.scaleMode = els.scaleMode.value;
      config.smoothUpscale = els.smoothUpscale.checked;
      config.showFps = els.showFps.checked;
      config.audioEnabled = els.audioEnabled.checked;
      config.sampleRate = clampNumber(els.sampleRate.value, 8000, 192000, 44100);
      config.touchHoldMs = clampNumber(els.touchHoldMs ? els.touchHoldMs.value : config.touchHoldMs, 0, 5000, DEFAULT_CONFIG.touchHoldMs);
      config.calibrationTouchHoldMs = clampNumber(els.calibrationTouchHoldMs ? els.calibrationTouchHoldMs.value : config.calibrationTouchHoldMs, 0, 5000, DEFAULT_CONFIG.calibrationTouchHoldMs);
      config.touchOffsetX = clampNumber(els.touchOffsetX ? els.touchOffsetX.value : config.touchOffsetX, -8, 8, DEFAULT_CONFIG.touchOffsetX);
      config.touchOffsetY = clampNumber(els.touchOffsetY ? els.touchOffsetY.value : config.touchOffsetY, -8, 8, DEFAULT_CONFIG.touchOffsetY);
      config.autoCalibration = els.autoCalibration ? !!els.autoCalibration.checked : !!config.autoCalibration;
      if (!config.autoCalibration) resetAutoCalibration(false);
      config.stateSlot = clampNumber(els.stateSlot.value, 0, 9, 0);
      saveConfig(); syncControls(); renderOnce();
      if (config.audioEnabled) await startAudio(); else clearAudioQueue();
    };
  }
  window.addEventListener('resize', () => renderOnce());
  window.addEventListener('keydown', e => {
    if (remapTarget) { config.keys[remapTarget] = e.code; remapTarget = null; saveConfig(); rebuildControlMap(); e.preventDefault(); return; }
    if (e.code === config.keys.Fullscreen) { setFullscreen(!document.fullscreenElement); e.preventDefault(); return; }
    if (e.code === config.keys.ScaleMode) { cycleScaleMode(); e.preventDefault(); return; }
    if (e.code === config.keys.Menu) { toggleMenu(); e.preventDefault(); return; }
    if (e.code === config.keys.Reset) { clearPen(); wasm?.bdm_wasm_soft_reset?.(); resetAutoCalibration(!!config.autoCalibration); if (config.autoCalibration) fastForwardAutoCalibration(AUTO_CAL_WAIT_FRAMES); else { runStartupFrames(2); renderOnce(); } resetFrameLimiter(); schedulePostLoadKick(180); e.preventDefault(); return; }
    if (e.code === config.keys.SaveState) { saveStateToLocalStorage(); e.preventDefault(); return; }
    if (e.code === config.keys.LoadState) { loadStateFromLocalStorage(); e.preventDefault(); return; }
    pressed.add(e.code);
    if (Object.values(config.keys).includes(e.code)) e.preventDefault();
  });
  window.addEventListener('keyup', e => pressed.delete(e.code));
  window.addEventListener('blur', () => { pressed.clear(); clearPen(); });
  els.canvas.addEventListener('contextmenu', e => e.preventDefault());
  const touchTarget = els.canvas;
  touchTarget.addEventListener('pointerdown', async e => {
    els.screenFrame.focus();
    await startAudio();
    pen.pointerId = e.pointerId;
    touchTarget.setPointerCapture?.(e.pointerId);
    pressPenFromEvent(e);
    e.preventDefault();
  });
  touchTarget.addEventListener('pointermove', e => {
    if (pen.physicalDown || e.pointerId === pen.pointerId) {
      movePenFromEvent(e);
      e.preventDefault();
    }
  });
  const penUp = e => {
    if (pen.pointerId === null || e.pointerId === pen.pointerId) {
      releasePen();
      try { touchTarget.releasePointerCapture?.(e.pointerId); } catch (_) {}
      pen.pointerId = null;
      e.preventDefault();
    }
  };
  touchTarget.addEventListener('pointerup', penUp);
  touchTarget.addEventListener('pointercancel', penUp);
  touchTarget.addEventListener('pointerleave', e => { if (pen.physicalDown && e.buttons === 0) penUp(e); });

  /* Safari/iOS fallback for builds where PointerEvent is partial or missing. */
  if (!window.PointerEvent) touchTarget.addEventListener('touchstart', async e => { await startAudio(); pressPenFromEvent(e); e.preventDefault(); }, { passive: false });
  if (!window.PointerEvent) touchTarget.addEventListener('touchmove', e => { movePenFromEvent(e); e.preventDefault(); }, { passive: false });
  if (!window.PointerEvent) touchTarget.addEventListener('touchend', e => { releasePen(); e.preventDefault(); }, { passive: false });
  if (!window.PointerEvent) touchTarget.addEventListener('mousedown', async e => { await startAudio(); pressPenFromEvent(e); e.preventDefault(); });
  if (!window.PointerEvent) touchTarget.addEventListener('mousemove', e => { if (pen.physicalDown) { movePenFromEvent(e); e.preventDefault(); } });
  if (!window.PointerEvent) window.addEventListener('mouseup', e => { if (pen.physicalDown) releasePen(); });
}

async function main() {
  initUi();
  ctx = els.canvas.getContext('2d', { alpha: false });
  try { await instantiateBackend(); status('WASM ready', 'status-ok'); }
  catch (e) { console.error(e); status(`WASM load failed: ${e.message}`, 'status-bad'); }
  frameRequest = requestAnimationFrame(tick);
}
main();
