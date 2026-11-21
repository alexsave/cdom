/* cdlits: the build step that moves a cdom module's string literals into the page (docs/strings-design.md,
 * "tools/cdlits.c").
 *
 *   cdlits in.wasm out.wasm out.lits     the build step
 *   cdlits --check in.wasm               a module built with -DCD_LITS_CHECK: compare, write nothing
 *   cdlits --list in.wasm                print "slot<TAB>spelling", for tests and for looking
 *
 * One file, no dependencies. The slots of the cd_lits section hold pointers to the literals' source
 * spellings in cd_chars; this tool turns each spelling into bytes exactly as clang does (a small C
 * string-literal lexer), cuts both sections out of the module and writes the table as JavaScript text.
 * "slot" in --list is the slot's index, not its address, so the same source lists the same at every
 * optimization level. */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define STAMP_INIT 0x7374696cu /* cdom.c's cd_lits_stamp before this tool touches it ("stil") */
#define MAX_SECS 1024
#define MAX_MEM (1u << 30)

static void die(const char *fmt, ...) __attribute__((noreturn, format(printf, 1, 2)));
static void die(const char *fmt, ...) {
  va_list ap;
  fputs("cdlits: ", stderr);
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fputc('\n', stderr);
  exit(1);
}

/* ---- growable byte buffer ---- */
typedef struct { uint8_t *p; size_t n, cap; } Buf;
static void bput(Buf *b, const void *src, size_t n) {
  if (b->n + n > b->cap) {
    b->cap = (b->n + n) * 2 + 64;
    b->p = realloc(b->p, b->cap);
    if (!b->p) die("out of memory");
  }
  if (n) memcpy(b->p + b->n, src, n);
  b->n += n;
}
static void bbyte(Buf *b, int c) { uint8_t x = (uint8_t)c; bput(b, &x, 1); }
static void bstr(Buf *b, const char *s) { bput(b, s, strlen(s)); }
static void buleb(Buf *b, uint64_t v) {
  do { uint8_t c = v & 127; v >>= 7; bbyte(b, v ? c | 128 : c); } while (v);
}
static void bsleb(Buf *b, int64_t v) {
  for (;;) {
    uint8_t c = v & 127;
    v >>= 7;
    if ((v == 0 && !(c & 64)) || (v == -1 && (c & 64))) { bbyte(b, c); return; }
    bbyte(b, c | 128);
  }
}

/* ---- bounded reader ---- */
typedef struct { const uint8_t *p, *end; } R;
static uint8_t r8(R *r) {
  if (r->p >= r->end) die("truncated module");
  return *r->p++;
}
static uint64_t ruleb(R *r) {
  uint64_t v = 0;
  int s = 0;
  for (;;) {
    uint8_t c = r8(r);
    if (s > 63) die("bad LEB128");
    v |= (uint64_t)(c & 127) << s;
    s += 7;
    if (c < 128) return v;
  }
}
static int64_t rsleb(R *r) {
  int64_t v = 0;
  int s = 0;
  for (;;) {
    uint8_t c = r8(r);
    if (s > 63) die("bad LEB128");
    v |= (int64_t)((uint64_t)(c & 127) << s);
    s += 7;
    if (c < 128) {
      if ((c & 64) && s < 64) v |= (int64_t)(~(uint64_t)0 << s);
      return v;
    }
  }
}
static const uint8_t *rbytes(R *r, uint64_t n) {
  if (n > (uint64_t)(r->end - r->p)) die("truncated module");
  const uint8_t *q = r->p;
  r->p += n;
  return q;
}

/* ---- the module ---- */
typedef struct { int id; const uint8_t *body; size_t len; } Sec;
typedef struct { uint32_t off; uint8_t *data; size_t len; } Seg;
typedef struct { int ok; uint32_t v; } Glob;
typedef struct { const uint8_t *name; size_t nlen; int kind; uint64_t idx; const uint8_t *raw; size_t rawlen; } Exp;

