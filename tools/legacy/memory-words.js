// The shim's C-memory functions for a browser without typed arrays (IE 9): tools/legacy/build.mjs puts each in place
// of shim/cdom.js's function of the same name (between "C's memory" and its end) when it builds
// cdom.legacy-ie9.js, and fails if one has no such function there, or if a typed array is left in the shim outside
// seed and randomBytes (which run only where crypto.getRandomValues exists, and so typed arrays do).
// memory is app.wasm.ie9.js's (tools/legacy/memory.mjs): words, the one Array of 32-bit words the app keeps C's
// memory in (growing pushes onto it, so it never changes), and words.js's byte and float accessors over it.
// Bytes are plain Arrays of numbers here, which the polyfilled TextDecoder and TextEncoder read and write.
let words; // memory.words

function views() {
  words = memory.words;
}
function fresh() {}
function load8(ptr) {
  return memory.load8_u(ptr);
}
function load32(index) {
  return words[index] >>> 0;
}
function store32(index, value) {
  words[index] = value | 0;
}
function storeF32(index, value) {
  memory.store_f32(index, value);
}
function loadF64(index) {
  return memory.load_f64(index);
}
function storeF64(index, value) {
  memory.store_f64(index, value);
}
// With no views, every read of bytes is a copy.
function bytes(ptr, length) {
  const out = [];
  for (let i = 0; i < length; i++) {
    out.push(memory.load8_u(ptr + i));
  }
  return out;
}
function copyBytes(ptr, length) {
  return bytes(ptr, length);
}
function writeBytes(ptr, data, length) {
  for (let i = 0; i < length; i++) {
    memory.store8(ptr + i, data[i]);
  }
}
// What reaches deliver here: the polyfilled TextEncoder's Arrays (no browser without typed arrays makes an
// ArrayBuffer).
function bytesOf(data) {
  return data;
}
function isBytes(data) {
  return Array.isArray(data);
}
function encodeTo(text, ptr, capacity) {
  const out = new Array(capacity); // encodeInto writes into it up to its length, whole characters only
  const written = textEncoder.encodeInto(text, out).written;
  writeBytes(ptr, out, written);
  return written;
}
function randomBytes(ptr, length) {
  const random = new Uint8Array(length);
  crypto.getRandomValues(random);
  writeBytes(ptr, random, length);
}
// IE 9 has no ImageData constructor, but its canvas makes one.
function pixels(context, ptr, width, height, channels) {
  const image = context.createImageData(width, height);
  const data = image.data;
  for (let from = ptr, to = 0; to < width * height * 4; from += channels, to += 4) {
    data[to] = memory.load8_u(from);
    data[to + 1] = memory.load8_u(from + 1);
    data[to + 2] = memory.load8_u(from + 2);
    data[to + 3] = channels === 3 ? 255 : memory.load8_u(from + 3);
  }
  return image;
}
function newMask(length, old) {
  const mask = old || [];
  while (mask.length < length) {
    mask.push(0);
  }
  return mask;
}
