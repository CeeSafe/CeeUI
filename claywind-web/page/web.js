// SPDX-License-Identifier: MIT
// The web host's page: it starts the worker that runs the WebAssembly module, tells it the pointer, the keys and the
// canvas's size, wakes it once for each of the browser's frames, and paints what it drew.
//
// The module's program is a loop that waits for the next frame, so it runs in a worker (a page may not wait). The
// two share five buffers: a few numbers (control), the events not yet taken (a ring), the last frame's pixels, and
// the texts of the last paste and the last copy.

const MAX_WIDTH = 2880, MAX_HEIGHT = 1800;   // the largest frame, in pixels (main.cee has the same)
const PASTE_BYTES = 1048576, COPY_BYTES = 1048576;
const EVENTS = 1024;                       // events the ring holds; each is a kind and four numbers
const TICK = 0, WIDTH = 2, HEIGHT = 3, CURSOR = 4, HEAD = 5, TAIL = 6, STATE = 7, WAKE = 8, BOXES = 9, PASTED = 10, COPIES = 11, COPIED = 12;
const BOX = 16, MAX_BOXES = 16;            // the changed boxes not yet painted: four numbers each, from control[BOX]
const MOVE = 1, BUTTON = 2, WHEEL = 3, KEY = 4, TEXT = 5, RESIZE = 6, FOCUS = 7, TOUCH = 8, ACCESS = 9;
// ClayWind's Key, in keyIndex's order (claywind/src/platform.cee).
const KEYS = ['Escape', 'Enter', 'Tab', 'Backspace', 'Delete', 'ArrowLeft', 'ArrowRight', 'ArrowUp', 'ArrowDown', 'Home',
  'End', 'PageUp', 'PageDown', 'ShiftLeft', 'ShiftRight', 'ControlLeft', 'ControlRight', 'AltLeft', 'AltRight',
  'F1', 'F2', 'F3', 'F4', 'F5', 'F6', 'F7', 'F8', 'F9', 'F10', 'F11', 'F12'];

const canvas = document.getElementById('frame');
const note = document.getElementById('note');
const access = document.getElementById('access');
const context = canvas.getContext('2d');

function say(text) { note.textContent = text; }

if (!crossOriginIsolated || typeof SharedArrayBuffer === 'undefined') {
  say('This page needs cross-origin isolation (COOP and COEP headers): serve it with serve.py.');
  throw new Error('not cross-origin isolated');
}

const control = new Int32Array(new SharedArrayBuffer((BOX + MAX_BOXES * 4) * 4));
const events = new Float64Array(new SharedArrayBuffer(EVENTS * 5 * 8));
const frame = new Uint8Array(new SharedArrayBuffer(MAX_WIDTH * MAX_HEIGHT * 4));
const pasted = new Uint8Array(new SharedArrayBuffer(PASTE_BYTES));
const copied = new Uint8Array(new SharedArrayBuffer(COPY_BYTES));
const apple = /Mac|iPhone|iPad/.test(navigator.platform);

// The canvas fills the window: its size in layout points (CSS pixels).
function size() { return [Math.max(1, Math.floor(innerWidth)), Math.max(1, Math.floor(innerHeight))]; }

function queue(kind, a = 0, b = 0, c = 0, d = 0) {
  const head = Atomics.load(control, HEAD), tail = Atomics.load(control, TAIL);
  if (((tail + 1) % EVENTS) === head) return;   // full: the event is dropped
  events.set([kind, a, b, c, d], tail * 5);
  Atomics.store(control, TAIL, (tail + 1) % EVENTS);
  // An idle program sleeps until an event: wake it.
  Atomics.add(control, WAKE, 1);
  Atomics.notify(control, WAKE);
}

function at(e) {
  const r = canvas.getBoundingClientRect();
  return [e.clientX - r.left, e.clientY - r.top];
}

function modifiers(e) { return (e.shiftKey ? 1 : 0) | (e.ctrlKey ? 2 : 0) | (e.altKey ? 4 : 0) | (e.metaKey ? 8 : 0); }

