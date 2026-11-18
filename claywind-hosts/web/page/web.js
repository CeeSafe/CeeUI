// SPDX-License-Identifier: MIT
// The web host's page: it starts the worker that runs the WebAssembly module, tells it the pointer, the keys and the
// canvas's size, wakes it once for each of the browser's frames, and paints what it drew.
//
// The module's program is a loop that waits for the next frame, so it runs in a worker (a page may not wait). The
// two share three buffers: a few numbers (control), the events not yet taken (a ring), and the last frame's pixels.

const MAX_WIDTH = 1600, MAX_HEIGHT = 1000;
const EVENTS = 1024;                       // events the ring holds; each is a kind and four numbers
const TICK = 0, SHOWN = 1, WIDTH = 2, HEIGHT = 3, CURSOR = 4, HEAD = 5, TAIL = 6, STATE = 7;
const MOVE = 1, BUTTON = 2, WHEEL = 3, KEY = 4, TEXT = 5, RESIZE = 6, FOCUS = 7;
// ClayWind's Key, in keyIndex's order (claywind/src/platform.cee).
const KEYS = ['Escape', 'Enter', 'Tab', 'Backspace', 'Delete', 'ArrowLeft', 'ArrowRight', 'ArrowUp', 'ArrowDown', 'Home',
  'End', 'PageUp', 'PageDown', 'ShiftLeft', 'ShiftRight', 'ControlLeft', 'ControlRight', 'AltLeft', 'AltRight',
  'F1', 'F2', 'F3', 'F4', 'F5', 'F6', 'F7', 'F8', 'F9', 'F10', 'F11', 'F12'];

const canvas = document.getElementById('frame');
const note = document.getElementById('note');
const context = canvas.getContext('2d');

function say(text) { note.textContent = text; }

if (!crossOriginIsolated || typeof SharedArrayBuffer === 'undefined') {
  say('This page needs cross-origin isolation (COOP and COEP headers): serve it with serve.py.');
  throw new Error('not cross-origin isolated');
}

const control = new Int32Array(new SharedArrayBuffer(8 * 4));
const events = new Float64Array(new SharedArrayBuffer(EVENTS * 5 * 8));
const frame = new Uint8Array(new SharedArrayBuffer(MAX_WIDTH * MAX_HEIGHT * 4));

function size() {
  return [Math.max(1, Math.min(MAX_WIDTH, Math.floor(innerWidth))), Math.max(1, Math.min(MAX_HEIGHT, Math.floor(innerHeight)))];
}

function queue(kind, a = 0, b = 0, c = 0, d = 0) {
  const head = Atomics.load(control, HEAD), tail = Atomics.load(control, TAIL);
  if (((tail + 1) % EVENTS) === head) return;   // full: the event is dropped
  events.set([kind, a, b, c, d], tail * 5);
  Atomics.store(control, TAIL, (tail + 1) % EVENTS);
}

function at(e) {
  const r = canvas.getBoundingClientRect();
  return [e.clientX - r.left, e.clientY - r.top];
}

function modifiers(e) { return (e.shiftKey ? 1 : 0) | (e.ctrlKey ? 2 : 0) | (e.altKey ? 4 : 0) | (e.metaKey ? 8 : 0); }

canvas.addEventListener('pointermove', (e) => queue(MOVE, ...at(e)));
canvas.addEventListener('pointerdown', (e) => { canvas.focus(); canvas.setPointerCapture(e.pointerId); queue(BUTTON, e.button, 1, ...at(e)); });
canvas.addEventListener('pointerup', (e) => queue(BUTTON, e.button, 0, ...at(e)));
canvas.addEventListener('contextmenu', (e) => e.preventDefault());
canvas.addEventListener('wheel', (e) => { e.preventDefault(); queue(WHEEL, -e.deltaX / 40, -e.deltaY / 40); }, { passive: false });
canvas.addEventListener('blur', () => queue(FOCUS, 0));
canvas.addEventListener('focus', () => queue(FOCUS, 1));

function key(e, down) {
  const index = KEYS.indexOf(e.code);
  const printable = e.key.length === 1 || [...e.key].length === 1;
  const character = printable ? e.key.codePointAt(0) : 0;
  queue(KEY, index, (down ? 1 : 0) | (e.repeat ? 2 : 0), modifiers(e), character);
  // A printable key with no Control or Command is typed text too.
  if (down && printable && !e.ctrlKey && !e.metaKey) queue(TEXT, character);
  // The page keeps its own shortcuts (reload, the developer tools); everything else is the app's.
  if (index >= 0 && !e.metaKey && !(e.code.startsWith('F') && e.code.length > 1)) e.preventDefault();
  if (e.code === 'Space') e.preventDefault();
}
canvas.addEventListener('keydown', (e) => key(e, true));
canvas.addEventListener('keyup', (e) => key(e, false));

function resize() {
  const [w, h] = size();
  canvas.style.width = w + 'px';
  canvas.style.height = h + 'px';
  queue(RESIZE, w, h);
}
addEventListener('resize', resize);

const CURSORS = ['default', 'pointer', 'text'];
let shown = 0, image = null;

function paint() {
  // One tick for each of the browser's frames: the worker, which waits on it, runs one frame of the program.
  Atomics.add(control, TICK, 1);
  Atomics.notify(control, TICK);
  const state = Atomics.load(control, STATE);
  if (state < 0) { say(state === -1 ? 'The program could not start (memory or a font).' : 'The program stopped (see the console).'); return; }
  const sequence = Atomics.load(control, SHOWN);
  if (sequence !== shown) {
    shown = sequence;
    const w = Atomics.load(control, WIDTH), h = Atomics.load(control, HEIGHT);
    if (canvas.width !== w || canvas.height !== h) { canvas.width = w; canvas.height = h; image = null; }
    if (image === null) image = context.createImageData(w, h);
    image.data.set(frame.subarray(0, w * h * 4));   // an ImageData cannot be over shared memory: one copy
    context.putImageData(image, 0, 0);
    canvas.style.cursor = CURSORS[Atomics.load(control, CURSOR)] || 'default';
  }
  requestAnimationFrame(paint);
}

const [width, height] = size();
canvas.style.width = width + 'px';
canvas.style.height = height + 'px';
say('Loading…');
const fonts = await Promise.all(['regular.ttf', 'bold.ttf', 'mono.ttf'].map(async (name) => {
  const response = await fetch('fonts/' + name);
  if (!response.ok) throw new Error('cannot load fonts/' + name);
  return new Uint8Array(await response.arrayBuffer());
}));
const worker = new Worker('worker.js', { type: 'module' });
worker.onerror = (e) => say('The worker failed: ' + e.message);
worker.onmessage = (e) => { if (e.data === 'running') { say(''); canvas.focus(); } else say(String(e.data)); };
worker.postMessage({ control, events, frame, fonts, width, height });
requestAnimationFrame(paint);
