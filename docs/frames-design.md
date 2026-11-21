# The frames tier: Netscape 2, 3 and 4, and IE 3 and 4

Alex set Netscape 2 (1995, the first browser with JavaScript) as cdom's floor.
This is the fifth legacy tier, for browsers that run script but cannot change the DOM after load.
It is a design and a prototype: Chrome runs it under LEGACY=frames (tools/cdp.mjs), and the real browsers are Alex's XP VM's to measure (section 12).
This document was written before the code, with the predictions in section 11; the results were added after measuring, and the predictions were not edited.

## 1. Where the tier sits

shim/boot.js picks one of four tiers today:

1. WebAssembly.
2. wasm2js on typed arrays (IE 10 and 11).
3. wasm2js on a words Array (IE 9).
4. ES3 on the old DOM (IE 5 to 8: docs/ie6-design.md, docs/ie5-design.md).

The frames tier is the fifth: script, but no getElementById or no DOM mutation.
That is Netscape 2, 3 and 4 and IE 3 and 4 (IE 4 has document.all and innerHTML, but no getElementById, and it is put here so that one tier covers the five).
Below it, a browser with no script at all gets the static fallback (another job, branch noscript-fallback).

## 2. What those browsers are assumed to do

None of this is measured yet; Alex's probe on the XP VM measures it, and each line is a row of that probe.

| # | Assumption | NS 2 | NS 3 | NS 4 | IE 3 | IE 4 |
| --- | --- | --- | --- | --- | --- | --- |
| A1 | document.write while the document loads | yes | yes | yes | yes | yes |
| A2 | From one frame, document.open, write and close of another frame's whole document | yes | yes | yes | yes | yes |
| A3 | document.open(type, "replace") reuses the history entry | no | yes | yes | ? | yes |
| A4 | Form fields whose value script reads and sets | yes | yes | yes | yes | yes |
| A5 | onClick on links and buttons, onChange on fields, onMouseOver on links | yes | yes | yes | yes | yes |
| A6 | onFocus and onBlur on fields | yes | yes | yes | yes | yes |
| A7 | javascript: URLs, and setTimeout with a string | yes | yes | yes | yes | yes |
| A8 | setTimeout with a function | no | no | yes | no | yes |
| A9 | Key events (onKeyDown, onKeyUp on fields) | no | no | yes | no | yes |
| A10 | innerHTML | no | no | no | no | yes |
| A11 | iframe | no | no | no | yes | yes |
| A12 | SCRIPT SRC | no | yes | yes | yes | yes |
| A13 | A FRAMESET written by a script in the BODY, before any content, replaces the body | ? | ? | ? | ? | ? |
| A14 | The top window keeps its script's globals when its own document is replaced by open, write and close | ? | ? | ? | ? | ? |
| A15 | window.scrollTo | no | yes | yes | yes | yes |
| A16 | String.fromCharCode and charCodeAt | no | no | yes | no | yes |
| A17 | Form controls render only inside a FORM | yes | yes | yes | no | no |
| A18 | Setting window.onerror to null suppresses the error dialog | ? | yes | yes | ? | ? |

The language, as Alex gave it:

- Netscape 2 has JavaScript 1.0: probably no Array, no typeof, no prototype, no object or array literals, no function expressions.
- Netscape 3 has JavaScript 1.1: Array, typeof, prototype and new Function, but no literals, no switch, no regex literals and no try/catch.
- Taken here as well, from the JavaScript 1.2 to 1.5 release notes: nested function declarations, delete, do-while, labels, push, pop, shift, unshift, splice, slice, concat and String.fromCharCode and charCodeAt are 1.2; ===, !==, undefined, NaN and Infinity as globals, Function.prototype.call and apply and \u escapes are 1.3; throw, try, in and instanceof are 1.4.
- IE 3 has JScript 1.0 (about JavaScript 1.0), IE 4 has JScript 3.0 (about JavaScript 1.3, with literals and switch, and no try/catch).

## 3. Page structure

A cdom page is a BODY with the mount, the string table, the shim's script, the boot's script, and the page's own script that calls cdomBoot.
On this tier the page becomes a frameset whose one frame shows the app:

