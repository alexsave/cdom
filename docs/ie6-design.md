# IE 6: the fourth tier

Alex asked for IE 6 support on 2026-11-20, with the design choices left to me.
This is the design, written before the code, with the predictions the real IE 6 run is checked against.

## 1. Where the tier sits

shim/boot.js picks one of three tiers today:

1. WebAssembly.
2. No WebAssembly, typed arrays (IE 10 and 11): cdom.legacy.js and app.wasm.js.
3. No typed arrays (IE 9): cdom.legacy-ie9.js and app.wasm.ie9.js, C's memory in a plain Array of words.

Below ES5 the boot throws.
The fourth tier takes that place: a browser with ES3 and the old DOM, which is IE 6, 7 and 8 (and IE 9 to 11 in their old document modes).
It loads cdom.legacy-ie6.js and the same app.wasm.ie9.js the third tier loads.

The boot's test is the one it already makes: any ES5 piece missing, or no addEventListener, means the fourth tier.
The new floor is what the fourth tier itself needs: document.getElementById, and attachEvent or addEventListener.
Below that floor the boot still throws one error naming what is missing.

## 2. One app file for the last two tiers

The words build of the app (wasm2js, then tools/legacy/memory.mjs) has no ES5 syntax and calls no ES5 method.
It is already ES3 to acorn, except for what JScript reads differently from the standard: a reserved word as a property name (`imports.cdom.class`) is a syntax error in JScript 5.6 to 5.8.
Terser's ie8 option quotes those, so the words build is minified with ie8 on, and checked as ES3 with no reserved word as a bare property name.
Then IE 6 to 9 share one app file, and an app ships no third build.

## 3. The shim: the same source, three changes

cdom.legacy-ie6.js is shim/cdom.js again, as the third tier builds it (Babel, memory-words.js in place of the typed-array functions), with three changes:

1. Babel targets ie 6, which adds the ES3 transforms (reserved words, property and member literals), and terser runs with ie8.
2. The DOM calls the old DOM lacks are rewritten to calls of tools/legacy/dom-old.js (section 4), by a Babel pass with one table of names.
   The build then refuses the file if any name of that table is left in the shim, as it refuses ES2015 names today.
3. PEP is left out (it needs addEventListener, defineProperty and Map); dom-old.js makes the pointer events from mouse events itself.

The polyfills gain a first file, tools/legacy/polyfills-es3.js: the ES5 built-ins the shim, Babel's helpers and promise-polyfill call, written for an engine with no defineProperty, plus a fix for JScript's splice(start), which removes nothing when the count is left out.
The build scans the ES3 output for ES5 names (Object.keys, Array.prototype.indexOf, Function.prototype.bind and the rest) and refuses any that polyfills-es3.js does not supply.
polyfills.js keeps its job, with each defineProperty it makes guarded, since IE 6 and 7 have none.

## 4. dom-old.js: the old DOM in one place

Each helper uses the W3C call when the object has it, and the old one otherwise, so the tier also runs on any ES3 browser with the W3C DOM.

