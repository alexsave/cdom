/* Native tests of the fed decoder (cdom_image.h, cd_image_begin .. cd_image_end) and its glue to a picked file
 * (src/cdom_image_file.c), the test plan of docs/chunked-file-read-design.md, section 8, items 1 to 9. Run by
 * tests/image/run.sh from its output directory (after oracle.py made cases.tsv and inputs/, and bench/chunked/edge.py
 * made edge/), under AddressSanitizer and UndefinedBehaviorSanitizer on tests/image/host_alloc.c's counted heap.
 * The reference is the whole decode (cd_image_resize's, through cd_image_decode for libjpeg's warnings): every fed
 * decode must answer the same status, the same size and the same bytes, and leave the heap where it found it.
 * The glue runs over a fake file module and event loop below (cd_file_open, cd_file_next_cb, cd_file_close,
 * cd_timer_cb, cd_cb_*), which can end a file early, fail a read or answer 0 bytes.
 * The one random source: CHUNKED_SEED (splitmix64), for the random piece sizes.
 * CHUNKED_FULL=1 runs every piece size on every input (minutes); without it, pieces of 1 to 3 bytes run on the inputs
 * up to 256 KB and 7 bytes on those up to 2 MB, and the marker splits of item 3 every byte only on the JPEGs up to
 * 64 KB (every 61st byte up to 1 MB, every 509th past; FULL: every byte up to 4 MB, every 61st past). CHUNKED_BENCH=dir adds bench/image/inputs.py's files (p12.jpg ...).
 * Prints "FAIL <what>" per failure and the counts; exits non-zero on any failure. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/types.h>
#include <dirent.h>
#include "cdom_image.h"
#include "cdom_image_int.h"
#include "cdom_image_file.h"
#include "cdom_file.h"
#include "cdom_cb.h"
#include "host_alloc.h"

static int failures, checks;
static long fed_runs, glue_runs, splits, cancels, rooms;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; if (failures < 60) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } } while (0)

#define CHUNKED_SEED 20261120u
static uint64_t rng = CHUNKED_SEED;
static uint32_t rnd(void) {
  uint64_t z = (rng += 0x9e3779b97f4a7c15ull);
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull; z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
  return (uint32_t)((z ^ (z >> 31)) >> 32);
}
static int full;

static uint8_t *read_file(const char *path, size_t *n) {
  FILE *f = fopen(path, "rb");
  if (!f) { *n = 0; return 0; }
  fseek(f, 0, SEEK_END); long l = ftell(f); fseek(f, 0, SEEK_SET);
  uint8_t *b = malloc(l ? (size_t)l : 1);
  if (fread(b, 1, (size_t)l, f) != (size_t)l) { printf("FAIL short read %s\n", path); failures++; }
  fclose(f);
  *n = (size_t)l;
  return b;
}
static const char *sname(uint32_t s) {
  static const char *n[] = {"OK", "NOT_IMAGE", "TOO_BIG", "UNSUPPORTED", "CORRUPT", "NO_ROOM", "BAD_BOX", "UNREADABLE", "MORE"};
  return s < 9 ? n[s] : s == 99 ? "CANCELLED" : "?";
}

/* ---- a decode, whole or fed ---- */
typedef struct { uint32_t status, w, h, warnings, steps, puts; uint8_t *rgba; } result;
static void whole(const uint8_t *f, uint32_t size, uint32_t bw, uint32_t bh, result *r) {
  memset(r, 0, sizeof *r);
  r->status = cd_image_decode(f, size, bw, bh, &r->rgba, &r->w, &r->h, &r->warnings);
}
/* The file in pieces: the first piece `first` bytes (when not 0), then each the sizer's (0: as many as the room
 * takes; RANDOM: 1 to 70,000 from the seed), never more than the room. cancel: cd_image_end after that many puts. */
