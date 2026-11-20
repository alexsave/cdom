# The no-script fallback: what a browser with no script at all sees

Alex set Netscape 2 (1995, the first browser with JavaScript) as cdom's floor.
Below it are the browsers with no script at all: NCSA Mosaic 1 and 2, IE 1.5 and 2, Netscape 1.1, and Lynx.
There is no server tier, so what they get is static HTML in the built page.

## The design, Alex's

His words, quoted from his answer:

> <img>[a single screenshot from build time]</img><p>Brother I'm sorry, the only way I could support your browser is if I had a server running just for you. And even if I did, anything you sent to that server would be decrypted. Regrettably I have chosen to drop support on this browser. Here's a snowman</p><img>cdom snowman</img>

So a browser with no script shows, and shows only:

1. one screenshot of the app, taken at build time, a GIF;
2. the paragraph, verbatim, in ASCII (the apostrophes are plain `'`);
3. a snowman, cdom's mascot (U+2603), rendered from the glyph at build time, a GIF.

## Predictions

Written before anything was measured; never edited after.
Each has a falsifier.
"The default demo" is the app `make -C cdom new` writes (tools/new.sh): "cdom" centered and a hint line under it.
Timings are on this Linux x86-64 VM and indicative only.

- P1, the screenshot GIF of the default demo, 640 by 480: about 4,000 bytes.
  Refuted if it is under 1,500 or over 10,000 bytes.
- P2, the snowman GIF, 96 by 96: about 1,200 bytes.
  Refuted if it is under 400 or over 3,000 bytes.
- P3, the page bytes the fallback adds to the default demo's page (the block at the marker and the comment wrappers of its scripts and style): about 620 bytes.
  Refuted if it is under 500 or over 800 bytes.
- P4, the build time the fallback adds to a clean build of the default demo (Chrome launched, the page rendered and shot, both GIFs written): about 2 s.
  Refuted if it is under 0.7 s or over 5 s.
- P5, a second make after touching nothing that changes the page's bytes (the page rewritten with the same bytes): the shot step takes under 0.5 s, since its content stamp matches and it launches no Chrome.
  Refuted if it launches Chrome or takes 0.5 s or more.
- P6, determinism: two shots of the same page, the stamp removed between them, write byte-identical GIFs, both of them.
  Refuted if any byte differs.
- P7, Chrome with script on requests neither GIF from the default demo's page.
  Refuted if the network log shows a request for either.
- P8, the reason for `<noscript>` beside the script-written comment: with the `<noscript>` element taken away (the comment alone hiding the fallback), Chrome with script on still requests the screenshot GIF, because its preload scanner reads ahead of the parser and does not run the script that opens the comment.
  Refuted if Chrome requests neither GIF without the `<noscript>`.
- P9, every no-script reading of the page (Chrome with script off, `lynx -dump`, and the HTML 2.0 lens) shows the paragraph exactly, and none of them shows a byte of the inlined shim, the string table or the stylesheet.
  Refuted if any of the three shows other text, or misses a word.

## Results

Measured after the predictions above were committed, by tests/noscript/run.sh on this VM (Chrome for Testing 153, Lynx 2.9.0, Pillow 13.0, LLVM 23.1.6).
Timings are indicative.

| | Predicted | Measured | Verdict |
|---|---|---|---|
| P1, the screenshot GIF | about 4,000 bytes (1,500 to 10,000) | 4,091 bytes | confirmed |
| P2, the snowman GIF | about 1,200 bytes (400 to 3,000) | 2,070 bytes | confirmed, inside the range, 70% over the point estimate |
| P3, page bytes added | about 620 (500 to 800) | 558 (30,220 to 30,778) | confirmed |
| P4, build time added | about 2 s (0.7 to 5) | 1.43 s (1.76 s against 0.33 s for a clean build; 1.79 s added in a run beside the mutants) | confirmed |
| P5, a make whose page has the same bytes | under 0.5 s, no Chrome | 0.11 s, neither GIF nor the stamp rewritten | confirmed |
| P6, determinism | the same bytes | the same bytes: a second shot with the stamp removed, and the app edited and put back | confirmed |
| P7, no GIF fetched with script | none | none (requests: the page and favicon.ico) | confirmed |
| P8, without `<noscript>` | the screenshot fetched | both GIFs fetched | confirmed |
| P9, the three no-script readings | the paragraph exactly, no source | exactly, in all three | confirmed |

