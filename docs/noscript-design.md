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