#define RANDOM 0xffffffffu
static void fed(const uint8_t *f, uint32_t size, uint32_t bw, uint32_t bh, uint32_t piece, uint32_t first, uint32_t cancel,
                result *r) {
  memset(r, 0, sizeof *r);
  cd_image_dec *d;
  uint32_t st = cd_image_begin(size, bw, bh, &d), at = 0;
  fed_runs++;
  if (st != CD_IMAGE_OK) { r->status = st; CHECK(!d, "begin answered %s and a decoder", sname(st)); return; }
  for (;;) {
    if (cancel && r->puts == cancel) { cd_image_end(d); r->status = 99; return; }
    uint32_t max;
    uint8_t *room = cd_image_room(d, &max);
    uint32_t n = 0;
    if (max > CD_IMAGE_PIECE || max > size - at) { CHECK(0, "the room is %u bytes, past a piece or the file's %u left", max, size - at); break; }
    if (max) {
      n = first && !at ? first : piece == RANDOM ? 1 + rnd() % 70000 : piece ? piece : max;
      if (n > max) n = max;
      memcpy(room, f + at, n);
      at += n;
    } else r->steps++;
    st = cd_image_put(d, n);
    r->puts++;
    if (st != CD_IMAGE_MORE) break;
    if (r->steps > 1000000) { CHECK(0, "a decode that makes no progress"); break; }
  }
  r->status = st;
  r->warnings = cd_image_dec_warnings(d);
  uint32_t max;
  cd_image_room(d, &max);
  CHECK(max == 0 && cd_image_put(d, 0) == st, "after the final status, the room is not 0 or a put answers another");
  if (st == CD_IMAGE_OK) cd_image_take(d, &r->rgba, &r->w, &r->h);
  cd_image_end(d);
}
static void same(const char *what, const result *a, const result *b, int warnings) {
  CHECK(a->status == b->status, "%s: status %s, whole %s", what, sname(b->status), sname(a->status));
  CHECK(a->w == b->w && a->h == b->h, "%s: %ux%u, whole %ux%u", what, b->w, b->h, a->w, a->h);
  if (a->rgba && b->rgba && a->w == b->w && a->h == b->h)
    CHECK(!memcmp(a->rgba, b->rgba, (size_t)a->w * a->h * 4), "%s: the pixels differ from the whole decode's", what);
  if (warnings) CHECK(a->warnings == b->warnings, "%s: %u warnings, whole %u", what, b->warnings, a->warnings);
}

/* ---- 1. byte-exact: every input at every piece size ---- */
static const uint32_t SIZES[] = {1, 2, 3, 7, 4093, 65536, 1u << 20, RANDOM};
static int is_scans(const char *name) { return strstr(name, "scans50") != 0; }
static void every_size(const char *name, const uint8_t *f, uint32_t n, uint32_t bw, uint32_t bh) {
  result a, b;
  size_t base = host_live;
  whole(f, n, bw, bh, &a);
  for (size_t i = 0; i < sizeof SIZES / sizeof *SIZES; i++) {
    uint32_t p = SIZES[i];
    if (!full && ((p <= 3 && n > 256u << 10) || (p == 7 && n > 2u << 20))) continue;
    char what[300];
    snprintf(what, sizeof what, "%s at %ux%u, pieces of %s%u", name, bw, bh, p == RANDOM ? "random size from seed " : "", p == RANDOM ? CHUNKED_SEED : p);
    fed(f, n, bw, bh, p, 0, 0, &b);
    /* libjpeg says a warning again when it reads a warning's marker again after a suspension (measured on scans500
     * and scans501 at pieces under 4 KiB): there the warnings are not compared */
    same(what, &a, &b, !(is_scans(name) && p < 4096));
    cd_free(b.rgba);
    CHECK(host_live == base + (a.rgba ? (size_t)a.w * a.h * 4 : 0), "%s: %zd bytes left allocated", what, (ssize_t)(host_live - base));
  }
  cd_free(a.rgba);
}
static void test_oracle_inputs(void) {
  FILE *m = fopen("cases.tsv", "r");
  if (!m) { printf("FAIL no cases.tsv\n"); failures++; return; }
  char line[4096];
  int k = 0;
  while (fgets(line, sizeof line, m)) {
    char *f[8]; int c = 0;
    for (char *t = strtok(line, "\t\n"); t && c < 8; t = strtok(0, "\t\n")) f[c++] = t;
    if (c < 8) continue;
    size_t n; uint8_t *in = read_file(f[1], &n);
    if (!in) { CHECK(0, "cannot open %s", f[1]); continue; }
    every_size(f[0], in, (uint32_t)n, (uint32_t)atoi(f[2]), (uint32_t)atoi(f[3]));
    free(in);
    k++;
  }
  fclose(m);
  printf("1. every oracle case (%d) at pieces of 1, 2, 3, 7, 4093, 65536, 1 MiB and random%s: as whole\n", k, full ? "" : " (1 to 7 on the smaller inputs)");
}
static void test_bench_inputs(const char *dir) {
  static const char *names[] = {"p12.jpg", "p12p.jpg", "p48.jpg", "p12.png", "p64.jpg"};
  int k = 0;
  for (size_t i = 0; i < sizeof names / sizeof *names; i++) {
    char path[1024]; snprintf(path, sizeof path, "%s/%s", dir, names[i]);
    size_t n; uint8_t *in = read_file(path, &n);
    if (!in) continue;
    every_size(names[i], in, (uint32_t)n, 900, 900);
    free(in);
    k++;
  }
  printf("1. the bench inputs (%d of %s) at every piece size: as whole\n", k, dir);
}

