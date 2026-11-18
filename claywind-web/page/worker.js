// SPDX-License-Identifier: MIT
// The web host's worker: it runs the WebAssembly module's program, which never returns. What the program imports is
// here: waiting for the page's next frame or its next event, taking the events, showing what changed of a frame,
// the pointer's shape, the clipboard, a panic.

const EVENTS = 1024;
const TICK = 0, WIDTH = 2, HEIGHT = 3, CURSOR = 4, HEAD = 5, TAIL = 6, STATE = 7, WAKE = 8, BOXES = 9, PASTED = 10, COPIES = 11, COPIED = 12;
const BOX = 16, MAX_BOXES = 16;
const PASTE_BYTES = 1048576;

onmessage = async (message) => {
  const { control, events, frame, pasted, copied, fonts, width, height, ratio, apple } = message.data;
  let changed = [], placed = [], pasteAt = 0n, take;
  const putLength = (pointer, n) => new BigUint64Array(memory.buffer, Number(pointer), 1).set([BigInt(n)]);
  let memory, seen = Atomics.load(control, TICK), last = performance.now();
  const bytes = () => new Uint8Array(memory.buffer);
  const text = (pointer) => { const m = bytes(); let end = Number(pointer); while (m[end]) end++; return new TextDecoder().decode(m.subarray(Number(pointer), end)); };
  const env = {
    web_start(values) { new Float64Array(memory.buffer, Number(values), 4).set([width, height, ratio, apple ? 1 : 0]); },
    // Font `index`, where it was put in the module's memory as the worker started.
    web_font(index, length) {
      if (index >= placed.length) return 0n;
      putLength(length, placed[index][1]);
      return placed[index][0];
    },
    // Returns at the page's next frame, with the seconds since the last.
    web_wait() {
      Atomics.wait(control, TICK, seen);
      seen = Atomics.load(control, TICK);
      const now = performance.now(), seconds = (now - last) / 1000;
      last = now;
      return seconds;
    },
    // Sleeps until the page queues an event or `seconds` pass (no limit when negative).
    web_idle(seconds) {
      const woken = Atomics.load(control, WAKE);
      if (Atomics.load(control, HEAD) !== Atomics.load(control, TAIL)) return;
      if (seconds < 0) Atomics.wait(control, WAKE, woken);
      else Atomics.wait(control, WAKE, woken, seconds * 1000);
    },
    web_event(values) {
      const head = Atomics.load(control, HEAD);
      if (head === Atomics.load(control, TAIL)) return 0;
      const kind = events[head * 5];
      new Float64Array(memory.buffer, Number(values), 4).set(events.subarray(head * 5 + 1, head * 5 + 5));
      Atomics.store(control, HEAD, (head + 1) % EVENTS);
      return kind;
    },
    web_box(x, y, w, h) { changed.push(x, y, w, h); },
    // Copies the changed boxes' rows where the page reads them, then lists the boxes for it to paint.
    web_present(pixels, w, h) {
      const source = new Uint8Array(memory.buffer, Number(pixels), w * h * 4);
      const resized = Atomics.load(control, WIDTH) !== w || Atomics.load(control, HEIGHT) !== h;
      if (resized) changed = [0, 0, w, h];
      for (let k = 0; k < changed.length; k += 4) {
        const x = Math.min(changed[k], w), y = Math.min(changed[k + 1], h);
        const bw = Math.min(changed[k + 2], w - x), bh = Math.min(changed[k + 3], h - y);
        for (let row = y; row < y + bh; row++) {
          const from = (row * w + x) * 4;
          frame.set(source.subarray(from, from + bw * 4), from);
        }
      }
      Atomics.store(control, WIDTH, w);
      Atomics.store(control, HEIGHT, h);
      // The page takes the list by setting its count to 0; with no room left, or a new size, the whole frame is one box.
      let count = Atomics.load(control, BOXES);
      if (resized || count + changed.length / 4 > MAX_BOXES) { count = 0; changed = [0, 0, w, h]; }
      control.set(changed, BOX + count * 4);
      Atomics.store(control, BOXES, count + changed.length / 4);
      changed = [];
    },
    web_cursor(kind) { Atomics.store(control, CURSOR, kind); },
    // The last paste's text, copied into a block of the module's memory taken at the first paste.
    web_pasted(length) {
      if (pasteAt === 0n) pasteAt = take(BigInt(PASTE_BYTES));
      if (pasteAt === 0n) return 0n;
      const n = Math.min(Atomics.load(control, PASTED), PASTE_BYTES);
      bytes().set(pasted.subarray(0, n), Number(pasteAt));
      putLength(length, n);
      return pasteAt;
    },
    // A worker has no clipboard: the text goes where the page reads it, which is told.
    web_copy(text, length) {
      let n = Math.min(Number(length), copied.length);
      const from = bytes().subarray(Number(text), Number(text) + n);
      while (n > 0 && n < Number(length) && (from[n] & 0xC0) === 0x80) n--;   // not through the middle of a character
      copied.set(from.subarray(0, n));
      Atomics.store(control, COPIED, n);
      postMessage({ copied: Atomics.add(control, COPIES, 1) + 1 });
    },
    web_panic(kind, file, line, column) { throw new Error(`panic ${kind} at ${text(file)}:${line}:${column}`); },
  };
  try {
    const { instance } = await WebAssembly.instantiateStreaming(fetch('web.wasm'), { env });
    const e = instance.exports;
    memory = e.memory;
    take = e.web_take;
    placed = fonts.map((font) => { const p = take(BigInt(font.length)); bytes().set(font, Number(p)); return [p, font.length]; });
    postMessage('running');
    e.run();   // returns only when it could not start
    Atomics.store(control, STATE, -1);
  } catch (error) {
    Atomics.store(control, STATE, -2);
    console.error(error);
    postMessage('The program stopped: ' + error.message);
  }
};