| The shim says | dom-old.js does on IE 6 to 8 |
| --- | --- |
| `x.addEventListener(type, f, options)` | `x.attachEvent("on" + type, g)`, g passing f an event object made W3C-shaped (below); the type mapped (pointer to mouse, wheel to mousewheel, focus and blur to focusin and focusout, DOMContentLoaded to the window's load) |
| `x.removeEventListener(type, f)` | detachEvent of the g made for f |
| `x.style.setProperty(name, value)` | `style[camelName] = value` in a try (IE 6 throws on a value it cannot parse, where other browsers ignore it); float as styleFloat; opacity also as an alpha filter; a custom property (`--x`) is dropped |
| `x.setAttribute(name, value)` | class, for, style and the camel-case attributes (colspan, tabindex, readonly, maxlength and the like) set as their properties, as IE 6 and 7 need |
| `x.textContent = ""` | its children removed |
| `x.remove()` | `x.parentNode.removeChild(x)` |
| `x.previousElementSibling` | previousSibling, skipping all but elements |
| `doc.createElementNS(svg, tag)` | createElement(tag): IE 6 to 8 have no SVG, so an SVG draws nothing there |
| `doc.elementsFromPoint(x, y)` | elementFromPoint, then its ancestors |
| `x.getBoundingClientRect()` | the same box, with width and height from its edges (IE 6 to 8 give no width or height) |
| `innerWidth`, `innerHeight`, `devicePixelRatio` | documentElement's clientWidth and clientHeight; screen.deviceXDPI over logicalXDPI |

The event object handed to the shim has what post() and the drop read: target (srcElement), clientX and clientY, button (IE's 1, 4, 2 as 0, 1, 2), buttons, pointerId 1 for the pointer types, keyCode, the modifier keys, timeStamp, deltaY from wheelDelta, and preventDefault and stopPropagation as returnValue and cancelBubble.
IE 6 to 8 do not bubble change, submit or input (IE has no input event).
For those, the listener on the mount is made on each field when it gets focus (focusin bubbles in IE): change and submit as they are, input as propertychange on value.
Events that do not bubble in IE and are not focus or blur (scroll, load and error inside the mount, the media events) are attached to the node itself on this tier: when C listens for one of them on a node, dom-old.js attaches a listener to that node that hands the event to the mount's listener, as if it had bubbled there (Alex's call, 2026-11-20).
Listeners on the window and the document are direct already.

## 5. The boot loads scripts the old way

IE 6 to 8 fire no load event on a script element; they fire readystatechange, with readyState loaded or complete.
The boot's load() takes either, once.

## 6. What the tier does not do

No canvas and no SVG, for now: an SVG is made of plain elements and draws nothing, and the canvas calls (cd_blit, cd_image_bytes, encode) do nothing and report once why, so the rest of the app runs (Alex's call, 2026-11-20).
The follow-up Alex asked for is to draw them after all, through VML or as an image baked into the page; it is not part of this change.
Also no WebSocket, IndexedDB, audio, file reads (UNSUPPORTED, as on IE 9) or jobs (UNSUPPORTED).
cd_http works (section 6c).
No History API: cd_url_go answers 0.
cd_seed is the IE 9 exception's weak seed.
A long run of C in one task can raise IE's "a script on this page is causing Internet Explorer to run slowly" prompt, which counts 5,000,000 statements; nothing in cdom can raise that limit for a user.
Alex's call (2026-11-20): measure first (P2), and only if the prompt shows, design a way for C to split long work across tasks.

## 6b. Float to integer

clang emits the non-trapping conversions (i32 and i64 trunc_sat from f32 and f64) for every C cast of a float to an integer, because nontrapping-fptoint is among its default wasm32 features.
wasm2js takes the i32 ones, but its i64 lowering fails on the four i64 ones ("local.set's value type must be correct", then "error in validating wasm2js output"), so an app with `(uint64_t)(v * (double)scale + 0.5)` could not be built for any legacy tier.
The fix is in the legacy build (tools/legacy/build.mjs, wasm2js()), so no app needs a flag.
When a module has an i64 trunc_sat, the build reads it as text, writes each as the trapping i64.trunc_f32_s, f32_u, f64_s or f64_u, and gives that module to wasm2js; a module with none goes to wasm2js as it is, and the first tier's .wasm is never written.
Why not the alternatives:

- binaryen's --llvm-nontrapping-fptoint-lowering builds, but is wrong in range: it takes INT32_MAX and UINT32_MAX themselves for out of range (u32(4294967295) is 0, i32(2147483647) is INT32_MIN), and gives the minimum or 0 for everything above.
  tools/fptoint.mjs catches that (a mutant runs it).
- -mno-nontrapping-fptoint in cdom.mk would give the same instructions as the rewrite, but needs a second compile of every app for the legacy twin, or the flag on the first tier, where a cast out of range would then trap instead of saturating.

In range (the integer part fits the type) every tier gives the same answer as WebAssembly: tools/fptoint.mjs checks 44 values to i32, u32, i64 and u64, including INT32_MAX, UINT32_MAX, 2^53 + 1, 2^63 - 1024 and 2^64 - 2048, a fixed-point round and the f32 forms, on LEGACY= '', 1, ie9 and ie6.
Out of range a C cast is undefined, and the tiers differ:

| | WebAssembly (tier 1) | tiers 2 to 4 |
| --- | --- | --- |
| i32, u32 | saturates; NaN is 0 | wraps modulo 2^32 (JavaScript's ToInt32 and ToUint32), so u32(-1) is 4294967295 and i32(2^31) is INT32_MIN; NaN and the infinities are 0 |
| i64, u64 | saturates; NaN is 0 | whatever wasm2js's i64 conversion leaves (it does not trap, and does not saturate): do not rely on it |

An app that needs the saturating answer clamps before it casts.

## 6c. The network

The shim's cd_http (src/cdom_net.c) calls fetch, which IE 5 to 11 have none of.
The fallback gives the tiers one, in tools/legacy/polyfills.js, so shim/cdom.js and the first tier's files are unchanged: fetch(url, { method, headers, body, signal }) answering { status, arrayBuffer() }, and AbortController for cd_http's deadline.
It is installed only where fetch is missing.
The request is an XMLHttpRequest, or, on IE 5 and 6, which have none, new ActiveXObject("Microsoft.XMLHTTP").
Answers are cdom_net.h's: the status and the body for any HTTP status (404 and its body too); CD_HTTP_FAILED (0) when nothing arrived (a dropped connection, a refused request, a status of 0 or one of WinInet's 12000s, which IE reports as the status; 1223 is 204); CD_HTTP_TIMEOUT (1) when deadline_ms passed first and the request was aborted.
Each is answered once, from the event loop.
tools/legacy.mjs runs these on every level (tests/legacy.c net_ask): a text GET beyond ASCII, a binary GET of all 256 byte values, a 404 with its body, a POST with a header, a dropped request, a deadline, and a POST whose body is not UTF-8.

How a body comes back as bytes depends on what the browser lets a script read:

| Tier and browser | Response bytes | Request body |
| --- | --- | --- |
| 1 (WebAssembly) | fetch | fetch |
| 2 (IE 10, 11) | XMLHttpRequest, responseType arraybuffer: exact | the bytes, exact |
| 3, 4 with overrideMimeType (a browser that has it, Firefox 1.0 on; IE has none) | overrideMimeType("text/plain; charset=x-user-defined") and charCodeAt & 0xff: exact whatever the server labels it | UTF-8 text only (below) |
| 3, 4 on IE 5 to 9 (no typed arrays, no responseType, no overrideMimeType) | responseText only, no VBScript: exact for ASCII; exact for any bytes when the server labels the body charset=windows-1252 or iso-8859-1 (Windows maps all 256 to a character, mapped back); for any other label the text is encoded as UTF-8, which is exact for UTF-8 text labelled utf-8, and not for a binary body without that label | UTF-8 text only (below) |

A request body on tiers 3 and 4 is sent as a string, which XMLHttpRequest encodes as UTF-8, so a body that is UTF-8 goes as it is and any other (a 0xff byte) answers CD_HTTP_FAILED, never other bytes.
IE 8 and 9 reach the page's own origin only: their cross-origin object is XDomainRequest, which is not used.
IE 10 and 11 reach any origin the server allows by CORS, as XMLHttpRequest does.
No VBScript means no responseBody, so a binary download from IE 5 to 9 needs the server's charset label; a server that cannot send one can send the bytes as base64 text.
cdp.mjs emulates all this: no fetch, AbortController, overrideMimeType (a page with ?mime in its URL keeps it) or, from IE 9 down, responseType; on LEGACY=ie6 no XMLHttpRequest either (?xhr keeps it, as IE 7 and 8 have), and a stand-in ActiveXObject over the real request with an ActiveX one's surface (its status throws where the request failed).
String.prototype.trim, which the shim reads headers with, is polyfilled too: the third tier lacked it.

## 6a. Decisions

Alex answered four questions on 2026-11-20:

1. The tier is for IE 6, 7 and 8 alike, not IE 6 alone: IE 8 is no longer refused.
2. Non-bubbling events inside the mount are attached to their node, not left out (section 4).
3. The slow-script prompt: measure, then decide (section 6).
4. SVG and canvas draw nothing for now; VML or a baked image later (section 6).

## 7. Tests

- tools/legacy.mjs under LEGACY=ie6: Chrome made into an ES3 browser with the old DOM (tools/cdp.mjs deletes the ES5 built-ins, addEventListener, textContent, previousElementSibling, style.setProperty and the rest, and gives attachEvent, window.event and srcElement), the same checks as the other tiers, plus the tier's own: the boot took cdom.legacy-ie6.js and app.wasm.ie9.js, the shim made no W3C call the page lacks.
- LEGACY=below: the new floor, no getElementById or no listener API at all, refused with one error.
- The build's gates (ES3 parse, reserved words, the ES5 names, the old-DOM names) each mutation-checked.
- The real check: Windows XP with IE 6 (the modern.ie image, under QEMU), and the IE 9 VM in its IE 7 and IE 8 document modes, running tests/out/legacy.html and the keyed benchmark's page.

## 8. Predictions

Written before any of it is built or run.

- P1: in real IE 6, tests/out/legacy.html boots with no script error, and pressing, dragging and releasing on the pad reaches C as pointerdown, pointermove and pointerup.
  Falsified by any script error dialog or a missing event.
- P2: the keyed benchmark's page in IE 6 creates 1,000 rows, swaps and deletes them, with no slow-script prompt.
  Falsified by the prompt (more than 5,000,000 statements in one task).
- P3: cdom.legacy-ie6.js is 40 to 52 KB minified: the third tier's file is 52 KB with PEP, PEP is about 17 KB minified, and dom-old.js and polyfills-es3.js add 5 to 10 KB.
  Falsified outside that range.
- P4: app.wasm.ie9.js changes by less than 1% in size with terser's ie8 option.
- P5: the IE 9 VM in IE 8 and IE 7 document modes takes the fourth tier and runs the same page.

## 9. What real IE 6 found

IE 6.0.2900.5512 (Windows XP SP3, the modern.ie image, under QEMU on this Mac: /Users/alex/VMs/ie6-xp, run.sh and vm.py).
The Chrome profile (LEGACY=ie6) passed before IE 6 ran anything; IE 6 still found five things it could not see.
Each is fixed, and each that Chrome can imitate is now in the profile, so CI keeps it fixed.

1. JScript's catch has no scope of its own: `catch (e)` writes the enclosing function's `e`.
   Babel's `_regeneratorDefine2(e, ...)` probes defineProperty in a `catch (e)`, which throws in IE 6, so the generator's methods went onto the error and every async function failed (cdom.start among them, silently).
   Fix: every catch parameter in the tier's files is renamed apart (es3.mjs, catchScoped), and the minified file is refused if a catch parameter's name is used elsewhere in its function (catchLeaks).
   Chrome cannot imitate the scope, so tools/es3.mjs checks the gate on planted code.
2. A failed start rejected a promise nobody watched.
   Fix: the boot throws the rejection again from a timeout, on every tier, so it reaches window.onerror (tested with ?nolits).
3. IE 6 to 8's DOM methods are host objects with no apply or call, and Babel compiles `object[key](...args)` to apply.
   Fix: the shim calls through invoke(), which shim-old.js replaces with a call by argument count; the Chrome profile strips apply and call from the DOM's methods.
4. JScript's `splice(start)` removes nothing, which would lose every event queued during a call (drain).
   Fix: polyfills-es3.js detects it and patches splice; the Chrome profile has JScript's splice.
5. IE 6 says interactive before the parser reaches the end, so a mount after the scripts was not there yet.
   Fix: the shim also waits while the mount is missing and the page is not complete.
   Not imitated in Chrome; checked in IE 6 on the keyed benchmark's page, whose mount comes after its scripts.

6. Firefox 1.0 (asked for by Alex on 2026-11-20, run in the same XP VM) crashed outright, a native crash of the browser, as soon as the app started.
   Babel's generator runtime, which async functions compile to, sets a function's `__proto__` where there is no Object.setPrototypeOf, and Firefox 1.0's engine crashes calling such a function (found by marking each step of the runtime with a synchronous request to the host, which a crash cannot lose).
   Fix: the shim has no async function left (start, the store's two requests and encode are promise chains), so no legacy file carries the runtime; each is 4.3 KB smaller for it, and the tier's build refuses `__proto__`.
   The inlined shim (cdom.min.js) grew 311 bytes for it, 55 with brotli.

Firefox 1.0 then runs the fourth tier on its W3C branch of dom-old.js: addEventListener, with pointer events made from mouse events (Firefox 1.0 has none) and the held buttons tracked by the document's own listeners.
On tests/out/legacy.html it passed the checks IE 6 did: pointer down, moves with the button held, up with none, click; typing echoed by C; a failed call reported once with the op after it applied; cd_call(focus) heard as a focus event.

Also learned: IE 6 caches the tier's two scripts across reloads even with Cache-Control: no-store, so a test in the VM sets IE to check every visit first (HKCU ... Internet Settings, SyncMode5 = 3).

## 10. The predictions, measured

- P1 held: tests/out/legacy.html boots with no script error; a press, drag and release on the pad reach C as pointerdown (buttons 1), pointermove (buttons 1) and pointerup (buttons 0), then click; typing reaches C (propertychange as input) and C's echo shows it; a failed call is reported once and the next op applies; cd_call(focus) focuses the input and C hears its focus event.
- P2 held: the keyed benchmark's page in IE 6 creates 1,000 rows in 11.5 s, updates every 10th row in 1.0 s, swaps rows in 0.25 s and clears in 1.9 s, with no slow-script prompt.
  These times are an emulated Pentium III under QEMU's TCG on arm64, so they say nothing about IE 6 on its own hardware.
  10,000 rows in one task does raise the prompt; answered No (go on), it finished in 152.8 s with all 10,000 rows (Alex's call: measured first; splitting long work across tasks is the follow-up if it matters).
- Re-run on the final build (after finding 6), IE 6 passed all of P1 again, and the broken image's error, which does not bubble, reached C through the image's own listener.
- P3 held: cdom.legacy-ie6.js is 47,604 bytes minified (15,361 brotli -q 11, 17,137 gzip -9), against 57,376 for the third tier's (which grew from the 52 KB the prediction assumed).
  Without the generator runtime (finding 6) it is 43,317 bytes (14,062 brotli), below the predicted range; the third tier's is 53,101 (16,336) and the second's 53,797 (16,427).
- P4 held: terser's ie8 adds 0.19% to 0.75% to the words build of the app (listfuzz 24,248 to 24,294 bytes, legacy 17,598 to 17,641, memforms 5,717 to 5,760).
- P5 held: IE 9 (the Windows 7 VM) in IE 7 and IE 8 document modes (an X-UA-Compatible meta on a copy of the legacy page) took the fourth tier, fetching cdom.legacy-ie6.js and legacy.wasm.ie9.js, and passed the same checks: the broken image's error, pointer down, moves and up with their buttons, click, typing echoed by C, focus.
  Its engine there is still JScript 9.0 (ScriptEngineMajorVersion), so these modes check the old DOM, not JScript 5's quirks; real IE 6 is what checked those (section 9).
