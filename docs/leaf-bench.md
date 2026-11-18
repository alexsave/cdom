# Leaf bench: one import for an element holding one text

This file records whether cdom should have a fused "leaf" call, one JS import that opens an element, sets its text and closes it, in place of `cd_open(tag); cd_text(s); cd_close();`.
The predictions were written and committed before anything was measured.

## What the triplet costs today

Every cdom operation is its own import: nothing is batched.
`cd_open`, `cd_text` and `cd_close` are each `ready(); j_x(...); tmp_done();` in `src/cdom.c`, so the triplet is three wasm-to-JS crossings, and `cd_el_text` with a class (`src/cdom_ui.c`) is four (`open`, `class`, `text`, `close`).
In the shim, `open` makes the element and appends it to the open element, `text` makes a text node and appends it, `close` pops the stack and places an outermost element.
A leaf saves two crossings (three with a class) and none of the DOM work: it still makes one element and one text node and appends both.

Templates do not record: `cd_tpl_save` takes the element just built with the ordinary calls, and `cd_slot` takes the shim's `last`.
After the triplet `last` and `lastText` are the text node and `lastElement` is the element; a leaf that leaves the same three behind behaves the same under `cd_slot`, `cd_name_el`, `cd_name_text`, `cd_listen` and `cd_tpl_save`, so it needs no fallback while a template is built.

Where the triplet is written today:

- tutorial: 01-hello 3, 04-regions 10, 05-todo 1 with a class, 06-stopwatch 2 with a class, 07-particles 1.
- bench/cdom-keyed: 2, the row's `<a>` label (built only without templates, `CDOM_TEMPLATES=0`) and the page's `<h1>`; a row with templates is a clone, and has no leaf.
- amgi: `el_text` at 29 sites, 10 of them with a class, none in a loop of more than a deck list.
- The 2,000-site synthetic page (`bench/strings/gen_synth.py`): 250.

## Predictions

The model: a crossing (the wasm-to-JS wrapper, the C function and its `ready()`) costs 2.5 to 6 ns; P13 measured a literal `cd_text` resolved only at 7.9 ns, which also holds the string lookup.
A leaf in the DOM costs 250 to 450 ns (P13: the text node alone with its literal is 138 ns), so two crossings are 1% to 5% of it.

| # | Prediction | Refuted by |
| --- | --- | --- |
| L1 | Micro, one crossing alone (`cd_name_open`, no DOM work): 2.5 to 6 ns | under 1.5 or over 10 ns |
| L2 | Micro, DOM: a literal triplet 250 to 450 ns; the leaf 5 to 12 ns cheaper (leaf/triplet 0.96 to 0.99); with a class, the 3-argument leaf 8 to 18 ns cheaper than its four calls | a gain over 25 ns, under 2 ns, or the leaf slower beyond the quartiles, in two passes |
| L3 | Keyed page without templates (one string leaf a row): create 1k script 0.005 to 0.012 ms faster, create 10k 0.05 to 0.12 ms faster, so both inside the noise (A2/A was 0.978 to 1.011 on script); every total inside the noise | create 10k script or total better by more than the noise band and by 0.5 ms or more, in two passes |
| L4 | Keyed page with templates: no change (no leaf in a row); every ratio inside the noise | a difference beyond the noise, either way, in two passes |
| L5 | wasm: 4 to 7 bytes saved a site, 30 to 45 bytes for `cd_leaf` and its import; the keyed app +20 to +40 bytes, hello 0 to +25, the 250-leaf synthetic page 1,000 to 1,750 bytes smaller | outside these bands |
| L6 | Shim: `shim/cdom.min.js` +60 to +120 bytes, +30 to +60 brotli -q 11 | outside these bands |
| L7 | Startup, synthetic page: `cd_main` 1.3 to 3 µs faster (250 leaves), not measurable against its run-to-run spread (about 0.1 ms); first paint not measurable | `cd_main` median lower by over 0.1 ms in two passes |

The decision rule, fixed before measuring: the leaf goes into `strings` only if L3 or L4 is refuted in its favour, a page win beyond the noise in two passes.
A micro win alone, or a size win on a page of 250 leaves, does not carry it.

## How it was measured

Date: 2026-11-18, headless Chrome 157, one job on the machine at a time.
The prototype was `cd_leaf(tag, s)` and `cd_leaf_class(tag, cls, s)` in `src/cdom.c`, each `ready(); j_leaf(...); tmp_done();`, over two shim functions that call `open`, `text` (and set the class) and `close`, with `cd_el_text` rebuilt on them.
It is kept as `bench/strings/leaf-bench.patch` (the product code and the harness below), since the branch it was on is deleted; `git apply` it on `strings` to run any of this again.