static uint8_t *file;
static size_t filelen;
static Sec secs[MAX_SECS];
static int nsecs;
static Glob *globs;
static size_t nglobs;
static Exp *exps;
static size_t nexps;
static Seg *segs;
static size_t nsegs;
static uint8_t *mem; /* the data segments laid over zeros: a byte no segment covers is 0 (wasm-ld trims
                        trailing zeros and splits segments at runs of zeros) */
static uint32_t memsize;

static const Sec *find_sec(int id) {
  for (int i = 0; i < nsecs; i++) if (secs[i].id == id) return &secs[i];
  return NULL;
}

/* A constant expression: only i32.const is understood as a value; the rest are skipped. */
static void rexpr(R *r, int *ok, uint32_t *v) {
  uint8_t op = r8(r);
  *ok = 0;
  *v = 0;
  if (op == 0x41) { *v = (uint32_t)rsleb(r); *ok = 1; }
  else if (op == 0x42) rsleb(r);
  else if (op == 0x43) rbytes(r, 4);
  else if (op == 0x44) rbytes(r, 8);
  else if (op == 0x23 || op == 0xd2) ruleb(r);
  else if (op == 0xd0) r8(r);
  else die("unsupported constant expression 0x%02x", op);
  if (r8(r) != 0x0b) die("unsupported constant expression (more than one instruction)");
}

static void rlimits(R *r) { uint8_t f = r8(r); ruleb(r); if (f & 1) ruleb(r); }

static void parse_module(void) {
  if (filelen < 8 || memcmp(file, "\0asm\1\0\0\0", 8)) die("not a wasm module (version 1)");
  R r = {file + 8, file + filelen};
  while (r.p < r.end) {
    if (nsecs == MAX_SECS) die("too many sections");
    int id = r8(&r);
    uint64_t n = ruleb(&r);
    secs[nsecs].id = id;
    secs[nsecs].body = rbytes(&r, n);
    secs[nsecs].len = (size_t)n;
    nsecs++;
  }
  /* imports: only the count of imported globals matters, they come first in the global index space */
  size_t nimp = 0;
  const Sec *s = find_sec(2);
  if (s) {
    R q = {s->body, s->body + s->len};
    uint64_t n = ruleb(&q);
    for (uint64_t i = 0; i < n; i++) {
      rbytes(&q, ruleb(&q)); rbytes(&q, ruleb(&q));
      int kind = r8(&q);
      if (kind == 0 || kind == 4) ruleb(&q);
      else if (kind == 1) { r8(&q); rlimits(&q); }
      else if (kind == 2) rlimits(&q);
      else if (kind == 3) { r8(&q); r8(&q); nimp++; }
      else die("unknown import kind %d", kind);
    }
  }
  s = find_sec(6);
  nglobs = nimp;
  if (s) {
    R q = {s->body, s->body + s->len};
    uint64_t n = ruleb(&q);
    globs = calloc(nimp + n + 1, sizeof *globs);
    for (uint64_t i = 0; i < n; i++) {
      r8(&q); r8(&q);
      rexpr(&q, &globs[nimp + i].ok, &globs[nimp + i].v);
    }
    nglobs = nimp + n;
  } else globs = calloc(nimp + 1, sizeof *globs);
  s = find_sec(7);
  if (!s) die("the module has no exports");
  {
    R q = {s->body, s->body + s->len};
    uint64_t n = ruleb(&q);
    exps = calloc(n + 1, sizeof *exps);
    nexps = (size_t)n;
    for (uint64_t i = 0; i < n; i++) {
      const uint8_t *start = q.p;
      exps[i].nlen = (size_t)ruleb(&q);
      exps[i].name = rbytes(&q, exps[i].nlen);
      exps[i].kind = r8(&q);
      exps[i].idx = ruleb(&q);
      exps[i].raw = start;
      exps[i].rawlen = (size_t)(q.p - start);
    }
  }
  s = find_sec(11);
  if (s) {
    R q = {s->body, s->body + s->len};
    uint64_t n = ruleb(&q);
    segs = calloc(n + 1, sizeof *segs);
    nsegs = (size_t)n;
    for (uint64_t i = 0; i < n; i++) {
      uint64_t flag = ruleb(&q);
      int ok;
      uint32_t off;
      if (flag != 0) die("data segment %zu is not an active segment of memory 0", (size_t)i);
      rexpr(&q, &ok, &off);
      if (!ok) die("data segment %zu does not start at an i32.const offset", (size_t)i);
      uint64_t len = ruleb(&q);
      segs[i].off = off;
      segs[i].len = (size_t)len;
      segs[i].data = (uint8_t *)rbytes(&q, len);
    }
  }
  uint64_t top = 0;
  for (size_t i = 0; i < nsegs; i++) if ((uint64_t)segs[i].off + segs[i].len > top) top = (uint64_t)segs[i].off + segs[i].len;
  if (top > MAX_MEM) die("data segments reach past 1 GB");
  memsize = (uint32_t)top;
  mem = calloc(memsize + 1, 1);
  for (size_t i = 0; i < nsegs; i++) memcpy(mem + segs[i].off, segs[i].data, segs[i].len);
}

