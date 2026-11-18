// cdpage: the build step that writes an app's page (docs/page-tool-design.md): the page's source with the
// string table and the shim at its /*CDOM_SHIM*/ line, the module at its /*CDOM_WASM*/ and the title at
// its /*CDOM_TITLE*/.
//
//   cdpage --page in.html --out out.html --shim cdom.min.js --lits app.wasm.lits
//          [--wasm app.wasm | --wasm-url URL] [--title TITLE] [--css-dir DIR]
//   cdpage --css-list DIR out.csslist
//
// The stylesheets (--css-dir, cdom.mk passes src): every file whose name ends in .css under DIR, in any
// subfolder, is written into the page's <style>, in byte order of its path, so 00-base.css comes before
// 10-layout.css, and that before theme/dark.css; a file or folder whose name starts with a dot is skipped,
// and nothing else is. A symbolic link to a file is read as that file; one to a folder is not entered. The
// files go at the line that holds only /*CDOM_CSS*/, inside a <style> and before /*CDOM_SHIM*/, byte for
// byte but for three things: a byte order mark at a file's start is dropped, a file that does not end in a
// newline gains one, and every </style (any letter case) is written <\/style, so no file can end the
// element. Each file must be whole by itself, or the page is refused, naming the file and the line: a
// comment, string or url( that never ends, a { never closed or a } never opened, an @import, a NUL, or
// bytes that are not UTF-8. With no files, the marker line is removed; with files and no marker, the page
// is refused.
// --css-list writes the list of what --css-dir would bundle, in the same order (one line a file: its path
// under DIR, a tab, the FNV-1a 64 of its bytes), to out.csslist, and replaces that file only when the text
// differs from what it holds, so its time moves exactly when a stylesheet is added, removed, renamed or
// edited. cdom.mk makes the page depend on it.
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
#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
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


/* ---- the stylesheets ---- */
static const char CSS[] = "/*CDOM_CSS*/";

typedef struct { char *rel, *path; } Sheet; /* the path under the directory, and the path to open */
typedef struct { Sheet *v; size_t n, cap; } Sheets;

static char *join(const char *a, const char *b) {
  size_t na = strlen(a), nb = strlen(b);
  char *s = malloc(na + nb + 2);
  if (!s) die("cdpage: out of memory");
  memcpy(s, a, na);
  s[na] = '/';
  memcpy(s + na + 1, b, nb + 1);
  return s;
}
static int is_css(const char *name) {
  size_t n = strlen(name);
  return n > 4 && !memcmp(name + n - 4, ".css", 4);
}
/* Every stylesheet under dir/rel (rel 0 at the top), by the rule above. */
static void walk(const char *dir, const char *rel, Sheets *out) {
  char *here = rel ? join(dir, rel) : strdup(dir);
  DIR *d = opendir(here);
  if (!d) die("cdpage: cannot read the directory %s", here);
  struct dirent *e;
  while ((errno = 0, e = readdir(d))) {
    if (e->d_name[0] == '.') continue; /* ., .., and every dot-name: not the author's */
    char *r = rel ? join(rel, e->d_name) : strdup(e->d_name), *path = join(dir, r);
    struct stat st;
    if (lstat(path, &st)) die("cdpage: cannot read %s", path);
    int link = S_ISLNK(st.st_mode);
    if (link && stat(path, &st)) st.st_mode = S_IFREG; /* a link that leads nowhere: read below, and refused there */
    if (S_ISDIR(st.st_mode) && !link) walk(dir, r, out);
    else if (!S_ISDIR(st.st_mode) && is_css(e->d_name)) {
      if (out->n == out->cap) {
        out->cap = out->cap * 2 + 16;
        out->v = realloc(out->v, out->cap * sizeof *out->v);
        if (!out->v) die("cdpage: out of memory");
      }
      out->v[out->n++] = (Sheet){ r, path };
      continue;
    }
    free(r);
    free(path);
  }
  if (errno) die("cdpage: cannot read the directory %s", here);
  closedir(d);
  free(here);
}
static int by_path(const void *a, const void *b) {
  return strcmp(((const Sheet *)a)->rel, ((const Sheet *)b)->rel); /* bytes, unsigned, the shorter first */
}
/* The stylesheets under dir, in byte order of their paths. An empty or absent dir has none. */
static Sheets discover(const char *dir) {
  Sheets s = {0};
  struct stat st;
  if (!dir || !*dir || (stat(dir, &st) && errno == ENOENT)) return s;
  walk(dir, 0, &s);
  qsort(s.v, s.n, sizeof *s.v, by_path);
  return s;
}

