# Strings: plain literals in C, a table baked into the page

Status: design, 2026-11-17, not implemented.
Branch it was written on: `tutorial` (0613c62).

## The problem

An app writes `#define L(s) CD_LIT(s)` and then `cd_open(L("div")); cd_text(L("Hello"));`.
Every string needs a wrapper, and the author has to choose between `CD_LIT`, `cd_intern`, `cd_internz`, `cd_tmp` and `cd_tmpz`.
At run time each literal is hashed and copied into a pool in C, copied again into a batch buffer, and decoded by the shim.

Alex's direction: the author writes a plain string in C, a build step turns each literal into a string id in the final wasm, and the table of literal strings is baked into `index.html`, so the shim never decodes a literal at run time.

## The design in one page

The author writes `cd_open("div"); cd_text("Hello");`.

1. **In the header.**
   Every cdom function that takes a string is also a macro of the same name that passes each string argument through `cd_s()`.
   `cd_s(x)` decides at compile time what `x` is.
   A string literal becomes a 4-byte slot in a data section named `cd_lits`, and the value passed on is the slot's address.
   The slot points at the literal's source spelling (`#x`, the preprocessor's stringification), which goes in a second section, `cd_chars`.
   Anything else of type `char *` becomes a runtime string: `cd_tmpz(x)`.
   A `cd_str` passes through unchanged.
2. **At link time.**
   wasm-ld gathers every slot into one contiguous range, `__start_cd_lits` to `__stop_cd_lits`, and drops the slots of functions that are never called.
3. **After the link, `tools/cdlits` (C, run by `cdom.mk`).**
   It reads the slots from the linked module, turns each spelling into its bytes with a small C string-literal lexer, removes both sections from the module, and writes the table as JavaScript text.
   The final wasm holds no byte of any literal that went to cdom: only `i32.const <slot address>` at each call site.
4. **In the page.**
   `tools/page.awk` puts the table where the page's `/*CDOM_WASM*/` marker is, next to the module, as plain text outside the base64.
5. **In the shim.**
   `lits[(s - base) >> 2]` is the string.
   A literal is never decoded, never copied and never crosses the bridge.
   A runtime string is read from C memory by the operation that uses it: no batch, no copy in C, no extra crossing.

The intern table, the hash, the pool, the batch buffers and the `strings` import are deleted.

## Evidence from the prototype

Measured on 2026-11-17 with Homebrew clang 23.1.5 and wasm-ld, in the session scratch directory (not durable; the parts that matter are reproduced in this file).

| Question | Result |
| --- | --- |
| Does the macro compile and classify at `-O0`, `-Oz` and `-Oz -flto`? | Yes, and the three builds produce the same 10 slots and the same 34 operations with the same strings. |
| Is a literal classified in the front end, the same way at every optimization level? | Yes: `__builtin_choose_expr` needs a constant, so the choice cannot depend on inlining. A literal passed through an inlined helper is a runtime string at `-O0` and at `-Oz`. |
| Does wasm-ld give `__start_cd_lits` and `__stop_cd_lits`, and export them with `--export-if-defined`? | Yes, as immutable i32 globals. |
| Are slots of unreferenced functions dropped? | Yes: `--gc-sections` removed both literals of a function nothing called. |
| Does a mutable static buffer get cached by mistake? | No: `static char mut[8] = "mut"` is spelled `mut`, not `"..."`, so it is a runtime string, and the page showed `nut` after C changed it. |
| Does the stripped module validate and run? | Yes (`wasm-validate`, and run in Node): 1,028 bytes became 719, with no literal byte left in it. |
| Does the tool's lexer agree with clang? | Yes on the torture literal (simple escapes, `\x41`, `\102`, `−`, `\U0001F600`, raw UTF-8, `\e`, `\?`, `"\x1" "2"`, a macro that expands to a literal, the empty literal), checked against the compiler's own bytes in the check build described below. |
| Is the macro's argument evaluated once? | Yes: `cd_text(next())` and `cd_text(*p++)` each make one call and one increment. |
| Is a non-string a compile error? | Yes: `cd_text(id)` with a `uint32_t` fails with "incompatible integer to pointer conversion". |

Bytes, the page as served (brotli -q 11), today against the same page with its literals moved out of the wasm into JS text:

| Lesson | Literal bytes | Today | Literals baked, removed from the wasm | Literals baked, left in the wasm too |
| --- | ---: | ---: | ---: | ---: |
| 01-hello | 399 | 8,942 | 8,829 (-1.3%) | 9,163 (+2.5%) |
| 02-counter | 468 | 10,021 | 9,838 (-1.8%) | 10,227 (+2.1%) |
| 04-regions | 2,007 | 11,557 | 10,907 (-5.6%) | 12,334 (+6.7%) |
| 05-todo | 2,229 | 15,119 | 14,527 (-3.9%) | 15,874 (+5.0%) |
| 06-stopwatch | 1,456 | 14,275 | 13,808 (-3.3%) | 14,844 (+4.0%) |

Text inside base64 compresses badly, so each literal byte moved from the bundled wasm to JS text saves about 0.3 bytes of brotli.
Baking only wins if the literals leave the wasm: that is why the tool strips them, and why the literal bytes need sections of their own.

Times (Node, best of 7 runs of 2,000,000):

| Operation | Cost |
| --- | ---: |
| Literal lookup, `lits[(s - base) >> 2]` | 1.5 ns |
| Literal lookup through a `Map` keyed by address | 10.2 ns |
| Runtime string, 1 byte, char loop | 4.7 ns |
| Runtime string, 9 bytes, char loop | 63 ns |
| Runtime string, 16 to 200 bytes, `TextDecoder` | 84 to 90 ns |
| Installing a table of 300 unique strings and 600 slots | 24 us, once |

Code size (clang `-Oz`, object code section): seven literal arguments cost 193 bytes with `CD_LIT` today and 94 bytes as slots, about 14 bytes less per literal.
The string machinery in `cdom.c` that this design deletes is about 1,500 bytes of code (`cd_intern` 726, `push_raw` 189, `cd_tmp` 110, `ref_name` 82, `send_strings` 71, `htab_put` 66, `utf16_len` 61, `push_str` 52, `cd_bytes` 42, `cd_lit_` 35 and the small ones), plus two static buffers of 256 KB and 48 KB (`sbytes`, `smeta`).

## How a literal is told from a runtime string

The rule is the compiler's, not the documentation's.

```c
#define CD_LITQ(s) (__builtin_strncmp(CD_SPELL(s), "\"", 1) == 0 && \
                    __builtin_constant_p(__builtin_strlen(CD_CHARP(s))))
```

An argument is a literal when both hold:

- its spelling after macro expansion starts with `"` (so it is written as a literal, or is a macro that expands to one);
- the compiler can fold `strlen` of it (so every byte up to the terminator is a compile-time constant).

Everything else is a runtime string and is read from memory each time it is used.