static uint8_t mbyte(uint64_t a) { return a < memsize ? mem[a] : 0; }
static uint32_t mword(uint64_t a) {
  return (uint32_t)mbyte(a) | (uint32_t)mbyte(a + 1) << 8 | (uint32_t)mbyte(a + 2) << 16 | (uint32_t)mbyte(a + 3) << 24;
}

/* The value of an exported i32 global by name; 1 and the value when it is there. */
static int export_global(const char *name, uint32_t *v) {
  size_t n = strlen(name);
  for (size_t i = 0; i < nexps; i++) {
    if (exps[i].nlen != n || memcmp(exps[i].name, name, n) || exps[i].kind != 3) continue;
    if (exps[i].idx >= nglobs || !globs[exps[i].idx].ok) die("export %s is not an immutable i32.const global", name);
    *v = globs[exps[i].idx].v;
    return 1;
  }
  return 0;
}

/* ---- the lexer: the bytes of adjacent C string literals, as clang reads them ---- */
enum { LEX_OK, LEX_NOT_LITERAL, LEX_NUL, LEX_NOT_UTF8 };

static int utf8_valid(const uint8_t *s, size_t n) {
  size_t i = 0;
  while (i < n) {
    uint8_t c = s[i];
    size_t k;
    uint32_t v;
    if (c < 0x80) { i++; continue; }
    else if (c >= 0xc2 && c <= 0xdf) { k = 1; v = c & 0x1f; }
    else if (c >= 0xe0 && c <= 0xef) { k = 2; v = c & 0x0f; }
    else if (c >= 0xf0 && c <= 0xf4) { k = 3; v = c & 0x07; }
    else return 0;
    if (i + k >= n) return 0;
    for (size_t j = 1; j <= k; j++) {
      if ((s[i + j] & 0xc0) != 0x80) return 0;
      v = v << 6 | (s[i + j] & 0x3f);
    }
    if ((k == 2 && v < 0x800) || (k == 3 && (v < 0x10000 || v > 0x10ffff)) || (v >= 0xd800 && v <= 0xdfff)) return 0;
    i += k + 1;
  }
  return 1;
}

