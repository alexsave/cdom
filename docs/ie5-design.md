# IE 5.5 and IE 5.0: the fourth tier, further down

Alex asked on 2026-11-20 to descend from IE 6 (docs/ie6-design.md) taking the cheap wins, and to pause once IE 5.0 works, saying what it took.
This is the state of that work, written for the agent that picks it up.

## What JScript 5.0 lacks that the tier's files use

JScript 5.5 (IE 5.5) added, and JScript 5.0 (IE 5.0 and 5.01) lacks: the global `undefined`, Array's push, pop, shift, unshift and splice, Function's call and apply, Object's hasOwnProperty and propertyIsEnumerable, encodeURIComponent and the rest, toFixed, and `replace` with a function as the replacement.
A token scan of cdom.legacy-ie6.js and legacy.wasm.ie9.js found push, pop, shift, splice, call, apply, hasOwnProperty, propertyIsEnumerable and 25 uses of `undefined`.

## Done on this branch (not yet tested in a real IE 5)

- tools/legacy/polyfills-es3.js supplies them first, each written without the others: `undefined` as a window property, push, pop, splice (the standard's), shift, unshift, apply (the function set on the object under a key of its own and called through eval with the arguments by index), call (through apply), hasOwnProperty (an approximation: has the key and the constructor's prototype does not hold the same value), and the DontEnum test by enumerating instead of propertyIsEnumerable.
- shim/boot.js runs before any polyfill, so it names none of them: `lack[lack.length] =` instead of push, `null` instead of `undefined`.
- tools/legacy/dom-old.js's camel() is a loop, not replace with a function.
- tools/legacy/es3.mjs: the JScript 5.5 names are in the ES5 scan (the supplied ones allowed), boot50() refuses any of them in the boot (build.mjs page() calls it), and es3() refuses replace with a function expression.
- tests/build.sh passes with all of it.

## Not done yet

- A Chrome profile, LEGACY=ie5 in tools/cdp.mjs: LEGACY=ie6's plus Array's push, pop, shift, unshift and splice, Function's call and apply (use Reflect in the profile's own code), Object.prototype.hasOwnProperty and propertyIsEnumerable deleted, and replace given a function returning the function's text; then tools/legacy.mjs passing on it, CI, and the polyfills checked against the built-ins in legacy.mjs.
- tools/es3.mjs cases for boot50, the JScript 5.5 names and replace with a function, and mutants for each (tools/mutate.py).
- Open question: whether JScript 5.0 has the binary `in` operator; the array polyfills and hasOwnProperty use it. Test it in IE 5.01 first (`javascript:alert("a" in {a:1})`).

## Testing in real IE 5

- The XP VM (/Users/alex/VMs/ie6-xp: run.sh, `FULL=1 ./run.sh` for full screen, vm.py for screenshots, keys and taps through QMP's tablet) has the Utilu IE Collection installed (archive.org item iecollection1721, sha1 4ad24d25a7ffd1c1daf36e46399cf7e02554269f): `"C:\Program Files\Utilu IE Collection\IE550\iexplore.exe" <url>` and `...\IE501\iexplore.exe`.
- Serve the worktree's build: `python3 /Users/alex/VMs/ie9-win7/serve.py /Users/alex/Dev/cdom-ie5/tests/out 8099`; the guest reaches it at http://10.0.2.2:8099/.
- Progress that survives a browser crash: a synchronous request to /mark/<step> (ActiveXObject("Microsoft.XMLHTTP") on IE 5) shows in the server's log.

## What IE 5 has shown so far

- The standalone IE 5.5 crashes outright on any broken image, even on a page with no script (tests/out/img0.html). It is the build, not cdom. For IE 5.5 runs, a real 1x1 GIF stands in at tests/out/no-such-image.png (the legacy page's deliberately broken image).
- In IE 5.5, a page with the tier's two files as static scripts that calls cdom.start itself starts the app and draws it (tests/out/step2.html), before the broken-image crash.
- But tests/out/legacy.html, which loads them through the boot (script elements added at run time), crashes IE 5.5 and IE 5.01 natively, after both files are fetched and before the app asks for its image. It still crashes with the inline modern shim taken out (nomodern.html) and with the boot not clearing onreadystatechange in its own handler and starting from a timeout instead (nomodern2.html).
  Marked runs got as far as the app's factory and its instance, then crashed in or after Promise.resolve(mount) (parsed(), shim/cdom.js), on the boot path only.
  Next suspects: the promise polyfill's scheduling when entered from a script element's readystatechange, the order the two scripts run in, and running the factory inside that handler. Compare step2.html (works) with legacy.html (crashes) one difference at a time.