| What the author passes | Classified as | What happens |
| --- | --- | --- |
| `"div"`, `"a" "b"`, `GREETING` where `#define GREETING "hi"` | literal | a slot; in the page's table; never decoded |
| a stack buffer, a `const char *` parameter, `names[i]` | runtime | read from C memory by the op that uses it |
| `static char buf[8]`, changed later | runtime | read each time, so it is never stale |
| `static const char NAME[] = "x"` | runtime | correct, read each time (its spelling is `NAME`) |
| `sel ? "danger" : ""` | runtime | correct, read each time; `sel ? cd_s("danger") : cd_s("")` makes both literals |
| a literal passed through a helper `label(const char *s)` | runtime | correct, read each time, at every optimization level |
| a `cd_str` (from `cd_s`, `cd_tmp`, `cd_tmpz`) | itself | passed through |
| an integer, a node id, a struct | | compile error |
| `"abc" + 1`, `L"wide"` and other spellings that start with `"` but are not only literals | literal by the macro | `cdlits` stops the build: "not a string literal: ..., write the literal itself or pass cd_tmpz(...)" |

There is no way to pass a runtime buffer "where a literal is expected": the same parameter takes both, and the wrong classification cannot happen silently.
The worst outcome of writing a string in an unusual way is a string that is read on each use (5 to 90 ns) where a literal would have cost 1.5 ns.

Why not `__builtin_constant_p` alone: the address of a mutable `static char buf[]` is a constant, and after optimization the compiler may or may not prove its bytes constant, so the answer could change with the optimization level.
The spelling test is purely textual and the `strlen` fold is evaluated in the front end (it sits in `__builtin_choose_expr`, which requires a constant), so the classification is the same at `-O0`, `-Oz` and under LTO.

Why the spelling and not the literal itself goes in the section: `static const char c[] = s;` only compiles when `s` is a literal, and both branches of `__builtin_choose_expr` and of `_Generic` are type-checked, so that line would make every runtime argument a compile error (tried: "array initializer must be an initializer list or string literal").
`#s` is a valid initializer whatever `s` is, and the unchosen branch emits nothing.
The price is that `cdlits` must turn a spelling into bytes exactly as clang does; the test plan has a differential check for that, against clang's own bytes.

## The API as it appears in cdom.h

```c
/* ---- strings ----
 * A string argument is written as plain C: cd_open("div"), cd_text(name), cd_text(cd_tmp(buf, n)).
 * Every function below that takes a cd_str is also a macro of the same name that passes each string
 * argument through cd_s(), so the functions are never called with a cd_str made by hand.
 *
 * A string literal costs nothing at run time: the build (tools/cdlits, run by cdom.mk) moves it out of
 * the module into the page's string table, and the argument is its slot. A literal is an argument spelled
 * as one ("...", adjacent literals, or a macro that expands to them).
 * Any other char pointer is a runtime string: its bytes (UTF-8, to the terminating 0) are read from C
 * memory by the call that uses it. Nothing is kept, so a string may change between calls.
 * cd_tmp is the same for bytes with a length and no terminator.
 *
 * A cd_str from a literal is good for the life of the page. A cd_str from a runtime string (cd_tmp,
 * cd_tmpz, or cd_s of a non-literal) is good for the next call that takes a string: that call reads the
 * bytes, and when it returns every such cd_str is dead (using one later throws out of the call that
 * uses it). The bytes must stay unchanged until that call. At most CD_TMP_CAP are alive at once. */
typedef struct { uint32_t v; } cd_str;  /* a slot address, or 0x80000000 | a serial; never made by hand */
#define CD_NONE ((cd_str){0})          /* no string: the path that reads the object itself */
#define CD_TMP_CAP 32
cd_str cd_tmp(const char *s, uint32_t len);
cd_str cd_tmpz(const char *s);          /* traps on a null pointer */

/* cd_s(x): the cd_str of x. Call it yourself only to keep a literal in a variable or to choose between
 * literals: cd_set_class(id, sel ? cd_s("danger") : cd_s("")). It is a statement expression, so it
 * works inside a function, not in a file-scope initializer. */
#define CD_SPELL_(s) #s
#define CD_SPELL(s) CD_SPELL_(s)
#define CD_CHARP(s) _Generic((s), cd_str: (const char *)0, default: (s))
#define CD_ASSTR(s) _Generic((s), cd_str: (s), default: (cd_str){0})
#define CD_LITQ(s) (__builtin_strncmp(CD_SPELL(s), "\"", 1) == 0 && \
                    __builtin_constant_p(__builtin_strlen(CD_CHARP(s))))
#ifdef CD_LITS_CHECK /* the check build: each slot also holds the compiler's own bytes (cdlits --check) */
typedef struct { const char *spell, *real; } cd_lit_slot;
#define CD_REAL(s) , (__builtin_constant_p(__builtin_strlen(CD_CHARP(s))) ? CD_CHARP(s) : 0)
#else
typedef struct { const char *spell; } cd_lit_slot;
#define CD_REAL(s)
#endif
#define CD_LIT_(s) ({ \
  static const char cd_c_[] __attribute__((section("cd_chars"), aligned(1))) = CD_SPELL(s); \
  static const cd_lit_slot cd_l_ __attribute__((section("cd_lits"))) = { cd_c_ CD_REAL(s) }; \
  (cd_str){(uint32_t)(uintptr_t)&cd_l_}; })
#define cd_s(s) _Generic((s), cd_str: CD_ASSTR(s), \
  default: __builtin_choose_expr(CD_LITQ(s), CD_LIT_(s), cd_tmpz(CD_CHARP(s))))

void cd_open(cd_str tag);
void cd_text(cd_str s);
void cd_attr(cd_str name, cd_str value);
/* ... every declaration as today, then, after all of them: */
#define cd_open(tag) (cd_open)(cd_s(tag))
#define cd_text(s) (cd_text)(cd_s(s))
#define cd_attr(name, value) (cd_attr)(cd_s(name), cd_s(value))
#define cd_class(value) (cd_class)(cd_s(value))
#define cd_open_id(tag) (cd_open_id)(cd_s(tag))
#define cd_value(s) (cd_value)(cd_s(s))
#define cd_tpl_text(t, k, s) (cd_tpl_text)(t, k, cd_s(s))
#define cd_set_text_node(id, s) (cd_set_text_node)(id, cd_s(s))
#define cd_set_attr(id, name, value) (cd_set_attr)(id, cd_s(name), cd_s(value))
#define cd_set_class(id, value) (cd_set_class)(id, cd_s(value))
#define cd_set_style(id, prop, value) (cd_set_style)(id, cd_s(prop), cd_s(value))
#define cd_set_style_num(id, prop, v, unit) (cd_set_style_num)(id, cd_s(prop), v, unit)
#define cd_encode_image(px, w, h, n, mime, token) (cd_encode_image)(px, w, h, n, cd_s(mime), token)
#define cd_encode_image_in(px, w, h, n, W, H, x, y, mime, token) \
  (cd_encode_image_in)(px, w, h, n, W, H, x, y, cd_s(mime), token)
#define cd_set_image_bytes(id, prop, p, len, mime, token) \
  (cd_set_image_bytes)(id, cd_s(prop), p, len, cd_s(mime), token)
#define cd_v_str(s) (cd_v_str)(cd_s(s))
#define cd_set(id, path, v) (cd_set)(id, cd_s(path), v)
#define cd_set_num(id, path, n) (cd_set_num)(id, cd_s(path), n)
#define cd_set_str(id, path, s) (cd_set_str)(id, cd_s(path), cd_s(s))
#define cd_set_bool(id, path, b) (cd_set_bool)(id, cd_s(path), b)
#define cd_call_args(id, method, n, a) (cd_call_args)(id, cd_s(method), n, a)
#define cd_call(id, method) (cd_call)(id, cd_s(method))
#define cd_get_num(id, path) (cd_get_num)(id, cd_s(path))
#define cd_get_str(id, path, dst, cap) (cd_get_str)(id, cd_s(path), dst, cap)
#define cd_get_id(id, path) (cd_get_id)(id, cd_s(path))
```

