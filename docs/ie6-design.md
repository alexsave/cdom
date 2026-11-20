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
Events that do not bubble in any IE and are not focus or blur (scroll, load and error inside the mount, the media events) are not delivered on this tier; listeners on the window and the document are direct and are delivered.

## 5. The boot loads scripts the old way

IE 6 to 8 fire no load event on a script element; they fire readystatechange, with readyState loaded or complete.
The boot's load() takes either, once.

## 6. What the tier does not do

No canvas (cd_blit, cd_image_bytes and encode throw on a missing getContext), no SVG, no WebSocket, fetch, IndexedDB, audio, file reads (UNSUPPORTED, as on IE 9) or jobs (UNSUPPORTED).
No History API: cd_url_go answers 0.
cd_seed is the IE 9 exception's weak seed.
A long run of C in one task can raise IE's "a script on this page is causing Internet Explorer to run slowly" prompt, which counts 5,000,000 statements; nothing in cdom can raise that limit for a user.

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
