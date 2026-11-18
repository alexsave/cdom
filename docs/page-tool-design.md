# The page tool: page.awk in C, and CSS files bundled into the page

Status: design, written 2026-11-17, nothing implemented or measured yet.
It supersedes the "The page" section of [strings-design.md](strings-design.md) where that section names `tools/page.awk`.

## What was asked

Alex, on the tutorial's `style()` functions: "what you just wrote is basically a CSS file within this C file ... I want you to make it so that we can include automatically bundle css files into the html. this way when we edit the css files we can get all the syntax highlighting you'd expect from a css file. and reduces a few cdom calls. this should probably be done by whoever replaces the /*CDOM*/ thing ... inline styles are fine though".

Today every lesson and the starter build a `<style>` element from C (`cd_open("style"); cd_text("...the whole sheet..."); cd_close();`) as the first child of the mount.
The page is written by `tools/page.awk`, fed by a `base64 | tr | awk` pipeline in `cdom.mk`.
Alex already agreed that `page.awk` becomes C.
This document designs both: the C tool that replaces `page.awk`, and CSS bundling through it.

## The design in one page

- A new host tool, `tools/cdpage.c`, built to `tools/out/cdpage` with `HOST_CC` exactly as `tools/cdlits` is, writes the page.
  It replaces `tools/page.awk`, `base64`, `tr` and the temporary-file dance in `cdom.mk`.
- An app names its stylesheets in its Makefile: `APP_CSS := src/app.css src/theme.css`.
  They are plain `.css` files, edited with the editor's CSS highlighting, linting and formatting.
- The page says where they go with a fourth marker, `/*CDOM_CSS*/`, on a line of its own inside a `<style>` element.
  It follows the rule the shim marker follows: a marker is a comment in the language it sits in, alone on its line, at most once.
- cdom's default page (`shim/page.html`) gets that `<style>` in its `<head>`, so an app on the default page only sets `APP_CSS`.
- The files are written at the marker in the order `APP_CSS` lists them, byte for byte, with only `</style` escaped.
  No minifier, no comment stripping, no `@import` or `url()` resolution.
- The sheet is in the page's HTML, before every script, so it applies to every paint the browser ever makes, in every tier including the IE 9 and 10 fallback.
- `APP_PAGE` depends on the `.css` files, so an edit rebuilds the page and nothing else (no compile, no link).
- An app can still build a `<style>` from C (`cd_open("style")` is not removed), and inline styles (`cd_attr("style", ...)`, `cd_set_style`) are untouched.

## The app-facing interface

### APP_CSS

`APP_CSS` is a list of CSS files, relative to the app's Makefile like `APP_SRC`.
It is read when make builds, like every `APP_*` variable, so it may be set before or after the include.
Unset or empty means the page gets no CSS from the build.
There is no default and no wildcard: `src/app.css` is not picked up because it exists, since an implicit list has no stated order and a stale file would ship silently.

### Order

One rule: the files are concatenated in the order `APP_CSS` lists them, into the one `<style>` that holds the marker.
A later file wins a tie in the cascade, as a later `<link>` would.
Rules the page's own `<style>` holds before the marker line come before all of them, and rules after the marker line come after all of them.
A file listed twice is an error.

### The marker, and the two kinds of page

```html
<style>
/*CDOM_CSS*/
</style>
```

| Page | `APP_CSS` set | `APP_CSS` empty |
| --- | --- | --- |
| Default page (`shim/page.html`), which has the marker | the files replace the marker line | the marker line is removed, and the `<style>` stays empty |
| The app's own page (`APP_PAGE_SRC`) with the marker | the files replace the marker line | the marker line is removed |
| The app's own page without the marker | build error, nothing written | nothing to do |

The error for the last case is `page.html: APP_CSS names src/app.css, and this page has no /*CDOM_CSS*/ line (a <style> whose line is only the marker, before the shim's script)`.
cdom never adds a `<style>` to an app's own page by itself, for the reason it never rewrites an app's `cdom.start` call: the app's page is the app's.
An app's own page may give its `<style>` attributes, or its own rules around the marker.

The marker must come before the `/*CDOM_SHIM*/` line.
That makes "the styles exist before the app starts" a build-time fact: `cd_main` may measure layout as it starts, and it must see the styled page.

### Where the style goes, and first paint

Today the page that arrives has an empty `<div id="app">` and no CSS at all.
`cdom.start` fetches and compiles the module asynchronously, and `cd_main` then builds the `<style>` and the content in one task.
So there is never a frame with content and no styles.
But any frame the browser paints before `cd_main` is the unstyled empty page: a white canvas even in dark mode (the `color-scheme` rule is not there yet), and the default 8 px body margin.
On the lessons from disk that frame is usually not painted (the baseline shows first paint equal to first contentful paint, about 44 ms, with `cd_main` starting near 14 ms), so nobody sees it.
It is painted whenever the module takes longer than a frame to arrive: a large app, a slow device, or `APP_WASM_SEPARATE` over a network.
There the dark-mode user gets a white flash.

After this change the sheet is parsed with the `<head>`, before any script runs.
Every paint has the right background, color scheme and margins, and the content appears into a styled page.
Nothing else about startup changes.

