// SPDX-License-Identifier: MIT
// The web host's worker: it runs the WebAssembly module's program, which never returns. What the program imports is
// here: waiting for the page's next frame, taking the page's events, showing a frame, the pointer's shape, a panic.

const EVENTS = 1024;
const TICK = 0, SHOWN = 1, WIDTH = 2, HEIGHT = 3, CURSOR = 4, HEAD = 5, TAIL = 6, STATE = 7;

onmessage = async (message) => {
  const { control, events, frame, fonts, width, height } = message.data;
  let memory, seen = Atomics.load(control, TICK), last = performance.now();
  const bytes = () => new Uint8Array(memory.buffer);
  const text = (pointer) => { const m = bytes(); let end = Number(pointer); while (m[end]) end++; return new TextDecoder().decode(m.subarray(Number(pointer), end)); };
  const env = {
    // Returns at the page's next frame, with the seconds since the last.
    web_wait() {
      Atomics.wait(control, TICK, seen);
      seen = Atomics.load(control, TICK);
      const now = performance.now(), seconds = (now - last) / 1000;
      last = now;
      return seconds;
    },
    web_event(values) {
      const head = Atomics.load(control, HEAD);
      if (head === Atomics.load(control, TAIL)) return 0;
      const kind = events[head * 5];
      new Float64Array(memory.buffer, Number(values), 4).set(events.subarray(head * 5 + 1, head * 5 + 5));
      Atomics.store(control, HEAD, (head + 1) % EVENTS);
      return kind;
    },
    web_present(pixels, w, h) {
      frame.set(new Uint8Array(memory.buffer, Number(pixels), w * h * 4));
      Atomics.store(control, WIDTH, w);
      Atomics.store(control, HEIGHT, h);
      Atomics.add(control, SHOWN, 1);
    },
    web_cursor(kind) { Atomics.store(control, CURSOR, kind); },
    web_panic(kind, file, line, column) { throw new Error(`panic ${kind} at ${text(file)}:${line}:${column}`); },
  };
  try {
    const { instance } = await WebAssembly.instantiateStreaming(fetch('web.wasm'), { env });
    const e = instance.exports;
    memory = e.memory;
    const put = (font) => { const p = e.web_take(BigInt(font.length)); bytes().set(font, Number(p)); return [p, BigInt(font.length)]; };
    const [regular, bold, mono] = fonts.map(put);
    postMessage('running');
    e.run(...regular, ...bold, ...mono, width, height);   // returns only when it could not start
    Atomics.store(control, STATE, -1);
  } catch (error) {
    Atomics.store(control, STATE, -2);
    console.error(error);
    postMessage('The program stopped: ' + error.message);
  }
};
