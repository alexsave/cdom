// cdpage: the build step that writes an app's page (docs/page-tool-design.md): the page's source with the
// string table and the shim at its /*CDOM_SHIM*/ line, the module at its /*CDOM_WASM*/ and the title at
// its /*CDOM_TITLE*/.
//
//   cdpage --page in.html --out out.html --shim cdom.min.js --lits app.wasm.lits
//          [--wasm app.wasm | --wasm-url URL] [--title TITLE]
//
// Every line of the page is written as it is (each output line ends in a newline, so a last line without
// one gains one), except:
// - the one line that holds only /*CDOM_SHIM*/ (spaces and tabs before it, spaces, tabs and CR after it),
//   which becomes the table (the .lits file tools/cdlits wrote beside the module), then </script>, then the
//   last <script ...> tag seen before it written again as it was, then the shim. So the table is a script
//   of its own that runs before the shim and the module (the IE 9 and 10 fallback needs that), and no page
//   needs a marker for it. A tag is seen only when it is whole on one line;
// - /*CDOM_WASM*/, anywhere in a line (cdom.start's first argument), which becomes the module as a
//   JavaScript string: with --wasm, a data:application/wasm;base64 URL of that file; with --wasm-url, that
//   URL; with neither, the marker is an error;
// - with --title, every /*CDOM_TITLE*/, which becomes the title escaped for HTML.
// Refused, with nothing written: no /*CDOM_SHIM*/ line, a second one, the marker beside other text (it
// would be lost with the line), no <script tag before it; a second /*CDOM_WASM*/; and, bundling, a page
// with no /*CDOM_WASM*/: a page that names its module by URL would fetch it besides carrying it, so it is
// refused, naming the URL it loads, and never rewritten. The first fault in line order is the one named;
// a fault of the whole page comes after every line's.
//
// One file, no dependencies, no standard input and no environment: every input is a named file or a flag.
// The page is built in memory, written to out.html.tmp<pid> and renamed, so a failed run leaves nothing.
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *page_name = "cdpage"; /* the page's name, first in every message about it */

static void die(const char *fmt, ...) __attribute__((noreturn, format(printf, 1, 2)));
static void die(const char *fmt, ...) {
  va_list ap;
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
    if (!b->p) die("cdpage: out of memory");
  }
  if (n) memcpy(b->p + b->n, src, n);
  b->n += n;
}
static void bbyte(Buf *b, int c) { uint8_t x = (uint8_t)c; bput(b, &x, 1); }
static void bstr(Buf *b, const char *s) { bput(b, s, strlen(s)); }

/* The whole file, or 0 when it cannot be read. */
static int slurp(const char *path, Buf *b) {
  FILE *f = fopen(path, "rb");
  if (!f) return 0;
  uint8_t chunk[65536];
  size_t got;
  while ((got = fread(chunk, 1, sizeof chunk, f)) > 0) bput(b, chunk, got);
  int ok = !ferror(f);
  fclose(f);
  return ok;
}

/* Text as lines, each written with a newline after it (a last line without one gains one; an empty
 * text writes nothing), as page.awk printed every line it read. */
static void put_lines(Buf *o, const uint8_t *p, size_t n) {
  bput(o, p, n);
  if (n && p[n - 1] != '\n') bbyte(o, '\n');
}

/* ---- the page's text ---- */
static const char SHIM[] = "/*CDOM_SHIM*/", WASM[] = "/*CDOM_WASM*/", TITLE[] = "/*CDOM_TITLE*/";

static const uint8_t *find(const uint8_t *p, size_t n, const char *s) {
  size_t k = strlen(s);
  for (size_t i = 0; i + k <= n; i++)
    if (!memcmp(p + i, s, k)) return p + i;
  return 0;
}
static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
/* p (n bytes left) starts with s (lower case), in any letter case */
static int starts_ci(const uint8_t *p, size_t n, const char *s) {
  size_t k = strlen(s);
  if (n < k) return 0;
  for (size_t i = 0; i < k; i++)
    if (lower(p[i]) != s[i]) return 0;
  return 1;
}
/* The line, trimmed of spaces and tabs before and of spaces, tabs and CR after, is exactly s. */
static int alone(const uint8_t *p, size_t n, const char *s) {
  while (n && (*p == ' ' || *p == '\t')) p++, n--;
  while (n && (p[n - 1] == ' ' || p[n - 1] == '\t' || p[n - 1] == '\r')) n--;
  return n == strlen(s) && !memcmp(p, s, n);
}
/* The tags "<name" then ">" or a space or tab and anything up to the next ">", in any letter case, on one
 * line; each calls seen(tag, len, ctx), in order. */