static int hexval(int c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static int lex(const uint8_t *sp, size_t n, Buf *o) {
  size_t i = 0;
  int any = 0;
  while (i < n) {
    uint8_t c = sp[i];
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f') { i++; continue; }
    if (i >= n || sp[i] != '"') return LEX_NOT_LITERAL;
    i++;
    any = 1;
    for (;;) {
      if (i >= n) return LEX_NOT_LITERAL;
      c = sp[i++];
      if (c == '"') break;
      if (c != '\\') { bbyte(o, c); continue; }
      if (i >= n) return LEX_NOT_LITERAL;
      c = sp[i++];
      switch (c) {
      case 'a': bbyte(o, 7); break;
      case 'b': bbyte(o, 8); break;
      case 'f': bbyte(o, 12); break;
      case 'n': bbyte(o, 10); break;
      case 'r': bbyte(o, 13); break;
      case 't': bbyte(o, 9); break;
      case 'v': bbyte(o, 11); break;
      case 'e': bbyte(o, 27); break;
      case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': {
        uint32_t v = c - '0';
        for (int k = 1; k < 3 && i < n && sp[i] >= '0' && sp[i] <= '7'; k++) v = v * 8 + (sp[i++] - '0');
        if (v > 255) return LEX_NOT_LITERAL;
        bbyte(o, (int)v);
        break;
      }
      case 'x': {
        uint32_t v = 0;
        int k = 0;
        while (i < n && hexval(sp[i]) >= 0) {
          v = v * 16 + (uint32_t)hexval(sp[i++]);
          k++;
          if (v > 255) return LEX_NOT_LITERAL;
        }
        if (!k) return LEX_NOT_LITERAL;
        bbyte(o, (int)v);
        break;
      }
      case 'u': case 'U': {
        int k = c == 'u' ? 4 : 8;
        uint32_t v = 0;
        if (i + (size_t)k > n) return LEX_NOT_LITERAL;
        for (int j = 0; j < k; j++) {
          int h = hexval(sp[i++]);
          if (h < 0) return LEX_NOT_LITERAL;
          v = v << 4 | (uint32_t)h;
        }
        if (v > 0x10ffff || (v >= 0xd800 && v <= 0xdfff)) return LEX_NOT_LITERAL;
        if (v < 0x80) bbyte(o, (int)v);
        else if (v < 0x800) { bbyte(o, 0xc0 | v >> 6); bbyte(o, 0x80 | (v & 63)); }
        else if (v < 0x10000) { bbyte(o, 0xe0 | v >> 12); bbyte(o, 0x80 | (v >> 6 & 63)); bbyte(o, 0x80 | (v & 63)); }
        else { bbyte(o, 0xf0 | v >> 18); bbyte(o, 0x80 | (v >> 12 & 63)); bbyte(o, 0x80 | (v >> 6 & 63)); bbyte(o, 0x80 | (v & 63)); }
        break;
      }
      default: bbyte(o, c); /* \\ \' \" \? and any other byte are themselves */
      }
    }
  }
  if (!any) return LEX_NOT_LITERAL;
  for (size_t j = 0; j < o->n; j++) if (!o->p[j]) return LEX_NUL;
  if (!utf8_valid(o->p, o->n)) return LEX_NOT_UTF8;
  return LEX_OK;
}

/* ---- the slots ---- */
typedef struct { uint8_t *spell; size_t slen; Buf bytes; uint32_t real; } Slot;
static Slot *slots;
static size_t nslots;
static uint32_t lits_start, chars_start;
static int have_lits;
static uint32_t lits_stop, chars_stop;

static void shown(FILE *f, const uint8_t *s, size_t n) {
  for (size_t i = 0; i < n; i++) fputc(s[i] >= 32 && s[i] < 127 ? s[i] : '?', f);
}