Notes on the API:

- The macro has the function's name, so `cd_text("x")` is what the author writes and reads, and `(cd_text)` (in parentheses) is the function, for a function pointer.
- `cd_str` becomes a struct, so a `cd_id` passed as a string, a `cd_str` stored in a `uint32_t`, and `a != b` on two strings are compile errors.
  Today all three compile and the last one silently depends on interning.
- Path 0 ("read the object itself") becomes `CD_NONE`: `cd_get_num(CD_RESULT, CD_NONE)`.
  A literal `0` there compiles (it is a null pointer) and traps in `cd_tmpz`, so a missed call site fails loudly on first use.
- A table of names is a `const char *` table read at run time, or a `cd_str` table filled inside a function with `cd_s("...")`.
- `cd_fmt` and `cd_fmt_len` are unchanged: they write bytes and return a length, which goes to `cd_tmp(buf, n)`.
- The optional modules (`cdom_net.h`, `cdom_store.h`, `cdom_url.h`, ...) take `(pointer, length)` already and do not change.
  The I/O contract (`tests/link/contract.c`) names no string function, so it is not touched.

### What is deleted

| Deleted | Replaced by |
| --- | --- |
| `CD_LIT(lit)`, `cd_lit_`, and every app's `#define L(s) CD_LIT(s)` | the literal itself |
| `cd_intern(s, len)` | `cd_tmp(s, len)` at each use; the app keeps its own bytes |
| `cd_internz(s)` | `s` |
| `cd_bytes(cd_str, &len)` | the app's own bytes |
| `cd_str_count()`, `cd_str_bytes()` | nothing: there is no table in C |
| `CDOM_INTERN_TEXT` and the `cdom-pertext` benchmark entry | nothing: a literal is always in the table, a runtime string is always read on use |
| in `cdom.c`: `pool`, `strs`, `sent`, `htab`, `hash_bytes`, `htab_put`, `utf16_len`, `push_raw`, `push_str`, `send_strings`, `sbytes`, `smeta`, `str_meta`, `ref_name`, `ref_text`, `tmp_check`, `svg_tag`, `svg_depth`, `unit_ids`, the `strings` and `open_svg` imports | a 264-byte record block for runtime strings |
| in the shim: `interned`, `transient`, `keep`, `decode`, `strings`, `stringOf`, `open_svg`, `SMALL` as a batch threshold | `lits`, `str`, `readString` |
| `tests/link/lit_ok.c`, `tests/link/lit_var.c` | `tests/lits/` (below) |
| `micro/svg/` | a dated entry in PERF_LOG.md (the namespace check moves to the shim, below) |

`cd_tmp` and `cd_tmpz` keep their names and arguments: the name says how long the result lives, and the existing callers (10 in amgi) stay as they are.

## cdom.c

- **Runtime strings.**
  One static block: `struct { uint32_t serial0, n; struct { uint32_t p, len; } r[CD_TMP_CAP]; } tmp;`.
  `cd_tmp(p, len)` traps when `tmp.n == CD_TMP_CAP`, writes `r[tmp.n] = {p, len}` and returns `{0x80000000 | ((tmp.serial0 + tmp.n++) & 0x7fffffff)}`.
  `cd_tmpz` traps on null, measures, and calls `cd_tmp`.
  No byte is copied.
- **Every function that takes a `cd_str`** passes `s.v` to its import and then calls `tmp_done()`: `tmp.serial0 += tmp.n; tmp.n = 0;`.
  That is the whole lifetime rule: a serial that is not in the current window is stale, and the shim throws on it.
- **The layout block** (`CD_LAYOUT`): the three string entries (`sbytes`, `smeta` and its fields) are replaced by the address of `tmp` and the offsets of `serial0`, `n`, `r`, and of `p` and `len` in a record, and its size.
  The shim and the module are always the same cdom, and `readLayout` already throws on a count mismatch.
- **The stamp.**
  `volatile uint32_t cd_lits_stamp = 0x7374696c;` and `EXPORT("cd_lits") uint32_t cd_lits(void) { return cd_lits_stamp; }`.
  `cdlits` overwrites the word in the data segment with the hash of the table it wrote.
  `volatile` and a non-zero initial value keep it a real word in `.data` under LTO.
- **SVG.**
  C can no longer compare a tag with "svg": a literal's bytes are not in the module.
  The namespace moves to the shim: `open(tag)` does `const name = str(tag); if (svgDepth || name === "svg") { svgDepth++; ...createElementNS } else ...createElement`, `clone` does `if (svgDepth) svgDepth++`, `close` does `if (svgDepth) svgDepth--`.
  The `open_svg` import and C's `svg_depth` go.
  PERF_LOG.md measured 7 ns per open in favour of C against a JS check of the tag and the parent's `namespaceURI`; the new check is one counter and one string compare, with no DOM read.
- **Units.**
  `cd_set_style_num` passes the unit's index, and the shim has `const UNITS = ["", "px", "%", "deg", "em", "rem", "vw", "vh", "ms", "s", "rad"]`.
  `UNIT_STR` stays in C for `cd_fmt`.
  A unit test sets one style per unit and reads it back, so the two lists cannot drift.
- `ready()` becomes `walk_done()`, and `cd_pending()` reports the held walk only.

## tools/cdlits.c

One C file, built for the host by `cdom.mk` (`HOST_CC ?= cc`), no dependencies, no JSON.

```
cdlits in.wasm out.wasm out.lits     the build step
cdlits --check in.wasm               a module built with -DCD_LITS_CHECK: compare, write nothing
cdlits --list in.wasm                print "slot<TAB>unique index<TAB>spelling", for tests and for looking
```

Steps of the build form:

1. Parse the sections.
   Globals (immutable `i32.const` initializers), exports, data (active segments at `i32.const` offsets in memory 0; anything else is an error), data count.
2. Find `__start_cd_lits`, `__stop_cd_lits`, `__start_cd_chars`, `__stop_cd_chars` and `cd_lits_stamp` among the exported globals.
   No `cd_lits_stamp`: "not a cdom module (link src/cdom.c with CDOM_LDFLAGS)".
   No `__start_cd_lits`: the module has no literal; the table is empty.
3. Read memory through an overlay of the data segments, where a byte no segment covers is 0 (wasm-ld trims trailing zeros and splits segments at runs of zeros: the prototype saw `cd_chars` in three segments).
4. For each 4-byte slot from start to stop: the pointer must lie in `cd_chars`; lex the spelling there.
   The lexer accepts a sequence of string literals separated by white space, each optionally prefixed `u8`, and nothing else.
   Escapes: `\a \b \f \n \r \t \v \\ \' \" \?`, `\e` (27, as clang), one to three octal digits, `\x` with one or more hex digits (value up to 255), `\uXXXX` and `\UXXXXXXXX` as UTF-8; any other byte is itself.
   Errors, each naming the spelling: not a string literal; a NUL inside a literal; bytes that are not UTF-8.
5. Deduplicate: `unique[]` in order of first use, and `map[slot] = index in unique`.
6. Stamp: FNV-1a 32 over every slot's bytes and a 0 after each, in slot order; 0 becomes 1.
   Find the 4 bytes of `cd_lits_stamp` in a data segment (they must read `0x7374696c`, else "already processed") and overwrite them.