static int utf8_at(const uint8_t *p, size_t n) { /* the length of the UTF-8 sequence at p, or 0 */
  uint8_t c = p[0];
  size_t k = c < 0x80 ? 1 : (c & 0xe0) == 0xc0 ? 2 : (c & 0xf0) == 0xe0 ? 3 : (c & 0xf8) == 0xf0 ? 4 : 0;
  if (!k || k > n) return 0;
  for (size_t i = 1; i < k; i++)
    if ((p[i] & 0xc0) != 0x80) return 0;
  uint32_t v = k == 1 ? c : k == 2 ? (c & 0x1fu) : k == 3 ? (c & 0x0fu) : (c & 0x07u);
  for (size_t i = 1; i < k; i++) v = v << 6 | (p[i] & 0x3fu);
  static const uint32_t least[5] = { 0, 0, 0x80, 0x800, 0x10000 };
  if (v < least[k] || v > 0x10ffff || (v >= 0xd800 && v <= 0xdfff)) return 0; /* overlong, past Unicode, a surrogate */
  return (int)k;
}

/* One stylesheet, refused unless it is whole by itself: the scanner's states are plain text, a comment,
 * a string in " or ', and an unquoted url( ... ). */
static void check_sheet(const char *name, const uint8_t *p, size_t n) {
  enum { TEXT, COMMENT, DQ, SQ, URL } state = TEXT;
  size_t line = 1, opened = 0, *opens = 0, depth = 0, cap = 0;
  for (size_t i = 0; i < n;) {
    uint8_t c = p[i];
    if (!c) die("%s:%zu: a NUL byte", name, line);
    int k = utf8_at(p + i, n - i);
    if (!k) die("%s:%zu: bytes that are not UTF-8 (a page is UTF-8)", name, line);
    if (c == '\n') line++;
    switch (state) {
    case TEXT:
      if (c == '/' && i + 1 < n && p[i + 1] == '*') { state = COMMENT; opened = line; i += 2; continue; }
      if (c == '"' || c == '\'') { state = c == '"' ? DQ : SQ; opened = line; break; }
      if (c == '\\' && i + 1 < n) { i++; if (p[i] == '\n') line++; break; } /* an escape: the next byte is text */
      if (c == '{') {
        if (depth == cap) { cap = cap * 2 + 16; opens = realloc(opens, cap * sizeof *opens); if (!opens) die("cdpage: out of memory"); }
        opens[depth++] = line;
      } else if (c == '}') {
        if (!depth) die("%s:%zu: a } with no { before it", name, line);
        depth--;
      } else if (c == '@' && starts_ci(p + i + 1, n - i - 1, "import")) {
        die("%s:%zu: @import; put the file in the same folder instead (every .css file there is in the page, in byte order of its path)", name, line);
      } else if ((c == 'u' || c == 'U') && starts_ci(p + i, n - i, "url(")) {
        size_t j = i + 4;
        while (j < n && (p[j] == ' ' || p[j] == '\t' || p[j] == '\n' || p[j] == '\r' || p[j] == '\f')) j++;
        if (j < n && p[j] != '"' && p[j] != '\'') { state = URL; opened = line; i += 4; continue; }
      }
      break;
    case COMMENT:
      if (c == '*' && i + 1 < n && p[i + 1] == '/') { state = TEXT; i += 2; continue; }
      break;
    case DQ:
    case SQ:
      if (c == '\\' && i + 1 < n) { i++; if (p[i] == '\n') line++; break; } /* an escape; a newline after it continues the string */
      if (c == '\n') die("%s:%zu: a string that never ends (a newline before its closing quote)", name, line - 1);
      if (c == (state == DQ ? '"' : '\'')) state = TEXT;
      break;
    case URL:
      if (c == '\\' && i + 1 < n) { i++; if (p[i] == '\n') line++; break; }
      if (c == ')') state = TEXT;
      break;
    }
    i += (size_t)k;
  }
  if (state == COMMENT) die("%s:%zu: a comment that never ends", name, opened);
  if (state == DQ || state == SQ) die("%s:%zu: a string that never ends", name, opened);
  if (state == URL) die("%s:%zu: a url( that never ends", name, opened);
  if (depth) die("%s:%zu: a { that is never closed", name, opens[depth - 1]);
  free(opens);
}

/* The stylesheets as the page holds them: each checked, its byte order mark dropped, every </style
 * written <\/style, and a newline after a file that does not end in one. */
static void sheets_text(const Sheets *s, Buf *o) {
  for (size_t f = 0; f < s->n; f++) {
    Buf b = {0};
    if (!slurp(s->v[f].path, &b)) die("cdpage: cannot read the stylesheet %s", s->v[f].path);
    const uint8_t *p = b.p;
    size_t n = b.n;
    if (n >= 3 && p[0] == 0xef && p[1] == 0xbb && p[2] == 0xbf) p += 3, n -= 3;
    check_sheet(s->v[f].path, p, n);
    for (size_t i = 0; i < n; i++) {
      if (p[i] == '<' && i + 1 < n && p[i + 1] == '/' && starts_ci(p + i + 2, n - i - 2, "style")) { bstr(o, "<\\/"); i++; continue; }
      bbyte(o, p[i]);
    }
    if (n && p[n - 1] != '\n') bbyte(o, '\n');
    free(b.p);
  }
}