/* ---- 2 and 3. edge inputs (bench/chunked/edge.py), and a JPEG's markers split at every byte ---- */
static void test_edges(const char *dir) {
  DIR *dd = opendir(dir);
  if (!dd) { CHECK(0, "no edge inputs in %s (bench/chunked/edge.py)", dir); return; }
  struct dirent *e;
  int k = 0;
  while ((e = readdir(dd))) {
    const char *nm = e->d_name;
    size_t l = strlen(nm);
    if (l < 5 || (strcmp(nm + l - 4, ".png") && strcmp(nm + l - 4, ".jpg"))) continue;
    char path[1024]; snprintf(path, sizeof path, "%s/%s", dir, nm);
    size_t n; uint8_t *in = read_file(path, &n);
    int saved = full; full = 1;          /* every size, 1 byte too: every split point of every chunk and marker */
    every_size(nm, in, (uint32_t)n, 300, 300);
    full = saved;
    free(in);
    k++;
  }
  closedir(dd);
  CHECK(k >= 10, "only %d edge inputs", k);
  /* the walker's own mistakes would be the whole decode's too: these must decode to their plain twin's pixels */
  static const char *twins[][2] = {{"e-zero.png", "e-plain.png"}, {"e-idat3.png", "e-plain.png"}, {"e-text.png", "e-plain.png"},
                                   {"e-com.jpg", "e-plain.jpg"}, {"e-fill.jpg", "e-plain.jpg"}};
  for (size_t i = 0; i < sizeof twins / sizeof *twins; i++) {
    char pa[1024], pb[1024];
    snprintf(pa, sizeof pa, "%s/%s", dir, twins[i][0]); snprintf(pb, sizeof pb, "%s/%s", dir, twins[i][1]);
    size_t na, nb; uint8_t *a = read_file(pa, &na), *b = read_file(pb, &nb);
    result ra, rb;
    if (a && b) {
      whole(a, (uint32_t)na, 300, 300, &ra); whole(b, (uint32_t)nb, 300, 300, &rb);
      CHECK(ra.status == CD_IMAGE_OK && rb.status == CD_IMAGE_OK && ra.w == rb.w && ra.h == rb.h && !memcmp(ra.rgba, rb.rgba, (size_t)ra.w * ra.h * 4),
            "%s: differs from %s (%s %ux%u, %s %ux%u)", twins[i][0], twins[i][1], sname(ra.status), ra.w, ra.h, sname(rb.status), rb.w, rb.h);
      cd_free(ra.rgba); cd_free(rb.rgba);
    } else CHECK(0, "no %s or %s", pa, pb);
    free(a); free(b);
  }
  printf("2, 3. the edge inputs (%d: split chunks, markers, fill, COM, Exif behind APP2, restarts) at every piece size: as whole\n", k);
}
static void test_splits(void) {
  FILE *m = fopen("cases.tsv", "r");
  if (!m) return;
  char line[4096], last[1024] = "";
  int k = 0;
  while (fgets(line, sizeof line, m)) {
    char *f[8]; int c = 0;
    for (char *t = strtok(line, "\t\n"); t && c < 8; t = strtok(0, "\t\n")) f[c++] = t;
    if (c < 8 || !strstr(f[1], ".jpg") || !strcmp(f[1], last)) continue;
    snprintf(last, sizeof last, "%s", f[1]);
    size_t n; uint8_t *in = read_file(f[1], &n);
    if (!in) continue;
    uint32_t bw = (uint32_t)atoi(f[2]), bh = (uint32_t)atoi(f[3]);
    result a, b;
    size_t base = host_live;
    whole(in, (uint32_t)n, bw, bh, &a);
    uint32_t stride = n <= 65536 || (full && n <= (4u << 20)) ? 1 : full || n <= (1u << 20) ? 61 : 509;
    for (uint32_t i = 1; i <= 4096 && i < n; i += stride) {
      char what[300]; snprintf(what, sizeof what, "%s split at %u", f[0], i);
      fed(in, (uint32_t)n, bw, bh, 0, i, 0, &b);
      same(what, &a, &b, !is_scans(f[0]));
      cd_free(b.rgba);
      splits++;
    }
    cd_free(a.rgba);
    CHECK(host_live == base, "%s: splits left %zd bytes", f[0], (ssize_t)(host_live - base));
    free(in);
    k++;
  }
  fclose(m);
  printf("3. every oracle JPEG (%d) split once in its first 4 KiB, %ld splits%s: as whole\n", k, splits,
         full ? " (at every byte up to 4 MB files, every 61st past)" : " (at every byte up to 64 KB files, every 61st up to 1 MB, every 509th past)");
}