### Before and after: tutorial/01-hello

`Makefile`, before:

```make
CDOM ?= $(abspath ../..)
APP_WASM := build/app.wasm
APP_SRC := src/app.c
APP_PAGE := build/index.html
APP_TITLE := Hello, cdom
APP_CFLAGS := -Wall -Wextra -Werror
include $(CDOM)/cdom.mk

all: $(APP_PAGE)
.PHONY: all
```

`Makefile`, after (one line added):

```make
CDOM ?= $(abspath ../..)
APP_WASM := build/app.wasm
APP_SRC := src/app.c
APP_CSS := src/app.css
APP_PAGE := build/index.html
APP_TITLE := Hello, cdom
APP_CFLAGS := -Wall -Wextra -Werror
include $(CDOM)/cdom.mk

all: $(APP_PAGE)
.PHONY: all
```

`src/app.c`, before (the part that changes):

```c
static void style(void) {
  cd_open("style");
  cd_text(":root { color-scheme: light dark; font-family: system-ui, sans-serif; }"
          "body { margin: 0; }"
          ".card { max-width: 32rem; margin: 3rem auto; padding: 0 1rem; line-height: 1.5; }"
          ".card h1 { margin: 0 0 0.5rem; }"
          ".card a { color: light-dark(#1f5fd0, #8ab4ff); }");
  cd_close();
}

void cd_main(void) {
  cd_at(CD_MOUNT, 0); /* build into the mount (<div id="app">), appending */
  style();

  cd_open("main"); cd_class("card");
```

`src/app.c`, after (`style()` and its call are gone, and one comment says where the CSS is):

```c
/* The page's CSS is src/app.css. The Makefile names it (APP_CSS), and the build puts it in a <style> in
 * the page's <head>, so the page is styled before this code runs. */

void cd_main(void) {
  cd_at(CD_MOUNT, 0); /* build into the mount (<div id="app">), appending */

  cd_open("main"); cd_class("card");
```

`src/app.css`, new:

```css
/* Lesson 1's styles. The build writes this file into the page (APP_CSS in the Makefile). */
:root {
  color-scheme: light dark;
  font-family: system-ui, sans-serif;
}
body {
  margin: 0;
}
.card {
  max-width: 32rem;
  margin: 3rem auto;
  padding: 0 1rem;
  line-height: 1.5;
}
.card h1 {
  margin: 0 0 0.5rem;
}
.card a {
  color: light-dark(#1f5fd0, #8ab4ff);
}
```

The built page's head, after:

```html
<title>Hello, cdom</title>
<style>
/* Lesson 1's styles. The build writes this file into the page (APP_CSS in the Makefile). */
:root {
  ...
}
</style>
</head>
```

## What the tool does with CSS

- **As is.**
  Each file is written byte for byte.
  View-source shows what the author wrote, and a line number in the browser's style inspector is the file's line number plus a fixed offset.
  A minifier is a second CSS implementation to keep correct, for bytes that brotli mostly removes anyway.
  Comment stripping is not in the first version; the measurement step records what it would save, and it is added only if it saves more than 100 brotli bytes on some lesson (the prediction is under 30).
- **`</style` cannot end the element.**
  Wherever a file has `</` followed by `style` in any letter case, the tool writes `<\/` instead.
  Inside a CSS string or a `url()` the backslash escape means the same `/`, inside a comment it is only text, and anywhere else `</style` was already a CSS syntax error.
  So the escape never changes what a valid sheet means.
  Nothing else in a `<style>` element is special to the HTML parser (`<!--` is not, unlike in a script).
- **Each file must be whole by itself.**
  Concatenation makes one file's mistake swallow the next file's rules without a word, so the tool scans each file with a small state machine (comment, double-quoted string, single-quoted string, unquoted `url(`, plain text) and refuses, naming file and line:
  - a comment or a string that never ends;
  - a `{` that is never closed, or a `}` with no `{`;
  - `@import` outside a comment or string, with `name the file in APP_CSS instead`: an `@import` after any rule is ignored by the browser without a word, it is a second way to compose that this design does not need, and a bundled page fetches nothing;
  - a NUL byte, or bytes that are not UTF-8 (the page is UTF-8, as `cdlits` requires of literals).
- **A UTF-8 byte order mark** at the start of a file is dropped, since in the middle of a sheet it would break the selector after it.
- **A file that does not end in a newline** gets one, so the next file starts on its own line.
- **`url()` is left alone.**
  A `data:` URL or an absolute URL means the same anywhere.
  A relative URL resolves against the page, not against the `.css` file, because the sheet is inline; the README says so.
  Inlining local files as `data:` URLs is a possible later feature and is not designed here.
- **Rebuilding.**
  The `APP_PAGE` rule's prerequisites gain `$(APP_CSS)` and the tool itself.
  A `.css` edit rewrites the page only.
  `APP_CSS` is also named in `.cdom-mk-sources`, so a missing file stops make with `No rule to make target 'src/app.css', needed by 'build/index.html'` instead of leaving a stale page.
  A change to the `APP_CSS` list itself does not rebuild, like a change to `APP_TITLE` or `APP_CFLAGS` today (make does not track variables); that existing gap is not widened or closed here.