// A finger is a touch, for the Ui's gestures; the first finger is the pointer too, as on a phone's window.
function touch(e, phase) {
  if (e.pointerType === 'touch') queue(TOUCH, e.pointerId % 0x20000000 * 4 + phase, ...at(e), e.timeStamp / 1000);
  return e.pointerType !== 'touch' || e.isPrimary;
}
canvas.addEventListener('pointermove', (e) => { if (touch(e, 1)) queue(MOVE, ...at(e)); });
canvas.addEventListener('pointerdown', (e) => {
  canvas.focus();
  try { canvas.setPointerCapture(e.pointerId); } catch (error) { /* not a pointer the browser knows: no capture */ }
  if (touch(e, 0)) queue(BUTTON, e.button, 1, ...at(e));
});
canvas.addEventListener('pointerup', (e) => { if (touch(e, 2)) queue(BUTTON, e.button, 0, ...at(e)); });
canvas.addEventListener('pointercancel', (e) => { if (touch(e, 3)) queue(BUTTON, 0, 0, ...at(e)); });
canvas.addEventListener('contextmenu', (e) => e.preventDefault());
canvas.addEventListener('wheel', (e) => { e.preventDefault(); queue(WHEEL, -e.deltaX / 40, -e.deltaY / 40); }, { passive: false });
// The keyboard is the app's while the canvas or one of its elements for a screen reader has the focus.
addEventListener('blur', () => queue(FOCUS, 0));
addEventListener('focus', () => queue(FOCUS, 1));

