// Every way C reaches memory, for tools/words.mjs: the same exports run as wasm and as app.wasm.ie9.js (wasm2js
// output on a plain Array of words) must agree on every result and every byte. Loads and stores of each width,
// signed and unsigned, aligned and in a packed struct (which wasm2js splits into bytes), i64 halves, f32 and f64
// values and their bits, f32 arithmetic (Math.fround), memory.copy, memory.fill, memory.grow and a data segment.
typedef unsigned long long u64; typedef long long i64; typedef unsigned u32;
struct __attribute__((packed)) P { char c; int i; long long l; float f; double d; short s; };
__attribute__((export_name("ld"))) i64 ld(void *p, int k) {
  switch (k) {
  case 0: return *(signed char *)p; case 1: return *(unsigned char *)p;
  case 2: return *(short *)p; case 3: return *(unsigned short *)p;
  case 4: return *(int *)p; case 5: return *(u32 *)p; case 6: return *(i64 *)p;
  case 7: return ((struct P *)p)->i; case 8: return ((struct P *)p)->l; case 9: return ((struct P *)p)->s;
  }
  return 0;
}
__attribute__((export_name("st"))) void st(void *p, int k, i64 v) {
  switch (k) {
  case 0: *(signed char *)p = v; break; case 2: *(short *)p = v; break; case 4: *(int *)p = v; break;
  case 6: *(i64 *)p = v; break; case 7: ((struct P *)p)->i = v; break; case 8: ((struct P *)p)->l = v; break;
  case 9: ((struct P *)p)->s = v; break;
  }
}
__attribute__((export_name("ldf"))) double ldf(void *p, int k) {
  switch (k) { case 0: return *(float *)p; case 1: return *(double *)p;
  case 2: return ((struct P *)p)->f; case 3: return ((struct P *)p)->d; }
  return 0;
}
__attribute__((export_name("stf"))) void stf(void *p, int k, double v) {
  switch (k) { case 0: *(float *)p = v; break; case 1: *(double *)p = v; break;
  case 2: ((struct P *)p)->f = v; break; case 3: ((struct P *)p)->d = v; break; }
}
__attribute__((export_name("bits"))) u64 bits(double d) { return __builtin_bit_cast(u64, d); }
__attribute__((export_name("fbits"))) u32 fbits(float f) { return __builtin_bit_cast(u32, f); }
__attribute__((export_name("frombits"))) double frombits(u64 b) { return __builtin_bit_cast(double, b); }
__attribute__((export_name("ffrombits"))) float ffrombits(u32 b) { return __builtin_bit_cast(float, b); }
__attribute__((export_name("fadd"))) float fadd(float a, float b) { return a + b; }
__attribute__((export_name("cpy"))) void cpy(void *d, void *s, unsigned n) { __builtin_memmove(d, s, n); }
__attribute__((export_name("set"))) void set(void *d, int c, unsigned n) { __builtin_memset(d, c, n); }
__attribute__((export_name("grow"))) int grow(int n) { return __builtin_wasm_memory_grow(0, n); }
__attribute__((export_name("size"))) int size(void) { return __builtin_wasm_memory_size(0); }
static char data[] = "hello\0world\xff\x80";
__attribute__((export_name("dat"))) char *dat(void) { return data; }
__attribute__((export_name("fdiv"))) float fdiv(float a, float b) { return a / b; }
__attribute__((export_name("fmul"))) float fmul(float a, float b) { return a * b; }
__attribute__((export_name("tof"))) float tof(double d) { return (float)d; }
__attribute__((export_name("i2d"))) double i2d(i64 v) { return (double)v; }
// No double to i64: clang emits i64.trunc_sat_f64_s for it, and wasm2js 130 aborts on that ("invalid type").