static void tags(const uint8_t *p, size_t n, const char *name, void (*seen)(const uint8_t *, size_t, void *), void *ctx) {
  size_t k = strlen(name), i = 0;
  while (i < n) {
    if (p[i] == '<' && starts_ci(p + i + 1, n - i - 1, name) && i + 1 + k < n) {
      size_t j = i + 1 + k;
      if (p[j] == '>') { seen(p + i, j + 1 - i, ctx); i = j + 1; continue; }
      if (p[j] == ' ' || p[j] == '\t') {
        const uint8_t *gt = memchr(p + j, '>', n - j);
        if (gt) { seen(p + i, (size_t)(gt + 1 - (p + i)), ctx); i = (size_t)(gt + 1 - p); continue; }
      }
    }
    i++;
  }
}

/* ---- JavaScript and HTML text ---- */
/* s as a JavaScript string: \ and " escaped, and so are <, control bytes, U+2028 and U+2029 (as cdlits
 * escapes table entries), so no URL can end the script or the literal. */
static void js_string(Buf *o, const char *s) {
  const uint8_t *u = (const uint8_t *)s;
  size_t n = strlen(s);
  bbyte(o, '"');
  for (size_t i = 0; i < n; i++) {
    uint8_t c = u[i];
    char t[8];
    if (c == '\\' || c == '"') { bbyte(o, '\\'); bbyte(o, c); }
    else if (c == '<' || c < 32 || c == 127) { snprintf(t, sizeof t, "\\x%02x", c); bstr(o, t); }
    else if (c == 0xe2 && i + 2 < n && u[i + 1] == 0x80 && (u[i + 2] == 0xa8 || u[i + 2] == 0xa9)) {
      bstr(o, u[i + 2] == 0xa8 ? "\\u2028" : "\\u2029");
      i += 2;
    } else bbyte(o, c);
  }
  bbyte(o, '"');
}
static void html_text(Buf *o, const char *s) {
  for (; *s; s++) {
    if (*s == '&') bstr(o, "&amp;");
    else if (*s == '<') bstr(o, "&lt;");
    else if (*s == '>') bstr(o, "&gt;");
    else bbyte(o, *s);
  }
}
static void base64(Buf *o, const uint8_t *p, size_t n) {
  static const char A[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t i = 0;
  for (; i + 3 <= n; i += 3) {
    uint32_t v = (uint32_t)p[i] << 16 | (uint32_t)p[i + 1] << 8 | p[i + 2];
    char q[4] = { A[v >> 18], A[v >> 12 & 63], A[v >> 6 & 63], A[v & 63] };
    bput(o, q, 4);
  }
  if (n - i == 1) {
    uint32_t v = (uint32_t)p[i] << 16;
    char q[4] = { A[v >> 18], A[v >> 12 & 63], '=', '=' };
    bput(o, q, 4);
  } else if (n - i == 2) {
    uint32_t v = (uint32_t)p[i] << 16 | (uint32_t)p[i + 1] << 8;
    char q[4] = { A[v >> 18], A[v >> 12 & 63], A[v >> 6 & 63], '=' };
    bput(o, q, 4);
  }
}

/* ---- the page ---- */
typedef struct {
  const char *page, *out, *shim, *lits, *wasm, *wasm_url, *title;
} Args;

typedef struct { const uint8_t *p; size_t n; } Tag;
static void last_tag(const uint8_t *p, size_t n, void *ctx) { Tag *t = ctx; t->p = p; t->n = n; }

/* The first string literal a line passes to cdom.start( or cdomBoot(, up to its closing quote (any of
 * " ' `) or the line's end; 0 when the line has none. */
static int loads_in(const uint8_t *p, size_t n, const uint8_t **url, size_t *len) {
  for (size_t i = 0; i < n; i++) {
    size_t j;
    if (n - i >= 10 && !memcmp(p + i, "cdom.start", 10)) j = i + 10;
    else if (n - i >= 8 && !memcmp(p + i, "cdomBoot", 8)) j = i + 8;
    else continue;
    while (j < n && (p[j] == ' ' || p[j] == '\t')) j++;
    if (j >= n || p[j] != '(') continue;
    j++;
    while (j < n && (p[j] == ' ' || p[j] == '\t')) j++;
    if (j >= n || (p[j] != '"' && p[j] != '\'' && p[j] != '`')) continue;
    size_t s = ++j;
    while (j < n && p[j] != '"' && p[j] != '\'' && p[j] != '`') j++;
    *url = p + s;
    *len = j - s;
    return 1;
  }
  return 0;
}

static void build(const Args *a, Buf *o) {
  Buf src = {0}, lits = {0}, shim = {0}, mod = {0};
  if (!slurp(a->page, &src)) die("%s: cannot read the page", a->page);
  Tag script = {0};
  int marks = 0, wasms = 0, have_loads = 0;
  Buf loads = {0};
  size_t lineno = 0;
  for (size_t at = 0; at < src.n;) {
    const uint8_t *l = src.p + at, *nl = memchr(l, '\n', src.n - at);
    size_t n = nl ? (size_t)(nl - l) : src.n - at;
    at += n + 1;
    lineno++;
    tags(l, n, "script", last_tag, &script);
    if (find(l, n, SHIM)) {
      if (!alone(l, n, SHIM)) die("%s:%zu: /*CDOM_SHIM*/ must be alone on its line", page_name, lineno);
      if (++marks > 1) die("%s:%zu: a second /*CDOM_SHIM*/ (the page takes the shim once)", page_name, lineno);
      if (!script.p) die("%s:%zu: /*CDOM_SHIM*/ with no <script tag before it", page_name, lineno);
      if (!slurp(a->lits, &lits)) die("%s: cannot read the string table %s", page_name, a->lits);
      put_lines(o, lits.p, lits.n);
      bstr(o, "</script>\n");
      bput(o, script.p, script.n);
      bbyte(o, '\n');
      if (!slurp(a->shim, &shim)) die("%s: cannot read the shim %s", page_name, a->shim);
      put_lines(o, shim.p, shim.n);
      continue;
    }
    /* The markers are found in the page's own text, never in what replaced one. */
    const uint8_t *w = find(l, n, WASM);
    if (w) {
      if (++wasms > 1 || find(w + sizeof WASM - 1, n - (size_t)(w - l) - (sizeof WASM - 1), WASM))
        die("%s:%zu: a second /*CDOM_WASM*/ (the page starts one module)", page_name, lineno);
    } else if (!have_loads) {
      const uint8_t *u;
      size_t un;
      /* an empty URL is no URL: the search goes on, as page.awk's did */
      if (loads_in(l, n, &u, &un) && un) { have_loads = 1; bput(&loads, u, un); bbyte(&loads, 0); }
    }
    for (size_t i = 0; i < n;) {
      if (a->title && n - i >= sizeof TITLE - 1 && !memcmp(l + i, TITLE, sizeof TITLE - 1)) {
        html_text(o, a->title);
        i += sizeof TITLE - 1;
      } else if (l + i == w) {
        if (a->wasm_url) js_string(o, a->wasm_url);
        else if (!a->wasm) die("%s:%zu: /*CDOM_WASM*/, and no module to put there", page_name, lineno);
        else {
          if (!slurp(a->wasm, &mod) || !mod.n) die("%s: cannot read the module to bundle %s", page_name, a->wasm);
          bstr(o, "\"data:application/wasm;base64,");
          base64(o, mod.p, mod.n);
          bbyte(o, '"');
        }
        i += sizeof WASM - 1;
      } else bbyte(o, l[i++]);
    }
    bbyte(o, '\n');
  }
  if (marks != 1)
    die("%s: no /*CDOM_SHIM*/ line (a <script> whose line is only the marker, where the shim goes)", page_name);
  if (a->wasm && !wasms) {
    if (have_loads)
      die("%s: this page loads %s; set APP_WASM_SEPARATE := 1, or start with the bundled module (the /*CDOM_WASM*/ marker)",
          page_name, (const char *)loads.p);
    die("%s: no /*CDOM_WASM*/ for the bundled module (cdom.start(/*CDOM_WASM*/, ...)); or set APP_WASM_SEPARATE := 1", page_name);
  }
}

static void write_out(const char *out, const Buf *o) {
  size_t k = strlen(out) + 32;
  char *tmp = malloc(k);
  if (!tmp) die("cdpage: out of memory");
  snprintf(tmp, k, "%s.tmp%ld", out, (long)getpid());
  FILE *f = fopen(tmp, "wb");
  if (!f) die("cdpage: cannot write %s", tmp);
  int ok = fwrite(o->p, 1, o->n, f) == o->n;
  ok = (fclose(f) == 0) && ok;
  if (!ok || rename(tmp, out) != 0) {
    remove(tmp);
    die("cdpage: cannot write %s", out);
  }
  free(tmp);
}

static void usage(void) __attribute__((noreturn));
static void usage(void) {
  die("usage: cdpage --page in.html --out out.html --shim cdom.min.js --lits app.wasm.lits\n"
      "              [--wasm app.wasm | --wasm-url URL] [--title TITLE]");
}

int main(int argc, char **argv) {
  Args a = {0};
  for (int i = 1; i < argc; i++) {
    const char *f = argv[i];
    const char **slot = !strcmp(f, "--page") ? &a.page : !strcmp(f, "--out") ? &a.out : !strcmp(f, "--shim") ? &a.shim
      : !strcmp(f, "--lits") ? &a.lits : !strcmp(f, "--wasm") ? &a.wasm : !strcmp(f, "--wasm-url") ? &a.wasm_url
      : !strcmp(f, "--title") ? &a.title : 0;
    if (!slot || i + 1 >= argc || *slot) usage();
    *slot = argv[++i];
  }
  if (!a.page || !a.out || !a.shim || !a.lits || (a.wasm && a.wasm_url)) usage();
  page_name = a.page;
  Buf o = {0};
  build(&a, &o);
  write_out(a.out, &o);
  return 0;
}