/* ---- the fake host for the glue: one picked file, timers, callback slots, an event loop ---- */
typedef struct { cd_cb_fn fn; void *ctx; uint16_t gen; uint8_t live; } slot;
static slot slots[4096];
uint32_t cd_cb_new(cd_cb_fn fn, void *ctx) {
  for (uint32_t i = 0; i < 4096; i++) if (!slots[i].live) {
    slots[i].fn = fn; slots[i].ctx = ctx; slots[i].live = 1; slots[i].gen++;
    return CD_TOKEN_CDOM | (uint32_t)(slots[i].gen & 0x7fff) << 16 | i;
  }
  abort();
}
static slot *named(uint32_t t) {
  uint32_t i = t & 0xffff;
  if (t < CD_TOKEN_CDOM || i >= 4096 || !slots[i].live || (slots[i].gen & 0x7fff) != ((t >> 16) & 0x7fff)) return 0;
  return &slots[i];
}
int cd_cb_take(uint32_t t, cd_cb_fn *fn, void **ctx) { slot *s = named(t); if (!s) return 0; *fn = s->fn; *ctx = s->ctx; s->live = 0; return 1; }
int cd_cb_get(uint32_t t, cd_cb_fn *fn, void **ctx) { slot *s = named(t); if (!s) return 0; *fn = s->fn; *ctx = s->ctx; return 1; }
void cd_cb_drop(uint32_t t) { slot *s = named(t); if (s) s->live = 0; }
static int slots_live(void) { int n = 0; for (int i = 0; i < 4096; i++) n += slots[i].live; return n; }
void *cd_alloc(uint32_t n) { void *p = cd_alloc_try(n); if (!p) abort(); return p; }

