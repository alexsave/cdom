# SVG through VML on the fourth tier (IE 5 to 8)

Alex asked on 2026-11-20 for the follow-up docs/ie6-design.md left open: draw SVG in IE 5 to 8, through VML.
This is the design Alex approved, worked out, with the predictions written before anything was measured.
The W3C VML note (https://www.w3.org/TR/NOTE-VML) could not be fetched from the cloud machine this was written on (www.w3.org is outside its network allowance), so the VML facts below are from what is documented elsewhere and what VML libraries of the time relied on (Raphael 2, excanvas), and each is listed under "Unverified" until real IE has run it.

## 1. Where it sits

The app does not change: C keeps emitting SVG with cd_open("svg") and cd_attr.
On every tier but the fourth, the shim makes the SVG elements with createElementNS, as before.
On the fourth tier, cdom.legacy-ie6.js, the shim's createElementNS is already rewritten to cdomOld.createElementNS (tools/legacy/es3.mjs, OLD_DOM).
Where the document has no createElementNS (IE 5 to 8), that call now hands the SVG tag to tools/legacy/vml.js (cdomVml), which makes the VML node in its place.
The shim's setAttribute is already rewritten to cdomOld.setAttribute; on a node cdomVml made, that hands the attribute to cdomVml.attr, which maps it to the VML node.
So the translation happens as the subtree is built, and later cd_set_attr calls reach the same mapping.

vml.js is one more part of cdom.legacy-ie6.js (tools/legacy/build.mjs shim-ie6), after dom-old.js, and passes the same ES3 and JScript 5.0 gates as the other parts.
cdom.legacy.js, cdom.legacy-ie9.js, shim/cdom.js, shim/cdom.min.js and every app build do not change (B1 below): IE 9 has native SVG and keeps it.

## 2. The mapping

Coordinates are baked, the way Raphael 2 drew in VML: every shape is a v:shape whose box is 1 px square at the svg's top left (style position:absolute;left:0;top:0;width:1px;height:1px), with coordsize "100,100" and coordorigin "0,0".
So one VML path unit is 1/100 px (K = 100), and the path's whole numbers are the points' pixel positions times 100, rounded.
The pixel position of a point is the shape's current transform matrix (CTM) applied to it: the svg's viewBox mapping, then each g's transform, then the shape's own, all translate and scale, so the CTM is always [sx 0 0 sy tx ty].
Bézier curves and lines keep their shape under such a map, so converting in user space and mapping the points is exact.
The svg and each g are spans, not v:group elements (section 9 says why): the svg's span is its box, W by H px with overflow hidden, and a g's span holds its shapes at the svg's top left, since every point is already baked through the CTM.

| SVG | VML (cdomVml) |
| --- | --- |
| `<svg width height viewBox>` | `span`, style position:relative;display:inline-block;overflow:hidden;width:Wpx;height:Hpx; the viewBox sets the CTM of everything inside (preserveAspectRatio's default, xMidYMid meet); no width or height is 300 by 150, as in a browser |
| `<g transform>` | `span`, style position:absolute;left:0;top:0; its translate and scale go into the CTM of what it holds |
| `<path d>` | `v:shape` with `path` from cdomVml.path(d, CTM) |
| `<rect x y width height rx ry>` | `v:shape`, the rect as a path (rounded corners as arcs) |
| `<circle cx cy r>`, `<ellipse cx cy rx ry>` | `v:shape`, two arcs |
| `<line x1 y1 x2 y2>` | `v:shape`, m and l, never filled |
| `<polyline points>`, `<polygon points>` | `v:shape`, m and l (and x for the polygon) |
| `fill` | `filled` (false for none) and `fillcolor` |
| `stroke` | `stroked` (false for none, the default) and `strokecolor`; the shape's `v:stroke` has SVG's defaults, joinstyle miter, miterlimit 4 and endcap flat (VML's are round) |
| `stroke-width` | `strokeweight`, in px: the width times the CTM's scale (the square root of sx times sy) |
| `fill-opacity`, `stroke-opacity`, `opacity` | `opacity` of the shape's `v:fill` and `v:stroke` children: fill-opacity (or stroke-opacity) times every opacity from the shape up |
| `id` | the VML node's id |

fill, stroke, stroke-width, fill-opacity and stroke-opacity are inherited from the nearest svg or g that sets them, as SVG's presentation attributes are.
A colour is #rgb (written out as #rrggbb), #rrggbb, rgb(r, g, b), or a colour name, which VML takes as IE's CSS does.

Path data: every SVG command, absolute and relative, with implicit repeats (extra pairs after M are lines).

| SVG | VML |
| --- | --- |
| M, m | m |
| L, l, H, h, V, v | l |
| C, c, S, s | c (S's first control point is the last C or S's second reflected, or the current point) |
| Q, q, T, t | c (each quadratic raised to a cubic: controls at two thirds toward the quadratic's control) |
| A, a | c, one cubic per quarter turn or less (the SVG endpoint-to-centre conversion, radii scaled up when too small, zero radii a line) |
| Z, z | x, then m back to the subpath's start when more follows that is not a move |
| (end) | e |

A malformed path draws what came before the error, as SVG says.
cdomVml.path(d, sx, sy, tx, ty) is a pure function of the string and the four numbers (tools/vml.mjs tests it in node).

## 3. What is not drawn

Elements: text (and tspan), linearGradient, radialGradient, stop, clipPath, mask, pattern, filter, marker, use, defs, symbol, image, foreignObject, and any tag not in the table above.
Each is made as a span with display:none, so it and everything in it draws nothing (a path in a defs is not drawn either, as SVG would not draw it there).
Attributes: a transform with rotate, skewX, skewY or matrix, a negative or zero scale, clip-path, mask, filter and marker-start, marker-mid, marker-end hide the node they are on (display:none) while they are set.
A paint that is not a colour (url(#gradient), currentColor, rgba(), hsl()) draws no fill or stroke.
Each kind is reported once, the first time it is met: an Error through window.reportError (the polyfill's, which reaches window.onerror), with a message naming the kind; cdomVml.lost lists the kinds met.
Nothing throws.
Also not done: SVG's nonzero fill rule (VML fills even-odd), stroke-linecap, stroke-linejoin, stroke-miterlimit and dashes (the defaults are drawn), and preserveAspectRatio other than the default.
Templates (cd_tpl) holding SVG are cloned by cloneNode, whose copy has no cdomVml state; an attribute set on such a copy later is mapped as if it were the first.

## 4. VML setup

The first VML node made sets the page up once, as real IE 6 needs it (section 9):

- document.namespaces.add("v", "urn:schemas-microsoft-com:vml"), with two arguments, where document.namespaces exists (IE 5.5 to 8), before any v: node is made;
- and a stylesheet rule a VML tag, `v\:shape`, `v\:fill` and `v\:stroke`, each `behavior: url(#default#VML)` (document.createStyleSheet's addRule): one rule a tag, since IE 8's standards mode ignores `v\:*`.

Nodes are made with document.createElement("v:shape"), "v:fill" and "v:stroke", and every VML property is set as a property of the node (node.path = ..., as Raphael does).
A shape's attributes are set before the shape is in the document: the shim builds an svg subtree off the document and places its root when it closes, so every node of a newly built svg has its VML properties before IE attaches the behavior; IE 6 takes them up then (measured, section 9).
A later change (cd_set_attr) sets the properties on the live node.
Moving or re-attaching a node inside an svg (cd_move, cd_attach) goes through the shim's place(), which on this tier (tools/legacy/shim-old.js) redraws what it moved, since its CTM and inherited paint can change with its parent.
Removing or replacing the svg needs nothing: the VML nodes are DOM nodes, removed as any other.

## 5. Tests

- tools/cdp.mjs, LEGACY=ie6: a VML stand-in in the IE6_OLD profile: document.namespaces.add and document.createStyleSheet's addRule recorded in window.__vml, and every document.createElement of a v: tag recorded too (Chrome makes an unknown element of it, which keeps the properties set on it).
  It models what real IE 6 did (section 9): a v: node made before the namespace, or with no behavior rule for its tag, and any v:group, go in __vml.bad, which the test wants empty; after a namespaces.add with an implementation, a v: node keeps IE's defaults whatever script sets on it.
- tests/legacy.c's svg() builds an svg (in an absolutely placed div, clear of the test's pad) with every element of the subset, a g with translate and scale, inherited paint, opacities, and the unsupported text, defs, linearGradient and a rotate; svg_set() changes a path's d and a rect's fill after the build, and the g's transform while the path in it is detached, then attaches it again; svg_drop() removes the svg; svg_many(n) builds n paths, for the timing.
- tools/legacy.mjs, LEGACY=ie6: each element became its v: node with the path string, coordsize, coordorigin, size, colours, weights and opacities it should have; the unsupported kinds reported once each and their nodes hidden; the later change reached the VML node; the removal left nothing; no SVG element was made.
  On the other tiers: the same svg is real SVG (the SVG namespace), and nothing reported.
- tools/vml.mjs, in node (vml.js loaded through new Function, not a vm context, whose global lookups are slower): cdomVml.path against hand-checked strings for every command, absolute and relative, implicit repeats, S and T reflection, Z's move back, a malformed path, the CTM; and arcs against points sampled on the true arc.
- Each test's mutants are in tools/mutate.py (vml-*).

## 6. Predictions

Written before any of it was measured.

- B1: cdom.legacy.js, cdom.legacy-ie9.js, shim/cdom.min.js and every app's .wasm, .wasm.js and .wasm.ie9.js are byte-identical before and after this change (the legacy test app aside, whose C changed).
  Falsified by any byte that differs.
- S1: vml.js and its hooks add 4,000 to 8,000 bytes to cdom.legacy-ie6.js minified (docs/ie6-design.md's P3 measured the file at 43,317 bytes, 14,062 brotli, before the IE 5 work).
  Falsified outside that range.
- S2: they add 1,500 to 3,200 bytes with brotli -q 11.
  Falsified outside that range.
- A1: every arc converted by cdomVml.path lies within 0.0003 r + 0.01 px of the true arc (r the larger radius): the quarter-turn cubic's own error is 0.027% of the radius, and rounding to 1/100 px adds at most 0.0071 px.
  Falsified by a sampled point of the true arc farther than that from the converted curve, on any of the test's arcs.
- T1: in node, cdomVml.path converts 100 to 2,000 paths per ms, each the 12-command path of tools/vml.mjs's timing case (lines, cubics, a quadratic and one arc).
  Falsified outside that range.
- T2: in headless Chrome under LEGACY=ie6, C building paths with svg_many (each a path with d, fill and stroke, through the wasm2js words app, the ES3 shim and cdomVml into the VML stand-in) runs at 20 to 300 paths per ms.
  Falsified outside that range.
- T3: the same build of 1,000 paths takes 1.0 to 5.0 times as long under LEGACY=ie6 (VML) as under LEGACY=ie9 (the same app file, real SVG).
  Falsified outside that range.

Timings here are indicative: a Linux x86-64 cloud machine, not Alex's Mac, and Chrome's VML stand-in is not IE's VML, which is known to be slow with thousands of shapes.

## 7. The predictions, measured

Measured on 2026-11-20 on a Linux x86-64 cloud machine (Intel Xeon at 2.10 GHz, 4 vCPUs), Chrome for Testing 153.0.8010.12 headless, node v22.23.5, clang 23.1.6, binaryen 130.
The baseline is origin/ie5 at 8198cfe, built with the same tools.
Every timing is indicative only.

- B1 confirmed: of the 58 files tests/build.sh writes to tests/out, the only ones that differ from the baseline's are cdom.legacy-ie6.js and the legacy test app's own (legacy.wasm, legacy.simd.wasm, their .lits, legacy.wasm.js, legacy.wasm.ie9.js, legacy.html, legacy.shim.html), whose C and page changed.
  cdom.legacy.js, cdom.legacy-ie9.js, every other app's .wasm, .wasm.js and .wasm.ie9.js, shim/cdom.js and shim/cdom.min.js are byte-identical.
- S1 refuted: cdom.legacy-ie6.js went from 45,970 to 55,008 bytes minified, 9,038 more, above the 4,000 to 8,000 predicted.
  (The baseline is the IE 5 branch's file: P3's 43,317 grew to 45,970 with the IE 5 work.)
  vml.js minified on its own is 9,009 bytes: the converter, the arc, the five shapes' geometry and the colours are about twice the code the prediction allowed for.
- S2 refuted: 14,819 to 18,217 bytes with brotli -q 11, 3,398 more, just above the 1,500 to 3,200 predicted (gzip -9: 16,465 to 20,193).
- A1 confirmed: over the nine arc cases of tools/vml.mjs (quarter, half, large, rotated, negative sweep, small arc of a big circle, nearly a whole turn, under a CTM, radii too small), the converted curve is never farther from the true ellipse than 63.3% of its tolerance; the worst is 0.019 px on a 90 by 30 ellipse (tolerance 0.037).
- T1 refuted as first measured, then within range: the first run gave 52 paths per ms, under the 100 to 2,000 predicted.
  That run loaded vml.js into a node vm context, where every global lookup (Math, String, parseFloat) goes through the context's proxy; the same function loaded through new Function, as tools/vml.mjs now does, converts 171 to 188 paths per ms (three runs, each the median of five runs of 20,000).
  The prediction stands as refuted on its first measurement; the second number is the converter's own speed.
- T2 confirmed: C building 1,000 paths through cdomVml under LEGACY=ie6 runs at 45.5 paths per ms timed one build at a time (22 ms, the clock's millisecond), and 56 to 66 paths per ms timed over 20 builds a run (15.3 to 17.8 ms a build), inside the 20 to 300 predicted.
- T3 refuted: the same 1,000 paths take 15.3 to 17.8 ms under LEGACY=ie6 (VML) and 1.2 to 1.75 ms under LEGACY=ie9 (real SVG, the same app file), 9 to 15 times as long, above the 1.0 to 5.0 predicted.
  The VML build makes three elements a path (the shape, its v:fill and v:stroke), parses a style per shape, and draws each shape again for each paint attribute set; the path itself is converted once (the drawing is keyed on its geometry and CTM).
  This is Chrome's cost of the translation into a stand-in; IE's own VML rendering, which the stand-in does not do, is the known slow part and is measured only in real IE (section 8).

Mutants: tools/mutate.py's 34 vml-* mutants are each caught by the check written for them.
The first run missed three, and each showed a gap now closed: the repeat-after-move branch was dead code (the move itself turns the command to a line, so that branch is gone and the mutant breaks the move's line instead), no case had a number ending at a second point (1.5.5 is now one), and the sweep mutant named a case whose sweep is 0 (it now names the sweep 1 one).

## 8. Verifying in real IE

Chrome's stand-in checks the VML the shim builds, not what IE draws.
In the XP VM (docs/ie5-design.md, "Driving the XP VM"):

1. Build: `tests/build.sh` (tests/out/legacy.html, cdom.legacy-ie6.js, legacy.wasm.ie9.js), and serve tests/out as for the IE 6 runs (`python3 /Users/alex/VMs/ie9-win7/serve.py <worktree>/tests/out 8099`).
2. IE 6: Start, Run, `"C:\Program Files\Utilu IE Collection\IE600\iexplore.exe" http://10.0.2.2:8099/legacy.html?svg`.
   ?svg makes the page call the app's svg() once it starts.
   Expected, in a 200 by 100 px box at 300 px from the left and 120 px from the top of the page (the viewBox doubles every coordinate):
   - a red rectangle from (20, 20) to (60, 40) px with a 4 px blue outline (the path);
   - a half-transparent mid-blue rectangle with rounded corners from (80, 10) to (120, 30) (the rect);
   - a green circle outline, radius 10, centred at (160, 20) (the circle);
   - a black ellipse 32 by 16 centred at (20, 80) (the ellipse);
   - a faint grey line from (40, 60) to (80, 90) (the line, black at 25%);
   - an open purple V through (100, 60), (120, 90) and (140, 60) (the polyline);
   - a pale blue-grey triangle at 25% through (160, 60), (180, 90) and (150, 90) (the polygon);
   - a dark navy triangle through (10, 12), (30, 12) and (30, 32) with a 12 px red outline (the path in the g);
   - no text, and nothing for the rotated or the gradient-filled rect.
   IE then reports six script errors, one per kind not drawn (`cdom: SVG <text> is not drawn in VML ...`, then defs, linearGradient, stop, transform rotate, paint url), in the status bar or as dialogs; that is the report path, not a fault.
3. IE 5.01 and IE 5.5: the same page, through the test hold (docs/ie5-design.md, section 2): run `python3 tools/ie5hold.py`, copy tests/out/legacy.html to legacy-hold.html with `<img src="http://10.0.2.2:8096/x.gif" width="1" height="1">` added, and open `legacy-hold.html?svg` from Run with IE501 or IE550.
   The Utilu IE 5.01 draws no VML at all, not even a static page's (section 9), so there the run checks only that the page runs and reports what it does not draw.
4. IE 7 and IE 8: the IE 9 VM in IE 7 and IE 8 document modes, a copy of legacy.html with an X-UA-Compatible meta, as for P5 in docs/ie6-design.md, opened with ?svg.
   IE 8's standards mode is where VML is most particular (section 9).
5. Speed: `legacy.html?svgmany=N` (with or without ?svg) builds N paths of the timing case in a 400 by 400 box at (520, 120) and puts the times in the window title: the build in ms, then the ms until a timeout could run, which in IE includes some of the layout.
   Run N = 100, 1,000 and 3,000 in IE 6, and for IE 5.01 the hold copy; note the times, and whether 3,000 raises the slow-script prompt.

## 9. What real IE 6 and IE 5.01 showed

Run on 2026-11-20 in the XP VM (QEMU, IE 6.0 and the Utilu IE 5.01), first on small static pages, one question each, then on `legacy.html?svg`.

The branch as the cloud machine wrote it drew nothing in IE 6: the six reports fired and the v:shape nodes had their path, but no shape drew, the svg's group measured 120 by 120 in place of 200 by 100, the group had two coordsize attributes (200,100 and 1000,1000), and a `<?import namespace="v" implementation="#default#VML">` sat in the div.
What each probe showed:

- `document.namespaces.add("v", URN, "#default#VML")`, the three-argument form, imports the behavior into the namespace, so it is bound as each v: node is made; every property script sets after that is lost, whether before or after the node is in the document (a rect drew white with IE's default outline; a shape drew nothing; coordsize stayed beside the default 1000,1000).
  That was the bug.
- The two-argument `namespaces.add("v", URN)` with a stylesheet rule giving `behavior: url(#default#VML)` draws, with the properties set before or after the node is placed, and with the rule written `v\:*` or one a tag.
  vml.js now does that, with one rule a tag (IE 8's standards mode ignores `v\:*`; not run here).
- A v:group measures what it holds, not its style's width and height (221 by 81 for a 200 by 100 group holding a shape past its edge), and does not clip.
  A span with position relative, display inline-block and overflow hidden measures 200 by 100 and clips, as an svg does.
  So the svg is that span, a g a plain span at its top left (the CTM is baked into every point), and VML is only v:shape, v:fill and v:stroke.
- VML's stroke defaults differ from SVG's: a round join and cap where SVG has a miter join (limit 4) and a butt cap; each v:stroke now says SVG's.
- IE draws VML written in the page only once the page has finished loading: under the test hold (an image that never answers) a static v:rect draws nothing, while VML made by script draws.
- The Utilu IE 5.01 draws no VML at all: not the canonical static page (`xmlns:v`, the `v\:*` rule, a v:rect) with `#default#VML`, nor with IE 5.0's `<object id="VMLRender" classid="CLSID:10072CEC-8CC1-11D1-986E-00A0C955B42E">` and `behavior: url(#VMLRender)`, with or without the hold (without it, IE 5.01 reaches Done and crashes, as docs/ie5-design.md says), nor VML made by script in any form (createElement of "v:rect", of `<rect xmlns="urn:..." class="vml">`, with the namespace declared on the html element).
  The same static page draws in IE 6 on the same machine, so this is the standalone IE 5.01's own limit (it runs beside IE 6's VML renderer, which it does not use), not vml.js's; a real IE 5.0 or 5.5 install is where IE 5's VML could be checked.

After the fix, `legacy.html?svg` in IE 6 draws every shape where headless Chrome draws the real SVG (tools/legacy.mjs's svg case), the svg's span measures 200 by 100, and the six reports fire, each once.

Still unverified, for want of the browser here:

- IE 7, and IE 8's standards mode, with this setup.
- That JScript 5.0's String.prototype.match with a g flag returns every match (the converter's tokens and numbers() rely on it, since JScript 5.0's regular expressions have no lastIndex); IE 5.01's JScript is 5.1, where the probe has it.
- VML's fill is even-odd where SVG's default is nonzero, so a self-overlapping path can show a hole SVG would fill.

## 10. Speed in real IE 6

Before the setup was fixed, `legacy.html?svgmany=500` took 33 s to build in IE 6 on the XP VM (QEMU's emulated Pentium III, so every number here is slower than a real machine of 2001), about 66 ms a path, and 2,000 did not finish in 4 minutes.
Those nodes had the behavior bound as they were made (the three-argument namespace), so every property set on them went through VML.

Predictions, written before measuring the fixed build, each on the same VM:

- V1: svg_many(500) through cdom now builds in 5 to 30 s.
- V2: the same 500 shapes made by hand on a bare page, node by node as vml.js makes them (a v:shape with its v:fill and v:stroke, every property set off the document, the svg placed last), build in 2 to 15 s; so cdom's own code (the app, the shim and vml.js's translation) is less than half of V1.
- V3: the same 500 shapes as one markup string placed with insertAdjacentHTML (the classic fast path, as excanvas drew) build 3 to 10 times faster than V2.

Which way vml.js builds is picked by these numbers: if V3 is not at least twice as fast as V2, or if V2 is a small part of V1, node by node stays.

Measured on the fixed build (section 9's setup), the title's numbers in IE 6's own clock, two runs each where two are given:

- V1 confirmed: svg_many(500) built in 23,438 ms, 25,000 ms to the next timeout.
- V2 confirmed: 1,828 and 2,000 ms to build, 11,657 and 14,844 ms to the next timeout: IE's own VML layout after the script is most of it.
  But V2's second half is refuted: V1 is not mostly IE's VML.
  The same svg_many(500) with vml.js doing all its work on spans that draw nothing took 22,188 ms (22,516 ms to the next timeout), and with vml.js stubbed out (a hidden span an element, nothing mapped) 1,579 ms (1,625): so the app and the shim are 1.6 s, vml.js's own script about 20 s, and IE's VML the rest.
- V3 refuted: the markup string built in 625 and 562 ms against node by node's 2,000 and 1,828 (3.2 times faster), but to the next timeout 10,640 and 8,297 ms against 14,844 and 11,657, only 1.4 times faster in all, since IE's layout of the shapes is the same either way.
  By the rule above, node by node stays: markup would also mean finding every node again after the string is parsed, since the shim holds each node C made.

vml.js's script, timed piece by piece on the real cdom.legacy-ie6.js (500 of each): cdomVml.path alone 8,422 ms (17 ms a path of 13 commands, a character at a time through charAt, eight closures made each call), create("path") 750 ms, and a shape made, put in an svg and given d, fill and stroke 16,250 ms: each of the three attributes walks up to the svg again (parsing its viewBox each time), converts colours through regular expressions and sets every VML property again.

Predictions for the two changes made from that, written before measuring them:

- V4: path() tokenizes the path with one regular expression match and keeps its state in vml.js's scope, not in closures made each call: 500 conversions take 1,500 to 4,000 ms (from 8,422).
- V5: a container's context (CTM, paint, opacity) is kept on it and handed to what it holds, colours are converted once each, and a VML property is set only when its value changes: with V4, the 500 shapes with d, fill and stroke take 3,000 to 8,000 ms (from 16,250).
- V6: svg_many(500) through cdom then builds in 5,000 to 12,000 ms (from 23,438).

Measured after the two changes (two runs each where two are given):

- V4 refuted: path() alone took 7,313 ms with the tokenizer (from 8,422), and 5,906 and 6,187 ms once a point's rounding was done without calls (Math.round and String were two calls a coordinate), above the 1,500 to 4,000 predicted.
  The page that timed it loops on global variables, which IE looks up on window, so its numbers are high; the converter's share of svg_many below is less.
- V5 refuted: the caches alone (the context, the colours, a property set only when it changes) measured 18,250 ms, inside the noise of the 16,250 before; drawing a shape once when the call into C ends, rather than once an attribute (shim/cdom.js calls drawHeld as the outermost call into C ends, an empty function everywhere but the fourth tier, whose shim-old.js draws the held shapes there), brought it to 11,922 and 12,297 ms, above the 3,000 to 8,000 predicted.
  JScript on this VM is slow in a way the prediction did not weigh: 30,000 turns of an empty loop take 328 ms, and an empty function's call about 76 microseconds.
- V6 confirmed: svg_many(500) builds in 5,343 and 4,953 ms (6,968 and 6,453 ms to the next timeout), from 23,438 (25,000): 3.6 to 3.9 times faster in all.
- svg_many(2,000), which had not finished in 4 minutes, builds in 23,078 ms, 28,938 ms to the next timeout, with no slow-script prompt.

shim/cdom.min.js, which apps inline, is byte-identical after the change: terser drops the empty drawHeld and its call.
