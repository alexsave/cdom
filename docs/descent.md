# The descent below IE 5

## The ladder and Alex's calls

Alex's calls of 2026-11-20:

- Netscape 2 (1995) is the floor.
  cdom runs on every browser from there up.
- Below IE 5 a frames tier covers Netscape 2 to 4 and IE 3 and 4.
  Each page is a frameset, and script rewrites a child frame.
- Side effects are acceptable (the Back history, flicker), but avoid them where it costs nothing.
- A browser with no script gets, at build time, a screenshot of the app, Alex's message and a snowman.
  There is no server for it.
- Whether Netscape 2's code is a WebAssembly interpreter written in JavaScript 1.0 or a transform of wasm2js's output is decided by measurement, after the probe below.

The ladder, from the top:

| Tier | Browsers | Files |
|---|---|---|
| modern | anything with WebAssembly | cdom.js and the module |
| legacy | ES5 without WebAssembly | cdom.legacy.js and the wasm2js file |
| legacy-ie9 | no typed arrays | cdom.legacy-ie9.js |
| legacy-ie6 | IE 5.5 to 8 (docs/ie6-design.md, docs/ie5-design.md) | cdom.legacy-ie6.js |
| frames | Netscape 2 to 4, IE 3, IE 4 | to be designed (branch frames-tier) |
| no script | any | a build-time screenshot, a message and a snowman |

## The probe

tests/probe-js1.html measures what each old engine has, one test per script block, so a syntax error or a run-time error kills only its own block.
It runs from the page's load: each block writes a 1x1 image, /mark/\<tag\>/p-\<n\>-\<name\>-\<escaped value\>, before the test (`start`), with its value, and after it (`ok`).
tools/probe_marks.py reads the serve.py log and prints the table.
tests/probe-frames.html checks that a script can rewrite a child frame (`parent.f.document.open`, `write`, `close`), and whether the Back history grows.
tools/gen_probe_js1.py writes probe-js1.html.

### What I expect, written before the first run

Netscape 2.02 is JavaScript 1.0, which I expect lacks:

- `typeof`, `void`, and every literal: `{a:1}`, `[1,2]`, `/re/`.
- Function expressions and `new Function`.
- `new Array` as an object (JS 1.0 arrays are plain objects with no length); the Array methods.
- `switch`, `do`-`while`, labels, `===`, `in` as an operator, `delete`, `try`/`catch`/`throw`, `with` probably present.
- `charCodeAt` and `String.fromCharCode` (JavaScript 1.2), `split` maybe present, `replace` absent, `escape` and `unescape` present.
- `setInterval`, `window.onerror`, `document.images`, `new Image`.
- Present: `for`-`in`, `arguments`, `this`, `new` with a function constructor, `Math.floor`, `parseInt` with a radix, the bitwise operators, `eval`, `setTimeout` with a string, forms and their elements.
- Number precision: doubles, so 2^53+1 rounds; `>>>` should work.
- Recursion: I expect depth 100 to work and 1000 to be doubtful; 5000 to fail.

Netscape 3 adds `typeof`, `void`, `new Function`, `Array` and `Image`, `document.images`, and `onerror`.
Netscape 4 adds literals, function expressions, switch, do-while, labels, regular expressions, `charCodeAt`, `fromCharCode`, `delete`, and `try` stays out until 4.5/JS 1.4.
IE 3 (JScript 1.0) I expect to lack arrays' methods, `new Image` and `document.images`.

Speed: the loops should run in the tens of milliseconds on this Mac's emulated Pentium: an interpreter's overhead decides the matter, not these engines' absolute speed.
