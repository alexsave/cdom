// The Node side of tests/lits/run.sh, test glue only.
//   node stub.mjs run mod.wasm mod.lits fn...   instantiate a stripped module with stub imports, run the
//                                               exported functions in order, print what reached each import
//   node stub.mjs merge in.wasm out.wasm        write the module with every data segment merged into one,
//                                               as a tool that repacked the module would
// The string lookup is the shim's str() of docs/strings-design.md, cut to what these tests use.
import fs from "node:fs";
import vm from "node:vm";

// A string as the log shows it: printable ASCII as it is, a backslash, a control character, DEL and the
// two line separators as \uXXXX, so a line of the log is one line and says exactly what the string holds.
const esc = (s) => Array.from(s, (c) => {
    const n = c.codePointAt(0);
    if (n >= 32 && n < 127 && n !== 92) return c;
    if (n > 127 && n !== 0x2028 && n !== 0x2029) return c;
    return String.fromCharCode(92) + "u" + n.toString(16).padStart(4, "0");
}).join("");

async function run(wasmPath, litsPath, fns) {
    const table = vm.runInNewContext(fs.readFileSync(litsPath, "utf8") + ";cdomLits");
    if (table[2] !== "☃") throw new Error("the table's canary is wrong");
    const lits = table[3].split("\x01");
    const base = table[0];
    let mem, exp;
    const dec = new TextDecoder();
    const str = (s) => {
        if (s > 0) return lits[(s - base) >> 2];
        const at = exp.stub_recs() + (s & 63) * 8;
        const u32 = new Uint32Array(mem.buffer, at, 2);
        return dec.decode(new Uint8Array(mem.buffer, u32[0], u32[1]));
    };
    const log = [];
    const imports = { env: {
        open: (t) => log.push("open:" + esc(str(t))),
        text: (s) => log.push("text:" + esc(str(s))),
        attr: (n, v) => log.push("attr:" + esc(str(n)) + "=" + esc(str(v))),
    } };
    const { instance } = await WebAssembly.instantiate(fs.readFileSync(wasmPath), imports);
    exp = instance.exports;
    mem = exp.memory;
    if (exp.stub_stamp && exp.stub_stamp() >>> 0 !== table[1]) throw new Error("the stamp in the module is not the table's");
    for (const f of fns) {
        if (typeof exp[f] !== "function") throw new Error("no export " + f);
        exp[f]();
    }
    console.log(log.join("\n"));
}

function merge(inPath, outPath) {
    const w = fs.readFileSync(inPath);
    const uleb = (b, i) => { let r = 0, s = 0, c; do { c = b[i++]; r += (c & 127) * 2 ** s; s += 7; } while (c & 128); return [r, i]; };
    const sleb = (b, i) => { let r = 0, s = 0, c; do { c = b[i++]; r |= (c & 127) << s; s += 7; } while (c & 128); if (c & 64) r |= -1 << s; return [r, i]; };
    const enc = (n) => { const o = []; do { let c = n & 127; n = Math.floor(n / 128); o.push(n ? c | 128 : c); } while (n); return o; };
    const out = [...w.subarray(0, 8)];
    let i = 8;
    while (i < w.length) {
        const id = w[i];
        const [n, j] = uleb(w, i + 1);
        let body = [...w.subarray(j, j + n)];
        if (id === 11) {
            let [cnt, k] = uleb(w, j);
            const segs = [];
            for (let s = 0; s < cnt; s++) {
                k++; // flag 0
                let off; [off, k] = sleb(w, k + 1); k++; // i32.const, end
                let len; [len, k] = uleb(w, k);
                segs.push([off, w.subarray(k, k + len)]);
                k += len;
            }
            const lo = Math.min(...segs.map((s) => s[0])), hi = Math.max(...segs.map((s) => s[0] + s[1].length));
            const flat = new Uint8Array(hi - lo);
            for (const [off, d] of segs) flat.set(d, off - lo);
            const so = []; let v = lo;
            for (;;) { const c = v & 127; v >>= 7; if ((v === 0 && !(c & 64)) || (v === -1 && (c & 64))) { so.push(c); break; } so.push(c | 128); }
            body = [1, 0, 0x41, ...so, 0x0b, ...enc(flat.length), ...flat];
        } else if (id === 12) body = [1];
        out.push(id, ...enc(body.length), ...body);
        i = j + n;
    }
    fs.writeFileSync(outPath, Buffer.from(out));
}

const [cmd, ...args] = process.argv.slice(2);
if (cmd === "run") await run(args[0], args[1], args.slice(2));
else if (cmd === "merge") merge(args[0], args[1]);
else { console.error("usage: stub.mjs run|merge ..."); process.exit(2); }