1. The boot finds no getElementById or no createElement.
2. It keeps its arguments in globals, and document.writes, while the page is still parsing, two SCRIPT SRCs (cdom.frames.js, the tier's shim, and the app's words build, url + ".ie9.js") and an inline script that calls cdomFramesStart.
3. cdomFramesStart document.writes `<frameset rows="*"><frame name="cdomView" src="about:blank"></frameset>`.
   The parser is still in the body, and nothing in it has content yet (the mount is an empty DIV, and SCRIPT does not count), so the FRAMESET replaces the body (HTML5's frameset-ok rule; A13 for the old browsers).
4. The shim keeps cdom's node tree in the top window, and writes the frame's whole document from it.

The top window keeps everything: the node tree, C's memory and the app.
The frame only shows HTML, and its handlers call `parent.cdomF`.
If the FRAMESET is not made (A13 false), the fallback is A14: once the page has loaded, the shim replaces the top document itself by open, write and close with the same FRAMESET.
The prototype has both, and takes the fallback by itself when no frame is there once the page has loaded (readyState complete, or 50 waits of 20 ms where there is no readyState).

Found while building it: a BODY tag sets HTML5's frameset-ok flag to "not ok", so Chrome ignores a FRAMESET written after `<body>`, even an empty one, and Netscape's documented rule is the same.
An implied BODY (the page opens with `</head><div id="m">`) keeps the flag, and the written FRAMESET takes the body's place.
So a frames-tier page should not open its BODY with a tag; one that does (shim/page.html, cdom.mk's default page, does) still runs, through the fallback.
tests/frames.html has no BODY tag, and tests/build.sh makes tests/out/frames-body.html, the same page with one; tools/frames.mjs runs both.

### The boot itself

The boot is the one script every browser runs, so the part Netscape 2 parses must be JavaScript 1.0.
shim/boot.js is now two parts, which the build writes as two SCRIPT elements:

- Part 1, JavaScript 1.0: cdomBoot, which tells this tier from the others and takes it, and cdomBootFrames.
  No typeof, no literals, no function expressions, no nested functions, no ===, no try, no throw, no Array, no split or join.
  tools/legacy/js11.mjs checks it (its js10 gate), and the build refuses a boot that fails.
- Part 2, ES3 as before: cdomBootDom, the old cdomBoot's body, for the four other tiers.
  Netscape 2 to 4 and IE 3 and 4 fail it with a syntax error (try, throw, function expressions), and never call it.

The test that picks the tier is `document.getElementById && document.createElement`: either missing is this tier.
The floor of the fourth tier is then attachEvent or addEventListener alone, as tools/cdp.mjs's FLOOR already says; a browser with getElementById and no listeners is still refused.

## 4. The shim: tools/legacy/frames.js

A shim of its own, written in JavaScript 1.1, not shim/cdom.js through Babel: that brings Babel's helpers and the Promise polyfill, which this tier must not have, and shim/cdom.js's DOM calls (createElement, insertBefore, addEventListener) are exactly what is gone.
It keeps shim/cdom.js's design where it can:

- The same imports by the same names, with the same meaning for the tree ops (at, open, close, text, attr, name_el, listen, walk, set_text, set_attr, style, remove, move, clear, detach, attach, tpl_save, clone and the rest).
  Each op changes a node tree of plain objects in the top window instead of the DOM: element and text nodes with parent, first, last, next and previous links, so insert and remove need no splice (JavaScript 1.2).
- The same strings: a literal's slot in cdomLits, a read-only pointer read once and kept, a runtime record in C's tmp block; ahead is a no-op, since every string is read from C's memory directly.
- The same event record and cd_layout read, and the same delivery to the nearest listening ancestor with the path down (8 bits of element index a level), walked on the node tree instead of the DOM.
- The same words memory as the third and fourth tiers (memory.words of the app's .ie9.js file).
- The seed: options.seed, else 0, noted in cdomF.errors.
  The seed rule's one exception (Math.random and the clock) is IE 9 and 10's alone (tools/seeds.mjs), and extending it to this tier is Alex's call, not taken here.

Rendering is once per outermost call into C, never once per op:

- Every op that changes what is shown sets a dirty flag.
- Each entry from the browser (a handler, a timer, cd_main through the start, a call of an export) holds a count; when it falls to 0 and the flag is set, the shim serializes the tree to HTML and rewrites the frame with one open, write and close.
- A browser event that becomes several cdom events (a change is input, then change) holds the count across all of them: one rewrite.
- An entry that changed nothing rewrites nothing.

JavaScript 1.1 has no try/finally, so a C trap leaves the count up.
The next entry from the browser (a handler or a timer, which run only with nothing on the stack) puts it back to 0, ends the dead call's runtime strings, and draws.

## 5. Events

Every handler in the written markup calls `parent.cdomF` with a handle, the node's index in this draw's table; the shim finds the node, and delivers the event to the nearest listening ancestor, as shim/cdom.js's delegate does.

| cdom event | Made from | Where |
| --- | --- | --- |
| CD_EV_CLICK | A link's href="javascript:parent.cdomF.k(n)"; a button's onClick | A, BUTTON (as INPUT TYPE=button), INPUT of type button, submit, reset, checkbox and radio; text under a listening element, outside a link or a field, is wrapped in a link of its own |
| CD_EV_INPUT | onChange; onKeyUp too where it fires (NS 4, IE 4), each only when the value differs from what C last saw | INPUT text and password, TEXTAREA; a checkbox's or radio's onClick; SELECT's onChange |
| CD_EV_CHANGE | onChange | the same fields; a checkbox's or radio's onClick |
| CD_EV_FOCUS, CD_EV_FOCUSIN | onFocus | fields |
| CD_EV_BLUR, CD_EV_FOCUSOUT | onBlur | fields |
| CD_EV_KEYDOWN, CD_EV_KEYUP | onKeyDown, onKeyUp, with keyCode or which | fields, NS 4 and IE 4 only |
| CD_EV_POINTEROVER, CD_EV_POINTEROUT | onMouseOver, onMouseOut | links |

Lost on this tier: pointer down, up and move, mouse and touch events, the wheel, contextmenu, drag and drop, dblclick, scroll, resize, visibility, load and error, the media events and submit (the one FORM always returns false).
Key events are lost on Netscape 2 and 3 and IE 3 entirely (no handler fires); every pointer event is lost on all five.
The record carries no coordinates (x and y are 0), no buttons and no timeStamp; the editing bit is set for a field.
preventDefault and stopPropagation do nothing: a javascript: URL navigates nowhere already, and nothing bubbles.

## 6. Field state, focus and scroll

All the controls are written inside one FORM (Netscape shows a control only in a FORM, A17), in document order, so the shim's list of the fields it wrote is in the order of form.elements.
Each is named cN after its index there, but a radio keeps the app's name attribute, which is what makes radios of one name one group; read-back goes by the index either way.
Before each rewrite (at each entry from the browser) the shim reads every field back: a text's value, a checkbox's checked, a SELECT's selectedIndex, and the focused field's selection where the browser has selectionStart.
That includes text the user typed that C has not seen yet; C sees it at the next onChange or onKeyUp, as an input event.
The new markup writes those values back, and after the rewrite the shim focuses the field that had focus, puts the selection back (setSelectionRange, or IE 4's createTextRange) and scrolls the frame back where it was (scrollTo, A15).
IE 4 has no selectionStart: the shim reads the caret from document.selection.createRange(), a TextRange whose text is the selection and which, moved back to the field's start, measures the selection's end.
Netscape 4 has neither, and so cannot put the caret back: there a key typed in a field whose handler changes the tree draws nothing, and the draw waits for the field's change or blur.
tools/cdp.mjs models both under LEGACY=frames: the top page's ?ie4 (no selectionStart, End or setSelectionRange, with createTextRange and document.selection) and ?ns4 (none of them).
Where there is no scrollTo, window.scroll (JavaScript 1.0) takes its place.
The focus that the shim gives back is not delivered to C as a new focus, and neither is a focus C asks for (cd_call "focus").

Leaving a field for a button fires change, blur and focus between the press and the release.
A rewrite there would replace the button under the pointer, and the click would be lost (tools/frames.mjs saw it lost before this rule).
So the draw after a change, a focus or a blur waits cf_LATER (250 ms), and the click that comes in that time draws once for all of them.
A press held longer than that would still lose it, so the frame's document also tracks its mouse button (document.onmousedown and onmouseup, captured on Netscape 4 and routed on), where the browser has them: a put-off draw waits while the button is down, up to 20 waits (5 s) for a release the frame never sees, and the release sets a 0 ms poll, which runs after the click the release makes.
Netscape 2 and 3 and IE 3 have no document.onmousedown, and keep the 250 ms rule alone.

## 7. History

Each rewrite uses `document.open("text/html", "replace")`: on Netscape 3 and 4 and IE 4 that reuses the frame's history entry (A3), and Netscape 2 takes the first argument alone, so it adds one entry a redraw.
Section 11 measures, in Chrome, what each way of rewriting a frame does to history.length.

## 8. Styles

CSS is dropped (Netscape 4's is partial and known to crash), and what can be carried is written as period markup:

| CSS | Written as |
| --- | --- |
| color | FONT COLOR |
| font-family | FONT FACE |
| font-size in px, pt or em | FONT SIZE 1 to 7 (up to 10px 1, 13 2, 16 3, 18 4, 24 5, 32 6, then 7) |
| font-weight bold or 600 and up | B |
| font-style italic | I |
| text-decoration underline, line-through | U, STRIKE |
| text-align center, right | CENTER; DIV ALIGN=right |
| background-color, background (a color) | a one-cell TABLE with BGCOLOR around the element; the mount's or the body's becomes the BODY's BGCOLOR |
| width, height in px on IMG, TABLE, TD, TH | WIDTH, HEIGHT |
| display: none | the element is not written (its fields keep their values in the tree) |
| a style attribute | read into the same properties, then as above |
| every other property | dropped |

Tags: BUTTON becomes INPUT TYPE=button with its text as the value; SECTION, ARTICLE, HEADER, FOOTER, NAV, MAIN, ASIDE and FIGURE become DIV; SVG and CANVAS and everything inside them, SCRIPT, STYLE and TEMPLATE are not written (they draw nothing here for now).
Attributes kept: id, href, src, alt, title, width, height, align, valign, border, colspan, rowspan, cellpadding, cellspacing, size, maxlength, rows, cols, type, value and target; every other one is dropped (class, style, data-, aria- and the rest).

## 9. What is lost

- Everything section 5 lists as lost, and every layout read: cd_rect answers 0, cd_hit 0, cd_viewport the frame's size where the browser says it.
- Canvas, SVG, images from bytes, WebSocket, fetch, IndexedDB, audio, files, jobs, share and the clipboard: each import answers as the API's unsupported case (0, or nothing), and the shim notes it in cdomF.errors.
- cd_url_go (no History API): 0.
- A C trap is not thrown out of the call (JavaScript 1.1 has no throw); it ends the call, and the next entry recovers (section 4).
- Errors from the shim go to cdomF.errors and window.status, not window.onerror.
- Flicker on each rewrite, and on Netscape 2 a history entry a rewrite.
- Text outside Latin-1 on browsers without String.fromCharCode (NS 2 and 3, IE 3): shown as "?".
- The URL bar shows the page's one URL whatever the app does.

## 10. The language surface, and what the app needs

tools/legacy/frames.js is JavaScript 1.1 as Netscape 3 has it, and tools/legacy/js11.mjs refuses, in it and in its minified output:

- try, catch, throw, switch, do-while, labels, delete, in, instanceof, ===, !==, regex literals, object and array literals, function expressions and nested function declarations, getters, const and let, and \u escapes;
- a member or a call of a new expression (`new Date().getTime()`, `(new Date()).getTime()`, terser's `(new Date).getTime()`), a syntax error in Netscape 2 and 3 (docs/descent.md): the shim keeps the new expression in a variable;
- an assignment anywhere in the test of if, while, for, do-while or ?:, which Netscape 2's parser silently reads as an equality test (netscape.exe's messages: "test for equality (==) mistyped as assignment (=)?", "Assuming equality test");
- the globals undefined, NaN and Infinity;
- the methods and globals of JavaScript 1.2 and later (push, pop, shift, unshift, splice, slice, concat, call, apply, substr, charCodeAt, fromCharCode, isNaN, setInterval), except charCodeAt and fromCharCode where the file uses them behind a check;
- the DOM this tier has not (createElement, appendChild, insertBefore, removeChild, getElementById, innerHTML, addEventListener, attachEvent).

It cannot tell a string's indexOf (JavaScript 1.0) from an Array's (ES5), or location's replace and search (1.0 and 1.1) from a string's (1.2), so it lets those names through.
The boot's first part goes through the same gate with js10 on, which also refuses typeof, void, Array, Object, Function, prototype, split and join, and what docs/descent.md measured Netscape 2 lacks: arguments (f.arguments it has), constructor, onerror, document.images, Image, screen and navigator.plugins (location.replace it has); it may name getElementById and createElement, which it tests for and never calls.

So the language surface the tier needs of the browser is: functions declared at the top level, new Object() and new Array(), for, for-in, while, if, ?:, the bitwise operators, typeof, new Function, string charAt, indexOf, substring, split and length, Array join, escape and unescape, Math, Date, setTimeout and clearTimeout with a string, and the document and form objects of section 2.
That list is what a Netscape 2 code path would have to give, whichever way it is made (a WebAssembly interpreter in JavaScript 1.0, or a transform of the wasm2js output): that choice is Alex's, after the probe.

The app itself runs from the words build (app-ie9) for the prototype, which is ES3.
What Netscape 3 lacks for it: function expressions and nested functions (the whole file is one), array literals (words.js's tables), push (words_init and words_grow), charCodeAt (the base64 data segments), === (16 in tests/out/legacy.wasm.ie9.js) and throw (3 there, wasm2js's traps), Math.imul, clz32 and trunc (frames.js supplies those three).
And the page: tools/cdpage writes the string table as `var cdomLits=[...]`, an array literal, which Netscape 2 and 3 cannot parse.

## 11. Predictions

Written before any measuring; each has a falsifier.
Timings are in headless Chrome on a Linux x86-64 VM under LEGACY=frames, and indicative only.

- P1, size: cdom.frames.js, minified, is at most 16,384 bytes, and at most 6,144 bytes gzipped (gzip -9).
  Falsifier: either is larger.
- P2, redraw cost: a redraw of a page of 2,000 nodes costs at most 6 microseconds a node, serialize and write together (the median of 10 redraws), and serializing is at most half of that.
  Falsifier: more than 6 microseconds a node, or serializing more than half.
- P3, redraw cost scales linearly: the cost a node at 4,000 nodes is within a factor of 1.5 of the cost a node at 1,000.
  Falsifier: outside that factor.
- P4, redraws per event: exactly 1 for a click whose handler changes the tree, exactly 1 for a field change that C answers (input and change together), exactly 0 for an event whose handler changes nothing, and exactly 1 for cd_main.
  Falsifier: any other count.
- P5, written size: the written HTML is at most 60 bytes a node on the grown list (an LI with a short text).
  Falsifier: more than 60.
- P6, history in Chrome: 20 rewrites by document.open("text/html", "replace") add 0 entries to the top's history.length; so do 20 by document.open() and by document.open("text/html"); 20 by the frame's location.replace with a javascript: URL add 0; 20 by setting the frame's location.href to a javascript: URL add 20.
  Falsifier: any count other than these.

## 12. Verifying in the real browsers

Alex's XP VM serves pages from the host at http://10.0.2.2:<port>/.

1. Build: `tests/build.sh` writes tests/out/frames.html, cdom.frames.js and frames.wasm.ie9.js.
2. Serve tests/out: `python3 -m http.server 8000 --directory tests/out` on the host.
3. In each of Netscape 2.02, 3.04 and 4.08 and IE 3 and 4, open http://10.0.2.2:8000/frames.html.
   Then frames-body.html, the same page with a BODY tag, which takes the fallback (A14).
4. Check, in order: the frame shows the heading, the count, the button and the fields (A13, or the fallback for A14); "inc" adds one to the count; typing in the note field, then clicking "inc", keeps the typed text; typing in the name field and leaving it echoes the text; the history list (Go menu) does not grow with each click (A3).
5. Record each assumption of section 2 that turned out otherwise.
   Expected on today's files, before the code path decision: every one of the five stops at the app file, which has throw (JavaScript 1.4, JScript 5), and Netscape 2 and 3 and IE 3 at the string table (an array literal) too, with the error dialog A18 is about.
   What the five can check today is the boot: that it parses, takes this tier, writes the FRAMESET and loads cdom.frames.js (A12, A13), and that cdom.frames.js parses (the JavaScript 1.1 gate's claim).

## 13. Results

Measured after the predictions were committed (e22b529); headless Chrome 153 on a Linux x86-64 VM, under LEGACY=frames, so indicative only.
`FRAMES_MEASURE=1 LEGACY=frames node tools/frames.mjs` prints them; four runs.

| | Predicted | Measured | Verdict |
| --- | --- | --- | --- |
| P1, size | at most 16,384 bytes minified, 6,144 gzipped | 30,024 bytes minified, 9,757 gzipped (30,059 after the seed change below) | refuted |
| P2, redraw cost at 2,000 nodes | at most 6 us a node, serializing at most half | 6.2, 5.15, 5.85 and 4.7 us a node; serializing 0.15 | refuted (one run of four over 6) |
| P3, linear | 4,000 nodes within 1.5 times 1,000's cost a node | 0.71 to 0.80 times (4.8 to 5.6 against 6.6 to 7.4 us) | confirmed |
| P4, redraws per event | 1 click, 1 change, 0 unchanged, 1 cd_main | 1, 1, 0, 1, and 1 for a timer, a change with the click after it, and an export of 15 ops | confirmed |
| P5, bytes a node | at most 60 | 5.7 (17 bytes an LI of 3 nodes) | confirmed |
| P6, history in Chrome | 0, 0, 0, 0 and 20 | 0 for all five, location.href = javascript: too | refuted |

What the misses say:

- P1: every name of the shim is a global, since JavaScript 1.1 has no closures to keep them in, so terser can mangle only the locals; and it runs with compress off, since compress could write what the gate refuses.
  The names (cf_ and the rest) are 10,053 of the 30,024 bytes.
  A build that renames the globals to short ones, as the markup's cdomF already is, is the obvious next step.
- P2: the cost is the browser's, not the shim's: serializing is 0.15 us a node, under 3% of it, and the rest is Chrome parsing and laying out the written document.
  The clock was new Date() (1 ms), not performance.now (IE 9 has none, and LEGACY=frames takes it), so each sample is the mean of 10 redraws in a row, and the median of 7 samples is kept, where P2 said the median of 10 redraws.
- P6: Chrome adds no history entry for any of the five; a javascript: URL's result replaces the frame's document in place (the HTML standard has such a navigation replace).
  So in Chrome the history check can only catch a rewrite that navigates by URL; tools/mutate.py's frames-history-entry plants a fragment navigation, which it catches.
  Netscape 3 and 4 and IE 4 are said to add one for open() without "replace", and Netscape 2 for every rewrite (A3); that is the probe's to measure.

## 14. Tests

tools/frames.mjs, under LEGACY=frames (CI runs it beside the other levels), checks:

- the boot takes the fifth tier and fetches its two files and no other tier's; the page and its frame have no createElement, appendChild, innerHTML, getElementById or listeners; the page is a frameset of one frame;
- the frame's HTML is the tree's (the frame's document, serialized, against the shim's serialization of the tree parsed afresh), after cd_main, after a click and at the end; every element of the tree with an id is in the frame with its text; display: none and the SVG are not written; the styles are FONT COLOR, B, I and U, with no style attribute; the button is an INPUT TYPE=button in the one FORM;
- a click on the button, on two links and on a span's text under one listener reach C, the latter three with the right paths;
- focus, input, change and blur reach C; the input's value is read in C (non-ASCII, through both UTF-8 directions); the click that left a field still reaches C;
- typed text C has not seen survives a redraw, with its focus and caret, and typing goes on at the caret; C reads it, sets a value and moves the focus;
- a checkbox's change, with checked read in C; a timer's callback;
- the redraw counts of P4, and one draw for an export of 15 ops;
- no redraw adds a history entry; no error noted, no page exception;
- a trap in C is thrown out of the call with nothing drawn, and the next click recovers;
- frames-body.html (a BODY tag) runs through the fallback, and a click there reaches C.

tools/js11.mjs checks each rule of the gates on code planted for it.
The other levels are unaffected: tools/legacy.mjs passes on '', 1, ie9, ie6 and below, LEGACY=ie6 tools/fuzz.mjs, tools/es3.mjs, tools/words.mjs and tools/legacyfresh.mjs pass, and every built file of the other tiers (cdom.legacy.js, cdom.legacy-ie9.js, cdom.legacy-ie6.js, each app's .wasm.js and .wasm.ie9.js) is byte-identical to main's.
The pages that inline the boot (legacy.html, listfuzz.html) differ by the boot alone: two scripts now, where there was one.
tools/mutate.py has 37 mutants for this tier (frames-, js11- and js10-), each caught by its own assertion.
