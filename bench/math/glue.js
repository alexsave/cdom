// The math bench's glue (docs/math-design.md): the browser arms' imports and the timed rounds, one file for the
// page (page.html loads it as a script) and for node (run.mjs and test.mjs load it), so every host times the same way.
//   mathBench.imports(arm, memory)  the "math" imports of the browser's arms: js1 passes Math.sin and the rest as the
//       imports themselves (a function of the C's arity: exp2 is 2 ** x and fmod is x % y, which have no Math function);
//       js1w wraps each in an arrow function, as a shim that adapts an argument would; the float imports round with
//       Math.fround. jsn's import fills a Float64Array (Float32Array) view of C memory with Math.sin of another view,
//       one call per 4096 values (each a loop of its own: batch below). memory() returns the module's memory once it
//       is instantiated.
//   mathBench.setup(mods, ms)  mods: [{ arm, bytes }]; instantiates each and warms every cell (arm, entry, domain) up,
//       setting its reps so a run takes about ms. Returns the entries' names and domains.
//   mathBench.round(r)  one round: every (entry, domain) once per arm, the arms in an order that rotates each round;
//       returns rows [round, entry, domain, arm, ns per call, reps].
//   mathBench.sums()  each arm's sum of every result it returned (printed at the end, so no loop is dead code).
(function (root) {
  'use strict';
  const TWO = { atan2: 1, pow: 1, hypot: 1, fmod: 1 };
  const MATH = {
    sqrt: Math.sqrt, sin: Math.sin, cos: Math.cos, tan: Math.tan, asin: Math.asin, acos: Math.acos, atan: Math.atan,
    atan2: Math.atan2, exp: Math.exp, exp2: (x) => 2 ** x, expm1: Math.expm1, log: Math.log, log2: Math.log2,
    log10: Math.log10, log1p: Math.log1p, pow: Math.pow, sinh: Math.sinh, cosh: Math.cosh, tanh: Math.tanh,
    cbrt: Math.cbrt, hypot: Math.hypot, fmod: (x, y) => x % y,
  };
  const DOMS = [[1, 'small'], [2, 'typical'], [4, 'large']];
  const N = 4096;

  // A batched import: a function of its own per name (new Function), so V8's feedback for its one call site stays
  // monomorphic and its optimizer inlines Math.sin and the rest, as it would in an app's own loop.
  const EXPR = {
    exp2: (x) => `2 ** ${x}`, fmod: (x, y) => `${x} % ${y}`,
  };
  function expr(name, x, y) { return EXPR[name] ? EXPR[name](x, y) : TWO[name] ? `Math.${name}(${x}, ${y})` : `Math.${name}(${x})`; }
  function batch(name, Arr, fround) {
    const e = expr(name, 'x[i]', 'y[i]');
    const loop = new Function('x', 'y', 'o', 'n', `for (let i = 0; i < n; i++) o[i] = ${fround ? `Math.fround(${e})` : e};`);
    let buf = null, views = new Map();
    return function (a, b, out, n, memory) {
      if (memory.buffer !== buf) { buf = memory.buffer; views = new Map(); }
      const v = (p) => { let w = views.get(p); if (!w) { w = new Arr(buf, p, n); views.set(p, w); } return w; };
      loop(v(a), TWO[name] ? v(b) : null, v(out), n);
    };
  }
  function imports(arm, memory) {
    const m = {};
    for (const name of Object.keys(MATH)) {
      const f = MATH[name], two = !!TWO[name];
      m[name] = arm === 'js1w' ? (two ? (x, y) => f(x, y) : (x) => f(x)) : f;
      m[name + 'f'] = two ? (x, y) => Math.fround(f(x, y)) : (x) => Math.fround(f(x));
      const bd = batch(name, Float64Array, false), bf = batch(name, Float32Array, true);
      m[name + '_n'] = (a, b, out, n) => bd(a, b, out, n, memory());
      m[name + 'f_n'] = (a, b, out, n) => bf(a, b, out, n, memory());
    }
    return { math: m };
  }
  // The C string at p in an instance's memory.
  function cstr(inst, p) {
    const m = new Uint8Array(inst.exports.memory.buffer);
    let s = '';
    for (let i = p; m[i]; i++) s += String.fromCharCode(m[i]);
    return s;
  }
  async function instantiate(arm, bytes) {
    let inst = null;
    const r = await WebAssembly.instantiate(bytes, imports(arm, () => inst.exports.memory));
    inst = r.instance;
    return inst;
  }

  const S = { arms: [], insts: {}, reps: {}, sums: {}, cells: [], names: [] };
  const now = () => performance.now();
  async function setup(mods, ms) {
    S.arms = mods.map((m) => m.arm);
    for (const m of mods) { S.insts[m.arm] = await instantiate(m.arm, m.bytes); S.sums[m.arm] = 0; }
    const x0 = S.insts[S.arms[0]].exports;
    for (let e = 0; e < x0.bm_n(); e++) S.names.push(cstr(S.insts[S.arms[0]], x0.bm_name(e)));
    S.cells = [];
    for (let e = 0; e < S.names.length; e++)
      for (const [d, dn] of DOMS)
        if (S.arms.some((a) => S.insts[a].exports.bm_doms(e) & d)) S.cells.push([e, d, dn]);
    for (const a of S.arms) {
      const x = S.insts[a].exports;
      for (const [e, d] of S.cells) {
        if (!(x.bm_doms(e) & d)) continue;
        x.bm_fill(e, d);
        let r = 1, t;
        for (;;) {
          const t0 = now();
          S.sums[a] += x.bm_run(e, r);
          t = now() - t0;
          if (t > ms / 4 || r > 1 << 20) break;
          r *= 2;
        }
        S.reps[`${a} ${e} ${d}`] = Math.max(1, Math.round((r * ms) / Math.max(t, 1e-3)));
      }
    }
    return { names: S.names, cells: S.cells.length };
  }
  function round(r) {
    const rows = [];
    for (const [e, d, dn] of S.cells)
      for (let k = 0; k < S.arms.length; k++) {
        const a = S.arms[(k + r) % S.arms.length], x = S.insts[a].exports;
        if (!(x.bm_doms(e) & d)) continue;
        const reps = S.reps[`${a} ${e} ${d}`];
        x.bm_fill(e, d);
        const t0 = now();
        S.sums[a] += x.bm_run(e, reps);
        const t = now() - t0;
        rows.push([r, S.names[e], dn, a, ((t * 1e6) / (reps * N)).toFixed(3), reps]);
      }
    return rows;
  }
  root.mathBench = { imports, instantiate, cstr, setup, round, sums: () => S.sums, MATH, TWO };
})(typeof window !== 'undefined' ? window : globalThis);