static void collect(int check) {
  uint32_t a;
  have_lits = export_global("__start_cd_lits", &lits_start);
  if (!have_lits) return; /* no literal: the table is empty */
  if (!export_global("__stop_cd_lits", &lits_stop) || !export_global("__start_cd_chars", &chars_start) ||
      !export_global("__stop_cd_chars", &chars_stop))
    die("the module has __start_cd_lits but not the other three section symbols (link with CDOM_LDFLAGS)");
  uint32_t stride = check ? 8 : 4;
  if (lits_stop < lits_start || (lits_stop - lits_start) % stride)
    die("cd_lits is %u bytes, not a whole number of %u-byte slots (is this a -DCD_LITS_CHECK module? use --check)",
        lits_stop - lits_start, stride);
  nslots = (lits_stop - lits_start) / stride;
  slots = calloc(nslots + 1, sizeof *slots);
  size_t k = 0;
  for (a = lits_start; a < lits_stop; a += stride, k++) {
    Slot *s = &slots[k];
    uint32_t p = mword(a);
    if (p < chars_start || p >= chars_stop) die("slot %zu points outside cd_chars", k);
    uint32_t e = p;
    while (e < chars_stop && mbyte(e)) e++;
    if (e >= chars_stop) die("slot %zu: its spelling has no terminator inside cd_chars", k);
    s->spell = mem + p;
    s->slen = e - p;
    int rc = lex(s->spell, s->slen, &s->bytes);
    if (rc != LEX_OK) {
      fprintf(stderr, "cdlits: ");
      if (rc == LEX_NOT_LITERAL) fprintf(stderr, "not a string literal: ");
      else if (rc == LEX_NUL) fprintf(stderr, "a NUL inside a literal: ");
      else fprintf(stderr, "bytes that are not UTF-8 in a literal: ");
      shown(stderr, s->spell, s->slen);
      if (rc == LEX_NOT_LITERAL) fprintf(stderr, ", write the literal itself or pass cd_tmpz(...)");
      fputc('\n', stderr);
      exit(1);
    }
    if (check) s->real = mword(a + 4);
  }
}

static void do_check(void) {
  for (size_t k = 0; k < nslots; k++) {
    Slot *s = &slots[k];
    uint32_t e = s->real;
    while (e < memsize && mem[e]) e++;
    if (e - s->real != s->bytes.n || memcmp(mem + s->real, s->bytes.p, s->bytes.n)) {
      fprintf(stderr, "cdlits: lexer mismatch: ");
      shown(stderr, s->spell, s->slen);
      fprintf(stderr, "\n  lexed:");
      for (size_t j = 0; j < s->bytes.n; j++) fprintf(stderr, " %02x", s->bytes.p[j]);
      fprintf(stderr, "\n  clang:");
      for (uint32_t j = s->real; j < e; j++) fprintf(stderr, " %02x", mem[j]);
      fputc('\n', stderr);
      exit(1);
    }
  }
  printf("cdlits: %zu literals agree with the compiler\n", nslots);
}

/* ---- the build step ---- */
static uint32_t fnv(void) {
  uint32_t h = 2166136261u;
  for (size_t k = 0; k < nslots; k++) {
    for (size_t j = 0; j <= slots[k].bytes.n; j++) {
      h ^= j < slots[k].bytes.n ? slots[k].bytes.p[j] : 0;
      h *= 16777619u;
    }
  }
  return h ? h : 1;
}

/* One entry of the table text. A control byte and "<" are written as a three-digit octal escape (\001, \074), which
 * ends after three digits whatever follows: Netscape 3 reads a \x escape greedily, as every hex digit after it
 * (docs/frames-design.md, section 15), so "\x01cd" would be one character. "<" is escaped so that "</script>" and
 * "<!--" cannot end the script. The separator is \001 (not NUL: Netscape 2 and 3 end a string at one). */
static void js_string(Buf *o, const uint8_t *s, size_t n) {
  char t[8];
  for (size_t i = 0; i < n; i++) {
    uint8_t c = s[i];
    if (c == '\\') bstr(o, "\\\\");
    else if (c == '"') bstr(o, "\\\"");
    else if (c == '<' || c < 32 || c == 127) { snprintf(t, sizeof t, "\\%03o", c); bstr(o, t); }
    else if (c == 0xe2 && i + 2 < n && s[i + 1] == 0x80 && (s[i + 2] == 0xa8 || s[i + 2] == 0xa9)) {
      bstr(o, s[i + 2] == 0xa8 ? "\\u2028" : "\\u2029"); /* line terminators end a JS string literal */
      i += 2;
    } else bbyte(o, c);
  }
}