/* FNV-1a, 64 bits */
static uint64_t fnv1a(const uint8_t *p, size_t n) {
  uint64_t h = 0xcbf29ce484222325u;
  for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 0x100000001b3u;
  return h;
}
/* cdpage --css-list DIR OUT: the list, and OUT replaced only when its text changes. */
static void css_list(const char *dir, const char *out) {
  Sheets s = discover(dir);
  Buf o = {0};
  for (size_t f = 0; f < s.n; f++) {
    Buf b = {0};
    char t[32];
    bstr(&o, s.v[f].rel);
    if (slurp(s.v[f].path, &b)) snprintf(t, sizeof t, "\t%016llx\n", (unsigned long long)fnv1a(b.p, b.n));
    else snprintf(t, sizeof t, "\tunreadable\n"); /* the page step names it */
    bstr(&o, t);
    free(b.p);
  }
  Buf old = {0};
  if (slurp(out, &old) && old.n == o.n && (!o.n || !memcmp(old.p, o.p, o.n))) return;
  write_out(out, &o);
}

/* ---- the page ---- */
typedef struct {
  const char *page, *out, *shim, *lits, *wasm, *wasm_url, *title, *css_dir;
} Args;

typedef struct { const uint8_t *p; size_t n; } Tag;
static void last_tag(const uint8_t *p, size_t n, void *ctx) { Tag *t = ctx; t->p = p; t->n = n; }
/* Whether the page is inside a <style> element: a <style tag opens one, a </style closes it. */
static void style_state(const uint8_t *p, size_t n, int *in) {
  for (size_t i = 0; i + 1 < n; i++) {
    if (p[i] != '<') continue;
    if (p[i + 1] == '/' && starts_ci(p + i + 2, n - i - 2, "style")) *in = 0;
    else if (starts_ci(p + i + 1, n - i - 1, "style") && i + 6 < n && (p[i + 6] == '>' || p[i + 6] == ' ' || p[i + 6] == '\t')) *in = 1;
  }
}

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
  /* The stylesheets are found and checked first: a fault in one is named before the page's. */
  Sheets sheets = discover(a->css_dir);
  Buf css = {0};
  sheets_text(&sheets, &css);
  Tag script = {0};
  int marks = 0, wasms = 0, have_loads = 0, css_marks = 0, in_style = 0;
  Buf loads = {0};
  size_t lineno = 0;
  for (size_t at = 0; at < src.n;) {
    const uint8_t *l = src.p + at, *nl = memchr(l, '\n', src.n - at);
    size_t n = nl ? (size_t)(nl - l) : src.n - at;
    at += n + 1;
    lineno++;
    tags(l, n, "script", last_tag, &script);
    if (find(l, n, CSS)) {
      if (!alone(l, n, CSS)) die("%s:%zu: /*CDOM_CSS*/ must be alone on its line", page_name, lineno);
      if (++css_marks > 1) die("%s:%zu: a second /*CDOM_CSS*/ (the page takes the stylesheets once)", page_name, lineno);
      if (!in_style) die("%s:%zu: /*CDOM_CSS*/ outside a <style> element", page_name, lineno);
      if (marks) die("%s:%zu: /*CDOM_CSS*/ after /*CDOM_SHIM*/ (the styles come before the shim's script)", page_name, lineno);
      bput(o, css.p, css.n);
      continue;
    }
    style_state(l, n, &in_style);
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
  if (sheets.n && !css_marks) {
    size_t k = strlen(a->css_dir);
    while (k > 1 && a->css_dir[k - 1] == '/') k--;
    char more[64] = "";
    if (sheets.n > 1) snprintf(more, sizeof more, " (and %zu more)", sheets.n - 1);
    die("%s: %.*s/ holds %s%s, and this page has no /*CDOM_CSS*/ line (a <style> whose line is only the marker, "
        "before the shim's script); add it, or move the files out of %.*s/", page_name, (int)k, a->css_dir, sheets.v[0].rel, more, (int)k, a->css_dir);
  }
}

static void usage(void) __attribute__((noreturn));
static void usage(void) {
  die("usage: cdpage --page in.html --out out.html --shim cdom.min.js --lits app.wasm.lits\n"
      "              [--wasm app.wasm | --wasm-url URL] [--title TITLE] [--css-dir DIR]\n"
      "       cdpage --css-list DIR out.csslist");
}

int main(int argc, char **argv) {
  Args a = {0};
  if (argc > 1 && !strcmp(argv[1], "--css-list")) {
    if (argc != 4) usage();
    css_list(argv[2], argv[3]);
    return 0;
  }
  for (int i = 1; i < argc; i++) {
    const char *f = argv[i];
    const char **slot = !strcmp(f, "--page") ? &a.page : !strcmp(f, "--out") ? &a.out : !strcmp(f, "--shim") ? &a.shim
      : !strcmp(f, "--lits") ? &a.lits : !strcmp(f, "--wasm") ? &a.wasm : !strcmp(f, "--wasm-url") ? &a.wasm_url
      : !strcmp(f, "--title") ? &a.title : !strcmp(f, "--css-dir") ? &a.css_dir : 0;
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