/* The event loop: timers and piece answers, in order. */
typedef struct { int kind; uint32_t token; uint32_t status, n; } event;
static event events[64];
static int ev_n;
static void push(int kind, uint32_t token, uint32_t status, uint32_t n) { if (ev_n == 64) abort(); events[ev_n++] = (event){kind, token, status, n}; }
uint32_t cd_timer_cb(uint32_t ms, cd_timer_fn fn, void *ctx) { (void)ms; uint32_t t = cd_cb_new((cd_cb_fn)fn, ctx); push(0, t, 0, 0); return t; }
void cd_timer_stop(uint32_t t) { cd_cb_drop(t); }

/* The picked file, and how it misbehaves: it ends at `ends` bytes (before its size), fails a read that reaches
 * `fails`, or answers 0 bytes at `zero`. */
static const uint8_t *file_p;
static uint32_t file_size, file_ends, file_fails, file_zero, file_open_status, file_pos, readers_open, zeros;
static uint8_t *pend_dst;
static uint32_t pend_token;
uint32_t cd_file_open(cd_id input, uint32_t index, uint32_t *reader, uint32_t *size) {
  (void)input; (void)index;
  if (file_open_status != CD_FILE_OK) { *reader = 0; *size = 0; return file_open_status; }
  file_pos = 0; readers_open++;
  *reader = 7; *size = file_size;
  return CD_FILE_OK;
}
void cd_file_next_cb(uint32_t reader, uint8_t *dst, uint32_t max, cd_file_piece_fn fn, void *ctx) {
  if (reader != 7 || pend_token) abort();         /* one next at a time */
  uint32_t t = cd_cb_new((cd_cb_fn)fn, ctx);
  uint32_t end = file_ends < file_size ? file_ends : file_size, n = end - file_pos < max ? end - file_pos : max;
  uint32_t st = CD_FILE_OK;
  if (file_fails && file_pos + n >= file_fails) { st = CD_FILE_UNREADABLE; n = 0; }
  else if (file_zero && file_pos >= file_zero) { n = 0; zeros++; }
  else if (file_zero && file_pos + n > file_zero) n = file_zero - file_pos;   /* the pieces before stop there */
  else if (!n && end < file_size) st = CD_FILE_UNREADABLE;   /* the file ended before its size: the shim's answer */
  pend_dst = dst; pend_token = t;
  push(1, t, st, n);
}
void cd_file_close(uint32_t reader) {
  if (reader != 7) abort();
  readers_open--;
  if (pend_token) { cd_cb_drop(pend_token); pend_token = 0; }
}
static void run_events(int most) {
  while (ev_n && most--) {
    event e = events[0];
    memmove(events, events + 1, sizeof *events * (size_t)--ev_n);
    cd_cb_fn fn; void *ctx;
    if (!cd_cb_take(e.token, &fn, &ctx)) continue;   /* closed, stopped */
    if (e.kind == 0) ((cd_timer_fn)fn)(ctx);
    else {
      pend_token = 0;
      if (e.status == CD_FILE_OK && e.n) { memcpy(pend_dst, file_p + file_pos, e.n); file_pos += e.n; }
      ((cd_file_piece_fn)fn)(ctx, e.status, e.n);
    }
  }
}

typedef struct { int calls; result r; } answer;
static void got(void *ctx, uint32_t status, uint8_t *rgba, uint32_t w, uint32_t h) {
  answer *a = ctx;
  a->calls++; a->r.status = status; a->r.rgba = rgba; a->r.w = w; a->r.h = h;
}
static void glue(const uint8_t *f, uint32_t size, uint32_t bw, uint32_t bh, answer *a, uint32_t *job) {
  memset(a, 0, sizeof *a);
  file_p = f; file_size = size;
  glue_runs++;
  cd_image_file_cb(1, 0, bw, bh, got, a, job);
  CHECK(a->calls == 0, "cd_image_file_cb answered inside the call");
}
static void glue_reset(void) { file_ends = 0xffffffffu; file_fails = 0; file_zero = 0; file_open_status = CD_FILE_OK; zeros = 0; }