P2's miss of the point estimate: the glyph is drawn with anti-aliasing in 63 grey levels, and DejaVu Sans's snowman has snowflakes around it, so there are many short runs for LZW.

## What the page holds

The default page has `/*CDOM_NOSCRIPT*/` alone on the line after `<body>`, and tools/cdpage writes this there (the default demo's names and sizes):

```html
<script><!--
document.write("<!-"+"-");
//--></script><noscript>
<img src="index.noscript.gif" width="640" height="480" alt="[A screenshot of this page in a browser with JavaScript]">
<p>Brother I'm sorry, the only way I could support your browser is if I had a server running just for you. And even if I did, anything you sent to that server would be decrypted. Regrettably I have chosen to drop support on this browser. Here's a snowman</p>
<img src="cdom-snowman.gif" width="96" height="96" alt="[A snowman]">
</noscript><wbr -->
```

And every `<script>` and `<style>` of the page with something in it is wrapped: `<script><!--` ... `//--></script>` and `<style><!--` ... `--></style>`.

How each kind of browser reads it:

- No script (Mosaic, IE 1.5 and 2, Netscape 1.1, Lynx, Chrome with script off): `<script>` is unknown or not run, and its text is a comment; `<noscript>` is unknown (its content shown, as Mosaic shows the content of every tag it does not know) or known and shown; `<wbr -->` is a tag it does not know, or a void element, and shows nothing.
  So the screenshot, the paragraph and the snowman, and nothing else: the head's `<style>` and the body's two scripts are comments.
- Script, no `<noscript>` (Netscape 2): the script runs while the page is parsed, and `document.write` puts `<!--` into the input right after the `</script>`, so the parser reads `<noscript>`, the images, the paragraph and `</noscript><wbr ` as a comment, which `-->` ends.
  The images are never requested, since the parser never sees their tags.
- Script and `<noscript>` (Netscape 3 and 4, IE 3 on, every modern browser): the same comment, and the block is a `<noscript>` besides.
  The `<noscript>` is what keeps a modern browser from fetching the images: its preload scanner reads ahead of the parser to start requests early, does not run scripts, so it does not see the comment open, but it does skip a `<noscript>`'s content when script is on (P8: without it, Chrome fetches both GIFs, though it never shows them).

The block comes first in the `<body>`, so a browser on a modem shows it before the inlined shim (some 25 KB) has arrived.

## Why this mechanism

Alternatives, and what is wrong with each alone:

- `<noscript>` alone: Netscape 2 has script and no `<noscript>` (it came with Netscape 3 and HTML 4), so it would show the fallback above the app and fetch both images.
- A script-written comment alone: hides the block from every browser that runs scripts during the parse, Netscape 2 among them, but a modern preload scanner fetches the images anyway (P8).
- CSS (`display: none`): Netscape 2 has no CSS, and a modern browser still fetches an image that is not displayed.
- A server tier: there is none, by design.

So both: the comment for Netscape 2, `<noscript>` for the preload scanners.
Neither costs the other anything: in Chrome with script on, the block is a comment node and no `<noscript>` element exists at all (tests/noscript checks that, which is how the comment path is tested in a browser that also has `<noscript>`).

The closer is `<wbr -->`, not the usual `<!-- -->`.
Inside the script-opened comment, `<!-- -->` ends the comment in a browser that ends comments at `-->` (the "minimal" reading); but under SGML's strict reading, where every `--` opens or closes, its first `--` closes the comment, its second opens another, and that one runs on to the next `--` in the page, the shim's, swallowing the app.
Any closer made only of comments has that problem, since a comment closer must hold an odd number of `--` pairs inside a comment and an even number outside one.
`<wbr -->` is a tag outside the comment (SGML has no comments inside tags; HTML 2.0 does not know WBR; HTML5 parses a void element with an attribute named `--`), and inside it the `--` closes the comment and the `>` ends the declaration under both readings.

Each script's text is wrapped in `<!--` and `//-->`, which are both comments to JavaScript (the HTML-like comments of ECMAScript's Annex B, in a classic script; Netscape 2's JavaScript already took `<!--` as a comment, which is where the convention comes from).
Each style's text is wrapped in `<!--` and `-->`, which CSS reads as CDO and CDC tokens and ignores at the top level of a sheet.
A module script cannot be wrapped (`<!--` is a syntax error in a module), so a page with the marker and a module script is refused.
A script or style whose own text holds `-->` would end its wrapper early, and an old browser would show the rest, so that is refused too, naming the line.

### The HTML parser's escaped states

Inside a `<script>`, the HTML parser has three states: plain script data; escaped, after a `<!--`; and double-escaped, after a `<script` (followed by a space, `/` or `>`) in escaped text.
In the double-escaped state `</script>` does not end the element: it only returns to escaped.
So a script whose text holds `<!--` and later `<script`, and nothing between that ends the escape, makes the parser run on past the `</script>` the page meant, and the rest of the page is read as script.

tools/cdpage runs every script of the page it writes through those states (the HTML standard's tokenizer states 13.2.5.4 to 13.2.5.25), and refuses the page when a script's text leaves the parser double-escaped where the page's `</script>` stands, or when the parser would not end the element at its first `</script>`.
This check is on for every page, with the fallback or without.
tests/noscript/run.sh checks it against Chrome's own parser: 18 planted script texts, and for each, cdpage refuses exactly when Chrome's scripts do not all end at their first `</script>`.

One finding from that: the fallback's wrapper makes the planted sequence harmless.
Its closing `-->` takes the parser from double-escaped back to plain script data (a `-->` does that from either escaped state), so the `</script>` after it always ends the element.
The risk is real for a page without the wrapper (CDOM_NOSCRIPT := 0, or an app's own page without the marker): a shim or string table holding `<!--` and then `<script` would break it, and cdpage now refuses that page.
Today's shim and every string table cdlits writes hold neither `<!--`, `-->` nor `<script`: cdlits writes `<` as `\x3c` in every entry.

## Browser matrix

| Browser | Script | `<noscript>` | Shows | Through | Checked |
|---|---|---|---|---|---|
| NCSA Mosaic 1, 2 | no | no | the fallback | unknown tags dropped, comments | the HTML 2.0 lens (a model) |
| IE 1.5, IE 2 | no | no | the fallback | the same | the lens; real ones on Alex's VM, below |
| Netscape 1.1 | no | no | the fallback | the same | the lens; the real one on Alex's VM, below |
| Lynx | no | yes, shown | the fallback, the ALT texts for the images | `<noscript>` shown, scripts and style skipped | `lynx -dump` (2.9.0) |
| Chrome, script off | no | yes, shown | the fallback | the same | tests/noscript (Emulation.setScriptExecutionDisabled) |
| Netscape 2 | yes | no | never the fallback | the script-written comment | the lens's Netscape 2 model |
| Netscape 3, 4; IE 3, 4 | yes | yes | never the fallback | both | not run: the frames tier's browsers |
| IE 5 to 8 | yes | yes | never the fallback | both | not run here |
| Chrome (and every modern browser) | yes | yes | never the fallback, no GIF fetched | both | tests/noscript |

Until the frames tier exists, Netscape 2 to 4 and IE 3 and 4 see a page that hides the fallback and cannot run the app (their JavaScript cannot parse the shim), which is the brief: a browser with script never shows the fallback.

The HTML 2.0 lens (tools/html2lens.py) reads the page as Latin-1, ends a comment at its first `-->`, drops any `<!...>` declaration, keeps only the tags of RFC 1866 and drops every other tag but keeps its text, and leaves out TITLE's text; it prints what is shown, an IMG with its source, size and ALT, a paragraph break, and text.
With `--script` it models Netscape 2: SCRIPT known and its text hidden, and the fallback's one `document.write` taken as the comment it opens.
It is a model of what those browsers were documented to do, not a run of them.

## The build

cdom.mk, for the APP_PAGE rule:

- `CDOM_NOSCRIPT ?= 1`: the fallback for any page that has the marker; 0 removes the marker line and wraps nothing, so the page is byte for byte what it was before this change.
- `CDOM_NOSCRIPT_SHOT_SIZE ?= 640x480`, `CDOM_NOSCRIPT_SNOWMAN ?= cdom-snowman.gif`, `CDOM_NOSCRIPT_SNOWMAN_SIZE ?= 96x96`; the screenshot is `<page's base name>.noscript.gif`, beside the page.
- tools/cdpage takes `--noscript-shot url=WxH --noscript-snowman url=WxH` and writes the block and the wrappers.
- Then, only when the page shows the block, `node tools/noscript.mjs page shot=WxH snowman=WxH`: it serves the page's folder on 127.0.0.1, opens the page in headless Chrome (tools/cdp.mjs) at the shot's size and a device pixel ratio of 1, with reduced motion and a light scheme emulated and every animation held at its start, waits for the page to load, its fonts, and 300 ms with no DOM change (5 s at most), and shoots it; then renders U+2603 in the first font that has it, black on white, and shoots that.
  tools/noscript_gif.py (Python 3 and Pillow) makes each PNG a GIF87a of 256 colours at most: all of a picture's colours when it has 256 or fewer (sorted by count, then value), else a median cut to 256, never dithered, so the palette depends on the pixels alone.
  A request for either GIF while the app loads fails the build.
- A content stamp, `<page>.noscript` beside the page, in the style of tools/legacy/build.mjs's: the tools' bytes, the Chrome binary (its resolved path, size and time), Python's and Pillow's versions, the page's bytes, every file the page fetched with its hash, and the two GIFs' hashes.
  When all of it is the same, the step launches nothing (P5); when any of it changes, both GIFs are made again.
- A failed step removes the page, so the next make tries again.
- Node, Chrome and Python with Pillow are needed only for a page with the marker; an app's own page without it builds as before, with plain make and C.

An app's own page (APP_PAGE_SRC) opts in with the same marker line in its `<body>`, outside any script and style.
The images are files beside the page, not data: URLs, which none of those browsers can show.

## Tests and mutants

tests/noscript/run.sh, in CI on both runners, builds the default demo as an app builds it and checks, among its 39 lines:

- with CDOM_NOSCRIPT=0, no fallback, marker, wrapper or GIF; with it on, the page is the off page plus the block and the wrappers, and nothing else;
- both GIFs: GIF87a, their sizes, a global colour table of 256 at most, more than one colour; the same bytes on a second shot; tools/noscript_gif.py's bytes the same twice, and a 64-colour picture kept exactly;
- the stamp: a page of the same bytes shoots nothing; an app that draws another word is shot again; the app put back gives the first bytes again;
- Chrome with script off: the text is exactly the paragraph, both images loaded and drawn at their sizes, both requested, the mount empty;
- Chrome with script on: the app drew, no word of the fallback, no image, the block a comment and no `<noscript>` element, neither GIF requested;
- `lynx -dump`: exactly the screenshot's ALT text, the paragraph and the snowman's ALT text, and no script or CSS source;
- the lens: exactly the fallback; as Netscape 2, nothing; on the page built without the fallback, the string table's source (so the lens does see a leak);
- an own page with the marker gets the fallback and its own screenshot, one without gets nothing and runs no Chrome;
- cdpage's refusals: the planted `<!--` then `<script` in a shim and in a string table without the wrapper, `-->` under the wrapper, a module script, the marker in a script, twice, an image URL with `--`; the planted texts taken with the wrapper, and Chrome reading every script to its first `</script>`; and the parse check agreeing with Chrome on 18 planted texts.

tools/mutate.py's ns-* mutants (21), each failing tests/noscript/run.sh on its own message: listed in the PR.

## Verifying in the real browsers

On Alex's XP VM (the Utilu IE collection has IE 1.5 and IE 2.0, and Netscape 1.1):

1. Build an app with the fallback (the default page) and copy the page's folder to the VM: the page and its two GIFs (`index.html`, `index.noscript.gif`, `cdom-snowman.gif`).
2. Serve it over plain HTTP 1.0 or open it from disk (File, Open): none of these browsers speaks today's TLS, so an https URL will not load at all.
   From disk avoids any server question; `python -m http.server` on the host, reached by the VM's address, works for HTTP.
3. In each of IE 1.5, IE 2.0 and Netscape 1.1, check: the screenshot first, at 640 by 480; the paragraph, with plain apostrophes in "I'm" and "Here's"; the snowman under it; and no other text anywhere on the page, above all no JavaScript and no CSS (scroll to the end: the shim is near the end of the file).
4. Note anything else shown, with the line of the page it came from: that names the comment reading the browser uses.

For NCSA Mosaic: Mosaic for X 2.7b6 builds on Linux today (several ports keep it building), and Mosaic for Windows 2.1.1 and 3.0 run on XP; Mosaic 1 for Windows is 16-bit and needs an older Windows.
The checks are the same.
In Netscape 2 or 3, if one is at hand: the page must not show the fallback, nor request the GIFs (a proxy log shows it); until the frames tier, it shows no app either.

## Limits

- The comment readings: the page assumes a browser ends a comment at `-->` ("minimal", Lynx's default, and HTML5's).
  The block itself is safe under SGML's strict reading too (it holds no `--` but at its comment's ends, and `<wbr -->` closes either way), but the shim holds `--` (a decrement), so a strict browser with no script could show part of the shim.
  A browser that ends a comment at its first `>` would show every script's text after its first `>`.
  Which of Mosaic 1, Mosaic 2, IE 1.5, IE 2 and Netscape 1.1 read comments which way is not known here: that is what the real-browser check above will say.
- WIDTH and HEIGHT on IMG are from HTML 3.2 (Netscape's extension), not HTML 2.0; a browser that ignores them shows the images at their own size, which is the same.
- The screenshot is the app's first still state at 640 by 480 with reduced motion and a light scheme: an app that draws the time or anything random gets a new screenshot whenever its page is rebuilt, and its GIF is not reproducible.
  The page is served from its own folder by a static server, so an app that needs its own server is shot without it.
- The stamp knows Chrome by its resolved binary's path, size and time; a wrapper script whose browser is updated elsewhere is not seen (make -B).
  A GIF deleted by hand is made again only when make rebuilds the page.
- The title is written as the app gives it; a non-ASCII APP_TITLE shows as Latin-1 in Mosaic's title bar.
- A page made by tools/legacy/build.mjs's `page` step (the boot inlined at /*CDOM_BOOT*/) is checked by cdpage before that step, not after.
  shim/boot.js holds a `<script` (in its first comment) but no `<!--` and no `-->`: unwrapped, the parser is never escaped, and wrapped, the wrapper's `-->` ends the escape, so it is safe either way today, unchecked.
- The browsers with script but too old to run the shim (Netscape 2 to 4, IE 3 and 4) see a page with nothing in it until the frames tier (branch frames-tier) lands.