- **`APP_WASM_SEPARATE`** changes nothing: the CSS stays inline.
  The module is kept apart for long caching of a large file and for the no-WebAssembly fallback; a stylesheet is small, and inline it costs no blocking request.
  An app that wants a separate, cacheable stylesheet writes `<link rel="stylesheet" href="app.css">` in its own page and leaves `APP_CSS` unset; cdom needs no option for that.
- **Old browsers.**
  The string table is a script of its own because one syntax error discards a whole `<script>`, and IE 9 and 10 cannot parse the shim.
  CSS has no such problem: a browser drops the declaration or rule it does not understand and keeps the rest, so one `<style>` element serves every tier.
  What matters is that the CSS is static HTML and never goes through a script: it then applies even where the shim cannot parse.
  The tool writes no new JavaScript; the only JavaScript it writes is what `page.awk` writes today (the table's script boundary and one ES3 string literal for the module).

## The C page tool

### One tool or two

`tools/cdpage.c` is a second tool, not a mode of `cdlits`.
`cdlits` reads and rewrites a wasm module after a link; the page tool assembles text files into HTML after that.
They run in different rules (`APP_WASM` and `APP_PAGE`), fail for unrelated reasons, and share only a byte buffer and `die()`, about 25 lines.
Each stays one file with no dependencies, compiled by one `cc` command, as `tests/lits/run.sh` relies on.

### Command line

```
cdpage --page in.html --out out.html --shim cdom.min.js --lits app.wasm.lits
       [--wasm app.wasm | --wasm-url URL] [--title TITLE] [--css file.css]...
```

- `--page`, `--out`, `--shim` and `--lits` are required; anything else missing or unknown prints the usage and exits 1.
- `--wasm FILE` bundles that module: `/*CDOM_WASM*/` becomes `"data:application/wasm;base64,..."`, the base64 made in the tool (standard alphabet, padded, one line).
- `--wasm-url URL` makes `/*CDOM_WASM*/` that URL as a JavaScript string.
- With neither, the page must not have `/*CDOM_WASM*/`.
- `--title TITLE` makes `/*CDOM_TITLE*/` the title, escaped for HTML; without it the marker is left as it is.
- `--css FILE`, any number of times, in order.
- Every input is a named file or a flag: no standard input, no environment variables.
- The whole page is built in memory, written to `out.html.tmp<pid>` and renamed.
  On any error nothing is written, nothing is left behind, a message goes to standard error and the exit status is 1.
- Messages keep `page.awk`'s form and text, `file:line: message` or `file: message` with the page's name first, so the tests that match them do not change.

In `cdom.mk`:

- `CDOM_PAGE_TOOL := $(CDOM)/tools/out/cdpage`, with a pattern rule beside the `cdlits` one (`%/out/cdpage: %/cdpage.c`, same flags, built to a temporary name and renamed).
- `cdom_mk_page` becomes one command: the tool with its flags, each value quoted by `cdom_mk_q`, and `$(foreach f,$(7),--css ...)`.
- `$(call cdom_page,in.html,out.html[,module.wasm[,lits[,css files]]])` gains the fifth argument.
- The `%.html` rule's prerequisites become the page source, the shim, `$(CDOM_PAGE_TOOL)`, `$(APP_CSS)` and `$(APP_WASM)`.
- `tools/cdomflags.sh` builds the tool and hands it to cdom's own scripts as `CDOM_PAGE_TOOL`, as it does `CDOM_LITS`.

### Every behaviour of page.awk, kept or dropped

| # | page.awk today | In cdpage | Why |
| --- | --- | --- | --- |
| 1 | Every other line is copied as it is; each output line ends in a newline, so a last line without one gains one | kept, byte for byte | step 1 proves the two tools write identical pages |
| 2 | The line that holds only `/*CDOM_SHIM*/` (spaces and tabs before, spaces, tabs and CR after) becomes: the `.lits` file, `</script>`, the last `<script ...>` tag seen written again as it was, the shim file | kept | the table must be a script of its own before the shim (the IE 9 and 10 fallback) |
| 3 | The script tag is found by a one-line, case-blind match of `<script` then `>` or attributes; a tag split across lines is not seen | kept | no page in the tree splits it; the limit goes in the README |
| 4 | Error: `/*CDOM_SHIM*/ must be alone on its line` | kept | the rest of the line would be lost |
| 5 | Error: `a second /*CDOM_SHIM*/ (the page takes the shim once)` | kept | |
| 6 | Error: `/*CDOM_SHIM*/ with no <script tag before it` | kept | |
| 7 | Error: `no /*CDOM_SHIM*/ line (...)`, at the end | kept | |
| 8 | Error: `no -v lits= (...)` | dropped | `--lits` is a required argument, so this is the usage error |
| 9 | Errors: `cannot read the string table F`, `cannot read the shim F` | kept | |
| 10 | `/*CDOM_WASM*/`, anywhere in a line, becomes the module: a data URL when bundling, the URL as a JavaScript string otherwise | kept | |
| 11 | The URL's JavaScript string escapes only `\` and `"` | kept and hardened: `<`, control bytes, U+2028 and U+2029 are escaped too, as `cdlits` escapes table entries | a URL holding `</script>` or a newline could end the script today |
| 12 | Error: `a second /*CDOM_WASM*/ (the page starts one module)`, on the same line or a later one | kept | |
| 13 | Error: `/*CDOM_WASM*/, and no module to put there`, with neither mode | kept | |
| 14 | Error: `cannot read the module to bundle` (empty standard input) | kept, naming the file | the module is now a file argument |
| 15 | Bundling, a page with no `/*CDOM_WASM*/`: `this page loads X; set APP_WASM_SEPARATE := 1, or start with the bundled module (the /*CDOM_WASM*/ marker)`, where X is the first string literal passed to `cdom.start(` or `cdomBoot(`; or `no /*CDOM_WASM*/ for the bundled module (...)` when no such call is found | kept, same text | tested with an exact line match; cdom never rewrites the app's call |
| 16 | With a URL and no marker, the page is fine (it names its module itself) | kept | |
| 17 | `/*CDOM_TITLE*/` is replaced only when a title is given, HTML-escaped (`&`, `<`, `>`), first occurrence on each line | kept, every occurrence | no page has two on a line |
| 18 | The first error in line order wins; end-of-page errors come last | kept | each refused test page has one fault |
| 19 | The base64 arrives on standard input from `base64 \| tr -d '\n\r'` (GNU base64 wraps, macOS does not) | dropped: the tool encodes | two programs and a portability patch fewer |
| 20 | The URL and the title arrive in environment variables, because `awk -v` rewrites backslashes in its values | dropped: flags | C has no such quirk; the shim and lits paths no longer pass through `-v` either |
| 21 | The recipe writes `out.tmp`, renames on success, removes on failure | moved into the tool | one owner of "no output on error" |
| 22 | The page is read as lines; the output of a failed run is partial text on standard output | dropped: nothing is written until the page is complete | |

New in cdpage: the `/*CDOM_CSS*/` marker and its errors.

- `/*CDOM_CSS*/ must be alone on its line`.
- `a second /*CDOM_CSS*/ (the page takes the stylesheets once)`.
- `/*CDOM_CSS*/ outside a <style> element` (no `<style` tag before it, or a `</style` since).
- `/*CDOM_CSS*/ after /*CDOM_SHIM*/ (the styles come before the shim's script)`.
- `--css` given and no marker in the page (the text is in "The marker, and the two kinds of page").
- `cannot read the stylesheet F`, `F is named twice`, and the per-file refusals above.

The CSS that was written is not scanned for markers, and a marker inside a `.css` file means nothing.

### JSON and ES3

The tool reads flags and files and writes HTML; there is no JSON anywhere on the path.
Its JavaScript output is one string literal and the `</script><script>` boundary, both ES3.

## Rejected alternatives

| Alternative | Why it loses |
| --- | --- |
| Add CSS to `page.awk` | Alex already chose C; awk would need the CSS scanner, base64 would stay a pipeline, and the `-v` backslash quirk would reach CSS paths |
| A `--page` mode in `cdlits` | two unrelated jobs in one file, sharing 25 lines |
| A shared `tools/cdtool.h` for the buffer and `die` | each tool stops being one file compiled by one command; the shared part has no logic that could drift into a bug |
| The tool writes the `<style>` tags itself at a bare `/*CDOM_CSS*/` line in `<head>` | saves the 16 bytes of an empty `<style>` on a default page with no CSS, but the marker is then bare text in `<head>` (invalid HTML at rest), and an app's own page cannot give the element attributes or mix its own rules |
| No marker: insert before `</head>` | rewrites an app's own page without being asked, and guesses where |
| `APP_CSS` defaults to `src/*.css` | no stated order; a stale file ships silently |
| One `<style>` element per file | more bytes, same cascade, and nothing reads them apart |
| A separate `app.css` and a `<link>` (an `APP_CSS_SEPARATE`) | a render-blocking request, a page that no longer opens as one file, and an app's own page can already do it without cdom |
| The CSS as a string-table entry the shim installs | the styles would depend on a script that parses, so the IE 9 and 10 tier and the pre-start paint lose them |
| `#embed` or `.incbin` of the `.css` into the C | gives the highlighting but keeps the three calls, keeps the sheet in the module (base64, 33% larger) and keeps the unstyled first frame |
| Minify | a second CSS implementation to keep correct, for bytes brotli mostly removes |
| Inline `@import` and local `url()` | a CSS-aware resolver for something `APP_CSS` order already expresses; `url()` inlining can come later if an app needs it |

## What Alex's message gets wrong, or leaves out

- **"reduces a few cdom calls" is true and is not a reason.**
  It is three calls out of every call the page makes, once per page load.
  The predicted gain in `cd_main` is hundredths of a millisecond, and the style parse is moved, not removed.
  The reasons that hold are the editor (highlighting, formatting, linting), the styled first paint, and that the sheet is ordinary HTML in every tier.
- **`<style>` in `<head>` is better than inside the mount, and nothing in cdom relies on styles living under the mount.**
  Checked: the shim listens on the mount and never looks at its children by position; no lesson's CSS uses `:first-child`, `:nth-child` or a sibling combinator on a child of the mount; `tutorial/test.mjs` reads `#app *` counts only as a before and after pair; `tools/starter.mjs` uses `#app h1` and `#app .hint`.
  One thing does change: a sheet built by C can be changed or removed by C through its id, and a sheet in `<head>` cannot.
  An app that needs that still builds its own `<style>`; this design removes nothing from the API.
- **The lessons stop being "one C file".**
  `tutorial/README.md` and lesson 1's README say so today, and that sentence has to go: a lesson becomes `src/app.c` and `src/app.css`.
- **The default page is a contract other repos may name, and it changes.**
  Its `<head>` gains a `<style>` element, empty for an app with no `APP_CSS` (16 bytes).
  The other repos' managers need to hear it through the super-coordinator, along with the removal of `tools/page.awk` for any script that runs it directly.
- **Inline styles are fine, with one note.**
  `cd_attr("style", ...)` sets a whole attribute; to change one property later, `cd_set_style` is the cheaper call.
  A page under a Content-Security-Policy needs `style-src 'unsafe-inline'` (or a hash) for the bundled `<style>`, as it already needs for a `<style>` built from C.

## Migration, file by file

There is no backwards compatibility to keep: `tools/page.awk` is deleted, and no lesson keeps a `style()`.

| File | Change |
| --- | --- |
| `tools/cdpage.c` | new: the tool |
| `tools/page.awk` | deleted |
| `cdom.mk` | `CDOM_PAGE_TOOL` and its build rule; `cdom_mk_page` and `cdom_page` call the tool; `APP_CSS` in the header comment, the `%.html` prerequisites and `.cdom-mk-sources`; the header's default-page contract gains the `<style>`; "awk" leaves the list of what the build needs |
| `shim/page.html` | `<style>`, the `/*CDOM_CSS*/` line and `</style>` after the `<title>` line |
| `tools/cdomflags.sh` | builds the tool and exports `CDOM_PAGE_TOOL` |
| `tests/page/run.sh` | new: the tool standing alone, as `tests/lits/run.sh` does for `cdlits` |
| `tests/page/*.css`, `tests/page/css.html` | new fixtures |
| `tests/link/run.sh` | comments name the tool; the default-page golden (`want_page`) gains the style lines; new cases: `APP_CSS` in order, a missing file, a `.css` edit rebuilds, the page opened in Chrome |
| `tests/link/app.mk` | `APP_CSS := $(CSS)` from the command line |
| `tests/link/css.mjs` | new: opens a built page in headless Chrome and checks the cascade order, the `</style` escape and that nothing ran |
| `tests/build.sh` | `page()` calls `$CDOM_PAGE_TOOL` with the unminified shim |
| `bench/build.sh`, `bench/strings/build-st.sh`, `bench/strings/pages-s.sh` | call `$CDOM_PAGE_TOOL` (after the running benchmark is done) |
| `tools/mutate.py` | the eleven `page.awk` mutants, `mk-b64-wrapped` and `mk-page-no-wasm-dep` are re-pointed at `tools/cdpage.c` and the new `cdom.mk` lines; new mutants for the CSS tests |
| `shim/cdom.js`, `shim/cdom.min.js` | the "no string table" message names `tools/cdpage`; the minified shim is regenerated with `node tools/minshim.mjs` |
| `tools/legacy/build.mjs` | the comment and the error in `page` name `tools/cdpage` |
| `tutorial/0[1-7]-*/src/app.css` | new, the lesson's sheet, one declaration per line |
| `tutorial/0[1-7]-*/src/app.c` | `style()` and its call removed; one comment says where the CSS is |
| `tutorial/0[1-7]-*/Makefile` | `APP_CSS := src/app.css` |
| `tutorial/01-hello/README.md` | the bullet "The CSS is a `<style>` element the app builds" becomes the `APP_CSS` explanation |
| `tutorial/0[2-7]-*/README.md` | any quoted `style()` code is removed (check each "the parts that matter" block) |
| `tutorial/README.md` | "each one C file" becomes "a C file and its stylesheet"; the layout sentence names `src/app.css`; the size table is measured again |
| `tutorial/test.mjs` | `SHOTS_START=dir`; every lesson checks that the styles applied before the app started and that the mount holds no `<style>` |
| `tools/new.sh` | writes a fourth file, `src/app.css`; the Makefile gains `APP_CSS := src/app.css`; `app.c` loses its style block; the header comment and the "wrote" line name four files |
| `README.md` | Quick start and "Starting an app" name `src/app.css`; "Building an app" documents `APP_CSS`, the marker, the order, `url()` and the script-tag limit; the diagram's `page.awk` and `base64` nodes become `tools/cdpage`; the default-page contract gains the `<style>` |
| `docs/strings-design.md` | one line at the top of "The page" pointing here |
| `docs/page-tool-results.md` | new, written by the measurement step |

## Predictions

Written 2026-11-17, before anything is built or measured.
The baseline is the tree at the commit before step 1; today's numbers below are from the builds already on disk and from `bench/strings/results/startup-pages.tsv`.
Each prediction is recorded as confirmed, refuted or not measurable, with numbers, in `docs/page-tool-results.md`.

| # | Prediction | Built on | Refuted by |
| --- | --- | --- | --- |
| Q1 | After step 2, every lesson's page is byte-identical to the one `page.awk` wrote | the tool is a port | one differing byte |
| Q2 | Lesson 1's page, brotli -q 11: between 8,780 and 8,870 (today 8,810); every lesson within -40 and +120 bytes of its size before | the same CSS text moves from the string table to a `<style>`; one declaration per line adds about 2 raw bytes a declaration (about 60 raw bytes on lesson 1, 250 on lesson 5), which brotli mostly removes; the module loses about 12 bytes | any lesson more than 120 bytes larger or 40 smaller |
| Q3 | Every lesson's `app.wasm` is 8 to 30 bytes smaller (lesson 1 today 2,452) | three calls and two slot references leave the code; the literals were already out of the module | not smaller, or more than 60 bytes smaller |
| Q4 | Every lesson's string table (`app.wasm.lits`) shrinks by its CSS length plus 13 bytes, within 2 (the `style` entry and two separators) | CSS lengths today: 251, 401, 695, 1,349, 1,981, 1,313, 953 bytes; none holds a byte the table escapes | off by more than 2 on any lesson |
| Q5 | `cd_main` (the `mainms` column of `bench/strings/startup.mjs`, median of 15 fresh processes): lesson 1 lower by 0.00 to 0.05 ms (today 0.615), lesson 5 lower by 0.01 to 0.10 ms | one `createElement`, one text node and one insert that makes the browser parse 0.25 to 2 KB of CSS; that parse moves to the HTML parser, it is not saved | a gain over 0.15 ms on any lesson (the time is going somewhere this design does not understand), or `cd_main` slower beyond the gap between two baseline passes |
| Q6 | First paint and first contentful paint: not measurable (they differed by 6 ms between two identical baseline pages) | same run | a shift over 10 ms either way in two passes |
| Q7 | Comment stripping would save under 30 brotli bytes on every lesson, so it is not added | the lessons carry one or two short comments | over 100 bytes on any lesson: then stripping is designed and added |
| Q8 | The start screenshots of all seven lessons are byte-identical before and after the migration | the same rules reach the same elements; a `<style>` element takes no space | one differing PNG that two baseline runs agree on |
| Q9 | A default page with no `APP_CSS` grows by exactly 16 bytes (`<style>` and `</style>` and their newlines) | the marker line is removed | any other difference |

## Test plan

End to end first: the claims about paint and escaping are checked in headless Chrome on a built page.
Every new test is mutation checked: apply the mutant, see the named assertion fail, restore by re-editing (never `git checkout`).
The mutants go into `tools/mutate.py` so they keep running.

| Test | What it proves | Mutant that must turn it red |
| --- | --- | --- |
| `tests/page/run.sh`, port | for every page in `tests/link/page/` and each mode (bundle, URL, none, with and without a title), the tool's output, exit status and message equal `page.awk`'s; removed with `page.awk`, when the composed goldens below take over | in `cdpage.c`, write the table after the shim; drop the final newline; base64 without padding |
| `tests/page/run.sh`, goldens | the same pages against expected output composed in the script from the parts, and each refused page fails with its message and leaves no file | the `page-*` mutants of `tools/mutate.py`, re-pointed |
| `tests/page/run.sh`, CSS in order | a page with the marker and `--css a.css --css b.css` is the page with `a.css` then `b.css` at the marker line, and both come before the table's script | emit the files in reverse; emit nothing |
| `tests/page/run.sh`, `</style` | a file with `</style>` in a string and `</STYLE>` in a comment yields a page with exactly one `</style` (the page's own) | remove the escape; make the match case-sensitive |
| `tests/page/run.sh`, refusals | each fires with its message and writes nothing: a missing file, a file named twice, no marker with `--css`, the marker outside a `<style>`, after the shim, twice, beside other text; an unterminated comment, an unterminated string, an unclosed `{`, a stray `}`, `@import`, a NUL, invalid UTF-8; and `@import` inside a comment is accepted | remove each check in turn; make the scanner ignore comments (the accepted file is then refused) |
| `tests/page/run.sh`, edges | a byte order mark is dropped; a file with no final newline gets one; an empty file adds nothing; no `--css` removes the marker line | keep the BOM; skip the newline |
| `tests/link/run.sh`, `APP_CSS` | through `cdom.mk` (include first, last, twice): the default page with `CSS="a.css b.css"` equals the golden; with no `CSS` it is the golden with an empty `<style>` | drop `--css` from `cdom_mk_page` |
| `tests/link/run.sh`, missing file | `make` with `CSS=no_such.css` fails, the log names `no_such.css`, and no page is written | take `$$(APP_CSS)` out of `.cdom-mk-sources` (make then names the page, not the file) |
| `tests/link/run.sh`, rebuild | after a build `make -q` is up to date; with the `.css` file's time moved forward `make -q` is not; after an edit and `make` the page holds the new rule; `make -q` on the module is still up to date | take `$$(APP_CSS)` out of the `%.html` prerequisites |
| `tests/link/css.mjs`, in Chrome | the built page of `tests/page/css.html`, from `file://`: `b.css` wins over `a.css` on the same selector; a `::before` content of `"</style><script>window.pwned=1</script>"` reads back exactly and `window.pwned` is undefined; no error; nothing fetched | reverse the order; remove the escape |
| `tutorial/test.mjs`, before the app starts | a script injected before the page's own (`Page.addScriptToEvaluateOnNewDocument`) defines an accessor for `window.cdomLits`; when the table's script assigns it, before the shim has run, it records the body's computed margin, the number of CSS rules and the mount's child count. Every lesson: margin `0px`, rules above 0, mount empty; and at the end `#app style` matches nothing | in `cdpage.c`, write no CSS (margin is `8px`); put `style()` back in one lesson and drop its `APP_CSS` (margin `8px`, and `#app style` matches) |
| `tutorial/test.mjs`, `SHOTS_START=dir` | a screenshot of each lesson as soon as the mount is not empty, two animation frames later, with every `<canvas>` hidden by the harness (lesson 7's start burst is random). Taken twice before the migration to prove the method (the two runs must be byte-identical), then once after; `cmp` on all seven | change one declaration in one `app.css` (for example lesson 1's `margin: 3rem auto` to `4rem auto`): that lesson's `cmp` fails |
| `tutorial/run.sh` (exists) | every lesson still behaves, from disk, with nothing fetched | none new |
| `tools/starter.mjs` through `tests/link/run.sh` (exists) | the starter, with its `app.css`, still centers "cdom" to a pixel | remove `APP_CSS` from the Makefile `tools/new.sh` writes: the centering check fails |
| `LEGACY=1 node tools/legacy.mjs` (exists) | the fallback page still finds its table after the tool change | none new |

Per step, run only what the step can break (each step names it); the whole suite runs once, before the merge.

## Implementation steps

Each step is one executor and leaves the tree building and green.
Sonnet unless marked Haiku.
Every executor: work in `/Users/alex/Dev/cdom-strings` on branch `strings`; commit and push after the step; no em dash; each Markdown sentence on its own line; no `Co-Authored-By` and no session trailer; never edit CHANGELOG or generated files by hand (`shim/cdom.min.js` comes from `node tools/minshim.mjs`); restore a mutant by re-editing; run `git status --short` on a path before touching a file someone else may have open.
Steps that build or open a browser run one at a time on this Mac, and none of them starts while the benchmark is running; ask the dispatcher first.
Only step 1 is safe during a benchmark (one host compile and text files).

1. **cdpage, a port of page.awk** (no CSS yet).
   Write `tools/cdpage.c` to the command line above, without `--css` and the CSS marker, following the keep and drop table row by row.
   Write `tests/page/run.sh`: compile the tool with `cc -O2 -Wall -Wextra -Werror`, then the port rows of the test plan (differential against `tools/page.awk`, using any small file as the module and a one-line text file as the table), then the goldens.
   Nothing else in the tree changes.
   Verify: `tests/page/run.sh`; then each port mutant by hand.
2. **cdom.mk builds pages with cdpage.**
   Before editing, copy the seven built lesson pages aside (`tutorial/run.sh`, then copy each `build/index.html` to `tests/out/page-before/`).
   Add `CDOM_PAGE_TOOL` and its rule, rewrite `cdom_mk_page` and `cdom_page`, put the tool in the `%.html` prerequisites, export it from `tools/cdomflags.sh`, and re-point the `page.awk` and `mk-b64-wrapped` mutants in `tools/mutate.py` at `tools/cdpage.c`.
   Verify: `tests/link/run.sh`; `tutorial/run.sh`; `cmp` of each lesson page against its copy (Q1); `python3 tools/mutate.py -j 1 page-no-shim page-mark-kept page-inline-taken page-two-taken page-none-taken page-url-unchecked page-url-unnamed page-two-wasm-taken page-url-mode-off page-url-unescaped page-title-unescaped mk-page-no-wasm-dep`.
3. **The other callers, and page.awk deleted** (Haiku for the edits, after the benchmark).
   Switch `tests/build.sh`, `bench/build.sh`, `bench/strings/build-st.sh` and `bench/strings/pages-s.sh` to `"$CDOM_PAGE_TOOL"`.
   Change the two messages that name `tools/page.awk` (`shim/cdom.js`, then `node tools/minshim.mjs`; `tools/legacy/build.mjs`) and the comments in `cdom.mk`, `tests/link/run.sh` and `README.md`, including the README diagram and "awk" and `base64` in the list of what a build needs.
   Delete `tools/page.awk` and the differential block of `tests/page/run.sh`.
   Verify: `grep -rn "page\.awk" --exclude-dir=node_modules --exclude-dir=docs .` prints nothing but PERF_LOG.md or RESULTS.md history; `tests/page/run.sh`; `tests/build.sh && node tools/unit.mjs && node tools/legacy.mjs`; `tests/link/run.sh`; `bench/build.sh`.
4. **CSS in the tool.**
   Add `--css`, the `/*CDOM_CSS*/` marker with its five page errors, the per-file scanner and its refusals, the `</style` escape, the BOM and final-newline rules.
   Add the fixtures and the CSS rows of `tests/page/run.sh`.
   No make or page change, so every built page is still byte-identical.
   Verify: `tests/page/run.sh`; each CSS mutant of the test plan's `tests/page/run.sh` rows, by hand, then added to `tools/mutate.py`.
5. **APP_CSS in cdom.mk and the default page.**
   Add the marker to `shim/page.html`; add `APP_CSS` to `cdom_mk_page`, the `%.html` prerequisites, `.cdom-mk-sources`, `cdom_page`'s fifth argument and the header comment (the variable, the marker, the default-page contract).
   Update `tests/link/app.mk` and the `want_page` golden, and add the `APP_CSS`, missing-file and rebuild cases and `tests/link/css.mjs` to `tests/link/run.sh`.
   The lessons still build their own `<style>`, and still pass.
   Verify: `tests/link/run.sh`; `tutorial/run.sh`; the three `cdom.mk` mutants and the two `css.mjs` mutants; Q9 by `cmp` against a golden.
6. **The screenshot harness.**
   Add `SHOTS_START=dir` to `tutorial/test.mjs` as the test plan describes.
   Run it twice on the unmigrated lessons into `tutorial/out/start-a` and `tutorial/out/start-b`, and `cmp` the seven pairs.
   If a pair differs, find the unstable region, hide it in the harness and say so in the commit; do not go on until two runs agree.
   Record each lesson's page bytes, brotli size (`brotli -q 11 -c build/index.html | wc -c`), `app.wasm` and `app.wasm.lits` sizes in `docs/page-tool-results.md` as the "before" table.
   Verify: `SHOTS_START=tutorial/out/start-a tutorial/run.sh && SHOTS_START=tutorial/out/start-b tutorial/run.sh && for f in tutorial/out/start-a/*.png; do cmp "$f" "tutorial/out/start-b/$(basename "$f")"; done`.
7. **The seven lessons move to src/app.css.**
   In the same session as its own baseline: `SHOTS_START=tutorial/out/start-before tutorial/run.sh` first.
   For each lesson: write `src/app.css` from the literals (one declaration per line, the rules in the same order, lesson 7's C comment as a CSS comment), remove `style()` and its call, add the comment, add `APP_CSS := src/app.css` to the Makefile.
   `tutorial/01-hello/src/.app.c.swp` exists today: someone has that file open, so check with the dispatcher before editing it.
   Add the "before the app starts" check to `tutorial/test.mjs`, and update the lesson READMEs and `tutorial/README.md` (wording now, the size table in step 9).
   Verify: `SHOTS_START=tutorial/out/start-after tutorial/run.sh`; `cmp` of the seven pairs (Q8); `grep -n 'cd_open("style")' tutorial/*/src/app.c` prints nothing; the two "before the app starts" mutants and the screenshot mutant.
8. **The starter.**
   `tools/new.sh` writes `src/app.css` and `APP_CSS`, and its `app.c` loses the style block; update its messages and header, the starter cases and messages in `tests/link/run.sh`, and the README's Quick start and "Starting an app".
   Verify: `tests/link/run.sh` (it generates the starter, builds it and runs `tools/starter.mjs`); the starter mutant.
9. **Measure, and write the results** (Haiku for the collection, after any benchmark).
   Fill the "after" table of `docs/page-tool-results.md` with the same commands as step 6, run `bench/strings/startup.mjs` on lessons 1 and 5 before (from the commit before step 7, in a temporary worktree) and after, measure what comment stripping would save with a throwaway script, and mark Q1 to Q9 confirmed, refuted or not measurable.
   Update the size table in `tutorial/README.md`, and add the pointer line to `docs/strings-design.md`.
   Verify: every Q row has a verdict and its numbers.
10. **The whole suite, once.**
    `tests/page/run.sh`, `tests/lits/run.sh`, `tutorial/run.sh`, `tests/build.sh`, `node tools/unit.mjs`, `node tools/fuzz.mjs`, `node tools/canvas.mjs`, `node tools/host.mjs`, `tests/link/run.sh`, `node tools/legacy.mjs`, and `python3 tools/mutate.py` for the page and CSS mutants.
    Then the dispatcher tells the other repos, through the super-coordinator, that the default page gained a `<style>` and that `tools/page.awk` is gone.

## Risks

- **The port is not exact.**
  `page.awk` has quiet behaviours (the final newline, CR on marker lines, first-error order).
  Step 1's differential and step 2's byte comparison of real pages exist for this; the awk file is deleted only after both pass.
- **The CSS scanner refuses a valid sheet.**
  Its grammar is five states, but CSS has corners (an unquoted `url()` holding `/*`, a backslash before a quote, a newline ending a string).
  Each corner is a fixture in `tests/page/`, and the refusal messages name file and line so a false refusal is easy to report.
- **The screenshot comparison is not stable.**
  A focused field's caret or a running clock could differ between runs; step 6 proves the method on unmigrated lessons before step 7 depends on it.
- **Other repos run `page.awk` directly or assert on the default page's bytes.**
  They are outside this tree; step 10 ends with the message to their managers.
- **A benchmark is running while this is written.**
  Steps 2 and later build or open browsers, and step 3 edits the benchmark's own build scripts, so they wait for it.
