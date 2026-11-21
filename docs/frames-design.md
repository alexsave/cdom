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
| A13 | A FRAMESET written by a script in the BODY, before any content, replaces the body | ? | no after any content, yes in the head (section 15) | no after any content, yes in the head (section 15) | ? | yes in the head (section 15) |
| A14 | The top window keeps its script's globals when its own document is replaced by open, write and close | ? | ? | no (section 15) | ? | ? |
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
On this tier the page becomes a frameset of two frames (changed on 2026-11-21 by the real runs, section 15: the first design, a frameset of one frame written by the boot into the loading page, did not run in Netscape 4):

1. The early script (shim/boot-early.js, JavaScript 1.0, which tools/legacy/build.mjs page writes before the head's end of every page) runs before any content.
   If there is no getElementById or no createElement, and the window is not named cdomApp, it document.writes `<frameset rows="*,1">` with two frames: cdomView, which shows the app, and cdomApp, 1 pixel high, whose source is the same page under another URL (the query gains `cdomApp`, since Netscape 4 drops a frame whose URL is the frameset's own).
2. The page, loaded again in cdomApp, runs as it always does: the boot finds no getElementById, keeps its arguments in globals, and document.writes, while the page is still parsing, two SCRIPT SRCs (cdom.frames.js, the tier's shim, and the app's JavaScript 1.0 build, url + ".js1.js") and an inline script that calls cdomBootWait, which calls cdomFramesStart once both files have run (IE 4 runs the inline script first, so it tries again every 50 ms).
   Since section 16 cdomApp is not the page again but its app document, which holds the boot's first part, the table, the shim and the module inline (Netscape 2 has no SCRIPT SRC), and nothing is written or fetched.
3. The shim keeps cdom's node tree in cdomApp, whose document nothing replaces, and writes the whole document of cdomView, the parent's frame 0, from it.
   The view's handlers call `parent.cdomApp.cdomF`.
   The app's URL ops read the top window's location, not the frame's.

Why not the first design: Netscape 4 ignores a FRAMESET written after any content (a DIV is enough: A13 is false there), and a window whose document is replaced by open, write and close loses every global (A14 is false there: "cf_FRAMESET is not defined" was the shim's own variable, gone).
A frameset written in the head, before any content, is made in Netscape 3 and 4 and IE 4, and then the script blocks after it are dropped, so the state cannot live in the top window.
The page is fetched twice (the second from the cache).
Netscape 2 makes the frameset as well, and has no SCRIPT SRC (section 15).

Found while building it: a BODY tag sets HTML5's frameset-ok flag to "not ok", so Chrome ignores a FRAMESET written after `<body>`; the early script is in the head, so a page that opens its BODY with a tag (shim/page.html, cdom.mk's default page) takes the same path.
tests/build.sh makes tests/out/frames-body.html, the same page with a BODY tag; tools/frames.mjs runs both.

### The boot itself

The boot is the one script every browser runs, so the part Netscape 2 parses must be JavaScript 1.0.
shim/boot.js is now two parts, which the build writes as two SCRIPT elements:

- Part 1, JavaScript 1.0: cdomBoot, which tells this tier from the others and takes it, and cdomBootFrames.
  No typeof, no literals, no function expressions, no nested functions, no ===, no try, no throw, no Array, no split or join.
  tools/legacy/js11.mjs checks it (its js10 gate), and the build refuses a boot that fails.
- Part 2, ES3 as before: cdomBootDom, the old cdomBoot's body, for the four other tiers.
  Netscape 2 to 4 and IE 3 and 4 fail it with a syntax error (try, throw, function expressions), and never call it.

The test that picks the tier is `document.getElementById && document.createElement`: either missing is this tier (the early script makes it first, in the head).
The inlined modern shim and the boot's second part are written as `<script language="JavaScript1.5">`: Netscape 2 to 4 (JavaScript 1.0 to 1.3) skip them without a syntax error alert, which Netscape 3 and IE 4 would show for each, and every browser that runs them takes the attribute as JavaScript.
The floor of the fourth tier is then attachEvent or addEventListener alone, as tools/cdp.mjs's FLOOR already says; a browser with getElementById and no listeners is still refused.

## 4. The shim: tools/legacy/frames.js

(JavaScript 1.0 since section 16.)
A shim of its own, written in JavaScript 1.1, not shim/cdom.js through Babel: that brings Babel's helpers and the Promise polyfill, which this tier must not have, and shim/cdom.js's DOM calls (createElement, insertBefore, addEventListener) are exactly what is gone.
It keeps shim/cdom.js's design where it can:

- The same imports by the same names, with the same meaning for the tree ops (at, open, close, text, attr, name_el, listen, walk, set_text, set_attr, style, remove, move, clear, detach, attach, tpl_save, clone and the rest).
  Each op changes a node tree of plain objects in the app frame (cdomApp) instead of the DOM: element and text nodes with parent, first, last, next and previous links, so insert and remove need no splice (JavaScript 1.2).
  An insert keeps the DOM's rules: a node before itself stays where it is, and before a node that is not a child of the parent is an error (shim/cdom.js's trap), noted here, and nothing moves.
- The same strings: a literal's slot in cdomLits, a read-only pointer read once and kept, a runtime record in C's tmp block; ahead is a no-op, since every string is read from C's memory directly.
- The same event record and cd_layout read, and the same delivery to the nearest listening ancestor with the path down (8 bits of element index a level), walked on the node tree instead of the DOM.
- The same words memory as the third and fourth tiers (memory.words of the app's .ie9.js file).
- The seed: options.seed when it is a number, else weak entropy from the engine's generator mixed with the clock (the recipe of shim/cdom.js's seed() for IE 9 and 10), and the clock alone where the generator is missing or broken.
  Alex decided on 2026-11-20 to extend the seed rule's one exception to this tier; tools/seeds.mjs allows exactly the one draw in cf_seed.
  Here cd_seed is neither secret nor unpredictable, and options.seed overrides it.

Rendering is once per outermost call into C, never once per op:

- Every op that changes what is shown sets a dirty flag.
- Each entry from the browser (a handler, a timer, cd_main through the start, a call of an export) holds a count; when it falls to 0 and the flag is set, the shim serializes the tree to HTML and rewrites the frame with one open, write and close.
- A browser event that becomes several cdom events (a change is input, then change) holds the count across all of them: one rewrite.
- An entry that changed nothing rewrites nothing.

JavaScript 1.1 has no try/finally, so a C trap leaves the count up.
The next entry from the browser (a handler or a timer, which run only with nothing on the stack) puts it back to 0, ends the dead call's runtime strings, and draws.
The shim's own draw can throw too (a focus given back, a browser's error), which would leave cf_drawing, the flag that keeps the handlers quiet during a draw, up for good: each draw sets a 0 ms timer that puts it down if it is still up, and notes "a draw did not finish"; the recovery above puts it down as well.
The restore sets a selection on text fields only, so cd_call(checkbox, "select") no longer throws there.

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

