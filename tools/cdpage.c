// cdpage: the build step that writes an app's page (docs/page-tool-design.md): the page's source with the
// string table and the shim at its /*CDOM_SHIM*/ line, the module at its /*CDOM_WASM*/ and the title at
// its /*CDOM_TITLE*/.
//
//   cdpage --page in.html --out out.html --shim cdom.min.js --lits app.wasm.lits
//          [--wasm app.wasm [--simd-wasm app.simd.wasm] | --wasm-url URL [--simd-wasm-url URL]]
//          [--title TITLE] [--css-dir DIR] [--job name=job.wasm]...
//          [--noscript-shot url=WxH --noscript-snowman url=WxH]
//   cdpage --css-list DIR out.csslist
//
// Job modules (--job name=file, once for each; cd_job, src/cdom_job.h): each is bundled as a data:application/wasm;base64 URL in
// var cdomJobs={"name":"data:..."}, written after the string table in its script, and the shim finds a job by that name before
// it tries the name as a URL. The page needs no marker for it.
//
// The stylesheets (--css-dir, cdom.mk passes src): every file whose name ends in .css under DIR, in any
// subfolder, is written into the page's <style>, in byte order of its path, so 00-base.css comes before
// 10-layout.css, and that before theme/dark.css; a file or folder whose name starts with a dot is skipped,
// and nothing else is. A symbolic link to a file is read as that file; one to a folder is not entered. The
// files go at the line that holds only /*CDOM_CSS*/, inside a <style> and before /*CDOM_SHIM*/, byte for
// byte but for four things: a comment that fills its lines is left out with them (one beside a rule is
// kept, since removing it could join two tokens), a byte order mark at a file's start is dropped, a file
// that does not end in a newline gains one, and every </style (any letter case) is written <\/style, so no
// file can end the element. Each file must be whole by itself, or the page is refused, naming the file and the line: a
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
//   URL; with neither, the marker is an error. With a SIMD twin as well (--simd-wasm, --simd-wasm-url: cdom.mk's
//   APP_SIMD), it becomes the pair ["<the module>", "<the twin>"], scalar first, in the same form, from which the shim
//   starts the one this browser runs (README.md, "Starting an app"); a page with no /*CDOM_WASM*/ is then refused, since
//   it would never start the twin;
// - with --title, every /*CDOM_TITLE*/, which becomes the title escaped for HTML.
// - the one line that holds only /*CDOM_NOSCRIPT*/ (docs/noscript-design.md), which becomes what a browser with no
//   script shows, and nothing else: the screenshot (--noscript-shot url=WxH), Alex's paragraph, the snowman
//   (--noscript-snowman url=WxH), in HTML 2.0, inside a <noscript> that a script before it also hides in a comment it
//   writes (Netscape 2 has script and no <noscript>). The page's every <script> and <style> with something in it is
//   then wrapped in <!-- and --> (//--> in a script), so a browser that knows neither element shows none of their text;
//   a module script (where <!-- is a syntax error) or one whose text holds --> (it would end that comment early) is
//   refused. Without the two flags, the marker line is removed and nothing is wrapped. The images are the build's
//   (tools/noscript.mjs); this tool only names them.
// Every <script> of the page as written is checked against the HTML parser's escaped states: after a <!--, a <script
// puts the parser in its double-escaped state, where </script> does not end the element, so the page would run on as
// script. A page whose script would do that is refused, naming the line, whatever the flags.
// Refused, with nothing written: no /*CDOM_SHIM*/ line, a second one, the marker beside other text (it
// would be lost with the line), no <script tag before it; a second /*CDOM_WASM*/; and, bundling, a page
// with no /*CDOM_WASM*/: a page that names its module by URL would fetch it besides carrying it, so it is
// refused, naming the URL it loads, and never rewritten. The first fault in line order is the one named;
// a fault of the whole page comes after every line's.
//
// One file, no dependencies, no standard input and no environment: every input is a named file or a flag.
// The page is built in memory, written to out.html.tmp<pid> and renamed, so a failed run leaves nothing.
/* lstat, S_IFREG and strdup are POSIX, not C: glibc and musl declare them under strict -std=c2x only with
 * this. */
#define _DEFAULT_SOURCE
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

/* The byte ranges of a stylesheet that the page leaves out: its whole-line comments. */
typedef struct { size_t (*v)[2]; size_t n, cap; } Spans;