/* ---- 4 and 5. a file shorter than claimed, a reader that answers 0, a read that fails ---- */
static void test_short_and_errors(const char *path) {
  size_t n; uint8_t *in = read_file(path, &n);
  if (!in) { CHECK(0, "no %s", path); return; }
  size_t base = host_live;
  answer a; uint32_t job;
  result w;
  whole(in, (uint32_t)n, 300, 300, &w);
  glue_reset();
  glue(in, (uint32_t)n, 300, 300, &a, &job);
  run_events(1 << 30);
  CHECK(a.calls == 1 && a.r.status == w.status && a.r.w == w.w && !memcmp(a.r.rgba, w.rgba, (size_t)w.w * w.h * 4), "%s: the glue answers as whole", path);
  cd_free(a.r.rgba); cd_free(w.rgba);
  uint32_t cuts[] = {(uint32_t)n / 16, (uint32_t)n / 2, (uint32_t)(n - n / 16)};
  for (int i = 0; i < 3; i++) {
    glue_reset(); file_ends = cuts[i];
    glue(in, (uint32_t)n, 300, 300, &a, &job);
    run_events(1 << 30);
    CHECK(a.calls == 1 && a.r.status == CD_IMAGE_UNREADABLE && !a.r.rgba, "%s ends at %u of its %zu bytes: %s, wanted UNREADABLE", path, cuts[i], n, sname(a.r.status));
    glue_reset(); file_fails = cuts[i];
    glue(in, (uint32_t)n, 300, 300, &a, &job);
    run_events(1 << 30);
    CHECK(a.calls == 1 && a.r.status == CD_IMAGE_UNREADABLE && !a.r.rgba, "%s fails a read at %u: %s, wanted UNREADABLE", path, cuts[i], sname(a.r.status));
    glue_reset(); file_zero = cuts[i];
    glue(in, (uint32_t)n, 300, 300, &a, &job);
    zeros = 0;
    run_events(1 << 30);
    CHECK(a.calls == 1 && a.r.status == CD_IMAGE_UNREADABLE, "%s: a reader that answers 0 bytes at %u: %s, wanted UNREADABLE", path, cuts[i], sname(a.r.status));
    CHECK(zeros == 1, "%s: the glue asked %u times after a 0-byte answer, wanted once (it must stop, not loop)", path, zeros);
    CHECK(host_live == base && !readers_open && !slots_live() && !ev_n, "%s, cut at %u: %zd bytes, %u readers, %d slots, %d events left", path, cuts[i],
          (ssize_t)(host_live - base), readers_open, slots_live(), ev_n);
  }
  /* nothing to read: NO_FILE, TOO_BIG and UNSUPPORTED from cd_file_open are UNREADABLE, from the event loop */
  uint32_t opens[] = {CD_FILE_NO_FILE, CD_FILE_TOO_BIG, CD_FILE_UNSUPPORTED};
  for (int i = 0; i < 3; i++) {
    glue_reset(); file_open_status = opens[i];
    glue(in, (uint32_t)n, 300, 300, &a, &job);
    run_events(1 << 30);
    CHECK(a.calls == 1 && a.r.status == CD_IMAGE_UNREADABLE, "cd_file_open answering %u: %s, wanted UNREADABLE", opens[i], sname(a.r.status));
  }
  glue_reset();
  glue(in, (uint32_t)n, 0, 300, &a, &job);
  run_events(1 << 30);
  CHECK(a.calls == 1 && a.r.status == CD_IMAGE_BAD_BOX && !readers_open, "a box of 0 through the glue: %s", sname(a.r.status));
  CHECK(host_live == base && !slots_live(), "the glue's refusals left %zd bytes, %d slots", (ssize_t)(host_live - base), slots_live());
  printf("4, 5. %s through the glue: ends early, fails a read, answers 0 (at 1/16, 1/2, 15/16): UNREADABLE, nothing left\n", path);
  free(in);
}

