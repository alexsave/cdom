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
The svg and each g are v:group elements whose own frame is the identity (the svg's box W by H px with coordsize "W,H"; a g's 1 px box with coordsize "1,1"), so a child's style lengths mean the same whether IE reads them as pixels or as the group's units.

| SVG | VML (cdomVml) |
| --- | --- |
| `<svg width height viewBox>` | `v:group`, style position:relative;display:inline-block;width:Wpx;height:Hpx, coordsize "W,H", coordorigin "0,0"; the viewBox sets the CTM of everything inside (preserveAspectRatio's default, xMidYMid meet); no width or height is 300 by 150, as in a browser |
| `<g transform>` | `v:group`, style position:absolute;left:0;top:0;width:1px;height:1px, coordsize "1,1"; its translate and scale go into the CTM of what it holds |
| `<path d>` | `v:shape` with `path` from cdomVml.path(d, CTM) |
| `<rect x y width height rx ry>` | `v:shape`, the rect as a path (rounded corners as arcs) |
| `<circle cx cy r>`, `<ellipse cx cy rx ry>` | `v:shape`, two arcs |
| `<line x1 y1 x2 y2>` | `v:shape`, m and l, never filled |
| `<polyline points>`, `<polygon points>` | `v:shape`, m and l (and x for the polygon) |
| `fill` | `filled` (false for none) and `fillcolor` |
| `stroke` | `stroked` (false for none, the default) and `strokecolor` |
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
Also not done: the svg's own clipping to its box (VML groups do not clip), SVG's nonzero fill rule (VML fills even-odd), line caps, joins and dashes, and preserveAspectRatio other than the default.
Templates (cd_tpl) holding SVG are cloned by cloneNode, whose copy has no cdomVml state; an attribute set on such a copy later is mapped as if it were the first.

## 4. VML setup

The first VML node made sets the page up once:

- document.namespaces.add("v", "urn:schemas-microsoft-com:vml", "#default#VML") where document.namespaces exists (IE 5.5 to 8; the two-argument form if the three-argument one throws);
- and in every case a stylesheet rule `v\:* { behavior: url(#default#VML); display: inline-block }` (document.createStyleSheet's addRule), which IE 5.0 and IE 6 and 7 need, and IE 8's standards mode wants per element.

Nodes are made with document.createElement("v:shape"), "v:group", "v:fill" and "v:stroke", and every VML property is set as a property of the node (node.path = ..., as Raphael does).
A shape's attributes are set before the shape is in the document: the shim builds an svg subtree off the document and places its root when it closes, so every node of a newly built svg has its VML properties before IE attaches the behavior.
A later change (cd_set_attr) sets the properties on the live node.
Moving or re-attaching a node inside an svg (cd_move, cd_attach) goes through the shim's place(), which on this tier (tools/legacy/shim-old.js) redraws what it moved, since its CTM and inherited paint can change with its parent.
Removing or replacing the svg needs nothing: the VML nodes are DOM nodes, removed as any other.

## 5. Tests

- tools/cdp.mjs, LEGACY=ie6: a VML stand-in in the IE6_OLD profile: document.namespaces.add and document.createStyleSheet's addRule recorded in window.__vml, and every document.createElement of a v: tag recorded too (Chrome makes an unknown element of it, which keeps the properties set on it).
- tests/legacy.c's svg() builds an svg with every element of the subset, a g with translate and scale, inherited paint, opacities, and the unsupported text, defs, linearGradient and a rotate; svg_set() changes a path's d and a rect's fill after the build; svg_drop() removes the svg; svg_many(n) builds n paths, for the timing.
- tools/legacy.mjs, LEGACY=ie6: each element became its v: node with the path string, coordsize, coordorigin, size, colours, weights and opacities it should have; the unsupported kinds reported once each and their nodes hidden; the later change reached the VML node; the removal left nothing; no SVG element was made.
  On the other tiers: the same svg is real SVG (the SVG namespace), and nothing reported.
- tools/vml.mjs, in node: cdomVml.path against hand-checked strings for every command, absolute and relative, implicit repeats, S and T reflection, Z's move back, a malformed path, the CTM; and arcs against points sampled on the true arc.
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