static void write_file(const char *path, const Buf *b) {
  FILE *f = fopen(path, "wb");
  if (!f || fwrite(b->p, 1, b->n, f) != b->n || fclose(f)) die("cannot write %s", path);
}

static int is_dropped_export(const Exp *e) {
  static const char *const gone[] = {"__start_cd_lits", "__stop_cd_lits", "__start_cd_chars", "__stop_cd_chars", "cd_lits_stamp"};
  for (size_t i = 0; i < sizeof gone / sizeof *gone; i++)
    if (e->nlen == strlen(gone[i]) && !memcmp(e->name, gone[i], e->nlen)) return 1;
  return 0;
}

static void put_sec(Buf *o, int id, const Buf *body) {
  bbyte(o, id);
  buleb(o, body->n);
  bput(o, body->p, body->n);
}

static void do_build(const char *outw, const char *outl) {
  /* step 5: the stamp */
  uint32_t stamp_at;
  if (!export_global("cd_lits_stamp", &stamp_at)) die("not a cdom module (link src/cdom.c with CDOM_LDFLAGS)");
  uint32_t stamp = fnv();
  if (mword(stamp_at) != STAMP_INIT) die("already processed (cd_lits_stamp is not its initial value)");
  size_t si;
  for (si = 0; si < nsegs; si++)
    if (stamp_at >= segs[si].off && (uint64_t)stamp_at + 4 <= (uint64_t)segs[si].off + segs[si].len) break;
  if (si == nsegs) die("cd_lits_stamp is not inside one data segment");
  for (int j = 0; j < 4; j++) segs[si].data[stamp_at - segs[si].off + (uint32_t)j] = (uint8_t)(stamp >> (8 * j));

  /* step 8, the table text */
  Buf lits = {0};
  char t[64];
  /* new Array(...), not an array literal: Netscape 2 and 3 cannot parse a literal (docs/frames-design.md, section 15); with four
   * arguments it is the four-element array in every engine. */
  snprintf(t, sizeof t, "var cdomLits=new Array(%u,%u,\"\xe2\x98\x83\",\"", have_lits ? lits_start : 0, stamp);
  bstr(&lits, t);
  for (size_t k = 0; k < nslots; k++) {
    if (k) bstr(&lits, "\\001");
    /* The separator is 0x01, not 0: Netscape 2 and 3 end a string at a NUL (docs/frames-design.md, section 15). */
    if (memchr(slots[k].bytes.p, 1, slots[k].bytes.n)) die("a string literal holds the byte 0x01, which separates the table's entries");
    js_string(&lits, slots[k].bytes.p, slots[k].bytes.n);
  }
  bstr(&lits, "\");\n");

  /* step 6: the data section without the two ranges. A segment inside a range is dropped, one that
   * overlaps is cut, its head and tail kept as two segments. */
  uint32_t rlo[2] = {0, 0}, rhi[2] = {0, 0};
  int nr = 0;
  if (have_lits) { /* the two ranges, in address order */
    int chars_first = chars_start < lits_start;
    rlo[chars_first] = lits_start; rhi[chars_first] = lits_stop;
    rlo[!chars_first] = chars_start; rhi[!chars_first] = chars_stop;
    nr = 2;
  }
  uint64_t pieces = 0;
  Buf body = {0};
  for (size_t i = 0; i < nsegs; i++) {
    uint64_t lo = segs[i].off, end = lo + segs[i].len, cur = lo;
    for (int pass = 0; pass <= nr && cur < end; pass++) {
      uint64_t cut_lo = pass < nr ? rlo[pass] : end, cut_hi = pass < nr ? rhi[pass] : end;
      if (cut_hi <= cur) continue;
      if (cut_lo > end) cut_lo = end;
      if (cut_lo > cur) { /* the part before this range stays */
        bbyte(&body, 0);
        bbyte(&body, 0x41);
        bsleb(&body, (int32_t)(uint32_t)cur);
        bbyte(&body, 0x0b);
        buleb(&body, cut_lo - cur);
        bput(&body, segs[i].data + (cur - lo), (size_t)(cut_lo - cur));
        pieces++;
      }
      if (cut_hi > cur) cur = cut_hi;
    }
  }
  Buf data = {0};
  buleb(&data, pieces);
  bput(&data, body.p, body.n);

  /* step 7: the exports without the five */
  Buf exb = {0}, exbody = {0};
  uint64_t kept = 0;
  for (size_t i = 0; i < nexps; i++) {
    if (is_dropped_export(&exps[i])) continue;
    bput(&exbody, exps[i].raw, exps[i].rawlen);
    kept++;
  }
  buleb(&exb, kept);
  bput(&exb, exbody.p, exbody.n);

  Buf out = {0};
  bput(&out, file, 8);
  for (int i = 0; i < nsecs; i++) {
    const Sec *s = &secs[i];
    if (s->id == 7) put_sec(&out, 7, &exb);
    else if (s->id == 11) put_sec(&out, 11, &data);
    else if (s->id == 12) { Buf c = {0}; buleb(&c, pieces); put_sec(&out, 12, &c); free(c.p); }
    else if (s->id == 0) {
      /* the name section's data segment names (subsection 9) count segments that are gone */
      R q = {s->body, s->body + s->len};
      uint64_t nl = ruleb(&q);
      const uint8_t *nm = rbytes(&q, nl);
      Buf c = {0};
      if (nl == 4 && !memcmp(nm, "name", 4)) {
        buleb(&c, 4);
        bput(&c, "name", 4);
        while (q.p < q.end) {
          int sub = r8(&q);
          uint64_t sl = ruleb(&q);
          const uint8_t *sb = rbytes(&q, sl);
          if (sub == 9) continue;
          bbyte(&c, sub);
          buleb(&c, sl);
          bput(&c, sb, (size_t)sl);
        }
        put_sec(&out, 0, &c);
      } else {
        bbyte(&out, 0);
        buleb(&out, s->len);
        bput(&out, s->body, s->len);
      }
      free(c.p);
    } else {
      bbyte(&out, s->id);
      buleb(&out, s->len);
      bput(&out, s->body, s->len);
    }
  }

  /* step 9: temporary names, renamed, out.wasm last */
  char tw[4096], tl[4096];
  snprintf(tw, sizeof tw, "%s.tmp%d", outw, (int)getpid());
  snprintf(tl, sizeof tl, "%s.tmp%d", outl, (int)getpid());
  write_file(tl, &lits);
  write_file(tw, &out);
  if (rename(tl, outl) || rename(tw, outw)) die("cannot rename the outputs into place");
}

