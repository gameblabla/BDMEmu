import fs from 'node:fs';
const wasmBytes = fs.readFileSync(new URL('../web/bdm_wasm_core.wasm', import.meta.url));
const { instance } = await WebAssembly.instantiate(wasmBytes, { pcfx: { read_host_file(){ return 0; } } });
const w = instance.exports;
function memu8(){ return new Uint8Array(w.memory.buffer); }
function copy(bytes){ const ptr=w.bdm_wasm_malloc(bytes.length); memu8().set(bytes, ptr); return ptr; }
const cart=fs.readFileSync('/mnt/data/bdm_roms/Dungeon Diver [G.02] (Japan).bin');
const media=fs.readFileSync('/mnt/data/bdm_roms/From TV Animation Slam Dunk [M.02] (Japan).bin');
if(!w.bdm_wasm_init(44100,2000000)) throw Error('init');
if(!w.bdm_wasm_load_cart(copy(cart), cart.length)) throw Error('cart');
if(!w.bdm_wasm_load_media(copy(media), media.length)) throw Error('media');
if(!w.bdm_wasm_start()) throw Error('start');
for(let i=0;i<10;i++) if(!w.bdm_wasm_frame(33333,0,0,0,0)) throw Error('frame');
const fb=w.bdm_wasm_get_framebuffer();
console.log({status:w.bdm_wasm_get_status(), frames:w.bdm_wasm_get_frame_count(), w:w.bdm_wasm_get_width(), h:w.bdm_wasm_get_height(), fb});