7. Remove the two address ranges from the data section: a segment inside a range is dropped, a segment that overlaps one is cut (its head and tail kept as two segments), so a module that another tool repacked still works.
   Update the data count section.
   Memory layout does not change: the addresses stay reserved and read as 0.
8. Remove the five exports.
9. Write `out.wasm`, then `out.lits`, one line of JavaScript text:

   ```
   BASE,STAMP,"☃","unique strings joined by \x00","map as decimals joined by ,"
   ```

   `BASE` is `__start_cd_lits` (0 with no literal).
   Strings are written with `\\`, `\"`, `\x3c` for `<` (so `</script>` and `<!--` cannot end the script), `\xNN` for bytes below 32 and 127, ` ` and ` ` escaped, and every other byte as it is (UTF-8).
   The separator is always written `\x00`, never `\0`, so a digit after it cannot make an octal escape.
   The third field is U+2603 as raw UTF-8: the shim compares it with `"☃"`, and a page served in another encoding fails at start with a message instead of showing wrong text.
10. Both outputs are written to temporary names and renamed, `out.wasm` last.

With `--check`, slots are 8 bytes (`spell`, `real`); for each slot the lexed bytes must equal the bytes at `real` up to its 0, else exit 1 naming the spelling and both byte strings.

The reference for the lexer, the overlay and the strip is the Python prototype reproduced in the appendix; the C tool must give byte-identical `out.wasm` and `out.lits` for the torture module.

## The page: cdom.mk, page.awk, the shim, the boot

- **`CDOM_LDFLAGS`** gains `-Wl,--export-if-defined=__start_cd_lits`, `=__stop_cd_lits`, `=__start_cd_chars`, `=__stop_cd_chars` and `=cd_lits_stamp`.
- **`CDOM_LITS := $(CDOM)/tools/out/cdlits`**, built by a rule from `tools/cdlits.c` with `$(HOST_CC) -O2 -Wall -Wextra -Werror` to a temporary name and renamed (several apps may build it at once); `tools/out/` is ignored by git.
- **The `APP_WASM` rule** links to `$@.linked`, runs `$(CDOM_LITS) $@.linked $@ $@.lits`, and removes `$@.linked`.
  `$(call cdom_lits,linked.wasm,out.wasm)` is the same step for an app's own rules, and `tools/cdomflags.sh` hands it to cdom's own build scripts as `CDOM_LITS`.
- **`/*CDOM_WASM*/`** still stands for `cdom.start`'s first argument, which is now the module as the build wrote it, an ES3 array literal:

  ```
  ["data:application/wasm;base64,...", BASE, STAMP, "☃", "strings", "map"]
  ```

  `page.awk` takes `-v lits=path/to/app.wasm.lits` and writes `[` the URL `,` the file's one line `]`.
  With `APP_WASM_SEPARATE` the first element is the URL and the rest is the same.
  A page that names its module by URL itself (`cdom.start("app.wasm", ...)`) is now an error in both modes, as it already is when bundling: the page must hold the marker.
  `$(call cdom_page,in.html,out.html,module.wasm)` reads `module.wasm.lits`.
- **`cdom.start(module, mount, env, options)`.**
  `module[0]` is the URL or the wasm2js factory.
  A string or a function instead of an array throws: "cdom: start takes the module as the build wrote it (the /*CDOM_WASM*/ marker): a bare URL has no string table".
  Before `cd_start`: the canary must equal `"☃"`, else "cdom: this page was not read as UTF-8"; `exports.cd_lits() >>> 0` must equal `module[2]`, else "cdom: the page's string table is not this module's (the page and the module are from different builds, or the module did not go through cdlits)".
  Then `const unique = module[4].split("\0"); lits = module[5] ? module[5].split(",") : []; for (i...) lits[i] = unique[lits[i]];` and `litBase = module[1]`.
- **`str(s)`**, the one way an import reads a string:

  ```js
  function str(s) {                       // s is an i32: a slot address, or negative for a runtime string
    if (s > 0) return lits[(s - litBase) >> 2];
    if (buffer !== memory.buffer) views();
    const k = ((s & 0x7fffffff) - u32[tmpSerial0]) & 0x7fffffff;
    if (k >= u32[tmpCount]) throw new Error("cdom: a cd_tmp string used after the call it was made for");
    const at = tmpRecords + k * 2;
    return readString(u32[at], u32[at + 1]);
  }
  ```

  `readString(ptr, length)`: up to 12 bytes, a char loop that ORs the bytes and falls back when one is 128 or more; otherwise one `TextDecoder` call on a subarray (the measurements above set the 12).
- **Paths** (`segmentsOf`): `CD_NONE` is no segments; a literal's split is cached by slot index; a runtime path is split each time, as a one-off path is today.
- **`shim/boot.js`**: `cdomBoot(module, mount, env, ready)` passes the array through, loads `module[0] + ".js"` without WebAssembly, and puts the factory in `module[0]`.
  It stays ES3.

### Old browsers

The no-WebAssembly path (the ES5 shim with polyfills, wasm2js, and the plain-array tier for IE 9 and 10) needs nothing new.
The table is an ES3 array of ES3 string literals in the page; the shim's new code uses `split`, array indexing, the typed views and `TextDecoder`, all of which that path already has or polyfills.
`tools/legacy/build.mjs app` compiles the stripped module, which has two data segments fewer.
The page must be read as UTF-8 for non-ASCII literals, as it must for any text in it, and the canary turns a wrong encoding into an error at start.

## The alternatives, and why each loses

**Where the rewrite happens.**