int main(int argc, char **argv) {
  int mode = 0; /* 1 build, 2 check, 3 list */
  const char *in = NULL;
  if (argc == 4 && argv[1][0] != '-') { mode = 1; in = argv[1]; }
  else if (argc == 3 && !strcmp(argv[1], "--check")) { mode = 2; in = argv[2]; }
  else if (argc == 3 && !strcmp(argv[1], "--list")) { mode = 3; in = argv[2]; }
  else {
    fputs("usage: cdlits in.wasm out.wasm out.lits | cdlits --check in.wasm | cdlits --list in.wasm\n", stderr);
    return 1;
  }
  FILE *f = fopen(in, "rb");
  if (!f) die("cannot read %s", in);
  fseek(f, 0, SEEK_END);
  filelen = (size_t)ftell(f);
  fseek(f, 0, SEEK_SET);
  file = malloc(filelen + 1);
  if (!file || fread(file, 1, filelen, f) != filelen) die("cannot read %s", in);
  fclose(f);
  parse_module();
  collect(mode == 2);
  if (mode == 1) do_build(argv[2], argv[3]);
  else if (mode == 2) do_check();
  else for (size_t k = 0; k < nslots; k++) { printf("%zu\t", k); fwrite(slots[k].spell, 1, slots[k].slen, stdout); putchar('\n'); }
  return 0;
}