/* ---- 6. cancel at every put, by cd_image_end and by cd_image_file_stop ---- */
static void test_cancel(const char *path) {
  size_t n; uint8_t *in = read_file(path, &n);
  if (!in) { printf("     (no %s: cancel skipped)\n", path); return; }
  size_t base = host_live;
  result r, all;
  const uint32_t piece = n > (1u << 20) ? 65536 : 4093;   /* the big ones at 64 KiB, the small ones at 4093 */
  fed(in, (uint32_t)n, 900, 900, piece, 0, 0, &all);
  cd_free(all.rgba);
  for (uint32_t k = 1; k < all.puts; k++) {
    fed(in, (uint32_t)n, 900, 900, piece, 0, k, &r);
    CHECK(r.status == 99, "%s: cancel after %u puts", path, k);
    CHECK(host_live == base, "%s: cd_image_end after %u of %u puts left %zd bytes", path, k, all.puts, (ssize_t)(host_live - base));
    cancels++;
  }
  /* through the glue: cd_image_file_stop after k events, every k */
  answer a; uint32_t job;
  for (uint32_t k = 0; ; k++) {
    glue_reset();
    glue(in, (uint32_t)n, 900, 900, &a, &job);
    run_events((int)k);
    int done = a.calls;
    cd_image_file_stop(job);
    run_events(1 << 30);
    CHECK(a.calls == done, "%s: an answer after cd_image_file_stop (after %u events)", path, k);
    if (done) { cd_free(a.r.rgba); }
    CHECK(host_live == base && !readers_open && !slots_live() && !ev_n, "%s: cd_image_file_stop after %u events left %zd bytes, %u readers, %d slots, %d events",
          path, k, (ssize_t)(host_live - base), readers_open, slots_live(), ev_n);
    cancels++;
    if (done) break;
  }
  printf("6. %s: cancelled after every one of its %u puts (cd_image_end) and every event (cd_image_file_stop): nothing left\n", path, all.puts);
  free(in);
}

/* ---- 7. a file cut short whose size says so: the status whole answers ---- */
static void test_cut(const char *path) {
  size_t n; uint8_t *in = read_file(path, &n);
  if (!in) { CHECK(0, "no %s", path); return; }
  size_t base = host_live;
  int k = 0;
  for (uint32_t cut = (uint32_t)n - 16; cut <= n; cut++) {
    uint32_t c = k == 0 ? (uint32_t)n / 2 : cut;
    result a, b;
    whole(in, c, 300, 300, &a);
    for (int p = 0; p < 3; p++) {
      char what[300]; snprintf(what, sizeof what, "%s cut at %u", path, c);
      fed(in, c, 300, 300, p == 0 ? 7 : p == 1 ? 4093 : 0, 0, 0, &b);
      same(what, &a, &b, 1);
      cd_free(b.rgba);
    }
    cd_free(a.rgba);
    if (k++ == 0) cut--;
  }
  CHECK(host_live == base, "%s: cuts left %zd bytes", path, (ssize_t)(host_live - base));
  printf("7. %s cut at half and at each of its last 16 bytes, size saying so, pieces of 7, 4093, a room's: as whole\n", path);
  free(in);
}

/* ---- 8. no room at every allocation, through the decoder ---- */
static void test_no_room(const char *path, uint32_t piece) {
  size_t n; uint8_t *in = read_file(path, &n);
  if (!in) { CHECK(0, "no %s", path); return; }
  size_t base = host_live;
  result r;
  host_reset(SIZE_MAX);
  fed(in, (uint32_t)n, 300, 300, piece, 0, 0, &r);
  CHECK(r.status == CD_IMAGE_OK, "%s: no image with room", path);
  cd_free(r.rgba);
  size_t no_room = 0;
  for (size_t room = 0; room < (size_t)64 << 20; room = room ? room * 9 / 8 + 1 : 1) {
    host_reset(room);
    fed(in, (uint32_t)n, 300, 300, piece, 0, 0, &r);
    rooms++;
    if (r.status == CD_IMAGE_OK) { cd_free(r.rgba); CHECK(!host_full, "%s: OK although a block found no room", path); break; }
    CHECK(r.status == CD_IMAGE_NO_ROOM, "%s, %zu bytes of room: %s", path, room, sname(r.status));
    no_room += r.status == CD_IMAGE_NO_ROOM;
    CHECK(host_live == base, "%s, %zu bytes of room: %zd bytes left", path, room, (ssize_t)(host_live - base));
  }
  host_reset(SIZE_MAX);
  CHECK(no_room >= 10, "%s: only %zu rooms ended NO_ROOM", path, no_room);
  printf("8. %s in pieces of %u: a heap out of room at every step of its run: NO_ROOM (%zu rooms), nothing left\n", path, piece, no_room);
  free(in);
}