Tags are lowercased, and one that is not letters and digits alone (it would be written into the markup as it is) is noted and drawn as a SPAN.
Tags: BUTTON becomes INPUT TYPE=button with its text as the value; SECTION, ARTICLE, HEADER, FOOTER, NAV, MAIN, ASIDE and FIGURE become DIV, and so does FORM (a FORM inside the shim's one is dropped by the parser, and its end tag would close the shim's); SVG and CANVAS and everything inside them, SCRIPT, STYLE and TEMPLATE are not written (they draw nothing here for now).
Attributes kept: id, href, src, alt, title, width, height, align, valign, border, colspan, rowspan, cellpadding, cellspacing, size, maxlength, rows, cols, type, value and target; every other one is dropped (class, style, data-, aria- and the rest).

## 9. What is lost

- Everything section 5 lists as lost, and every layout read: cd_rect answers 0, cd_hit 0, cd_viewport the frame's size where the browser says it.
- Canvas, SVG, images from bytes, WebSocket, fetch, IndexedDB, audio, files, jobs, share and the clipboard: each import answers as the API's unsupported case (0, or nothing), and the shim notes it in cdomF.errors.
- cd_url_go (no History API): 0.
- A C trap is not thrown out of the call (JavaScript 1.1 has no throw); it ends the call, and the next entry recovers (section 4).
  A trap in cd_start (in cd_main) is noted too ("cd_start did not return"), and the frame is still drawn with what cd_main flushed; the start sets its first poll before calling C.
- Errors from the shim go to cdomF.errors and window.status, not window.onerror.
- Flicker on each rewrite, and on Netscape 2 a history entry a rewrite.
- Text outside Latin-1 on browsers without String.fromCharCode (NS 2 and 3, IE 3): shown as "?".
- The URL bar shows the page's one URL whatever the app does.

## 10. The language surface, and what the app needs

Since section 16 frames.js is gated as JavaScript 1.0 (js10), as the boot's first part is; what follows is the JavaScript 1.1 gate as it was, which still applies on top.
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
Since branch js1-compile it runs from tools/js1c's JavaScript 1.0 build (url + ".js1.js", docs/js1-compile-design.md) instead, and what follows is what the words build would have needed.
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
   Then frames-body.html, the same page with a BODY tag.
   tools/vmserve.py serves tests/out for the guest (scripts as application/x-javascript, which Netscape 3 needs), tools/vmopen.sh opens a URL in a browser of it, tools/vmprobe.sh also restarts the server and takes the screenshot.
4. Check, in order: the frame shows the heading, the count, the button and the fields; "inc" adds one to the count; typing in the note field, then clicking "inc", keeps the typed text; typing in the name field and leaving it echoes the text; the history list (Go menu) does not grow with each click (A3).
5. Record each assumption of section 2 that turned out otherwise.
   The runs of 2026-11-21 are in section 15.

## 13. Results

Measured after the predictions were committed (e22b529); headless Chrome 153 on a Linux x86-64 VM, under LEGACY=frames, so indicative only.
`FRAMES_MEASURE=1 LEGACY=frames node tools/frames.mjs` prints them; four runs.

| | Predicted | Measured | Verdict |
| --- | --- | --- | --- |
| P1, size | at most 16,384 bytes minified, 6,144 gzipped | 30,024 bytes minified, 9,757 gzipped (30,051 and 9,768 before the seed exception of 2026-11-20, 30,089 and 9,779 after) | refuted |
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

The frames tier in the real browsers (Netscape 4.08, IE 4.01, Netscape 3.04, one try of Netscape 2.02), with its own predictions and what they met, is section 15.
It changed the design of section 3 (the early script and the app frame), the string table's format, and js1c's output (initial memory, trailing return, signed data words).

## 14. Tests

tools/frames.mjs, under LEGACY=frames (CI runs it beside the other levels), checks:

- the boot takes the fifth tier and fetches its two files and no other tier's; the page and its frame have no createElement, appendChild, innerHTML, getElementById or listeners; the page is a frameset of the view and cdomApp, and the shim lives in cdomApp (its URL is the page's with cdomApp in the query); the early script is in the head ahead of any content; the inlined shim and the boot's second part are language="JavaScript1.5"; the string table is `new Array(...)` with entries separated by 0x01 and no \x escape and no digit after an escape; the app reads the top window's query;
- the frame's HTML is the tree's (the frame's document, serialized, against the shim's serialization of the tree parsed afresh), after cd_main, after a click and at the end; every element of the tree with an id is in the frame with its text; display: none and the SVG are not written; the styles are FONT COLOR, B, I and U, with no style attribute; the button is an INPUT TYPE=button in the one FORM;
- a click on the button, on two links and on a span's text under one listener reach C, the latter three with the right paths;
- focus, input, change and blur reach C; the input's value is read in C (non-ASCII, through both UTF-8 directions); the click that left a field still reaches C;
- typed text C has not seen survives a redraw, with its focus and caret, and typing goes on at the caret; C reads it, sets a value and moves the focus;
- a checkbox's change, with checked read in C; a timer's callback;
- the redraw counts of P4, and one draw for an export of 15 ops;
- no redraw adds a history entry; no error noted, no page exception;
- a trap in C is thrown out of the call with nothing drawn, and the next click recovers;
- frames-body.html (a BODY tag) runs the same way, and a click there reaches C; the boot waits for cdom.frames.js (cdomBootWait); a timer's draw waits while a button is down in the view.
- markup characters in a text, an attribute, a textarea's value and an option's text read back from the frame as themselves;
- a click held 600 ms that leaves an edited field still reaches C; two radios of one name are one group; a form from C is a DIV, and the fields in and after it stay the one FORM's; tags in capitals are the same tags, and one that is not letters and digits is a SPAN;
- a throw during a draw (cd_call(checkbox, "select"), a focus that throws) does not lock the page; cd_move before itself or before another parent's node moves nothing;
- a trap in cd_start is noted, and the frame still drawn, on frames.html and frames-body.html (?trapstart);
- typing in the middle of a field C answers keeps the caret, on IE 4 (?ie4: a draw a key, the caret through document.selection) and Netscape 4 (?ns4: no draw until the field is left).