function key(e, down) {
  // A screen reader steps a slider with the arrow keys on its element: the app is asked for the step itself.
  const slider = e.target !== canvas && e.target.getAttribute('role') === 'slider';
  if (slider && ['ArrowUp', 'ArrowRight', 'ArrowDown', 'ArrowLeft'].includes(e.code)) {
    if (down) queue(ACCESS, Number(e.target.dataset.id), e.code === 'ArrowUp' || e.code === 'ArrowRight' ? 2 : 3);
    e.preventDefault();
    return;
  }
  // Paste: the text comes with the browser's paste event, which queues the key itself (below).
  // Copy and Cut likewise: the browser's event asks the program for the text.
  if (down && ['KeyV', 'KeyC', 'KeyX'].includes(e.code) && (apple ? e.metaKey : e.ctrlKey) && !e.altKey) return;
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
for (const target of [canvas, access]) {
  target.addEventListener('keydown', (e) => key(e, true));
  target.addEventListener('keyup', (e) => key(e, false));
}

// The text of the program's copy number `sequence`.
let taken = 0;
function copyText() {
  taken = Atomics.load(control, COPIES);
  return new TextDecoder().decode(copied.slice(0, Atomics.load(control, COPIED)));
}

// A copy or a cut: the shortcut goes to the program, and the text it copies in answer is put on the clipboard here,
// inside the browser's event, which is the one place a page may always write it. The program answers within a frame,
// in its worker; the page waits for that, a tenth of a second at most.
function copy(e, letter) {
  e.preventDefault();
  const before = Atomics.load(control, COPIES);
  queue(KEY, -1, 1, apple ? 8 : 2, letter.codePointAt(0));
  // The page's frames stop while it waits, so it ticks here: the program may need more than one frame to answer.
  const until = performance.now() + 100;
  let ticked = 0;
  while (Atomics.load(control, COPIES) === before && performance.now() < until) {
    if (performance.now() - ticked >= 2) {
      ticked = performance.now();
      Atomics.add(control, TICK, 1);
      Atomics.notify(control, TICK);
    }
  }
  if (Atomics.load(control, COPIES) !== before) e.clipboardData.setData('text/plain', copyText());
}
document.addEventListener('copy', (e) => copy(e, 'c'));
document.addEventListener('cut', (e) => copy(e, 'x'));

// A paste: its text where the program reads it (web_pasted), then the shortcut that makes the Ui ask for it.
document.addEventListener('paste', (e) => {
  e.preventDefault();
  const text = new TextEncoder().encode(e.clipboardData.getData('text/plain'));
  let n = Math.min(text.length, PASTE_BYTES);
  while (n > 0 && n < text.length && (text[n] & 0xC0) === 0x80) n--;   // not through the middle of a character
  pasted.set(text.subarray(0, n));
  Atomics.store(control, PASTED, n);
  queue(KEY, -1, 1, apple ? 8 : 2, 'v'.codePointAt(0));
});

function resize() {
  const [w, h] = size();
  canvas.style.width = w + 'px';
  canvas.style.height = h + 'px';
  queue(RESIZE, w, h, devicePixelRatio);
}
addEventListener('resize', resize);   // a change of zoom, or of the screen's pixel ratio, is a resize too

// Accessibility. The canvas is pixels, so the app's elements (ClayWind's access list: a role, a name, a state and a
// box each) are kept as elements of the page over it, in reading order, for a screen reader to find. What the reader
// does to one (a click, the focus, a slider's step) goes to the app as a request; the app's focus is the page's.
const ROLES = ['paragraph', 'button', 'checkbox', 'radio', 'textbox', 'slider', 'tab', 'button', 'menuitem', 'button', 'img'];
const TEXT_ROLE = 0, CHECKBOX = 2, RADIO = 3, TEXTBOX = 4, SLIDER = 5, TAB = 6, LIST_ITEM = 7, DROPDOWN = 9;
const CHECKED = 1, SELECTED = 2, FOCUSED = 4, DISABLED = 8, EXPANDED = 16, SECURE = 32;
const mirrored = new Map();   // an element's id to its element here
let focusing = null;          // the element the page is giving the focus because the app said so

function mirror(nodes) {
  const live = new Set();
  let previous = null, focused = null;
  for (const node of nodes) {
    if (live.has(node.id)) continue;   // the page takes each id once
    live.add(node.id);
    let element = mirrored.get(node.id);
    if (element === undefined || Number(element.dataset.role) !== node.role) {
      if (element !== undefined) element.remove();
      element = document.createElement('div');
      element.dataset.id = node.id;
      element.dataset.role = node.role;
      element.setAttribute('role', ROLES[node.role] || 'paragraph');
      if (node.role !== TEXT_ROLE) element.tabIndex = -1;   // a reader may focus it; Tab stays the app's
      mirrored.set(node.id, element);
    }
    const set = (name, value) => { if (value === null) element.removeAttribute(name); else if (element.getAttribute(name) !== String(value)) element.setAttribute(name, value); };
    const has = (flag) => (node.flags & flag) !== 0;
    element.style.left = node.x + 'px';
    element.style.top = node.y + 'px';
    element.style.width = Math.max(1, node.w) + 'px';
    element.style.height = Math.max(1, node.h) + 'px';
    if (node.role === TEXT_ROLE) {
      if (element.textContent !== node.label) element.textContent = node.label;
    } else {
      set('aria-label', node.label);
    }
    set('aria-disabled', has(DISABLED) ? 'true' : null);
    if (node.role === CHECKBOX || node.role === RADIO) set('aria-checked', has(CHECKED) ? 'true' : 'false');
    if (node.role === TAB) set('aria-selected', has(SELECTED) ? 'true' : 'false');
    if (node.role === LIST_ITEM) set('aria-current', has(SELECTED) ? 'true' : null);
    if (node.role === DROPDOWN) { set('aria-haspopup', 'listbox'); set('aria-expanded', has(EXPANDED) ? 'true' : 'false'); }
    if (node.role === DROPDOWN || node.role === LIST_ITEM) set('aria-description', node.value === '' ? null : node.value);
    if (node.role === TEXTBOX) {
      const shown = has(SECURE) ? '•'.repeat([...node.value].length) : node.value;
      if (element.textContent !== shown) element.textContent = shown;
    }
    if (node.role === SLIDER) {
      // With no text of its own, a slider is read as a percentage of its range.
      const span = node.max - node.min;
      set('aria-valuemin', node.value === '' ? 0 : node.min);
      set('aria-valuemax', node.value === '' ? 100 : node.max);
      set('aria-valuenow', node.value === '' ? (span > 0 ? Math.round((node.now - node.min) / span * 100) : 0) : node.now);
      set('aria-valuetext', node.value === '' ? null : node.value);
    }
    // In reading order: each after the one before it.
    const before = previous === null ? access.firstChild : previous.nextSibling;
    if (before !== element) access.insertBefore(element, before);
    previous = element;
    if (has(FOCUSED)) focused = element;
  }
  for (const [id, element] of mirrored) {
    if (!live.has(id)) { element.remove(); mirrored.delete(id); }
  }
  // The app's focus is the page's, so a reader says where it went; with none, the canvas keeps the keyboard.
  const wanted = focused === null ? canvas : focused;
  const inside = document.activeElement === canvas || access.contains(document.activeElement);
  if (inside && document.activeElement !== wanted) {
    focusing = wanted;
    wanted.focus({ preventScroll: true });
    focusing = null;
  }
}

// A reader's click on an element is a press; its focus, moved there by the reader, is the app's focus.
access.addEventListener('click', (e) => { if (e.target.dataset.id) queue(ACCESS, Number(e.target.dataset.id), 0); });
access.addEventListener('focusin', (e) => { if (e.target.dataset.id && e.target !== focusing) queue(ACCESS, Number(e.target.dataset.id), 1); });

const CURSORS = ['default', 'pointer', 'text'];
let image = null;

function paint() {
  // One tick for each of the browser's frames: the worker, which waits on it, runs one frame of the program.
  Atomics.add(control, TICK, 1);
  Atomics.notify(control, TICK);
  const state = Atomics.load(control, STATE);
  if (state < 0) { say(state === -1 ? 'The program could not start (memory or a font).' : 'The program stopped (see the console).'); return; }
  // The boxes the program drew since the last paint. It may add one while they are taken: then they are read again
  // (a box painted twice shows the same pixels).
  let count, boxes;
  do {
    count = Atomics.load(control, BOXES);
    boxes = Array.from(control.subarray(BOX, BOX + count * 4));
  } while (Atomics.compareExchange(control, BOXES, count, 0) !== count);
  if (count > 0) {
    const w = Atomics.load(control, WIDTH), h = Atomics.load(control, HEIGHT);
    if (canvas.width !== w || canvas.height !== h) { canvas.width = w; canvas.height = h; image = null; }
    if (image === null) { image = context.createImageData(w, h); boxes = [0, 0, w, h]; }
    for (let k = 0; k < boxes.length; k += 4) {
      const x = Math.min(boxes[k], w), y = Math.min(boxes[k + 1], h);
      const bw = Math.min(boxes[k + 2], w - x), bh = Math.min(boxes[k + 3], h - y);
      // An ImageData cannot be over shared memory: the box's rows are copied, then drawn.
      for (let row = y; row < y + bh; row++) {
        const from = (row * w + x) * 4;
        image.data.set(frame.subarray(from, from + bw * 4), from);
      }
      context.putImageData(image, 0, 0, x, y, bw, bh);
    }
  }
  canvas.style.cursor = CURSORS[Atomics.load(control, CURSOR)] || 'default';
  requestAnimationFrame(paint);
}

const [width, height] = size();
canvas.style.width = width + 'px';
canvas.style.height = height + 'px';
say('Loading…');
// app.json (claywind-pack writes it) lists the app's fonts, in the order the program asks for them.
const app = await (await fetch('app.json')).json();
const fonts = await Promise.all(app.fonts.map(async (name) => {
  const response = await fetch('fonts/' + name);
  if (!response.ok) { say('Cannot load fonts/' + name); throw new Error('cannot load fonts/' + name); }
  return new Uint8Array(await response.arrayBuffer());
}));
const worker = new Worker('worker.js', { type: 'module' });
worker.onerror = (e) => say('The worker failed: ' + e.message);
worker.onmessage = (e) => {
  if (e.data === 'running') { say(''); access.setAttribute('aria-label', document.title); canvas.focus(); }
  else if (e.data.access !== undefined) mirror(e.data.access);
  // A copy the page did not ask for (a menu's, a button's): the browser may refuse it without a recent key or click.
  else if (e.data.copied !== undefined) { if (e.data.copied !== taken) navigator.clipboard.writeText(copyText()).catch(() => {}); }
  else say(String(e.data));
};
worker.postMessage({ control, events, frame, pasted, copied, fonts, width, height, ratio: devicePixelRatio, apple });
requestAnimationFrame(paint);