/* ---- 9. a progressive JPEG's output pass in steps ---- */
static void test_steps(const char *path, uint32_t box) {
  size_t n; uint8_t *in = read_file(path, &n);
  if (!in) { printf("     (no %s: steps skipped)\n", path); return; }
  result a, b;
  whole(in, (uint32_t)n, box, box, &a);
  fed(in, (uint32_t)n, box, box, 1u << 20, 0, 0, &b);
  same(path, &a, &b, 1);
  CHECK(b.steps >= 10, "%s: %u steps with no bytes wanted, wanted at least 10", path, b.steps);
  printf("9. %s: %u steps of its output pass with the room 0, the pixels as whole\n", path, b.steps);
  cd_free(a.rgba); cd_free(b.rgba);
  free(in);
}

/* CHUNKED_ONLY=a,b: only those groups (oracle bench edges splits short cancel cut noroom steps). */
static int want(const char *g) {
  const char *o = getenv("CHUNKED_ONLY");
  if (!o || !*o) return 1;
  size_t n = strlen(g);
  for (const char *p = o; (p = strstr(p, g)); p += n) if ((p == o || p[-1] == ',') && (p[n] == ',' || !p[n])) return 1;
  return 0;
}

int main(void) {
  setvbuf(stdout, NULL, _IONBF, 0);
  full = getenv("CHUNKED_FULL") && !strcmp(getenv("CHUNKED_FULL"), "1");
  const char *bench = getenv("CHUNKED_BENCH");
  const char *edge = getenv("CHUNKED_EDGE") ? getenv("CHUNKED_EDGE") : "edge";
  printf("chunked: CD_IMAGE_PIECE %u, seed %u%s\n", CD_IMAGE_PIECE, CHUNKED_SEED, full ? ", CHUNKED_FULL" : "");
  if (want("oracle")) test_oracle_inputs();
  if (bench && want("bench")) test_bench_inputs(bench);
  if (want("edges")) test_edges(edge);
  if (want("splits")) test_splits();
  if (want("short")) { test_short_and_errors("inputs/s420.jpg"); test_short_and_errors("inputs/rgb.png"); }
  char p[1024];
  const char *dir = bench ? bench : "inputs";
  if (want("cancel")) {   /* the design's three (the oracle's or the bench's big inputs), and three small ones, which are always there */
    static const char *big[] = {"p12.jpg", "p12p.jpg", "p12.png"}, *small[] = {"inputs/s420.jpg", "inputs/prog.jpg", "inputs/rgb.png"};
    for (int i = 0; i < 3; i++) { snprintf(p, sizeof p, "%s/%s", dir, big[i]); test_cancel(p); }
    for (int i = 0; i < 3; i++) test_cancel(small[i]);
  }
  if (want("cut")) { test_cut("inputs/s420.jpg"); test_cut("inputs/prog.jpg"); test_cut("inputs/rgb.png"); }
  if (want("noroom")) { test_no_room("inputs/s420.jpg", 4093); test_no_room("inputs/prog.jpg", 4093); test_no_room("inputs/rgba.png", 4093); }
  if (want("steps")) { snprintf(p, sizeof p, "%s/p12p.jpg", dir); test_steps(p, 900); test_steps("inputs/prog.jpg", 1203); }
  printf("%d checks, %d failures (%ld fed decodes, %ld through the glue, %ld splits, %ld cancels, %ld rooms)\n", checks, failures,
         fed_runs, glue_runs, splits, cancels, rooms);
  return failures != 0;
}