tools/js11.mjs checks each rule of the gates on code planted for it, terser's output of a new expression's member, and frames.js itself before and after terser.
The other levels are unaffected: tools/legacy.mjs passes on '', 1, ie9, ie6 and below, LEGACY=ie6 tools/fuzz.mjs, tools/es3.mjs, tools/words.mjs and tools/legacyfresh.mjs pass, and every built file of the other tiers (cdom.legacy.js, cdom.legacy-ie9.js, cdom.legacy-ie6.js, each app's .wasm.js and .wasm.ie9.js) is byte-identical to main's.
The pages that inline the boot (legacy.html, listfuzz.html) differ by the boot alone: two scripts now, where there was one.
tools/mutate.py has 59 mutants for this tier (frames-, js11- and js10-), each caught by its own assertion.

## 15. The real browsers, branch frames-real (2026-11-21)

The runs are in the XP VM (tools/vmopen.sh opens a URL in a browser of it), tests/out served on port 8110, the screenshots /Users/alex/VMs/ie6-xp/shots/fr-*.png.
Predictions, written before the first run:

- R1, Netscape 4.08, frames.html: the inline modern shim (`var cdom = (() => {`) is a syntax error that Netscape 4 shows no dialog for (its errors go to the JavaScript console, as in docs/descent.md); the boot's part 1 runs, takes this tier, writes the SCRIPT SRCs; cdom.frames.js, the app's .js1.js and the string table (an array literal) parse in 4.08.
  The frame shows the heading, the count, the button and the fields.
  What I expect to break is the redraw: Netscape 4 cannot put a caret back, and its string handling is slow, so a start-up of several seconds.
  Falsifier: a blank page, or an error on any of the files.
- R2, IE 4.01, frames.html: the same files parse (JScript 3.0), but the modern shim's syntax error raises a Script Error dialog, behind the window, that has to be answered; after it the frame shows the same.
  IE 4 has innerHTML but the tier does not use it.
  Falsifier: no dialog, or a stack overflow in the app.
- R3, Netscape 3.04, frames.html: the string table's array literal is a syntax error ("missing ] after element list" or similar), with an alert, after which cdomFramesStart finds no table and nothing is shown; and the modern shim raises an alert of its own first.
  Falsifier: the table parses.
- R4, frames-body.html in each: the fallback (A14) replaces the top document, as in Chrome; same results as frames.html.
- R5, Netscape 2.02: stops at the modern shim's syntax error, then at SCRIPT SRC (A12), and shows nothing but alerts.

### 15.1 What the runs found

The runs are in the XP VM (QEMU on this Mac: every time below is slow in absolute terms), tests/out served by tools/vmserve.py on port 8110, each run under its own /r<n>/ path.
The screenshots are in /Users/alex/VMs/ie6-xp/shots (fr-*.png, and uttt-ie4.png, uttt-ns3.png, uttt-ns4.png).
Each failure is listed with its cause and where it was fixed.

1. Netscape 4.08, frames.html (the predictions R1: blank, not refuted by the checklist but by the first part): blank page, and in the JavaScript console "syntax error." at `var cdom = (() => {` (the inlined shim, as predicted), "throw is a reserved identifier." at the boot's second part (as predicted), and "cf_FRAMESET is not defined." in cdom.frames.js (fr-ns4-1.png, fr-ns4-2.png).
   Cause, from tests/probe-fs.html and probe-fsd.html (A13 and A14): the FRAMESET written after the DIV of the mount is ignored (modes B and C of the probe: frames=0), and a window whose document is replaced by open, write and close loses every global ("IMGS is not defined": the fallback's `cf_FRAMESET` was gone).
   A frameset written in the head, before any content, is made (mode A), and the script blocks after it are dropped.
   Fix: the two-frame design of section 3 (shim/boot-early.js, tools/legacy/frames.js, tests in tools/frames.mjs, mutants frames-early-after-content, frames-early-always, frames-early-query, frames-handler-parent, frames-url-own).
   tests/probe-fs3.html is the design in miniature, and ran in all three (marks: the app frame ran, the view was written from it, the view's script and link and button called `parent.cdomApp.hit`): Netscape 4.08, IE 4.01 and Netscape 3.04 (fr-ns4-probeF.png, fr-ie4-probeF.png, fr-ns3-probeF.png).
   One finding on the way: Netscape 4 drops a frame whose URL is the frameset's own (the frameset then had one frame), so the app frame's query gains `cdomApp`; and a frame of height 0 is not made either (rows="*,1").
2. IE 4.01 (R2): a Script Error dialog, "Syntax error", line 17 char 14, as predicted (the inlined shim; fr-ie4-1.png).
   Fix: `<script language="JavaScript1.5">` on the shim and the boot's second part (tools/legacy/build.mjs page; mutants frames-shim-tag-plain and frames-boot2-tag-plain): Netscape 2 to 4 skip them, and IE 4 raised no dialog after it.
   Then "Object expected", line 2115, char 1 (fr-ie4-2.png): IE 4 runs the written inline script that starts the shim before the two SCRIPT SRC files it wrote have run.
   Fix: shim/boot.js's cdomBootWait, which calls cdomFramesStart once both are there and tries again every 50 ms (test and mutant frames-boot-no-wait).
3. Netscape 3.04 (R3: the array literal): in order, each one the next after the one before was fixed, with an alert or a stall:
   - "Unknown File Type", text/javascript: it takes only application/x-javascript for a SCRIPT SRC (fr-ns3-1.png).
     tools/vmserve.py sends that; a server of the app's has to (cbase's rules for .js files are the app's to set).
   - "syntax error." at `function failed(e) {` (the boot's second part): hidden by the language attribute of item 2.
   - "syntax error." at `var cdomLits=[...` (the prediction): the table is now `new Array(...)` (tools/cdlits.c; tests/lits/run.sh and mutant lits-array-literal).
   - the table's text empty or shifted (fr-ns3-4.png, fr-ns3-lits.png): Netscape 3 ends a string at a NUL, so the separator \x00 is now 0x01, written \001 (a literal that holds the byte 0x01 is refused at build time, and tests/lits's own literals use 0x03); it reads a `\x` escape as every hex digit after it and an octal escape as every octal digit, so controls and "<" are three-digit octal escapes and a digit after an escape starts a new literal (`\001"+"4`).
     Tests: tests/lits/run.sh, tools/frames.mjs's static check, mutants lits-separator-nul, lits-escape-hex, lits-digit-after-escape, lits-separator-in-literal.
   - frames.html then ran (fr-ns3-5.png, fr-ns3-9.png).
   - uttt.html: "function does not always return a value." (fr-ns3-uttt-1.png at that run): Netscape 3 refuses, as Netscape 2 does, a function that returns a value and ends in a `while(1)` with no return after it; js1c now ends every direct function in `return 0;` and tools/js10.mjs refuses the shape (mutant js1c-no-trailing-return).
   - uttt.html: the string table's stamp (3396446046) read as 2147483647 in the app ("the page's string table is not this module's"): Netscape 3's parseInt keeps a value only up to 2^31 - 1, so js1c writes a data word as a signed int32 in base 36 (tests/js1diff.c's bigword, tools/js1.mjs's check, mutant js1c-data-unsigned).
   Not a fix: a stall of Netscape 3 that I took for a cache poisoned by a browser killed mid-load, and for a hang of a script, was a server that the guest's keep-alive requests wedged (the frame's page never answered, "Connect: Host contacted. Waiting for reply..."): tools/vmserve.py is HTTP/1.1 now, and tools/vmprobe.sh starts a fresh one each run.
   Two other things I wrote down as causes were wrong and were taken out again: `window.cdomFactory=` at the end of a file (it loads), and the table's size.
4. Start-up memory (step 3): see 15.3.
5. uttt (step 4): see 15.4.

### 15.2 The checklist, per browser

| | Predicted | frames.html | frames-body.html |
| --- | --- | --- | --- |
| Netscape 4.08 | R1: the shim's syntax error silently, the heading, count, button and fields shown; a slow start | passes (fr-ns4-2.png, -3, -4: heading, count 0, inc, links, fields; "inc" twice gives count 2; typed "hello" kept; "xyz" typed in the name field and the field left: "echo startxyz"; Back greyed throughout: the history does not grow) | passes (fr-ns4-body.png) |
| IE 4.01 | R2: one Script Error dialog, then the same | passes after items 2 (fr-ie4-4.png, -5, -6: count 3, "hello" kept, "echo startxyz"; Back greyed) | passes (fr-ie4-body.png) |
| Netscape 3.04 | R3: the array literal stops it, with an alert, and the shim's alert first | passes after item 3 (fr-ns3-5.png, -6, -9.png: count 3, "hello" kept, "echo startxyz"; Back greyed) | not run |
| Netscape 2.02 | R5: the shim's syntax error, SCRIPT SRC, alerts | see 15.5 | not run |

R4 (frames-body.html takes the fallback) is refuted: there is no fallback any more, and a BODY tag changes nothing since the early script is in the head.
Where a prediction was wrong: R1 (blank, with the reason of item 1, which the prediction did not have), R3 (the array literal was the third stop of five), and R2's "the same after the dialog" (a second error came first).

### 15.3 Start-up memory

js1c wrote every word of the initial memory (32,768 words for frames.wasm) before the data: Netscape 2.02 dies on `undefined | 0` ("33041 is not a number").
Netscape 3.04, Netscape 4.08 and IE 4.01 do not (tests/probe-undef.html, fr-ns4-undef.png, fr-ns3-undef.png, fr-ie4-undef.png): every bitwise operator reads a missing property as 0, as node does, and a load is `(JM[i]|0)`.
So the factory tells Netscape 2 by an Array with no length (`JA=new Array(1);JZ=!JA.length`), which keeps writing every word and every page memory.grow adds, and the others write only the words below __heap_base (the C stack and the data: 16,440 of the 32,768; the heap is written by the program before it reads it, or reads as 0 as wasm's does).
Prediction, written before the runs: at least 40% off the factory's time on Netscape 4 and 3, since half the words are not written.
Measured with tests/probe-zero.html, the factory called three times (ms):

| | before (every word) | after (below __heap_base) |
| --- | --- | --- |
| Netscape 4.08 | 1094, 969, 1156 | 468, 485, 390 (2.4 times faster) |
| Netscape 3.04 | 1515, 1438, 1531 | 813, 781, 781 (1.9 times faster) |

Confirmed.
tools/js1.mjs runs every case again on a memory written only below __heap_base (297 of 297 agree with wasm, including tests/js1diff.c's peek, peekb and peekh of heap words nothing wrote, and the words of a grown page), and on the Netscape 2 emulation (an Array with no length: every word written, as before); four mutants (js1c-heap-written-eagerly, js1c-ns2-not-told, js1c-grow-always-written, js1c-grow-ns2-unwritten) and the two that were there (js1c-memory-unwritten, js1c-grow-unwritten) are caught.
What is left: the stack's 16,384 words are written too, and need not be on these three engines; not done, since the speed of Netscape 4's object after a dense write (js-compile-design 12.7) was not re-measured.

### 15.4 uttt

demo/uttt builds uttt.wasm.js1.js now (it built only the words file, which the frames tier no longer loads: the Makefile's all and tools/uttt.mjs follow).
tools/uttt.mjs passes on the WebAssembly, frames and frames-ns4 profiles (72 checks).
uttt.html?AJG7NYNQG4RFXXGVPGD7QR2ETSOYTWMCREAQ in the VM:

| | board | > | < | \|< and >\| | Play | Open, typed code | bad code | screenshot |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Netscape 4.08 | shows, the code from the query in the field (fr-ns4-uttt-1.png) | one move (fr-ns4-uttt-2.png) | back (fr-ns4-uttt-3.png) | to the end, "Move 50 of 50. O wins." (fr-ns4-uttt-4.png), and back | advances a move a tick (fr-ns4-uttt-5.png), but the Pause click is lost: each tick's draw replaces the button (the draw now waits while a button is down in the view, which Chrome tests, and did not help on Netscape 4: not understood); stops at the end: not run | not run | not run | uttt-ns4.png |
| IE 4.01 | shows (fr-ie4-uttt-1.png) | yes (fr-ie4-uttt-2.png) | yes | yes (fr-ie4-uttt-3.png: "Move 50 of 50. O wins.") | advances, Pause stops it (fr-ie4-uttt-4.png, -5.png: move 3 and still 3); end: not run | opens (fr-ie4-uttt-7.png) | "That is not a game code" and no board (fr-ie4-uttt-6.png) | uttt-ie4.png |
| Netscape 3.04 | shows (fr-ns3-uttt-1.png) | yes, in about 10 s | yes | yes: >\| in about 20 s (fr-ns3-uttt-3.png), \|< in about 45 s | not run | not run | not run | uttt-ns3.png (move 1) |

On Netscape 3.04 every draw of the 230-node board takes tens of seconds (the page is replaced as a whole, and a click that came before it ended looked lost for a long while: it was not), and a run of the guest stalled for minutes now and then.
A load of uttt.html that stalled with the frameset made and no request for the app frame happened, before the server was HTTP/1.1.

### 15.5 Netscape 2.02

One try each, nothing fixed (the next job's input).
frames.html (fr-ns2-1.png, fr-ns2-2.png) and uttt.html (fr-ns2-uttt.png): the page is fetched, then the app frame's page (`frames.html?cdomApp`), so the early script makes the frameset in Netscape 2 as well; no script file is requested (it has no SCRIPT SRC: A12), no alert appears (the inlined shim and the boot's second part are language="JavaScript1.5", which it skips), the title is the page's and the view stays blank, "Document: Done".
cdomBootWait gives up after 20 s without a message.
The first run of Netscape 2 and 3 after a kill shows the license agreement; Accept.

### 15.6 Still broken

- Netscape 4.08: the click on Pause during Play is lost on this machine, and Netscape 3.04's draws take tens of seconds; neither is fixed.
- Netscape 3.04 needs `application/x-javascript` for a script from the server.
- Not run: frames-body.html and Open, a typed code and the bad code on Netscape 3 and 4 (the first two also), and Play to its end on all three; IE 3 has no script.
- The inlined modern shim and the boot's second part are still sent to the frames tier (about 24 KB of a page).

## 16. One shim in JavaScript 1.0, one inline path (branch frames-js10)

Netscape 2.02 made the frameset and then showed nothing: it has no SCRIPT SRC (A12), so neither cdom.frames.js nor the app's .js1.js was ever requested (15.5).
This section makes the tier right for it by construction, and proves what Chrome can prove.

### 16.1 Predictions, written before any measuring

Seen before writing them: cdom.frames.js was 30,024 to 32,112 bytes minified (13 and the uttt README), uttt.wasm.ie9.js 19,819 bytes, and uttt.html 28,494 bytes; no .js1.js size and no app document has been measured.

- S1, the shim: tools/legacy/frames.js passes the js10 gate with only charCodeAt, fromCharCode and the mouse and capture hooks in its allowed list, and its minified size grows by at most 15% (at most 35,000 bytes), since the export wrappers and the lacking imports become declared functions instead of new Function.
- S2, what the js10 gate finds in today's frames.js: about 40 sites, mostly Array (about 15), typeof (about 8), split (about 5), new Function (2), join (1) and arguments-free code otherwise; no nested function, no mixed returns.
- S3, the app documents: tests/out/frames.app.html is at most 100,000 bytes and 30,000 gzipped (gzip -9); demo/uttt's out/uttt.app.html at most 120,000 bytes and 36,000 gzipped.
  The .js1.js module is the largest part of each, about twice cdom.frames.js.
- S4, the modern pages do not grow: frames.html and uttt.html are each at most their size on main, and I expect each to shrink by 50 to 300 bytes (the boot's first part no longer writes SCRIPT SRCs, and the early script names the app document instead of rebuilding the query).
- S5, blocks: at 30,000 bytes a block, frames.app.html has 4 to 6 script blocks and uttt.app.html 5 to 7; the largest top-level statement of each js1c output is under 30,000 bytes (falsifier: the build refuses a statement over the limit, and js1c would have to split functions).
- S6, the recursion: the deepest element nesting is about 8 in tests/frames.c and about 10 in uttt; the serializer's cf_node and cf_kids hold about 16 slots a level of the 512, so about 160 at uttt's depth: not near the limit for these two apps, but an app's depth is its own (a tree 30 deep would overflow), so the walk is made a loop with no JavaScript recursion, and that changes no drawn byte.
- S7, Chrome: tools/frames.mjs, tools/uttt.mjs, tools/js1.mjs, js10.mjs, js11.mjs and tests/lits pass through the app document; with document.onmousedown and onmouseup never firing, and no charCodeAt or fromCharCode (the Netscape 2 hooks this Chrome can take away), the frames page still counts clicks, takes field changes and draws once a click.
- What I expect to break first: the string table, since Netscape 2's `new Array(a, b, c)` drops its arguments and its split returns undefined, so cdomFramesStart's `table[3].split(...)` would fail there even once the files load; and any numbered table of the shim that is not filled 0, 1, 2 in order (cf_tok and cf_tid start at 1), since Netscape 2 reads a number key below the object's property count as its nth property.

### 16.2 The design

One shim, in JavaScript 1.0, and one inline path, for every browser of the tier (Netscape 2, 3 and 4, IE 3 and 4 alike).

- tools/legacy/frames.js is JavaScript 1.0: tools/legacy/build.mjs gates it with js10 on (tools/legacy/js11.mjs), on the source and on terser's output.
  What it names of Netscape 3 and later is behind a check, and listed in js11.mjs's FRAMES_ALLOWED: charCodeAt and fromCharCode (cf_hasAt, cf_hasFrom), String and Number (read as window.String and window.Number first: Netscape 2 has no String global), captureEvents, routeEvent and Event (Netscape 4), and the two button hooks, onmousedown and onmouseup, which it only assigns: where the browser fires no such event they are plain properties, cf_pressed never becomes 1, and the cf_LATER rule of section 6 works alone.
- What changed in the shim for Netscape 2: no Array (its `new Array(a, b)` drops the arguments and nothing has a length), so a list is a `new Object()` filled from 0 in order with its count kept apart (pair lists are a cf_Pairs of n and e); no split or join (they return undefined), so cf_split cuts by indexOf and substring, and the markup is written in pieces of about 1 KB that are joined in pairs (cf_joined); no typeof, so a string is told by its substring, a node by z, a function by its text, and the scroll and the caret by what the browser has (setSelectionRange for selectionStart); no new Function, so the page's export wrappers are a pool of 48 declared functions (cf_x0 to cf_x47) and each import the tier lacks is a declared function of its own; tables keyed by an id from C (nodes, masks, templates, slots, timers) are keyed by a letter and the id, since Netscape 2 reads a number key below the object's count of properties as its nth property; a mask word is read through cf_maskOf, since `undefined | x` dies; a field's kind comes from the tree, not from the field's type (JavaScript 1.1); and the doubles of cd_set and cd_call are read from their two words (cf_f64: the .js1.js memory has no load_f64, which was a bug on Netscape 3 and 4 too).
- The gate gained what it can check of Netscape 2: a function that returns a value on one path and none on another, and one that returns a value but does not end in a return (both "function does not always return a value." in Netscape 2.02 and 3.04), in every mode; in js10, the names String, Number, captureEvents, routeEvent, Event, onmousedown and onmouseup; and Object only as `new Object()`.
  Assignment in a condition, nested functions, typeof and void it refused before.
- No recursion: the serializer (cf_kids and cf_node), cf_textOf and cf_deep walk the tree by its parent links, and an opened element keeps what its children inherit and what closes it on itself (wi, wx, wc).
- The app document: `node tools/legacy/build.mjs app-doc <page.html> <app.js1.js> <page.app.html>` writes, beside each page, the document of its cdomApp frame: the boot's first part, the string table, the minified shim, the app's .js1.js and the page's own scripts (every script after the boot, gated as JavaScript 1.0 too), all inline, with no `<script src`.
  The early script (shim/boot-early.js) names it, for every browser of the tier: the build writes the page's name with .app.html for .html into it, and the frame's URL is that and the page's query (`frames.app.html?q`).
  The app's URL ops read the top window's location as before (cf_op_url_read).
  The boot's cdomBootFrames no longer writes SCRIPT SRCs: it keeps its arguments and calls cdomBootWait, which starts the shim at once, since the shim and the module are already there.
  The modern page carries none of it, and no longer grows.
- The string table: the page's `new Array(base, stamp, "U+2603", text)` cannot be read on Netscape 2, so the app document has four globals instead (cdomLitsBase, cdomLitsStamp, cdomLitsUtf8, cdomLitsText), and the text is assigned in statements of at most LITS_PIECE (4,096) bytes, cut between literals or inside one but never inside an escape.
  The build owns this, since the build writes the app document; tools/cdlits is unchanged.
- Blocks: every script is cut at top-level statement boundaries into `<script>` blocks of at most BLOCK_MAX bytes, a constant in tools/legacy/build.mjs, 30,000 for now, since Netscape 2's limit on one inline script is not known yet (a probe on the XP VM measures it).
  A statement over BLOCK_MAX stops the build, naming it; js1c's largest statement is 5,589 bytes (frames.wasm.js1.js) and 4,892 (uttt.wasm.js1.js), and its data strings are 4,096 characters at most already, so js1c needs no change.
  A block that holds `</script` or `<!--` stops the build too.
- cdom.frames.js is still built by tests/build.sh (its size is measured), but no page loads it; demo/uttt no longer builds it.
- tools/cdp.mjs's ?ns2 flavour of LEGACY=frames is what of Netscape 2 this Chrome can take away: Netscape 4's fields (no selectionStart), no mouse button or key event reaching the page, no charCodeAt or fromCharCode, no type on a field, and no String or Number global.

### 16.3 Results

Measured in headless Chromium (Playwright's, build 1243) on a Linux x86-64 VM, under LEGACY=frames; gzip -9; main is origin/main at fc7093f, built the same way.

| | Predicted | Measured | Verdict |
| --- | --- | --- | --- |
| S1, the shim | passes js10 with the hooks allowed; at most 35,000 bytes minified | passes, with FRAMES_ALLOWED; 40,148 bytes, 11,929 gzipped (main 32,033 and 10,381): 25% and 15% larger | refuted (size) |
| S2, what js10 found in main's frames.js | about 40 sites: Array about 15, typeof about 8, split about 5, new Function 2, join 1 | 49: Array 21, Object 9 (as `new Object()`, which the gate then refused everywhere), typeof 9, split 7, Function 2, join 1; no nested function, no mixed returns | about right |
| S3, the app documents | frames at most 100,000 and 30,000 gzipped; uttt at most 120,000 and 36,000 | frames.app.html 83,380 and 20,687; uttt.app.html 83,879 and 23,304 | confirmed |
| S4, the modern pages | each at most main's, 50 to 300 bytes smaller | frames.html 86,791 (main 87,100: 309 smaller), uttt.html 28,975 (main 29,286: 311 smaller); gzipped 27,120 and 11,514 (main 27,240 and 11,619) | confirmed that they do not grow; the shrink is 9 and 11 bytes over the range |
| S5, blocks | frames 4 to 6, uttt 5 to 7; js1c's largest statement under 30,000 | 3 each (frames 29,352, 29,600 and 24,284 bytes; uttt 29,030, 29,943 and 24,741); largest statements 5,589 and 4,892 | refuted on the count, confirmed on the statements |
| S6, the recursion | depth about 8 in tests/frames.c and 10 in uttt; about 16 slots a level | the deepest element under the mount is 3 levels in tests/frames.c (48 nodes) and 11 in uttt (333 nodes); see 16.4 | wrong on frames.c, right on uttt |
| S7, Chrome | the suites pass through the app document, and the ?ns2 page works | they do (16.5) | confirmed |
| Expected to break first | the string table and the numbered tables | the table (new Array and split) and the timers (cf_tok from 1) were rewritten before any run; nothing else broke in Chrome | not testable in Chrome beyond that |

What the misses say:

- S1: the pool of 48 export wrappers and the 34 declared functions for what the tier lacks are about 7 KB minified of the 8 KB; new Function made each at run time before.
  A build that generates them would not make the file smaller, only the source; renaming the globals (13's P1) is still the way to a smaller shim.
- S5: the shim and the module minify better than I took them to; the app documents' 83 KB fits in 3 blocks of 30,000.

### 16.4 The recursion estimate

Netscape 2's value stack is 512 slots of 16 bytes, and a frame holds the function, its arguments and (I take it, unmeasured) its locals and the temporaries of the expression it is in; docs/descent.md measured 2, 3 and 5 slots for frames of 1 and 4 arguments with no locals.
Before this branch the serializer recursed twice a level: cf_node (the function, 3 arguments, 6 locals: 10) and cf_kids (the function, 3 arguments, 1 local: 5), and each call pushes its arguments (about 4) first, so between 6 slots a level (arguments alone) and about 23 (locals too).
At uttt's depth of 11 that is 66 to 250 slots, on top of the handler's chain (cdomF.k, cf_onClick, cf_release, cf_draw, cf_html: about 30); at the worst, half the stack for an app of 11 levels, and an app 25 levels deep would overflow.
cf_textOf recursed once a level (called from inside a call into C, under js1c's own frames), and cf_deep once a level (cd_clone).
So all three are loops now: the deepest chain of the shim's own calls while drawing a tree 60 deep is 5 (tools/frames.mjs wraps every cf_ function to count it), whatever the depth.
What is left on the stack is js1c's: its direct functions nest at most its printed "JS depth bound" (7 for frames.wasm, 10 for uttt.wasm), each frame holding its arguments and locals, plus the imports' few frames under them; that is js1c's to bound (docs/js1-compile-design.md).

### 16.5 Tests

- tools/frames.mjs (LEGACY=frames): the early script names frames.app.html; the app document's scripts are plain `<script>` blocks, none with a src, none over BLOCK_MAX (read from build.mjs), holding, in order, the boot, the table, the shim, the module and the page's own script, and the page holds neither the shim nor the module; a string table of 71,340 bytes (the real one joined to itself and a long literal of escapes) goes through app-doc and, run in node, is the page's four values again, cut into 19 statements over 3 blocks; the app frame is frames.app.html with the page's query, and the app reads the top window's path, not the frame's; a tree 60 deep is written, copied and read with the shim's calls nested 5 deep; and first of all, on ?ns2, the page boots with no charCodeAt, fromCharCode, String or Number, a click is drawn within 500 ms with cf_pressed still 0, a field's change (with an é, through escape) reaches C, and nothing is noted.
  97 checks pass.
- tools/uttt.mjs: the webassembly, frames, frames-ns4 and (new) frames-ns2 profiles pass, through uttt.app.html.
- tools/js11.mjs: the new rules on planted code, and frames.js before and after terser as JavaScript 1.0.
- tools/js1.mjs, tools/js10.mjs, tests/lits/run.sh, tools/seeds.mjs, LEGACY=1, ie9, ie6 and below tools/legacy.mjs (the tier files: no tier fetches cdom.frames.js or a .js1.js now) and tools/legacyfresh.mjs (which also gains app-doc, and copies shim/boot-early.js, which it lacked on main and so failed there) pass.
- tools/mutate.py: the mutants of this section are in the PR, each with whether it was caught.

### 16.6 What the VM job must check in Netscape 2.02

1. frames.html and uttt.html?<code>: the frameset is made, and the cdomApp frame requests frames.app.html (uttt.app.html) with the page's query, and nothing else: no script file at all.
2. No alert: a syntax error alert names a block of the app document by its line, so the line says which part (the boot, the table, the shim, the module, the page's script) broke; LEGACY_PRETTY=1 makes the shim one statement a line for that.
3. A block of 30,000 bytes parses: if one is refused (an alert, or a part of the document's scripts silently missing: cdomFramesStart or cdomFactory undefined), BLOCK_MAX is the one constant to lower, and the probe's measured limit is the value.
4. The view shows the heading, the count, the button and the fields (frames.html), the board (uttt); "inc" adds one; a typed name echoes once the field is left; > and < step the game.
5. What this design assumes of Netscape 2 and Chrome cannot check: that `undefined == null` (the shim compares missing properties with null, as before); that `"" + f` of a function starts "function"; that a number key read on an object with only number keys filled from 0 in order is that key; that `escape` gives a %XX for every byte above 127 (cf_code without charCodeAt); that document.forms[0].elements[k] is the kth control in the markup; that setting document.onmousedown is harmless; that `parent.cdomApp.cdomF.k(n)` from a javascript: URL and an onClick reaches the app frame; and that a document of 83 KB of script in 3 blocks is not over some other limit (atoms, a script's total size, memory).
6. History: each redraw is one more entry on Netscape 2 (A3), as before.

### 16.7 The mutants

tools/mutate.py ran every mutant whose file this branch touched or whose test is tools/frames.mjs, tools/js11.mjs, tools/legacyfresh.mjs or tests/lits/run.sh: 115, of which 108 were caught at the first run.
The seven misses were expectations that moved, and one hole: frames-url-own (the app reading its own frame's location) was no longer caught, since the app document now has the page's query too; tools/frames.mjs now checks the path the app reads (/frames.html, not /frames.app.html), which catches it.
Of the other six, four now fail on a message this branch renamed or on the build refusing first (frames-boot-no-wait, js10-allowed-members-ignored, frames-lits-one-statement, lits-array-literal: the app document cannot read a table that is an array literal), one on the static check of the early script (frames-early-query-dropped), and app-doc now finds the boot's second part by its text, not its tag, so frames-boot2-tag-plain reaches the test that is meant to catch it.
All 115 are caught.

## 17. Netscape 2.02 plays a uttt.live replay (branch ns2-uttt, 2026-11-21)

The runs are in the real Netscape 2.02 of the XP VM (QEMU on this Mac), through tools/vmprobe.sh and tools/vmopen.sh, with Netscape 3.04, 4.08 and IE 4.01 as controls where it fails.
The milestone is demo/uttt's `uttt.html?AJG7NYNQG4RFXXGVPGD7QR2ETSOYTWMCREAQ` in it, as a screenshot and a video.

### 17.1 Predictions, written before the first run

Known before writing them: docs/descent.md measured, in this guest, a rewrite of a 5 KB view at 1.5 s wall, 150 KB of inline script loading in 2.4 s, a limit of about 65,536 names ("too many atoms"), and the engine's clock at a third of the wall clock.

- N1, what breaks first: not the load (the blocks are 30,000 bytes and the limits probe passed them) but the first run of the shim, on something section 16.6 item 5 lists as assumed.
  My order of likelihood: the string table (the app document's four globals, assigned in 4,096-byte statements), then a number key read as the nth property, then `"" + f` for a function, then the first click reaching `parent.cdomApp.cdomF.k`.
  I expect at least two failures before the board is drawn, and one of them an alert naming a line of a block of the app document.
- N2, atoms: uttt.app.html's names (the shim's, the module's memory and function names, the strings of the table are values, not names) are under 65,536; falsifier: "too many atoms" in the app document.
- N3, load time, from the GET of uttt.html to the board drawn (the mark of the first draw): 6 to 15 s wall (the frameset, the app document of 84 KB in 3 blocks, the start of the module, the first redraw of about 230 nodes, in pieces of 1 KB).
- N4, the first draw is the costly one: the board is about 6 KB of markup (333 nodes), 1.5 s a 5 KB view in the earlier probe, so 2 to 5 s, more than the 5 KB probe since the tables nest.
- N5, per move: one redraw of the board, 2 to 6 s wall of which most is Netscape's layout; a move every 600 ms in Play is then far faster than the guest can draw, so Play runs at the pace of the draw, not of the timer, and 20 moves take 1 to 2 minutes.
  Falsifier: a move under 1.5 s (the probe's own floor for 5 KB), or over 10 s.
- N6, the typing trap: the `u` of the URL is dropped or doubled by vm.py's sendkey path, not by the guest's Run dialog; the cause is in tools/vmopen.sh's or vm.py's timing or key names, and a fix there is under 30 minutes.

### 17.2 What the runs found

The runs are of 2026-11-21 in the XP VM, driven by a small agent in the guest (tools/vmagent/agent.c, started once as C:\a.exe, polling tools/vmagent.py on the Mac), so that nothing is typed into the guest; tools/vmns2.sh opens a page in Netscape 2.02 through it, and tools/vmjs.sh and Open Location run `javascript:alert(...)` in the page to read its state.
The screenshots are /Users/alex/VMs/ie6-xp/shots/ns2u-*.png.
Every failure below was found by a probe page in tests/out (lex*.html, written for the purpose), not by reading the engine.

Failures, in the order they came, each with its cause, its owner and its test:

1. `is not a numeric literal` alert (the first run: frames.app.html "line 1028"; the line number is not a line of the document, and I could not explain it).
   Dropped theory, from reading the document: the literal `1e308 * 10` (the lexer taking an exponent); it was changed to a loop, then found innocent and reverted (probe lex5: `1e308` reads back as 1e+308, and `1e308 * 10` as 1.#INF).
   Cause, bisected with probes lex2 to lex4: Netscape 2's `"".indexOf(t)` gives no number at all (it prints as nothing), and any comparison of it raises that alert.
   The shim called indexOf on empty strings (an empty field value, `location.search`), as did tests/frames.html's own script.
   Owner: tools/legacy/frames.js, whose only indexOf calls are now cf_idx and cf_idxFrom; tests/frames.html reads ("x" + location.search).
   Tests: tools/cdp.mjs's ?ns2 flavour makes an empty indexOf throw; tools/frames.mjs counts the indexOf calls (2); mutants frames-idx-raw and frames-idx-no-empty-guard, both caught.
2. A blank view and no alert: `cdomBootWait` gave up after 400 tries, though `window.cdomFramesStart` and `window.cdomFactory` were defined.
   Cause (probe lex5): Netscape 2 counts a function as false in a condition (`if (g)` false, `!g` true, `g != null` true, `g && g` false; objects and numbers are fine).
   Owner: shim/boot.js and tools/legacy/frames.js (a dozen tests of a function or a method: `el.focus`, `el.setSelectionRange`, `factory`, `cf_raw.cd_lits`, `d.captureEvents`, ...), now compared with null; the js10 gate refuses a declared function, a method of the tier's list or a builtin as a bare condition or operand of `!`, `&&` and `||` (tools/legacy/js11.mjs, planted cases in tools/js11.mjs); mutant frames-function-in-condition, caught.
   Chrome cannot make a function false, so the gate is the test.
3. The board drew and `>` worked once; the next clicks did nothing.
   Evidence: a trace in cf_onClick showed one click reaching the shim (`D k5 D`).
   Cause (not proven, a fix that works): rewriting the view from inside the click's own handler leaves the frame deaf to later clicks; the same draw from a timer is fine.
   Owner: frames.js: on Netscape 2 (no fromCharCode) a click's draw goes through cf_release's `later`, from a timer cf_LATER (250 ms) after the handler (cf_deferClick).
   Tests: tools/frames.mjs (?ns2: a synchronous click() draws nothing until the timer, then once), tools/uttt.mjs waits for it; mutant frames-click-draws-in-handler, caught.
   After it, 4 and then 7 further clicks all worked.
4. Play drew the first tick's label and then stopped.
   Cause (probe lex7): Netscape 2 reads -2147483648 and 2147483648 as equal to null (its sentinel for no value); cdom's own timer tokens start at 2^31, so `cf_tok["t" + n] == null` took a pending timer for fired (cf_onTimer returned at once).
   Owner: frames.js: a token is kept as the string "k" and the token; tools/frames.mjs checks the source (Chrome has no such ==); mutant frames-timer-token-bare, caught.
5. "Lengthy JavaScript still running. Continue?" every dozen moves of Play (three times in one game).
   Not fixed: it is Netscape 2's own runaway-script guard, and each tick is a long chain of its interpreter; OK continues, and tools/vmfilm-uttt.sh answers it (it is in the video).
6. Not fixed: changing Options (toolbar, location, directory buttons) while the page is up resizes the window and the frames reload, after which the view shows the app document's script as text.
   The options were set before the page was opened and saved (Options, Save Options), so this guest's Netscape 2 now starts with no toolbar and no directory buttons; the Location bar is still on.
7. Start hangs: about half of the starts stop with "Waiting for reply" and a grey view; the server has seen the page's GET and often not the app document's, so it happens before the frames; it is not the app.
   tools/vmns2.sh detects it (the view's pixel, the server's log) and starts again.
   Two runs of the page without a heading hung after both GETs; it was never seen with a heading in six; the heading is back (a guess, unproven).
8. Layout, not a failure: the 640 x 480 screen showed the board only down to its second row, so the page lost its h2 for a small heading, the buttons and the move count moved above the board, and the blocks are 66 pixels (demo/uttt/uttt_replay.c); the whole board and the counter are in view with the toolbar off.

The typing trap (a `u` in the URL): not reproduced.
`C:\NS202\Program\netscape.exe http://10.0.2.2:8110/r1/uttt.html?AJG7...` typed through vm.py into Run opened the page (shots/ns2u-type3.png), so vm.py's key mapping was not changed; the agent made the question moot.
The agent needed a .vbs to be installed: `mshta vbscript:Execute(...)` with XMLHTTP is refused ("Permission denied"), so three typed lines wrote C:\d.vbs, which fetched a.exe.

### 17.3 The checklist of 16.6, in Netscape 2.02

1. frames.html and uttt.html?<code>: the frameset is made, the app frame requests the app document with the page's query, and no script file (the server's log: the page and the app document only).
2. No alert, after failures 1 to 4 were fixed.
3. The blocks of 29,605, 29,645 and 24,292 bytes (frames.app.html) and of uttt.app.html parse; BLOCK_MAX stays 30,000.
4. frames.html: heading, count, button and fields; "inc" adds one; a typed name echoes once the field is left (the trace `f41 c4 f40 ...`: focus, change, blur reach the shim).
   uttt: the board, `>` (and `<`, `|<`, `>|` as the video's last frames show), and Play to the end of the game: "Move 50 of 50. O wins."
5. The assumptions: `undefined == null` holds (lex5); `"" + f` was not needed; number keys, escape, forms[0].elements[k] and `parent.cdomApp.cdomF.k(n)` from onClick and a javascript: URL work (frames.html and uttt run); setting document.onmousedown is harmless; 83 KB of script in 3 blocks is not over a limit (no atoms alert).
   Not run: the Open button with a typed code, and a bad code.
6. History: not looked at.

The controls (Netscape 3.04, 4.08, IE 4.01) were not needed: each failure was found with probe pages in Netscape 2 itself.

### 17.4 Predictions against the runs

| | Predicted | Found | Verdict |
| --- | --- | --- | --- |
| N1, what breaks first | the string table, then number keys, `"" + f`, the click | none of them: `"".indexOf` (1), a function as false (2), the click redraw (3), INT_MIN == null (4) | refuted: four engine quirks that 16.6 item 5 did not list |
| N2, atoms | under 65,536 | no alert | confirmed |
| N3, load time | 6 to 15 s from GET to the board | 15 to 20 s when the Mac is otherwise idle (the detector polls every 5 s, minimum 15 s); 25 to 30 s under load | refuted by a little |
| N4, the first draw | 2 to 5 s | not separated from N3 | not measured |
| N5, per move | 2 to 6 s a step; Play at the pace of the draw, 20 moves in 1 to 2 minutes | 1.8 s a step on an idle Mac, 4.4 to 5.4 s while other jobs ran (tap to the counter changing, polled twice a second); Play 33 changes in 98 s, about 3 s a move, plus three dialogs; the whole game in about 2 minutes | confirmed |
| N6, the `u` trap | in vm.py's path, fixed there in under 30 minutes | not reproduced | refuted |

### 17.5 The screenshot and the video

/Users/alex/VMs/ie6-xp/shots/uttt-ns2.png: Move 34 of 50, Netscape 2.02's title bar and menu.
/Users/alex/VMs/ie6-xp/shots/uttt-ns2.mp4: 1280 x 960, 136 s, the real pace: the desktop, the page opened, the board, four `>`, Play, three "Lengthy JavaScript" dialogs answered, to "Move 50 of 50. O wins."
Method: QEMU screendumps (tools/vmfilm.py, about six a second, a frame equal to the last dropped), each shown for the real time to the next, assembled by ffmpeg (nearest neighbour, 2 times); macOS's screencapture was not used because the QEMU window is a full-screen Space that is not on screen.
Frames from the start, the middle and the end were extracted and looked at.