/* One stylesheet, refused unless it is whole by itself: the scanner's states are plain text, a comment,
 * a string in " or ', and an unquoted url( ... ). It also finds the comments that fill their lines (only
 * spaces and tabs before the comment on its first line, and only spaces, tabs and a CR after it on its
 * last), and adds each, with its lines, to drop. A comment beside other text is kept: removing it could
 * join two tokens or part a compound selector (a, a comment, .b is a.b; with a space, a descendant), and a
 * whole line holds no token to join. */
static void check_sheet(const char *name, const uint8_t *p, size_t n, Spans *drop) {
    enum { TEXT, COMMENT, DQ, SQ, URL } state = TEXT;
    size_t line = 1, opened = 0, *opens = 0, depth = 0, cap = 0;
    /* where this line starts, and where the comment being scanned would be left out from (SIZE_MAX: it is kept) */
    size_t line_at = 0, from = SIZE_MAX;
    int blank = 1;                        /* only spaces and tabs so far on this line, outside a comment */
    for (size_t i = 0; i < n;) {
        uint8_t c = p[i];
        if (!c) die("%s:%zu: a NUL byte", name, line);
        int k = utf8_at(p + i, n - i);
        if (!k) die("%s:%zu: bytes that are not UTF-8 (a page is UTF-8)", name, line);
        if (c == '\n') line++;
        if (state == TEXT && c == '\n') line_at = i + 1, blank = 1;
        else if (state == TEXT && c != ' ' && c != '\t' && !(c == '/' && i + 1 < n && p[i + 1] == '*')) blank = 0;
        switch (state) {
        case TEXT:
            if (c == '/' && i + 1 < n && p[i + 1] == '*') { state = COMMENT; opened = line;
                from = blank ? line_at : SIZE_MAX; /* it may fill its lines: then they go from this line's start */
                i += 2;
                continue;
            }
            if (c == '"' || c == '\'') { state = c == '"' ? DQ : SQ; opened = line; break; }
            if (c == '\\' && i + 1 < n) { i++; if (p[i] == '\n') line++; break; } /* an escape: the next byte is text */
            if (c == '{') {
                if (depth == cap) {
                    cap = cap * 2 + 16;
                    opens = realloc(opens, cap * sizeof *opens);
                    if (!opens) die("cdpage: out of memory");
                }
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
            if (c == '*' && i + 1 < n && p[i + 1] == '/') {
                state = TEXT;
                i += 2;
                size_t j = i;
                while (j < n && (p[j] == ' ' || p[j] == '\t' || p[j] == '\r')) j++;
                if (from != SIZE_MAX && (j == n || p[j] == '\n')) { /* it fills its lines: they go, with the newline */
                    if (drop->n == drop->cap) {
                        drop->cap = drop->cap * 2 + 16;
                        drop->v = realloc(drop->v, drop->cap * sizeof *drop->v);
                        if (!drop->v) die("cdpage: out of memory");
                    }
                    drop->v[drop->n][0] = from;
                    drop->v[drop->n++][1] = j < n ? j + 1 : n;
                } else blank = 0;
                continue;
            }
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

/* The stylesheets as the page holds them: each checked, its byte order mark dropped, its whole-line
 * comments left out, every </style written <\/style, and a newline after a file that does not end in one. */
static void sheets_text(const Sheets *s, Buf *o) {
    for (size_t f = 0; f < s->n; f++) {
        Buf b = {0};
        if (!slurp(s->v[f].path, &b)) die("cdpage: cannot read the stylesheet %s", s->v[f].path);
        const uint8_t *p = b.p;
        size_t n = b.n, at = o->n, d = 0;
        if (n >= 3 && p[0] == 0xef && p[1] == 0xbb && p[2] == 0xbf) p += 3, n -= 3;
        Spans drop = {0};
        check_sheet(s->v[f].path, p, n, &drop);
        for (size_t i = 0; i < n; i++) {
            if (d < drop.n && i == drop.v[d][0]) { i = drop.v[d++][1] - 1; continue; }
            if (p[i] == '<' && i + 1 < n && p[i + 1] == '/' && starts_ci(p + i + 2, n - i - 2, "style")) { bstr(o, "<\\/"); i++; continue; }
            bbyte(o, p[i]);
        }
        if (o->n > at && o->p[o->n - 1] != '\n') bbyte(o, '\n');
        free(drop.v);
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

/* ---- what a browser with no script shows (docs/noscript-design.md) ---- */
static const char NOSCRIPT[] = "/*CDOM_NOSCRIPT*/";
/* Alex's paragraph, verbatim, in ASCII: Mosaic has no UTF-8, so the apostrophes are plain. */
static const char NOSCRIPT_TEXT[] = "Brother I'm sorry, the only way I could support your browser is if I had a server running just for you. "
  "And even if I did, anything you sent to that server would be decrypted. Regrettably I have chosen to drop support on this browser. "
  "Here's a snowman";

/* url=WxH: the url (printable ASCII, none of " < > & ' and no --, since it stands in an attribute inside the comment
 * that hides the block from a browser with script) and the two sizes, 1 to 4096. */
typedef struct { char url[256]; unsigned w, h; } Img;
static Img img_arg(const char *flag, const char *v) {
    Img m = {0};
    const char *eq = strrchr(v, '=');
    if (!eq || eq == v || (size_t)(eq - v) >= sizeof m.url) die("%s takes url=WxH, not %s", flag, v);
    memcpy(m.url, v, (size_t)(eq - v));
    for (const char *c = m.url; *c; c++)
        if (*c <= ' ' || *c > '~' || strchr("\"<>&'", *c) || (c[0] == '-' && c[1] == '-'))
            die("%s: the url %s may hold only printable ASCII, none of \" < > & ' and no --", flag, m.url);
    char x;
    if (sscanf(eq + 1, "%ux%u%c", &m.w, &m.h, &x) != 2 || !m.w || !m.h || m.w > 4096 || m.h > 4096)
        die("%s: the size in %s is not WxH (1 to 4096 each)", flag, v);
    return m;
}
static void img_tag(Buf *o, const Img *m, const char *alt) {
    char t[400];
    snprintf(t, sizeof t, "<img src=\"%s\" width=\"%u\" height=\"%u\" alt=\"%s\">\n", m->url, m->w, m->h, alt);
    bstr(o, t);
}
/* The block at the marker. A browser with no script knows neither <script> nor <noscript>: it skips the script's
 * text (a comment) and shows the three. A browser with script runs the script, whose document.write opens a comment
 * that the parser reads the rest through, to the --> of <wbr -->: so Netscape 2, which has no <noscript>, never shows
 * the block. <noscript> is for the rest: a modern browser's preload scanner reads ahead of the parser and does not
 * run the script, but skips a <noscript>'s contents when script is on, so the images are never fetched. <wbr -->
 * ends the comment in both readings, SGML's strict one (the -- closes it, then the >) and the minimal one (-->), and
 * outside a comment it is a tag HTML 2.0 does not know and HTML5 a void element: nothing to show either way. */
static void noscript_block(Buf *o, const char *shot_arg, const char *snow_arg) {
    Img shot = img_arg("--noscript-shot", shot_arg), snow = img_arg("--noscript-snowman", snow_arg);
    bstr(o, "<script><!--\ndocument.write(\"<!-\"+\"-\");\n//--></script><noscript>\n");
    img_tag(o, &shot, "[A screenshot of this page in a browser with JavaScript]");
    bstr(o, "<p>");
    bstr(o, NOSCRIPT_TEXT);
    bstr(o, "</p>\n");
    img_tag(o, &snow, "[A snowman]");
    bstr(o, "</noscript><wbr -->\n");
}

/* Whether the page is inside a <script> element, as style_state is for <style>. */
static void script_state(const uint8_t *p, size_t n, int *in) {
    for (size_t i = 0; i + 1 < n; i++) {
        if (p[i] != '<') continue;
        if (p[i + 1] == '/' && starts_ci(p + i + 2, n - i - 2, "script")) *in = 0;
        else if (starts_ci(p + i + 1, n - i - 1, "script") && i + 7 < n && (p[i + 7] == '>' || p[i + 7] == ' ' || p[i + 7] == '\t')) *in = 1;
    }
}

/* The page as written, element by element: each <script ...> and <style ...> and the text up to its first </script
 * (</style), outside the page's own comments. A tag with no > or an element with no end tag ends the walk. */
typedef struct { size_t tag, open_end, body, body_end; int style; } Elem;
static int delim(uint8_t c) { return c == '\t' || c == '\n' || c == '\f' || c == '\r' || c == ' ' || c == '/' || c == '>'; }
static int next_elem(const uint8_t *p, size_t n, size_t *at, Elem *e) {
    for (size_t i = *at; i < n; i++) {
        if (p[i] != '<') continue;
        if (n - i >= 4 && !memcmp(p + i, "<!--", 4)) {
            const uint8_t *end = find(p + i + 4, n - i - 4, "-->");
            if (!end) return 0;
            i = (size_t)(end - p) + 2;
            continue;
        }
        int style = starts_ci(p + i + 1, n - i - 1, "style") && i + 6 < n && delim(p[i + 6]);
        if (!style && !(starts_ci(p + i + 1, n - i - 1, "script") && i + 7 < n && delim(p[i + 7]))) continue;
        const uint8_t *gt = memchr(p + i, '>', n - i);
        if (!gt) return 0;
        size_t body = (size_t)(gt - p) + 1, j = body;
        const char *close = style ? "</style" : "</script";
        while (j < n && !starts_ci(p + j, n - j, close)) j++;
        if (j >= n) return 0;
        *e = (Elem){ i, body, body, j, style };
        *at = j;
        return 1;
    }
    return 0;
}
static size_t line_of(const uint8_t *p, size_t at) {
    size_t l = 1;
    for (size_t i = 0; i < at; i++) l += p[i] == '\n';
    return l;
}
/* A script's text through the HTML parser's script data states (the HTML standard, 13.2.5.4 to .25): plain, escaped
 * (after <!--), and double-escaped (after a <script in escaped text), each with its dash states. Returns the offset
 * of the <script that left the parser double-escaped at the text's end, where the </script> that follows would not
 * end the element, or SIZE_MAX when it ends there. */
static size_t double_escaped_at(const uint8_t *p, size_t n) {
    enum { DATA, ESC, ESC_D, ESC_DD, DESC, DESC_D, DESC_DD } st = DATA;
    size_t from = SIZE_MAX;
    for (size_t i = 0; i < n;) {
        uint8_t c = p[i];
        switch (st) {
        case DATA:
            if (n - i >= 4 && !memcmp(p + i, "<!--", 4)) { st = ESC_DD; i += 4; continue; }
            break;
        case ESC: case ESC_D: case ESC_DD:
            if (c == '-') { st = st == ESC ? ESC_D : ESC_DD; break; }
            if (c == '>' && st == ESC_DD) { st = DATA; break; }
            st = ESC;
            if (c == '<' && starts_ci(p + i + 1, n - i - 1, "script") && i + 7 < n && delim(p[i + 7])) { st = DESC; from = i; i += 8; continue; }
            break;
        case DESC: case DESC_D: case DESC_DD:
            if (c == '-') { st = st == DESC ? DESC_D : DESC_DD; break; }
            if (c == '>' && st == DESC_DD) { st = DATA; break; }
            st = DESC;
            if (c == '<' && n - i >= 2 && p[i + 1] == '/' && starts_ci(p + i + 2, n - i - 2, "script") && i + 8 < n && delim(p[i + 8])) { st = ESC; i += 9; continue; }
            break;
        }
        i++;
    }
    return st >= DESC ? from : SIZE_MAX;
}
/* Every script of the page: refused if the parser would read on past its </script>. */
static void check_scripts(const uint8_t *p, size_t n) {
    Elem e;
    for (size_t at = 0; next_elem(p, n, &at, &e);) {
        if (e.style) continue;
        size_t d = double_escaped_at(p + e.body, e.body_end - e.body);
        if (d != SIZE_MAX)
            die("%s: the <script> on line %zu of the page as built holds \"<!--\" and then \"<script\" (line %zu): the HTML parser is "
                "then double-escaped, and the </script> on line %zu would not end the element; write that <script as \\x3cscript or '<'+'script'",
                page_name, line_of(p, e.tag), line_of(p, e.body + d), line_of(p, e.body_end));
    }
}
static int blank(const uint8_t *p, size_t n) {
    for (size_t i = 0; i < n; i++)
        if (p[i] != ' ' && p[i] != '\t' && p[i] != '\n' && p[i] != '\r' && p[i] != '\f') return 0;
    return 1;
}
/* The page with every script's and style's text in a comment, for a browser that knows neither element. A text that
 * is blank needs none; one that already starts with <!-- is kept, and checked as the others are: its first --> must
 * be its end, or a browser with no script would show what follows as text. */
static void wrap_all(const Buf *in, Buf *o) {
    const uint8_t *p = in->p;
    size_t n = in->n, done = 0;
    Elem e;
    for (size_t at = 0; next_elem(p, n, &at, &e);) {
        const uint8_t *b = p + e.body;
        size_t bn = e.body_end - e.body, line = line_of(p, e.tag);
        const char *what = e.style ? "<style>" : "<script>";
        if (blank(b, bn)) continue;
        if (!e.style) {
            const uint8_t *ty = 0;
            for (size_t i = e.tag; i + 5 < e.open_end; i++)
                if (starts_ci(p + i, e.open_end - i, "type") && (p[i - 1] == ' ' || p[i - 1] == '\t' || p[i - 1] == '\n')) { ty = p + i; break; }
            if (ty && find(ty, (size_t)(p + e.open_end - ty), "module"))
                die("%s: line %zu: a module script, where <!-- is a syntax error, cannot be hidden from a browser with no script (/*CDOM_NOSCRIPT*/)", page_name, line);
        }
        size_t lead = 0;
        while (lead < bn && (b[lead] == ' ' || b[lead] == '\t' || b[lead] == '\n' || b[lead] == '\r')) lead++;
        int had = bn - lead >= 4 && !memcmp(b + lead, "<!--", 4);
        const uint8_t *from = had ? b + lead + 4 : b, *end = find(from, bn - (size_t)(from - b), "-->");
        if (end && (!had || !blank(end + 3, bn - (size_t)(end + 3 - b))))
            die("%s: the %s on line %zu of the page as built holds \"-->\" (line %zu), which would end the comment that hides it from a "
                "browser with no script, and that browser would show the rest as text (/*CDOM_NOSCRIPT*/)", page_name, what, line, line_of(p, (size_t)(end - p)));
        if (had && !end)
            die("%s: the %s on line %zu of the page as built opens a <!-- it never closes (/*CDOM_NOSCRIPT*/)", page_name, what, line);
        if (had) continue;
        bput(o, p + done, e.body - done);
        bstr(o, "<!--");
        if (b[0] != '\n') bbyte(o, '\n');
        bput(o, b, bn);
        if (b[bn - 1] != '\n') bbyte(o, '\n');
        bstr(o, e.style ? "-->" : "//-->");
        done = e.body_end;
    }
    bput(o, p + done, n - done);
}

/* ---- the page ---- */
typedef struct {
    const char *page, *out, *shim, *lits, *wasm, *wasm_url, *simd_wasm, *simd_wasm_url, *title, *css_dir;
    const char *noscript_shot, *noscript_snowman; /* url=WxH each, or both 0 */
    const char *jobs[16]; /* --job name=file, each */
    int njobs;
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

/* One module at the CDOM_WASM marker: its URL, or the file bundled as a data:application/wasm;base64 URL. */
static void module_at(Buf *o, const char *file, const char *url) {
    if (url) {
        js_string(o, url);
        return;
    }
    Buf mod = {0};
    if (!slurp(file, &mod) || !mod.n) die("%s: cannot read the module to bundle %s", page_name, file);
    bstr(o, "\"data:application/wasm;base64,");
    base64(o, mod.p, mod.n);
    bbyte(o, '"');
    free(mod.p);
}

static void build(const Args *a, Buf *o, int *noscripts) {
    Buf src = {0}, lits = {0}, shim = {0};
    if (!slurp(a->page, &src)) die("%s: cannot read the page", a->page);
    /* The stylesheets are found and checked first: a fault in one is named before the page's. */
    Sheets sheets = discover(a->css_dir);
    Buf css = {0};
    sheets_text(&sheets, &css);
    Tag script = {0};
    int marks = 0, wasms = 0, have_loads = 0, css_marks = 0, in_style = 0, in_script = 0;
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
        if (find(l, n, NOSCRIPT)) {
            if (!alone(l, n, NOSCRIPT)) die("%s:%zu: /*CDOM_NOSCRIPT*/ must be alone on its line", page_name, lineno);
            if (++*noscripts > 1) die("%s:%zu: a second /*CDOM_NOSCRIPT*/ (the page shows its fallback once)", page_name, lineno);
            if (in_style || in_script) die("%s:%zu: /*CDOM_NOSCRIPT*/ inside a <%s> element (it goes in the <body>, outside any script)", page_name, lineno, in_style ? "style" : "script");
            if (a->noscript_shot) noscript_block(o, a->noscript_shot, a->noscript_snowman);
            continue;
        }
        style_state(l, n, &in_style);
        script_state(l, n, &in_script);
        if (find(l, n, SHIM)) {
            if (!alone(l, n, SHIM)) die("%s:%zu: /*CDOM_SHIM*/ must be alone on its line", page_name, lineno);
            if (++marks > 1) die("%s:%zu: a second /*CDOM_SHIM*/ (the page takes the shim once)", page_name, lineno);
            if (!script.p) die("%s:%zu: /*CDOM_SHIM*/ with no <script tag before it", page_name, lineno);
            if (!slurp(a->lits, &lits)) die("%s: cannot read the string table %s", page_name, a->lits);
            put_lines(o, lits.p, lits.n);
            if (a->njobs) { /* the job modules cd_job names, bundled: var cdomJobs={"name":"data:application/wasm;base64,...",...}; */
                bstr(o, "var cdomJobs={");
                for (int j = 0; j < a->njobs; j++) {
                    const char *eq = strchr(a->jobs[j], '=');
                    if (!eq || eq == a->jobs[j]) die("--job takes name=file, not %s", a->jobs[j]);
                    char name[256];
                    if ((size_t)(eq - a->jobs[j]) >= sizeof name) die("--job: the name in %s is too long", a->jobs[j]);
                    memcpy(name, a->jobs[j], (size_t)(eq - a->jobs[j]));
                    name[eq - a->jobs[j]] = 0;
                    Buf job = {0};
                    if (!slurp(eq + 1, &job) || !job.n) die("%s: cannot read the job module to bundle %s", page_name, eq + 1);
                    if (j) bbyte(o, ',');
                    js_string(o, name);
                    bstr(o, ":\"data:application/wasm;base64,");
                    base64(o, job.p, job.n);
                    bbyte(o, '"');
                }
                bstr(o, "};\n");
            }
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
                if (!a->wasm_url && !a->wasm) die("%s:%zu: /*CDOM_WASM*/, and no module to put there", page_name, lineno);
                int pair = a->simd_wasm || a->simd_wasm_url;
                if (pair) bbyte(o, '[');
                module_at(o, a->wasm, a->wasm_url);
                if (pair) {
                    bbyte(o, ',');
                    module_at(o, a->simd_wasm, a->simd_wasm_url);
                    bbyte(o, ']');
                }
                i += sizeof WASM - 1;
            } else bbyte(o, l[i++]);
        }
        bbyte(o, '\n');
    }
    if (marks != 1)
        die("%s: no /*CDOM_SHIM*/ line (a <script> whose line is only the marker, where the shim goes)", page_name);
    if ((a->simd_wasm || a->simd_wasm_url) && !wasms)
        die("%s: no /*CDOM_WASM*/ for the module and its SIMD twin (APP_SIMD := 1: cdom.start(/*CDOM_WASM*/, ...))", page_name);
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
        "              [--wasm app.wasm [--simd-wasm app.simd.wasm] | --wasm-url URL [--simd-wasm-url URL]]\n"
        "              [--title TITLE] [--css-dir DIR] [--job name=job.wasm]...\n"
        "              [--noscript-shot url=WxH --noscript-snowman url=WxH]\n"
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
        if (!strcmp(f, "--job") && i + 1 < argc) {
            if (a.njobs == 16) die("--job: at most 16 job modules");
            a.jobs[a.njobs++] = argv[++i];
            continue;
        }
        const char **slot = !strcmp(f, "--page") ? &a.page : !strcmp(f, "--out") ? &a.out : !strcmp(f, "--shim") ? &a.shim
          : !strcmp(f, "--lits") ? &a.lits : !strcmp(f, "--wasm") ? &a.wasm : !strcmp(f, "--wasm-url") ? &a.wasm_url
          : !strcmp(f, "--simd-wasm") ? &a.simd_wasm : !strcmp(f, "--simd-wasm-url") ? &a.simd_wasm_url
          : !strcmp(f, "--title") ? &a.title : !strcmp(f, "--css-dir") ? &a.css_dir
          : !strcmp(f, "--noscript-shot") ? &a.noscript_shot : !strcmp(f, "--noscript-snowman") ? &a.noscript_snowman : 0;
        if (!slot || i + 1 >= argc || *slot) usage();
        *slot = argv[++i];
    }
    /* A twin goes with a module of its own form: bundled with bundled, a URL with a URL. */
    if (!a.page || !a.out || !a.shim || !a.lits || (a.wasm && a.wasm_url) || (a.simd_wasm && !a.wasm) || (a.simd_wasm_url && !a.wasm_url)
        || !a.noscript_shot != !a.noscript_snowman)
        usage();
    page_name = a.page;
    Buf o = {0};
    int noscripts = 0;
    build(&a, &o, &noscripts);
    if (noscripts && a.noscript_shot) {
        Buf w = {0};
        wrap_all(&o, &w);
        free(o.p);
        o = w;
    }
    check_scripts(o.p, o.n);
    write_out(a.out, &o);
    return 0;
}