- Micro: `bench/strings/leaf-micro.sh`, rows added to `micro-s.c`, four passes with the order reversed every other pass (`results/leaf-micro-<k>.tsv`). Triplet and leaf are two exports of one module on one shim.
- Pages: `bench/strings/build-leaf.sh` builds the keyed app as committed and a copy with its two triplets as `cd_leaf`, with and without templates, each twice (the second copy is the noise); `bench/strings/leaf-jfb.sh` runs create 1k and create 10k through `tools/pair.sh`, 15 iterations, in ABBA order in pass 1 and the reverse in pass 2, loads 1.71 to 3.95 at the starts (`results/leaf-jfb-p<k>.tsv`). `tools/domcheck.mjs` found the leaf entries' DOM identical to the triplet entries' over 22 snapshots, with and without templates, and `tools/smoke.mjs` passed on the leaf entry (wasm and legacy).
- Startup and size: `bench/strings/leaf-pages.sh`, the 2,000-site page as written (250 triplets) and with its triplets as `cd_leaf`, `startup.mjs` at 15 loads each, in both orders (`results/leaf-startup-<k>.tsv`).

## Results

| # | Measured | Verdict |
| --- | --- | --- |
| L1 | 7.21, 6.83, 6.83, 6.84 ns (quartiles within 0.1) | outside the band, under the falsifier: a crossing costs about 7 ns, not 2.5 to 6 |
| L2 | literal triplet 314.6 / 297.9 / 318.6 / 299.1 ns, leaf 293.9 / 264.6 / 297.9 / 260.9: 20.8 / 33.3 / 20.7 / 38.1 ns cheaper (6.6% to 12.7%), the quartiles touching in passes 1 and 3; runtime 9-byte text 23 to 37 ns cheaper, 30-byte 0 to 38; four calls with a class against `cd_leaf_class`: -62 and -30 ns in the forward passes, +74 and +59 in the reversed ones, quad8's third quartile up to 551 ns | refuted: the gain is over 25 ns in two passes, more than the two crossings it removes (V8 compiles the three shim functions as one); the class rows follow the run order, not the call, and give no verdict |
| L3 | leaf/triplet without templates, pass 1 and 2 (noise is triplet2/triplet): create 1k total 0.997, 0.990 and 0.991, 0.991 (noise 0.994, 0.994); 1k script 4.4 against 4.4, and 4.4 against 4.5 (the harness reports 0.1 ms steps); create 10k total 1.002, 1.003 and 0.996, 0.999 (noise 1.002, 0.996); 10k script 1.003, 1.006 and 1.000, 1.000 (noise 1.003, 0.997) | confirmed: no gain beyond the noise. Even the micro's 21 to 38 ns would be 0.2 to 0.4 ms of 35.4 at 10k; the four leaf runs measured 35.3 to 35.5 against the triplet's 35.2 to 35.5 |
| L4 | with templates: create 1k total 1.003, 1.000 and 0.997, 1.003; script 2.5 to 2.6 in every run; create 10k total 0.999, 0.997 and 0.995, 0.998 (noise 0.997, 0.999); 10k script 1.000, 0.995 and 0.990, 0.995 (noise 1.005, 0.995) | confirmed: no change |
| L5 | keyed app with templates 10,160 to 10,201 bytes (+41), without 9,843 to 9,882 (+39); 2,000-site page 15,192 to 14,247 (-945), its page brotli 15,914 to 15,231 (-683), about 3.8 bytes a site; hello not built | refuted at the edges: +41 is one byte over the keyed band, and -945 is short of the 1,000 to 1,750 band |
| L6 | `shim/cdom.min.js` 17,612 to 17,723 bytes (+111), brotli 6,682 to 6,712 (+30) | confirmed |
| L7 | `cd_main` medians: triplet 1.61 and 1.59 ms, leaf 1.55 and 1.96, both with quartiles 1.50 to 2.07 (bimodal) | confirmed: not measurable |

## Decision: no leaf call

The rule set before measuring was a page win beyond the noise in two passes, and there is none: L3 and L4 are confirmed, with and without templates.
A leaf removes crossings and none of the DOM work, and the DOM work is about 90% of a leaf even in the micro; with templates a row is a clone and holds no leaf at all.
The only win is size, and only at hundreds of sites: about 3.8 bytes a site, against 111 bytes of shim (30 brotli) that every page would carry, two imports and two more API names.
cdom's own apps have 1 to 10 triplets a lesson and amgi has 29 `el_text` sites, none in a hot loop, so none of them would get smaller once the shim's 111 bytes and `cd_leaf`'s 40 are counted.
`cd_el_text` stays the three calls (four with a class), and the triplet stays the way to write a leaf.