| Route | Verdict |
| --- | --- |
| The compiler places, the linker gathers, a post-link tool extracts and strips (chosen) | Uses only documented behaviour: section attributes, `__start_`/`__stop_` symbols, `--gc-sections`. No code is rewritten: the id in the code is the slot's address, exactly as the linker wrote it, so nothing the optimizer does with that constant (a lookup table of them, a select between two) can go wrong. |
| A clang plugin or an LLVM pass | It would see each literal as it reaches a `cd_*` parameter, which is the ideal view, but a plugin is C++ built against one exact LLVM, breaks on each Homebrew update, and cannot load into a clang built without plugin support. Rejected for robustness. |
| A source preprocessor (rewrite `cd_text("x")` to `cd_text(17)` before compiling) | It has to parse C well enough to know which arguments are string parameters through macros, wrappers and conditionals, and every editor and debugger then sees different source from what compiles. Rejected. |
| Rewriting constants in the linked wasm with `--emit-relocs` | It could turn each slot address into a dense id per unique string (so `lits[id]` with no subtraction and no map). It needs the linking section's symbol table and both reloc sections parsed, and it breaks if the optimizer ever derives one address from another. The gain over the chosen route is one subtraction and a shift per use (inside 1.5 ns) and the per-slot map in the page (about 1 byte of brotli per call site). Rejected: more machinery and a new failure mode for no measurable gain. |
| Slots that point at the real literals in `.rodata`, stripped by reference analysis with `--emit-relocs` | It would avoid the lexer, since the bytes are the compiler's. But the linker merges identical literals, so a cdom literal shares its bytes with any other use of the same text, and proving a string unreferenced needs the full reloc and symbol analysis. A hole in that analysis silently blanks a string some other code reads. The lexer's failure mode is checkable (the check build compares every literal with the compiler's bytes); this one is not. Rejected. |

**Where the strings live at run time.**

| Route | Verdict |
| --- | --- |
| Baked into the page as JS text, stripped from the wasm (chosen) | 1.3% to 5.6% fewer page bytes on the lessons, from the strings alone; no decode at run time; the wasm holds no literal byte. |
| Baked into the page, left in the wasm too | 2.1% to 6.7% more page bytes (measured). Rejected. |
| The shim reads the slots from wasm memory once at load (no tool) | The simplest build: no host tool, no page change, and it works with a bare URL. It costs one decode of the table at start (tens of microseconds) and keeps the strings inside the base64, so it gives up the 1.3% to 5.6%. It is the fallback if the tool turns out to be a maintenance problem; the header and the shim's lookup would not change. Rejected in favour of baking because the bytes are measurably better and Alex's direction is to bake. |
| The address of the literal itself as the id, cached by the shim on first sight | It needs the shim to know which addresses hold immutable bytes. wasm-ld has no symbol for the end of `.rodata`, and a marker object's position is not guaranteed, least of all under LTO. A `Map` keyed by address also costs 10.2 ns per use against 1.5. Rejected. |
| A wasm custom section holding the table | No skew is possible when the module is a separate file, and no page change is needed. But a bundled page would carry it inside the base64 again, the wasm2js path has no module object to read it from, and it would be a second carrier beside the page. One carrier with a stamp is simpler. Rejected. |

**The shape of the API.**

| Route | Verdict |
| --- | --- |
| Macros named like the functions, classifying each argument (chosen) | `cd_text("x")` and `cd_text(buf)` both work, and neither can be misclassified. |
| Literals only, runtime strings always wrapped by hand | The compiler would enforce it, but `cd_text(name)` is exactly the plain C Alex asked for, and the wrapped form adds nothing the classification does not already know. Rejected. |
| Keep `cd_intern` for strings used many times | The benchmark's own numbers (RESULTS.md, "Interned strings against per-op strings") show interning ties everywhere but one benchmark, and it leaks: nothing ever leaves the table. Two ways to pass a runtime string is what this change removes. If prediction P5 below is refuted, the answer is a handle the app owns and frees (`cd_keep`, `cd_drop`), designed then, not an intern table. |

## Migration, file by file

In cdom:

| File | Change |
| --- | --- |
| `src/cdom.h` | the strings section above; `cd_str` a struct; the wrapper macros; `CD_NONE`; comments that name `CD_LIT` or interning (the header comment on paths, `cd_text`'s, `cd_set_style`'s) |
| `src/cdom.c` | the "cdom.c" section above |
| `shim/cdom.js`, `shim/cdom.min.js` | the "page" section above; regenerate the minified shim with `node tools/minshim.mjs` |
| `shim/boot.js` | the module array |
| `shim/page.html` | unchanged (it already uses the marker) |
| `cdom.mk` | `HOST_CC`, `CDOM_LITS` and its rule, `cdom_lits`, the linked-then-cdlits recipe, `-v lits=`, the new flags, the header comment |
| `tools/page.awk` | the array at the marker; the bare-URL error in URL mode |
| `tools/cdlits.c` | new |
| `tools/cdomflags.sh` | export `CDOM_LITS` and a `cdom_lits` shell function; add `tests/lits/run.sh` to `CDOM_FLAG_USERS` |
| `tools/new.sh` | the starter app without `CD_LIT` |
| `tools/mutate.py` | drop the mutants of deleted code (`tmp-interned`, `tmp-unchecked`, the `CD_LIT` ones); add the mutants listed in the test plan |
| `tools/offsets.mjs` | the layout's new entries |
| `tools/legacy/build.mjs` | `page` works on a page whose marker `page.awk` already filled; nothing else |
| `tools/final.sh` | drop `cdom-pertext` from the default list |
| `tools/starter.mjs`, `tools/unit.mjs`, `tools/fuzz.mjs`, `tools/host.mjs`, `tools/canvas.mjs`, `tools/legacy.mjs`, `tools/smoke.mjs` | only where they name a deleted export or a page that changed name |
| `bench/cdom-keyed/src/app.c` | labels in an arena the app owns, `cd_tmp` at each use, a revision number for the diff (step 3) |
| `bench/cdom-keyed/index.html` | `cdomBoot(/*CDOM_WASM*/, 'main')` |
| `bench/build.sh` | run cdlits; `page.awk` in URL mode with the lits; drop the `cdom-pertext` build |
| `demo/click.c`, `demo/click.html` | no `L()`; the shim and module markers |
| `tutorial/01-hello` to `07-particles`: `src/app.c`, `README.md` | no `L()`; 01 passes `FRUITS[i]` directly; 05 keeps each todo's text in its own struct; the READMEs' paragraphs on `CD_LIT`, interning and `cd_tmp` |
| `tutorial/README.md` | the "Strings are interned" bullet, lesson 1's row, the size column after measuring |
| `tests/unit.c`, `tests/unit.html` | no `L()`; the interning tests replaced by the string tests in the test plan |
| `tests/listfuzz.c`, `tests/host.c`, `tests/canvas.c`, `tests/legacy.c` | no `cd_intern*`; the `z()` helpers go; `host.c`'s `P(len)` uses `cd_tmp` |
| `tests/*.html` | the shim and module markers, built by `page.awk` |
| `tests/build.sh` | cdlits after each link; pages through `page.awk` |
| `tests/link/alloc.c`, `mk_app.c`, `run.sh`, `page/*.html`, `app.mk` | no `cd_intern*` (alloc.c grows the heap another way); the `lit_*` cases go; the page cases expect the array |
| `tests/lits/` | new: `torture.c`, `kinds.c`, `run.sh` |
| `micro/svg/` | deleted |
| `README.md`, `RESULTS.md`, `PERF_LOG.md` | the strings paragraphs and the diagram label in README; a new dated section in RESULTS with the predictions and what was measured; a dated PERF_LOG entry for SVG. Old sections stay as history. |

Outside cdom (each repo's manager owns it; through the super-coordinator, since it is a pin that moves with an API change):

| Repo | Change |
| --- | --- |
| amgi (`ui/src/*.c`, 15 files, about 450 `CD_LIT` sites) | delete `CD_LIT(` wrappers; `cd_internz(x)` becomes `x` (13); `cd_intern(p, n)` becomes `cd_tmp(p, n)` with the bytes kept by the app (1, deck names in `state.c`); `cd_bytes` (4) reads the app's own copy; `dom.c`'s helpers that take a `cd_str name` keep working; its page takes the marker; its build runs through `cdom.mk`'s rule or `cdom_lits` |
| cbase (`example/minimal/page/page.c`, and the adapter's page) | delete three `CD_LIT(` wrappers; the page takes the marker |

Step 8 keeps `#define CD_LIT(s) cd_s(s)` in `cdom.h` so that these repos, and cdom's own call sites, compile unchanged while they are cleaned up; step 10 deletes it once both repos have moved.

## Predictions, written before any measurement of the real thing

Baselines are today's numbers at 0613c62; step 1 records the ones not yet in RESULTS.md.
Each prediction is recorded as confirmed or refuted, with the numbers, in `docs/strings-results.md`.

| # | Prediction | Why | Refuted by |
| --- | --- | --- | --- |
| P1 | Page, brotli -q 11: lesson 1 from 8,942 to between 7,600 and 8,300; lesson 5 from 15,119 to between 13,100 and 14,100; every lesson smaller than today | strings out of the base64 (-113 and -592 measured), about 1,400 bytes of string code gone, about 14 bytes less per literal site | lesson 1 saving under 500 bytes, lesson 5 saving under 900, or any lesson larger |
| P2 | Lesson 1's `app.wasm` from 4,030 bytes to between 2,000 and 2,600 | the same three causes, before compression | above 3,000 |
| P3 | The module's initial memory from 6 pages to 2 | `sbytes` (256 KB) and `smeta` (48 KB) go | 3 or more |
| P4 | Installing the table takes under 0.05 ms for lesson 5, and first paint of the keyed benchmark entry stays within 5 ms of its baseline median (86.6 ms in RESULTS.md) | 24 us for 300 strings in Node; nothing else at start changes, and `cd_main` no longer hashes or copies its literals | install over 0.2 ms, or first paint median worse by more than 5 ms over five runs |
| P5 | Every js-framework-benchmark operation of the `cdom` entry within 3% of baseline in total time, and create 10k script time between 0.0 and 1.0 ms worse | a label costs about 85 ns to read where the batch decode cost about 20 ns, and C no longer hashes and copies it (about 50 ns) | any operation's median over 1.05 of baseline in two passes, or create 10k script more than 1.5 ms worse |
| P6 | JS heap after five create-and-clear cycles from 0.80 MB to 0.72 MB or less | the intern table's 0.09 MB (RESULTS.md) is gone | 0.78 MB or more |
| P7 | The minified shim within 150 bytes of 5,930 brotli | the batch decoder goes, `str`, the table install and the SVG counter come | outside that band |
| P8 | `cdom-notpl` create 1k script within 3% of baseline | 12 literal lookups per row at 1.5 ns each, as `interned[id]` today, plus the SVG check in the shim, under 3 ns per open | more than 3% worse in two passes |
| P9 | The lexer and clang agree on every literal in cdom's tree and in the torture file | the grammar is small and the prototype already agrees on the torture literal | one mismatch |
| P10 | No literal that went to cdom is in any final wasm | both sections are removed whole | `strings app.wasm` finds one |

The noise band for P4, P5 and P8 comes from two baseline passes run back to back before the change (step 1); a prediction whose band the noise exceeds is recorded as "not measurable", not as confirmed.

## Test plan

End to end first: the tests drive a built page in headless Chrome, as a user would load it.
Every new test is mutation checked: the mutant is applied, the named test must fail on the named assertion, and the code is restored by re-editing (never `git checkout`).

| Test | What it proves | Mutant that must turn it red |
| --- | --- | --- |
| `tutorial/run.sh` (exists) | every lesson's page, built by `cdom.mk` from plain literals, shows the right text from disk (`file://`), with no fetch | in `cdlits.c`, write `map` shifted by one: lesson texts are wrong and `test.mjs` fails on the first text check |
| `tests/lits/run.sh`, differential | `torture.c` and every app in the tree (the seven lessons, the bench app, the demo, `tests/*.c`) built with `-DCD_LITS_CHECK` pass `cdlits --check` | in the lexer, `\t` gives 8; then octal reads four digits; then `\x` stops after two digits: each fails on the torture literal |
| `tests/lits/run.sh`, kinds | `kinds.c` has one call of each row of the classification table; `cdlits --list` must show exactly the expected slots, and a Node run with stub imports must see the expected strings, with `mut` reading `nut` after C changes it | `CD_LITQ` without the spelling test: `NAME` becomes a slot and the tool stops on it; `CD_LITQ` always 0: no slots |
| `tests/lits/run.sh`, three builds | `kinds.c` at `-O0`, `-Oz` and `-Oz -flto` gives the same `--list` output and the same strings | none needed beyond the above; it is a comparison |
| `tests/lits/run.sh`, strip | the output validates (`wasm-validate` when present), no literal's bytes appear in `out.wasm`, the five exports are gone, and a module whose `cd_chars` segment was merged with its neighbour by hand still strips | skip the strip: the bytes are found; drop instead of cut on overlap: the neighbour's bytes are lost and the Node run fails |
| `tests/lits/run.sh`, errors | each build error fires with its message: `"abc" + 1`, `L"x"`, a literal with `\0` inside, invalid UTF-8, a module without `cd_lits_stamp`, a module processed twice | remove each check in turn |
| `tests/lits/run.sh`, compile errors | `cd_text(some_id)`, `cd_text(5)`, `cd_str a, b; a != b` do not compile, and a control file does | make `cd_str` a `uint32_t` again: all three compile |
| `tools/unit.mjs` + `tests/unit.c`, strings | a literal, a `cd_tmp`, a `cd_tmpz`, a ternary of `cd_s`, ASCII, two-byte, three-byte and astral text, 11, 12, 13 and 300 bytes (both read paths), the empty string, in every string position (tag, text, attribute name and value, class, style, path, method, `cd_v_str` in arguments and in an object) | `readString`'s threshold test inverted for non-ASCII (skip the fallback): the two-byte case fails; `lits` index off by one |
| the same, lifetime | a `cd_tmp` used after another string call throws with the shim's message; 32 alive at once work; the 33rd traps | `tmp_done()` removed from one function: the stale use is not caught; the `k >= count` check removed |
| the same, argument evaluated once | `cd_text(next())` with a counter | `cd_s` naming its argument twice in the chosen branch |
| the same, units and SVG | each `CD_UNIT_*` through `cd_set_style_num`, read back; an `<svg>` with children, a clone inside it, and an element after it, by `namespaceURI` | swap two entries of `UNITS`; drop the decrement in `close` |
| `tests/link/run.sh`, page cases | bundled and URL pages carry the array; a page naming its module by URL fails the build with the message; `cdom.start("x.wasm")` throws the bare-URL message | make `start` accept a string |
| `tools/unit.mjs`, skew | a page whose table comes from another build (one literal changed, same count) throws the stamp message at start | remove the stamp compare |
| the same, encoding | the page served with `charset=iso-8859-1` in its header throws the UTF-8 message | remove the canary compare |
| `LEGACY=1 node tools/legacy.mjs` and `LEGACY=1 node tools/fuzz.mjs` (exist) | the table and the runtime path on the wasm2js build and the polyfilled `TextDecoder`, with Latin, Cyrillic, CJK and an emoji | none new: the existing mutants of the polyfills still apply |
| a gate in `tests/link/run.sh` | `grep` finds no `CD_LIT`, `cd_intern`, `cd_internz`, `cd_bytes(`, `cd_str_count`, `CDOM_INTERN_TEXT` outside `docs/`, RESULTS.md, PERF_LOG.md and history | reintroduce one |

Per step, run only what the step can break (each step names it); the whole suite runs once, before the merge.

## Implementation steps

Each step is one executor, one commit or a few, and leaves the tree building and its tests green.
Sonnet unless marked Haiku.
Every executor: no em dash, each Markdown sentence on its own line, no `Co-Authored-By`, do not edit CHANGELOG or generated files by hand (regenerate `shim/cdom.min.js` with `node tools/minshim.mjs`), restore a mutant by re-editing.
Steps that build or run browsers run one at a time on this Mac.

1. **Record the baselines** (Haiku).
   Run `tutorial/run.sh`; for each lesson record `wc -c build/app.wasm`, `brotli -q 11 -c build/index.html | wc -c`, and the initial memory pages (`wasm-objdump -x`).
   Record `wc -c` and brotli of `shim/cdom.min.js`.
   Run the keyed benchmark for `cdom` and `cdom-notpl` twice, back to back, as RESULTS.md "Reproduce" says, and record both passes (they are the noise band), with first paint and the memory numbers.
   Write them to `docs/strings-results.md` under "Baseline", with the P1 to P10 table copied in and an empty "Measured" column.
   Verify: the file's lesson numbers match this document's for lessons 1 and 5.

2. **`tools/cdlits.c` and `tests/lits/`, standing alone.**
   Write the tool to the specification above.
   `tests/lits/lits.h` is a private copy of the macro block from "The API" (with `cd_tmpz` declared), used only by this step's tests until step 8 replaces it with `cdom.h`.
   `tests/lits/torture.c` holds the torture literals (every escape form, the cases in the evidence table); `tests/lits/kinds.c` the classification table; `tests/lits/run.sh` builds them with `tools/cdomflags.sh`'s flags plus the five `--export-if-defined` flags and a small stub runtime (`cd_tmp`, `cd_tmpz`, two imports, `cd_lits_stamp`), and runs the differential, kinds, three-builds, strip and error cases, with a Node stub for the run.
   Nothing else in the tree changes.
   Verify: `tests/lits/run.sh` green; each lexer and strip mutant from the test plan red, then restored.

3. **The benchmark app stops interning**, under today's API.
   In `bench/cdom-keyed/src/app.c`: labels live in a byte arena the app owns (`Row` holds offset, length and a revision that changes whenever the label does; `RowView` holds the revision); the arena is reset when the rows are replaced or cleared, and compacted when dead bytes exceed live ones; every use is `cd_tmp(arena + off, len)`; the `S_*` statics filled by `z()` become `CD_LIT` at their call sites.
   Hypothesis for this step (it is P5 measured early, on the old string path): every operation within 3% of step 1's baseline.
   Verify: `bench/build.sh`, `node tools/smoke.mjs`, `node tools/domcheck.mjs`; then one benchmark pass against the baseline, recorded in `docs/strings-results.md`.
   If an operation is more than 5% worse in two passes, stop and report to the repo manager before any later step.

4. **Tests and lessons stop interning**, under today's API.
   `tests/listfuzz.c`, `tests/host.c`, `tests/canvas.c`, `tests/legacy.c`, `tests/link/alloc.c`, `tests/link/mk_app.c`, `micro/svg/open.c`: `cd_internz("x")` and `z("x")` become `CD_LIT("x")` at the call site; interned runtime strings become `cd_tmp` at each use; `alloc.c` grows cdom's tables with ids instead of strings.
   `tutorial/01-hello` passes `cd_tmpz(FRUITS[i])`; `tutorial/05-todo` keeps each todo's text in its struct and uses `cd_tmp`.
   `tests/unit.c` keeps its interning tests (the feature still exists) and changes nothing else.
   Verify: `tests/build.sh && node tools/unit.mjs && node tools/fuzz.mjs && node tools/canvas.mjs && node tools/host.mjs && tests/link/run.sh && node tools/legacy.mjs`, and `tutorial/run.sh tutorial/01-hello tutorial/05-todo`.

5. **SVG and units move to the shim**, under today's API.
   The shim's `open` counts SVG depth as described; `open_svg`, `svg_tag` and `svg_depth` are deleted; `style_num` takes the unit's index and the shim has `UNITS`.
   Add the units and SVG unit tests and their two mutants; delete `micro/svg/` and remove it from `CDOM_FLAG_USERS`; add the PERF_LOG entry.
   Verify: `tests/build.sh && node tools/unit.mjs`, `node tools/offsets.mjs`, `node tools/minshim.mjs check`, `tutorial/run.sh` (lesson 6 or 7 if either draws SVG, else any one).

6. **Every page takes the marker.**
   `tests/*.html`, `demo/click.html`, `bench/cdom-keyed/index.html`, `tests/link/page/*.html`: the shim at `/*CDOM_SHIM*/` and the module at `/*CDOM_WASM*/`, built by `page.awk` in URL mode (`tests/build.sh` and `bench/build.sh` call it; tests inline the unminified `shim/cdom.js` so `tools/mutate.py` still mutates what runs).
   No shim or awk change yet: the marker still becomes a URL string.
   Verify: the same commands as step 4, plus `bench/build.sh && node tools/smoke.mjs`.

7. **The build carries a table** (empty for now).
   `cdom.c` gets `cd_lits_stamp` and `cd_lits`; `cdom.mk` gets `HOST_CC`, `CDOM_LITS`, the cdlits step in the `APP_WASM` rule, `cdom_lits`, the five flags and `-v lits=`; `tools/cdomflags.sh` hands `CDOM_LITS` to the scripts, and `tests/build.sh`, `tests/link/run.sh` and `bench/build.sh` run it after each link; `page.awk` writes the array and refuses a bare URL in URL mode; the shim's `start` and `boot.js` take the array, check the canary and the stamp, install `lits` (empty: no module has a slot yet), and throw on a bare URL; `tools/legacy/build.mjs` is checked against a filled marker.
   Add the page, skew and encoding tests with their mutants.
   Verify: everything in step 6, `tutorial/run.sh`, `tests/lits/run.sh`, and `make -C path/to/cdom new DIR=...` builds and opens (`node tools/starter.mjs`).

8. **The switch.**
   `cdom.h`: the strings section and wrapper macros, `cd_str` a struct, `CD_NONE`, and for now `#define CD_LIT(s) cd_s(s)` marked "temporary, deleted in step 10".
   `cdom.c`: the intern machinery out, the `tmp` block and `tmp_done()` in, the layout entries.
   The shim: `str`, `readString`, the path cache by slot; `interned`, `transient`, `decode`, `strings` out; regenerate the minified shim.
   `tests/lits/lits.h` is deleted and its tests include `cdom.h`.
   `tests/unit.c`: the interning tests go, the string, lifetime and evaluated-once tests come, with their mutants; path 0 becomes `CD_NONE` everywhere.
   `tools/mutate.py`, `tools/offsets.mjs`: as in the migration table.
   `bench/build.sh` and `tools/final.sh`: `cdom-pertext` goes.
   `tests/link`: the `lit_*` cases go, the compile-error cases come.
   All call sites keep compiling through the temporary `CD_LIT`.
   Verify: the full list from RESULTS.md "Reproduce" (this is the one step that touches everything): `tests/build.sh && node tools/unit.mjs && node tools/fuzz.mjs && node tools/canvas.mjs && node tools/host.mjs && tests/link/run.sh && node tools/legacy.mjs`, `LEGACY=1 node tools/fuzz.mjs && LEGACY=1 node tools/legacy.mjs`, `tests/lits/run.sh`, `tutorial/run.sh`, `bench/build.sh && node tools/smoke.mjs`.
   Then `strings` on a lesson's `app.wasm` must not find "Hello, cdom".

9. **Plain literals everywhere in cdom** (Haiku, three commits: `tutorial/`, then `tests/` and `demo/` and `bench/`, then `tools/new.sh`).
   Delete `#define L(s) CD_LIT(s)`, turn `L("x")` and `CD_LIT("x")` into `"x"`, drop `cd_tmpz(` around plain pointers where the argument goes straight into a call, and rewrite the comments and lesson READMEs that explain `L`, `CD_LIT` or interning (lesson 1's README and `tutorial/README.md`'s strings bullet say: write the string; a literal is in the page's table, anything else is read when used).
   Verify per commit: `tutorial/run.sh`; then `tests/build.sh && node tools/unit.mjs && node tools/host.mjs && tests/lits/run.sh` (the differential now covers every app); then `node tools/starter.mjs`.
   Look at each lesson's screenshot (`SHOTS=tutorial/out tutorial/run.sh`) against the one from before: they must be identical.

10. **Delete the temporary `CD_LIT`** and add the grep gate.
    Only after amgi and cbase have moved (the repo manager confirms through the super-coordinator).
    Verify: `tests/link/run.sh`; the gate's mutant.

11. **Measure and write up.**
    Repeat step 1's measurements; fill the "Measured" column and mark each of P1 to P10 confirmed, refuted or not measurable, with the numbers.
    Profile nothing unless a prediction is refuted; if one is, sample first (`sample`) and report to the repo manager.
    Update `tutorial/README.md`'s size column, README.md (the strings sentence, the diagram label, the shim's size line, "Building an app": the marker is required, the build needs a host C compiler), `cdom.mk`'s header comment, and add a dated section to RESULTS.md that points at `docs/strings-results.md`.
    Verify: `tutorial/run.sh`; the numbers in the READMEs match the results file.

## Risks

- **The lexer must agree with clang**, or a page shows wrong text.
  The differential check runs over every literal in the tree on every `tests/lits/run.sh`, and `cdom.mk` can run it for an app (`make CDOM_CFLAGS+=-DCD_LITS_CHECK` and `cdlits --check`); the grammar is closed and small.
  A spelling the lexer does not know stops the build; it never guesses.
- **The API functions are macros.**
  A diagnostic for a wrong argument points into `cd_s`, and a function pointer needs `(cd_text)`.
  A string argument that contains a top-level comma outside parentheses (a compound literal) needs parentheses, as with any macro.
- **Two other repos must move with the change.**
  The temporary `CD_LIT` covers most of amgi's call sites, but its 18 uses of `cd_intern`, `cd_internz` and `cd_bytes` and its page need real edits before its cdom pin can move past step 8.
- **The build gains a host compiler.**
  `cc` is present wherever clang is, but a cross-only environment must set `HOST_CC`.
- **Runtime strings are read on every use.**
  An app that showed the same 10,000 runtime strings again and again would pay about 85 ns each time where interning paid about 2 ns.
  P5 is the check; the fallback is an owned handle, not interning.

## Appendix: the prototype tool

The Python below is the prototype that produced the evidence; `tools/cdlits.c` is its port and must match it byte for byte on the torture module.
It is here because the scratch directory it ran in does not survive.

```python
import sys, struct
def uleb(b, i):
    r = s = 0
    while True:
        c = b[i]; i += 1; r |= (c & 127) << s; s += 7
        if c < 128: return r, i
def sleb(b, i):
    r = s = 0
    while True:
        c = b[i]; i += 1; r |= (c & 127) << s; s += 7
        if c < 128:
            if c & 64: r -= 1 << s
            return r, i
def enc(n):
    o = bytearray()
    while True:
        c = n & 127; n >>= 7
        if n: o.append(c | 128)
        else: o.append(c); return bytes(o)
def die(m): sys.exit('cdlits: ' + m)
w = open(sys.argv[1], 'rb').read(); check = '--check' in sys.argv
secs = []; i = 8
while i < len(w):
    sid = w[i]; n, j = uleb(w, i + 1); secs.append((sid, w[j:j + n])); i = j + n
def sec(k): return next((b for s, b in secs if s == k), None)
gl = []; b = sec(6); n, i = uleb(b, 0)            # globals: i32.const or i64.const initializers
for _ in range(n):
    i += 2; v, i = sleb(b, i + 1); i += 1; gl.append(v & 0xffffffff)
ex = {}; exraw = []; b = sec(7); n, i = uleb(b, 0)  # exports
for _ in range(n):
    s = i; ln, i = uleb(b, i); name = b[i:i + ln].decode(); i += ln; kind = b[i]; idx, i = uleb(b, i + 1)
    exraw.append((name, b[s:i]))
    if kind == 3: ex[name] = gl[idx]
need = ['__start_cd_lits', '__stop_cd_lits', '__start_cd_chars', '__stop_cd_chars']
ls, le, cs, ce = (ex[k] for k in need)
segs = []; b = sec(11); n, i = uleb(b, 0)          # active data segments of memory 0
for _ in range(n):
    s = i; flag, i = uleb(b, i); off, i = sleb(b, i + 1); i += 1
    hdr = b[s:i]; ln, i = uleb(b, i); segs.append([off, b[i:i + ln], hdr]); i += ln
def mem(a, n):                                    # bytes no segment covers are zero
    out = bytearray(n)
    for off, d, _ in segs:
        lo, hi = max(a, off), min(a + n, off + len(d))
        if lo < hi: out[lo - a:hi - a] = d[lo - off:hi - off]
    return bytes(out)
def cstr(a, lim):
    m = mem(a, lim - a); z = m.find(b'\0')
    return m if z < 0 else m[:z]
SIMPLE = {'a': 7, 'b': 8, 'f': 12, 'n': 10, 'r': 13, 't': 9, 'v': 11, '\\': 92, "'": 39, '"': 34, '?': 63, 'e': 27}
def lex(sp):   # the bytes of adjacent C string literals as spelled; None for anything else
    o = bytearray(); i = 0; n = len(sp); any_ = False
    while i < n:
        if sp[i] in b' \t\n': i += 1; continue
        if sp[i:i + 2] == b'u8': i += 2
        if i >= n or sp[i] != 34: return None
        i += 1; any_ = True
        while True:
            if i >= n: return None
            c = sp[i]; i += 1
            if c == 34: break
            if c != 92: o.append(c); continue
            if i >= n: return None
            e = chr(sp[i]); i += 1
            if e in SIMPLE: o.append(SIMPLE[e])
            elif e in '01234567':
                v = int(e); k = 1
                while k < 3 and i < n and chr(sp[i]) in '01234567': v = v * 8 + sp[i] - 48; i += 1; k += 1
                if v > 255: return None
                o.append(v)
            elif e == 'x':
                v = 0; k = 0
                while i < n and chr(sp[i]) in '0123456789abcdefABCDEF': v = v * 16 + int(chr(sp[i]), 16); i += 1; k += 1
                if not k or v > 255: return None
                o.append(v)
            elif e in 'uU':
                k = 4 if e == 'u' else 8
                if len(sp[i:i + k]) != k: return None
                v = int(sp[i:i + k].decode(), 16); i += k; o += chr(v).encode()
            else: return None
    return bytes(o) if any_ else None
stride = 8 if check else 4
slots = []
for a in range(ls, le, stride):
    p = struct.unpack('<I', mem(a, 4))[0]
    sp = cstr(p, ce); v = lex(sp)
    if v is None: die('not a string literal: ' + sp.decode('utf8', 'replace'))
    if check:
        r = struct.unpack('<I', mem(a + 4, 4))[0]
        if cstr(r, r + len(v) + 2) != v: die('lexer mismatch: %r' % sp)
    slots.append(v)
uniq = []; index = {}; smap = []
for v in slots:
    if v not in index: index[v] = len(uniq); uniq.append(v)
    smap.append(index[v])
# then: the stamp, the JS text, and the data and export sections rewritten without the two ranges
```

The prototype did not patch the stamp, cut overlapping segments or write the canary; the specification above is the authority for those.
