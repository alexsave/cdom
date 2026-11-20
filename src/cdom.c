/* cdom core: allocator, runtime strings, DOM calls, node ids, regions, events, keyed diff. */
#define CD_NO_WRAPPERS /* this file defines the functions the wrappers in cdom.h call */
#include "cdom.h"
#include "cdom_cb.h"
#include <stddef.h>
#include <stdlib.h> /* malloc, free, realloc and calloc, which this file defines: the declarations apps see */

#define IMPORT(n) __attribute__((import_module("cdom"), import_name(n)))
#define EXPORT(n) __attribute__((export_name(n)))


/* ---- memory: power-of-two size classes for small blocks, 4 KiB grains for large ones, over memory.grow ----
 * These are malloc, realloc, free and calloc for an app that has none (weak: an app's own, all
 * four strong, replaces them), and cd_alloc goes through malloc either way, so cdom and the app
 * share one heap. free and realloc trap on a pointer this heap did not hand out: an app that
 * replaced malloc alone would otherwise feed its pointers to this free list.
 * Every block has an 8-byte header before the pointer handed out. A small block (n + 8 up to 64 KiB) is
 * one of the power-of-two classes 16 to 65,536 bytes, its header the class; a freed one goes on its
 * class's list and serves the next request of that class. A large block (above that) is n + 8 rounded up
 * to 4 KiB, its header BIG and its size, so it costs its size and at most 4 KiB more. A freed large block
 * goes back where it came from: at the top of the heap it lowers the top (and so does a freed large block
 * right under it), so the room serves any block, small or large; below the top it joins an address-ordered
 * list of free large runs, merged with a free neighbour on either side, from which a large request takes
 * the first run that fits, split. The memory itself never shrinks (wasm has no memory.shrink). */
extern unsigned char __heap_base;
static uintptr_t heap_lo, heap_ptr, heap_end;
static void *free_lists[32];
#define HEAP_SMALL_MAX 65536u /* the largest small block, header included */
#define HEAP_GRAIN 4096u      /* a large block's size is a multiple of this */
#define HEAP_BIG 0xffffffffu  /* a large block's header word 0: no class is this */
/* A large block at its start: in use, the header (tag, size); free, the same and the next free run. */
typedef struct heap_run { uint32_t tag, size; struct heap_run *next; } heap_run;
static heap_run *runs; /* the free large runs below the top, by address */

/* CDOM_HEAP_PAGES_MAX, when an app (or a test) defines it, bounds the memory the heap grows to, in
 * 64 KB pages; unset, the heap grows until memory.grow refuses, and the check is not compiled. */
static int heap_grow(uint32_t pages) {
#ifdef CDOM_HEAP_PAGES_MAX
  if ((uint32_t)__builtin_wasm_memory_size(0) + pages > (uint32_t)(CDOM_HEAP_PAGES_MAX)) return 0;
#endif
  /* memory.grow answers the old size in pages, or -1 when it refuses; the builtin's type is size_t, so the
   * -1 is read signed (unsigned, every answer is >= 0 and a refusal reads as room). */
  return (intptr_t)__builtin_wasm_memory_grow(0, pages) >= 0;
}
/* Room up to need at the top: the memory grown when need is past its end. 0: memory.grow refused. The
 * compare is inlined (the bump path's whole cost while the memory has room); the grow is one function, so
 * the module holds one copy of it, not one per caller. */
static __attribute__((noinline)) int heap_more(uintptr_t need) {
  uint32_t pages = (uint32_t)((need - heap_end + 65535) >> 16);
  if (!heap_grow(pages)) return 0;
  heap_end += (uintptr_t)pages << 16;
  return 1;
}
static inline __attribute__((always_inline)) int heap_room(uintptr_t need) {
  return need <= heap_end || heap_more(need);
}
static inline __attribute__((always_inline)) void heap_init(void) {
  if (!heap_ptr) {
    heap_lo = heap_ptr = ((uintptr_t)&__heap_base + 15) & ~(uintptr_t)15;
    heap_end = (uintptr_t)__builtin_wasm_memory_size(0) << 16;
  }
}
/* A large block of size bytes (a multiple of HEAP_GRAIN, header included) from the free runs: the first that
 * fits, the rest of it left a run of its own. 0: none fits. */
static __attribute__((noinline)) void *heap_run_take(uint32_t size) {
  for (heap_run **at = &runs; *at; at = &(*at)->next) {
    heap_run *r = *at;
    if (r->size < size) continue;
    if (r->size == size) *at = r->next;
    else {
      heap_run *rest = (heap_run *)((uintptr_t)r + size);
      rest->tag = HEAP_BIG;
      rest->size = r->size - size;
      rest->next = r->next;
      *at = rest;
      r->size = size;
    }
    return (uint32_t *)r + 2;
  }
  return 0;
}
/* may_fail 0 (malloc and everything on it): no room traps, as it always has, since in wasm a
 * pointer 0 is ordinary memory and code that does not check would write over it. may_fail 1
 * (cd_alloc_try alone): no room answers 0. A small block (n + 8 up to HEAP_SMALL_MAX) comes from its class's
 * list, a large one (n under 2^31) from the free runs; else either is bumped off the top, its header the class
 * (HEAP_BIG for a large block) and its size. */
static inline __attribute__((always_inline)) void *heap_alloc_or(uint32_t n, int may_fail) {
  uint32_t c, size;
  void *p;
  if (n > HEAP_SMALL_MAX - 8) {
    c = HEAP_BIG;
    size = (n + 8 + HEAP_GRAIN - 1) & ~(HEAP_GRAIN - 1);
    p = heap_run_take(size);
  } else {
    c = 4;
    while ((1u << c) < n + 8) c++;
    size = 1u << c;
    p = free_lists[c];
    if (p) free_lists[c] = *(void **)p;
  }
  if (p) return p;
  heap_init();
  uintptr_t need = heap_ptr + size;
  if (!heap_room(need)) {
    if (may_fail) return 0;
    __builtin_trap(); /* out of memory */
  }
  uint32_t *h = (uint32_t *)heap_ptr;
  heap_ptr = need;
  h[0] = c;
  h[1] = size; /* read for a large block alone */
  return h + 2;
}
/* The two bodies of heap_alloc_or, each one function: realloc and cd_realloc_try call these rather than
 * inline a third and fourth copy of it (each copy was about 200 bytes of -Oz code). */
static void *heap_alloc(uint32_t n) { return heap_alloc_or(n, 0); }
static void *heap_alloc_try(uint32_t n) { return heap_alloc_or(n, 1); }

static uint32_t heap_class(void *p) {
  if ((uintptr_t)p < heap_lo + 8 || (uintptr_t)p >= heap_ptr) __builtin_trap(); /* not from this heap */
  return ((uint32_t *)p)[-2];
}
/* The bytes a block holds, past its header. */
static uint32_t heap_cap(void *p) {
  uint32_t c = heap_class(p);
  return (c == HEAP_BIG ? ((uint32_t *)p)[-1] : 1u << c) - 8;
}
/* A freed large block: the top lowered when it is the top (and again when a free run then ends there),
 * else into the runs by address, merged with the run that ends where it starts and the one that starts
 * where it ends. */
static __attribute__((noinline)) void heap_big_free(void *p) {
  /* at: the link b goes in (runs, or the next of prev, the last run below b); pat: the link prev is in. */
  heap_run *b = (heap_run *)((uint32_t *)p - 2), *prev = 0, **pat = 0, **at = &runs;
  while (*at && *at < b) pat = at, prev = *at, at = &prev->next;
  heap_run *next = *at;
  if ((uintptr_t)b + b->size == heap_ptr) { /* the top: then next is 0, and prev the only run that may end at b */
    if (prev && (uintptr_t)prev + prev->size == (uintptr_t)b) b = prev, at = pat;
    heap_ptr = (uintptr_t)b;
    *at = 0;
    return;
  }
  if (next && (uintptr_t)b + b->size == (uintptr_t)next) b->size += next->size, next = next->next;
  if (prev && (uintptr_t)prev + prev->size == (uintptr_t)b) prev->size += b->size, prev->next = next;
  else b->next = next, *at = b;
}
/* A block at the top grown where it is to a large one of n bytes: p, its header now BIG and the new
 * size; 0 when it is not at the top or the memory cannot grow (then nothing changed). */
static void *heap_grow_top(void *p, uint32_t n) {
  uint32_t *h = (uint32_t *)p - 2;
  uint32_t size = (n + 8 + HEAP_GRAIN - 1) & ~(HEAP_GRAIN - 1);
  if ((uintptr_t)h + heap_cap(p) + 8 != heap_ptr || !heap_room((uintptr_t)h + size)) return 0;
  heap_ptr = (uintptr_t)h + size;
  h[0] = HEAP_BIG;
  h[1] = size;
  return p;
}
/* realloc and cd_realloc_try: p kept while it holds n; a block at the top grown in place to a large size;
 * else a new block, the bytes copied and p freed. 0 (p kept) when there is no room and may_fail. */
static inline __attribute__((always_inline)) void *heap_realloc_or(void *p, size_t n, int may_fail) {
  uint32_t cap = p ? heap_cap(p) : 0;
  if (p && cap >= n) return p;
  if (n >> 31) return 0;
  if (p && n > HEAP_SMALL_MAX - 8 && heap_grow_top(p, (uint32_t)n)) return p;
  void *q = may_fail ? heap_alloc_try((uint32_t)n) : heap_alloc((uint32_t)n);
  if (p && q) {
    __builtin_memcpy(q, p, cap);
    free(p);
  }
  return q;
}

/* malloc is cdom_heap_malloc, weakly: cd_alloc_try tells cdom's heap from an app's own by comparing them. */
void *cdom_heap_malloc(size_t n) { return n >> 31 ? 0 : heap_alloc((uint32_t)n); }
void *malloc(size_t n) __attribute__((weak, alias("cdom_heap_malloc")));

__attribute__((weak)) void free(void *p) {
  if (!p) return;
  uint32_t c = heap_class(p);
  if (c == HEAP_BIG) {
    heap_big_free(p);
    return;
  }
  *(void **)p = free_lists[c];
  free_lists[c] = p;
}

__attribute__((weak)) void *realloc(void *p, size_t n) { return heap_realloc_or(p, n, 0); }

__attribute__((weak)) void *calloc(size_t n, size_t sz) {
  size_t t = n * sz;
  if (sz && t / sz != n) return 0;
  void *p = malloc(t);
  if (p) __builtin_memset(p, 0, t);
  return p;
}

void *cd_alloc(uint32_t n) {
  void *p = malloc(n);
  if (!p) __builtin_trap();
  return p;
}

void cd_free(void *p) { free(p); }

void *cd_realloc(void *p, uint32_t n) {
  void *q = realloc(p, n);
  if (!q) __builtin_trap();
  return q;
}

/* On cdom's own heap, the non-trapping path; on an app's own allocator, whatever its malloc and
 * realloc answer (an allocator of the app's own says itself what no room is). */
static int heap_is_cdoms(void) { return malloc == cdom_heap_malloc; }
void *cd_alloc_try(uint32_t n) {
  if (!heap_is_cdoms()) return malloc(n);
  return n >> 31 ? 0 : heap_alloc_try(n);
}
void *cd_realloc_try(void *p, uint32_t n) {
  if (!heap_is_cdoms()) return realloc(p, n);
  return heap_realloc_or(p, n, 1);
}

/* A job module (src/cdom_job_mod.c) links only the heap above: -DCDOM_HEAP_ONLY ends this file here, so
 * no DOM import or export comes with it. An app's build never sets it, and its bytes do not change. */
#ifndef CDOM_HEAP_ONLY

/* ---- calls ----
 * Every DOM operation is its own import, called the moment C emits it. A string is a cd_str: a literal's
 * slot (the shim finds its text in the page's table) or a runtime string's serial (the shim reads its
 * bytes from C memory in the call that uses it). Paired harness runs measured the calls against a
 * batched op list that the shim interpreted: the calls won on creates and clears (PERF_LOG.md, the
 * direct-call experiment), except for walks, which travel as one call. */
#define IMPORTS(n) __attribute__((import_module("cdom"), import_name(n)))
IMPORTS("at") void j_at(cd_id parent, cd_id before);
IMPORTS("open") void j_open(uint32_t tag);
IMPORTS("close") void j_close(void);
IMPORTS("text") void j_text(uint32_t s);
IMPORTS("text_num") void j_text_num(uint32_t n);
IMPORTS("attr") void j_attr(uint32_t name, uint32_t v);
IMPORTS("class") void j_class(uint32_t v);
IMPORTS("name_el") void j_name_el(cd_id id);
IMPORTS("name_text") void j_name_text(cd_id id);
IMPORTS("name_open") void j_name_open(cd_id id);
IMPORTS("listen") void j_listen(cd_id id, uint32_t ev);
IMPORTS("value") void j_value(uint32_t s);
IMPORTS("value_num") void j_value_num(uint32_t n);
IMPORTS("tpl_save") void j_tpl_save(uint32_t t);
IMPORTS("clone") void j_clone(uint32_t t);
IMPORTS("slot") void j_slot(uint32_t k);
IMPORTS("ahead") void j_ahead(uint32_t p, uint32_t len);
IMPORTS("tpl_text") void j_tpl_text(uint32_t t, uint32_t k, uint32_t s);
IMPORTS("tpl_text_num") void j_tpl_text_num(uint32_t t, uint32_t k, uint32_t n);
IMPORTS("walk") void j_walk(cd_id from, uint32_t steps, uint32_t n);
IMPORTS("walk_value") void j_walk_value(cd_id from, uint32_t steps, uint32_t n, uint32_t s);
IMPORTS("walk_value_num") void j_walk_value_num(cd_id from, uint32_t steps, uint32_t n, uint32_t v);
IMPORTS("set_text") void j_set_text(cd_id id, uint32_t s);
IMPORTS("set_text_num") void j_set_text_num(cd_id id, uint32_t n);
IMPORTS("set_attr") void j_set_attr(cd_id id, uint32_t name, uint32_t v);
IMPORTS("set_class") void j_set_class(cd_id id, uint32_t v);
IMPORTS("style") void j_style(cd_id id, uint32_t prop, uint32_t v);
IMPORTS("style_num") void j_style_num(cd_id id, uint32_t prop, double v, uint32_t unit);
IMPORTS("encode") void j_encode(const uint8_t *px, uint32_t w, uint32_t h, uint32_t n, uint32_t W, uint32_t H, uint32_t x, uint32_t y,
                             uint32_t mime, uint32_t quality, uint32_t token);
IMPORTS("image_bytes") void j_image_bytes(cd_id id, uint32_t prop, const uint8_t *p, uint32_t len, uint32_t mime, uint32_t token);
IMPORTS("remove") void j_remove(cd_id id);
IMPORTS("free") void j_free(cd_id id);
IMPORTS("move") void j_move(cd_id id, cd_id before);
IMPORTS("clear") void j_clear(cd_id id);
IMPORTS("detach") void j_detach(cd_id id);
IMPORTS("attach") void j_attach(cd_id id);
IMPORTS("unlisten") void j_unlisten(cd_id id, uint32_t ev);
IMPORTS("hit") cd_id j_hit(int32_t x, int32_t y);
IMPORTS("viewport") void j_viewport(cd_viewport_rec *v);
IMPORTS("rect") uint32_t j_rect(cd_id id, double *out);
IMPORTS("blit") void j_blit(cd_id id, const void *rgba, uint32_t w, uint32_t h);
IMPORTS("set") void j_set(cd_id id, uint32_t path, const cd_val *v);
IMPORTS("call") void j_call(cd_id id, uint32_t method, uint32_t n, const cd_val *a);
IMPORTS("get") double j_get(cd_id id, uint32_t path, uint32_t kind, void *dst, uint32_t cap);
IMPORTS("timer") uint32_t j_timer(uint32_t ms, uint32_t token);
IMPORTS("timer_stop") void j_timer_stop(uint32_t handle);
IMPORTS("raf") void j_raf(uint32_t token);
IMPORTS("now") double j_now(void);

/* The runtime strings of the call being made: cd_tmp writes a record (p, len) here and copies nothing; the
 * shim reads the record and then the bytes in the import that takes the string. serial0 is the serial of
 * r[0], so a cd_str whose serial is not in [serial0, serial0 + n) is dead and the shim throws on it. */
typedef struct { uint32_t p, len; } tmp_rec;
static struct tmp_block { uint32_t serial0, n; tmp_rec r[CD_TMP_CAP]; } tmp;
static cd_event_rec inbox;

/* The shim's map of C memory, which cd_layout hands it once at start: where the runtime-string records and the
 * event record live, and the offset of every field and the size of every struct the shim reads or
 * writes, from offsetof and sizeof, so the shim hardcodes no offset (tools/offsets.mjs checks it does
 * not). The block is its entry count, then the entries in this order, which shim/cdom.js (start) reads
 * in the same order; an entry is only ever appended. rect's out-parameter is a double[4] in the order
 * cdom.h documents, and the byte buffers are (pointer, length): neither has fields to map. */
#define CD_LAYOUT(X) \
  X((uintptr_t)&tmp) X(offsetof(struct tmp_block, serial0)) X(offsetof(struct tmp_block, n)) \
  X(offsetof(struct tmp_block, r)) X(sizeof(tmp_rec)) X(offsetof(tmp_rec, p)) X(offsetof(tmp_rec, len)) \
  X((uintptr_t)&inbox) \
  X(offsetof(cd_event_rec, type)) X(offsetof(cd_event_rec, id)) X(offsetof(cd_event_rec, x)) \
  X(offsetof(cd_event_rec, y)) X(offsetof(cd_event_rec, button)) X(offsetof(cd_event_rec, key)) \
  X(offsetof(cd_event_rec, mods)) X(offsetof(cd_event_rec, path)) X(offsetof(cd_event_rec, depth)) \
  X(offsetof(cd_event_rec, ch)) X(offsetof(cd_event_rec, ms)) X(offsetof(cd_event_rec, pointer)) \
  X(offsetof(cd_event_rec, buttons)) X(offsetof(cd_event_rec, dx)) X(offsetof(cd_event_rec, dy)) \
  X(offsetof(cd_viewport_rec, w)) X(offsetof(cd_viewport_rec, h)) X(offsetof(cd_viewport_rec, hidden)) \
  X(offsetof(cd_viewport_rec, dpr100)) \
  X(sizeof(cd_val)) X(offsetof(cd_val, tag)) X(offsetof(cd_val, num)) X(offsetof(cd_val, u))
#define CD_LAYOUT_ONE(v) +1
#define CD_LAYOUT_ENTRY(v) (uint32_t)(v),
/* Data, not code: the linker writes the addresses in, so cd_layout only hands the block over. */
static const uint32_t layout[] = {0 CD_LAYOUT(CD_LAYOUT_ONE), CD_LAYOUT(CD_LAYOUT_ENTRY)};
/* The shim indexes 32-bit fields as words and cd_val's number as a double. */
_Static_assert(sizeof(tmp_rec) % 4 == 0 && sizeof(cd_event_rec) % 4 == 0, "word-sized records");
_Static_assert(offsetof(cd_event_rec, dx) % 4 == 0 && offsetof(cd_event_rec, dy) % 4 == 0, "word-aligned floats");
_Static_assert(sizeof(cd_val) % 8 == 0 && offsetof(cd_val, num) % 8 == 0 && offsetof(cd_val, u) % 4 == 0, "aligned cd_val");

/* ---- strings ----
 * No byte is copied: the shim reads them in place. A cd_tmp is good until the next call that takes a
 * string returns (tmp_done), and at most CD_TMP_CAP are alive at once. */
cd_str cd_tmp(const char *s, uint32_t len) {
  if (tmp.n == CD_TMP_CAP) __builtin_trap();
  tmp.r[tmp.n] = (tmp_rec){(uint32_t)(uintptr_t)s, len};
  return (cd_str){0x80000000u | ((tmp.serial0 + tmp.n++) & 0x7fffffffu)};
}
/* A pointer into read-only data is its own id: the shim keeps its string by address.
 * C never writes const data, so one address names one string for the life of the page.
 * Mutable statics, the stack and the heap lie outside the range and are read on every use.
 * wasm-ld defines the two symbols from LLVM 23 on; an older one leaves them undefined and the link fails,
 * which cdom.mk's toolchain check turns into the version it found and the fix. There is no fallback: a
 * cd_bound_set value and the shim's string cache need a literal to be its own id. */
extern const char __rodata_start[], __rodata_end[];
cd_str cd_tmpz(const char *s) {
  if ((uintptr_t)s - (uintptr_t)__rodata_start < (uintptr_t)__rodata_end - (uintptr_t)__rodata_start)
    return (cd_str){(uint32_t)(uintptr_t)s};
  if (!s) __builtin_trap();
  uint32_t n = 0;
  while (s[n]) n++;
  return cd_tmp(s, n);
}
/* The shim keeps the range; C holds no state for it. */
void cd_tmp_ahead(const char *p, uint32_t len) { j_ahead((uint32_t)(uintptr_t)p, len); }
/* Every function that takes a cd_str ends with this: its runtime strings are dead now. */
static void tmp_done(void) {
  tmp.serial0 += tmp.n;
  tmp.n = 0;
}

/* ---- numbers ---- */
static const char UNIT_STR[CD_UNITS][4] = {"", "px", "%", "deg", "em", "rem", "vw", "vh", "ms", "s", "rad"};

uint32_t cd_fmt(char *d, int32_t v, uint32_t dec, uint32_t unit) {
  char t[12]; /* digits, least significant first */
  uint32_t k = 0, n = 0, u = v < 0 ? 0u - (uint32_t)v : (uint32_t)v;
  if (dec > 9 || unit >= CD_UNITS) __builtin_trap();
  do t[k++] = (char)('0' + u % 10); while ((u /= 10));
  while (k <= dec) t[k++] = '0';
  uint32_t lo = 0; /* fraction digits below lo are trailing zeros */
  while (lo < dec && t[lo] == '0') lo++;
  if (v < 0) d[n++] = '-';
  for (uint32_t i = k; i-- > dec;) d[n++] = t[i];
  if (lo < dec) {
    d[n++] = '.';
    for (uint32_t i = dec; i-- > lo;) d[n++] = t[i];
  }
  for (const char *c = UNIT_STR[unit]; *c; c++) d[n++] = *c;
  return n;
}

uint32_t cd_fmt_len(char *d, double v, uint32_t unit) {
  return cd_fmt(d, (int32_t)(v * 10000.0 + (v < 0 ? -0.5 : 0.5)), 4, unit);
}

/* ---- emitters ---- */
/* A walk (cd_sel, cd_down, cd_next, cd_up) is held here and crosses as one call: with the value
 * it leads to, or before the next op that reads the node it reached. Steps are 2 bits each:
 * 1 down, 2 next, 3 up. */
#define WALK_LAST 0xffffffffu /* walk from the last node rather than a named one */
static cd_id walk_from;
static uint32_t walk_steps, walk_n, walking;
static void walk_done(void) {
  if (walking) { walking = 0; j_walk(walk_from, walk_steps, walk_n); }
}
static void walk_step(uint32_t s) {
  if (walking && walk_n == 16) walk_done();
  if (!walking) { walking = 1; walk_from = WALK_LAST; walk_steps = walk_n = 0; }
  walk_steps |= s << (2 * walk_n++);
}
/* While a region's cleanups run (cd_on_free) the DOM must not change: they release C state only, and
 * the region's nodes are about to go. Every operation that changes the DOM or calls host code passes
 * here; reads (get) and frees only end the walk, so a cleanup may read a field or forget a list. */
static uint32_t releasing;
static inline void ready(void) {
  if (releasing) __builtin_trap(); /* a cleanup changed the DOM */
  walk_done();
}

/* ---- renders: cd_dirty ----
 * renders holds the renders asked for, in the order asked. A pass runs them from the front:
 * renders[0, render_at) have run, and the rest wait, each (fn, ctx) once; a render that asks for
 * more appends them, and the entries past where the round started form the next round. They run in
 * cd_flush, which ends every entry from JS, so one pass per event, timer, frame or answer.
 * asked (with the pairs, below) finds a pending (fn, ctx) in a probe or a few, so ten thousand distinct
 * asks are ten thousand lookups, not fifty million compares; the pass's end empties it. */
#define CD_RENDER_ROUNDS 16
typedef struct { cd_render_fn fn; void *ctx; } render_ent;
static render_ent *renders;
static uint32_t nrenders, renders_cap, render_at, rendering;
/* The pass, reached through a pointer cd_dirty sets: an app that never asks for a render does not
 * carry it, and its flush pays one load. volatile, or clang sees the one value the pointer can take,
 * calls run_renders directly and inlines it into every export (236 bytes in the keyed bench app). */
static void run_renders(void);
static void (*volatile render_pass)(void);
/* The views of shared state (cd_state, below), reached the same way: cd_changed and cd_watch set it, so
 * an app that renders but never uses a state does not carry them. views_pass(0) runs one round of views, and
 * views_pass(1) ends the pass. changed is the list of changed states; CHANGED_END ends it, so a state
 * is on it exactly when its changed is not null. */
static void (*volatile views_pass)(int end);
static cd_state *changed, *changed_last;
#define CHANGED_END ((cd_state *)&changed)
static void asked_reset(void); /* cd_dirty, cd_undirty and their table are with the pairs, below */
/* One round of renders: those asked before it began. */
static void renders_round(void) {
  for (uint32_t end = nrenders; render_at < end;) {
    render_ent e = renders[render_at++];
    if (e.fn) e.fn(e.ctx);
  }
}
/* Runs what is waiting, views and renders, in rounds; nothing while a pass runs (the pass takes it) or
 * while a region's cleanups run (they may not change the DOM: the flush after them does it). */
static void run_renders(void) {
  if ((render_at == nrenders && !changed) || rendering || releasing) return;
  rendering = 1;
  for (uint32_t round = 1;; round++) {
    if (round > CD_RENDER_ROUNDS) __builtin_trap(); /* renders or views that keep asking for each other */
    if (changed) views_pass(0);
    renders_round();
    if (render_at == nrenders && !changed) break;
  }
  if (views_pass) views_pass(1);
  render_at = nrenders = rendering = 0;
  asked_reset();
}
static inline void renders_now(void) { if (render_pass) render_pass(); }
void cd_render_now(void) { renders_now(); }

void cd_flush(void) { renders_now(); walk_done(); }
uint32_t cd_pending(void) { return walking | tmp.n; }

static cd_id at_parent = CD_DETACHED - 1, at_before = CD_DETACHED - 1;

void cd_at(cd_id parent, cd_id before) {
  if (parent == at_parent && before == at_before) return;
  ready(); j_at(parent, before);
  at_parent = parent;
  at_before = before;
}
/* The shim says which elements are SVG (it counts the open ones), so C does not look at the tag. */
void cd_open(cd_str tag) { ready(); j_open(tag.v); tmp_done(); }
void cd_close(void) { ready(); j_close(); }
void cd_text(cd_str s) { ready(); j_text(s.v); tmp_done(); }
void cd_text_num(uint32_t n) { ready(); j_text_num(n); }
void cd_attr(cd_str name, cd_str v) { ready(); j_attr(name.v, v.v); tmp_done(); }
void cd_class(cd_str v) { ready(); j_class(v.v); tmp_done(); }
void cd_name_el(cd_id id) { ready(); j_name_el(id); }
void cd_name_text(cd_id id) { ready(); j_name_text(id); }
void cd_name_open(cd_id id) { ready(); j_name_open(id); }
/* One import for both: the shim tells a host object (it listens on it) from a node (it names the last
 * node and marks it in the mount's delegation). */
void cd_listen(cd_id id, uint32_t ev) {
  if (ev >= CD_EV_TYPES) __builtin_trap();
  ready(); j_listen(id, ev);
}
void cd_unlisten(cd_id id, uint32_t ev) {
  if (ev >= CD_EV_TYPES) __builtin_trap();
  ready(); j_unlisten(id, ev);
}
void cd_value(cd_str s) {
  if (walking) { walking = 0; j_walk_value(walk_from, walk_steps, walk_n, s.v); } else j_value(s.v);
  tmp_done();
}
void cd_value_num(uint32_t n) {
  if (walking) { walking = 0; j_walk_value_num(walk_from, walk_steps, walk_n, n); } else j_value_num(n);
}
/* What each template slot holds now (kind 0 unknown, 1 string id, 2 number), so an unchanged
 * value is not written again. */
static uint32_t slot_val[CD_TPLS][CD_SLOTS];
static uint8_t slot_kind[CD_TPLS][CD_SLOTS];
void cd_tpl_save(uint32_t t) {
  if (t < CD_TPLS) __builtin_memset(slot_kind[t], 0, CD_SLOTS);
  ready(); j_tpl_save(t);
}
void cd_clone(uint32_t t) { ready(); j_clone(t); }
void cd_slot(uint32_t k) {
  if (k >= CD_SLOTS) __builtin_trap();
  ready(); j_slot(k);
}
static int slot_same(uint32_t t, uint32_t k, uint8_t kind, uint32_t v) {
  if (t >= CD_TPLS || k >= CD_SLOTS) __builtin_trap();
  if (slot_kind[t][k] == kind && slot_val[t][k] == v) return 1;
  slot_kind[t][k] = kind;
  slot_val[t][k] = v;
  return 0;
}
void cd_tpl_text(uint32_t t, uint32_t k, cd_str s) {
  if (s.v >> 31) { /* a runtime string: the same text can be new bytes, so it is never skipped as unchanged */
    if (t >= CD_TPLS || k >= CD_SLOTS) __builtin_trap();
    slot_kind[t][k] = 0;
  } else if (slot_same(t, k, 1, s.v)) { /* only a literal may skip: its slot always means the same text */
    tmp_done();
    return;
  }
  ready(); j_tpl_text(t, k, s.v); tmp_done();
}
void cd_tpl_text_num(uint32_t t, uint32_t k, uint32_t n) {
  if (slot_same(t, k, 2, n)) return;
  ready(); j_tpl_text_num(t, k, n);
}
void cd_down(void) { walk_step(1); }
void cd_next(void) { walk_step(2); }
void cd_up(void) { walk_step(3); }
void cd_sel(cd_id id) { walk_done(); walking = 1; walk_from = id; walk_steps = walk_n = 0; }
void cd_set_text_node(cd_id id, cd_str s) { ready(); j_set_text(id, s.v); tmp_done(); }
void cd_set_text_node_num(cd_id id, uint32_t n) { ready(); j_set_text_num(id, n); }
void cd_set_attr(cd_id id, cd_str name, cd_str v) { ready(); j_set_attr(id, name.v, v.v); tmp_done(); }
void cd_set_class(cd_id id, cd_str v) { ready(); j_set_class(id, v.v); tmp_done(); }
void cd_set_style(cd_id id, cd_str prop, cd_str v) { ready(); j_style(id, prop.v, v.v); tmp_done(); }
/* The unit crosses as its index; the shim's UNITS list has the same order as CD_UNIT_* and UNIT_STR. */
void cd_set_style_num(cd_id id, cd_str prop, double v, uint32_t unit) {
  if (unit >= CD_UNITS) __builtin_trap();
  ready(); j_style_num(id, prop.v, v, unit); tmp_done();
}
void cd_encode_image_in(const uint8_t *px, uint32_t w, uint32_t h, uint32_t n, uint32_t W, uint32_t H, uint32_t x,
                        uint32_t y, cd_str mime, uint32_t quality, uint32_t token) {
  if ((n != 3 && n != 4) || x + w > W || y + h > H || quality > 100) __builtin_trap();
  ready(); j_encode(px, w, h, n, W, H, x, y, mime.v, quality, token); tmp_done();
}
void cd_encode_image(const uint8_t *px, uint32_t w, uint32_t h, uint32_t n, cd_str mime, uint32_t quality,
                     uint32_t token) {
  cd_encode_image_in(px, w, h, n, w, h, 0, 0, mime, quality, token);
}
void cd_set_image_bytes(cd_id id, cd_str prop, const uint8_t *p, uint32_t len, cd_str mime, uint32_t token) {
  ready(); j_image_bytes(id, prop.v, p, len, mime.v, token); tmp_done();
}
void cd_move(cd_id id, cd_id before) { ready(); j_move(id, before); }
void cd_clear(cd_id id) { ready(); j_clear(id); }
void cd_detach(cd_id id) { ready(); j_detach(id); }
void cd_attach(cd_id id) { ready(); j_attach(id); }
void cd_blit(cd_id canvas, const void *rgba, uint32_t w, uint32_t h) { ready(); j_blit(canvas, rgba, w, h); }

/* ---- host access ----
 * The values stay in C memory: the shim reads the cd_val array during the call (tag at +0, payload at +8),
 * and a string value is a cd_str, read there like any other. Host code sees the DOM, so the renders
 * asked for so far run first: the reads, cd_set and the calls (focus, scrollIntoView, a measure through
 * call) see what the state says, not what it said. */
void cd_set(cd_id id, cd_str path, cd_val v) {
  if (v.tag == CD_V_OBJ) __builtin_trap(); /* its pairs cannot follow a value passed alone */
  renders_now();
  ready(); j_set(id, path.v, &v); tmp_done();
}
void cd_call_args(cd_id id, cd_str method, uint32_t n, const cd_val *a) {
  renders_now();
  ready(); j_call(id, method.v, n, a); tmp_done();
}
void cd_call(cd_id id, cd_str method) { cd_call_args(id, method, 0, 0); }

enum { GET_NUM, GET_STR, GET_ID, GET_STR_LEN };
static double get(cd_id id, cd_str path, uint32_t kind, void *dst, uint32_t cap) {
  renders_now();
  walk_done(); /* a read: a cleanup may read */
  double r = j_get(id, path.v, kind, dst, cap);
  tmp_done();
  return r;
}
double cd_get_num(cd_id id, cd_str path) { return get(id, path, GET_NUM, 0, 0); }
uint32_t cd_get_str(cd_id id, cd_str path, char *dst, uint32_t cap) { return (uint32_t)get(id, path, GET_STR, dst, cap); }
cd_id cd_get_id(cd_id id, cd_str path) {
  double r = get(id, path, GET_ID, 0, 0);
  return r < 0 ? CD_DETACHED : (cd_id)r;
}

/* ---- node ids ---- */
static cd_id next_id = CD_FIRST_ID;
static cd_id *free_ids;
static uint32_t nfree;
static uint32_t *tags, tags_cap;
static uint8_t *alive; /* a freed id freed again would be handed out twice */
static cd_region *recording;

static void record(cd_region *r, cd_id id) {
  if (r->n == r->cap) {
    r->cap = r->cap ? r->cap * 2 : 8;
    r->ids = cd_realloc(r->ids, r->cap * 4);
  }
  r->ids[r->n++] = id;
}

cd_id cd_id_new(void) {
  cd_id id;
  if (nfree) id = free_ids[--nfree];
  else {
    id = next_id++;
    if (id >= tags_cap) {
      uint32_t old = tags_cap;
      tags_cap = tags_cap ? tags_cap * 2 : 4096;
      tags = cd_realloc(tags, tags_cap * 4);
      __builtin_memset(tags + old, 0, (tags_cap - old) * 4); /* cd_tag of a fresh id reads 0 */
      free_ids = cd_realloc(free_ids, tags_cap * 4);
      alive = cd_realloc(alive, tags_cap);
    }
  }
  alive[id] = 1;
  if (recording) record(recording, id);
  return id;
}

static void on_drop(cd_id id);
static uint8_t *on_ids; /* by id: 1 while the id has a cd_on handler, so freeing the others costs one load */
static uint32_t on_cap;

static void release(cd_id id) {
  if (id >= next_id || !alive[id]) __builtin_trap();
  alive[id] = 0;
  tags[id] = 0;
  if (id < on_cap && on_ids[id]) on_drop(id); /* a reused id must not inherit the handlers */
  free_ids[nfree++] = id;
}

/* cd_open, cd_id_new and cd_name_open, in that order: the id is made once the element is open, so a
 * region that is building records it, and it names the open element, not the last node. */
cd_id cd_open_id(cd_str tag) {
  (cd_open)(tag);
  cd_id id = cd_id_new();
  cd_name_open(id);
  return id;
}

uint32_t cd_ids_live(void) { return next_id - CD_FIRST_ID - nfree; }
uint32_t cd_ids_high(void) { return next_id - 1; }

/* The mount and the reserved ids are never freed, so they stay bound and cd_id_new never returns them. */
void cd_id_free(cd_id id) {
  if (id < CD_FIRST_ID) return;
  walk_done(); j_free(id); /* no DOM change: a cleanup may free (cd_list_forget) */
  release(id);
}

void cd_remove(cd_id id) {
  if (id < CD_FIRST_ID) return;
  ready(); j_remove(id);
  release(id);
}

void cd_tag_set(cd_id id, uint32_t tag) { tags[id] = tag; }
uint32_t cd_tag(cd_id id) { return id < tags_cap ? tags[id] : 0; }

int cd_input_whole(cd_id id, char *dst, uint32_t cap, uint32_t *len) {
  uint32_t n = (uint32_t)get(id, cd_s("value"), GET_STR_LEN, dst, cap); /* the whole length, cut or not */
  *len = n;
  return n <= cap;
}

/* ---- regions ----
 * building is the stack of regions whose builds are open, innermost last; recording mirrors its top
 * (0 when nothing builds, or inside cd_list_update, which pushes 0 so a list's ids stay the list's).
 * open_depth is the depth of the innermost region building or view of shared state running, whichever
 * opened last (0 for neither); a list's level keeps it, so a region opened in a row's create counts
 * under the region or view that updates the list. open_was keeps it across each build. */
#define CD_REGION_DEPTH 8
static cd_region *building[CD_REGION_DEPTH];
static uint32_t nbuilding, open_depth, open_was[CD_REGION_DEPTH];
#ifdef CD_STATE_CHECK
static const cd_state **reads[CD_REGION_DEPTH]; /* the check build's: the states each open build read */
static uint32_t nreads[CD_REGION_DEPTH], reads_cap[CD_REGION_DEPTH];
#endif
static void build_push(cd_region *r) {
  if (nbuilding == CD_REGION_DEPTH) __builtin_trap(); /* nested deeper than CD_REGION_DEPTH */
#ifdef CD_STATE_CHECK
  nreads[nbuilding] = 0;
#endif
  open_was[nbuilding] = open_depth;
  if (r) open_depth = r->depth;
  building[nbuilding++] = r;
  recording = r;
}
static void build_pop(void) {
  nbuilding--;
  open_depth = open_was[nbuilding];
  recording = nbuilding ? building[nbuilding - 1] : 0;
}

/* Takes r away: its child regions first, newest first, each forgotten (r's own DOM op takes their nodes
 * with it); then r's cleanups, newest first, while the DOM may not change; then, when dom, r's one DOM
 * op (a clear, or a remove of its root); then its ids. It does not unlink r from its parent. */
static void views_drop(cd_region *r);
static void (*volatile views_gone)(cd_region *r); /* views_drop, set by cd_watch */
static void take(cd_region *r, int dom) {
  for (uint32_t i = 0; i < nbuilding; i++)
    if (building[i] == r) __builtin_trap(); /* taken away while it builds */
  for (cd_region *k = r->kids, *next; k; k = next) {
    next = k->next;
    take(k, 0);
  }
  r->kids = 0;
  if (r->views) views_gone(r);
  while (r->nfns) {
    cd_cleanup c = r->fns[--r->nfns];
    releasing++;
    c.fn(c.ctx);
    releasing--;
  }
  uint32_t i = 0;
  if (dom && r->parent) cd_clear(r->parent);
  else if (dom) { cd_remove(r->root); i = 1; } /* the root is ids[0] */
  for (; i < r->n; i++) cd_id_free(r->ids[i]);
  /* A region taken away owns no heap memory, so the struct it lives in may be freed after it (a row of
   * an array the app frees); the next show allocates again. */
  cd_free(r->ids);
  cd_free(r->fns);
  r->ids = 0;
  r->fns = 0;
  r->n = r->cap = r->nfns = r->fcap = 0;
  r->root = 0;
  r->shown = 0;
}

/* Out of its parent's list of children, when it goes alone. */
static void region_unlink(cd_region *r) {
  if (!r->up) return;
  for (cd_region **k = &r->up->kids; *k; k = &(*k)->next)
    if (*k == r) { *k = r->next; break; }
  r->next = 0;
}

int cd_region_show(cd_region *r, int want) {
  if (!want == !r->shown) return 0;
  if (!want) {
    region_unlink(r);
    take(r, 1);
    return 0;
  }
  cd_region *in = recording; /* the region building now, if any, is its parent */
  if (in) {
    if (r->up && r->up != in) __builtin_trap(); /* r lives in another region */
    r->up = in;
  }
  if (r->up) {
    if (!r->up->shown) __builtin_trap(); /* its parent is not shown */
    r->next = r->up->kids;
    r->up->kids = r;
  }
  /* One deeper than what it opens in (a region building or a view running, with any list between) and
   * than its parent: every view that may take it away runs before its views (shared state). */
  uint32_t d = open_depth;
  if (r->up && r->up->depth > d) d = r->up->depth;
  r->depth = d + 1;
  r->shown = 1;
  r->n = 0;
  build_push(r);
  if (!r->parent) r->root = cd_id_new();
  return 1;
}
#ifdef CD_STATE_CHECK
static void reads_check(uint32_t level);
#endif
void cd_region_end(cd_region *r) {
  if (recording != r) __builtin_trap();
#ifdef CD_STATE_CHECK
  reads_check(nbuilding - 1);
#endif
  build_pop();
}
void cd_region_forget(cd_region *r) {
  if (!r->shown) return;
  region_unlink(r);
  take(r, 0);
}
void cd_on_free(cd_region *r, cd_cleanup_fn fn, void *ctx) {
  if (!r) r = recording;
  if (!r || !r->shown || releasing) __builtin_trap(); /* no region building, or one not shown, or inside a cleanup */
  if (r->nfns == r->fcap) {
    r->fcap = r->fcap ? r->fcap * 2 : 4;
    r->fns = cd_realloc(r->fns, r->fcap * sizeof(cd_cleanup));
  }
  r->fns[r->nfns++] = (cd_cleanup){fn, ctx};
}
void cd_region_hide(cd_region *r) { cd_region_show(r, 0); }

/* ---- shared state: cd_state, cd_changed, cd_watch ----
 * A view is a (fn, ctx) a region's build made: it hangs off the region (r->views, through next) and
 * goes with it. A link is one view watching one state: it is in the state's list (prev, next) and in
 * the view's (vnext). Nothing is searched in the pass: cd_changed puts the state on the changed list,
 * and a round walks each changed state's links.
 * A round takes the changed list, collects the views of its states, each once (queued) and none that
 * ran after the state's last change (ran > tick), orders them by depth, stably, and runs them. tick
 * counts in the pass: each change, and each view's run or making, moves it on, so "ran after the
 * change" is one compare. Outside a pass it is 0, and so is every view's ran: the pass's end puts
 * back to 0 the ran of every view it touched (pv: the views queued or made in the pass), so no count
 * outlives its pass and none can wrap.
 * A view taken away during a pass may still be in pv: it is unlinked at once (fn 0, so a round skips
 * it) and freed at the pass's end (dead); taken away outside a pass it is freed at once. */
typedef struct cd_view {
  cd_render_fn fn; void *ctx;
  struct cd_view *next;      /* the region's next view; once dead, the next dead view */
  struct cd_link *links;     /* what it watches */
  uint32_t ran;              /* the tick it last ran at, or was made at, in this pass; 0 outside one */
  uint16_t depth;            /* its region's (r->depth) */
  uint8_t queued;            /* in the round being collected */
} cd_view;
typedef struct cd_link {
  cd_state *s; cd_view *v;
  struct cd_link *prev, *next; /* s's links, in the order they were made */
  struct cd_link *vnext;       /* v's links */
} cd_link;
_Static_assert(sizeof(cd_state) == 24, "cdom.h says a cd_state is 24 bytes");
_Static_assert(sizeof(cd_view) + 8 <= 32 && sizeof(cd_link) + 8 <= 32, "a view and a link each fit the heap's 32-byte class");
static cd_view **pv, **pv_tmp, *dead;
static uint32_t npv, pv_cap, tick, *depth_n, depth_cap;
#ifdef CD_STATE_CHECK
static cd_view *running;      /* the view running now */
static uint32_t running_base; /* the builds open when it began: one opened after it is a build's read */
#endif

static void pv_push(cd_view *v) {
  if (npv == pv_cap) {
    pv_cap = pv_cap ? pv_cap * 2 : 64;
    pv = cd_realloc(pv, pv_cap * sizeof *pv);
    pv_tmp = cd_realloc(pv_tmp, pv_cap * sizeof *pv_tmp);
  }
  pv[npv++] = v;
}
/* pv[a, b) by depth, outermost first, keeping the order within a depth (a counting sort). Most rounds
 * are in order already (one view, or views of one depth), and are left as they are. */
static void views_order(uint32_t a, uint32_t b) {
  uint32_t top = 0, in_order = 1;
  for (uint32_t i = a; i < b; i++) {
    uint32_t d = pv[i]->depth;
    if (d < top) in_order = 0; else top = d;
  }
  if (in_order) return;
  if (top + 1 > depth_cap) {
    depth_cap = top + 1;
    depth_n = cd_realloc(depth_n, depth_cap * sizeof *depth_n);
  }
  __builtin_memset(depth_n, 0, (top + 1) * sizeof *depth_n);
  for (uint32_t i = a; i < b; i++) depth_n[pv[i]->depth]++;
  for (uint32_t d = 0, at = a; d <= top; d++) { uint32_t n = depth_n[d]; depth_n[d] = at; at += n; }
  for (uint32_t i = a; i < b; i++) pv_tmp[depth_n[pv[i]->depth]++] = pv[i];
  __builtin_memcpy(pv + a, pv_tmp + a, (b - a) * sizeof *pv);
}
/* views_pass: end 0 runs one round; end 1 ends the pass. */
static void views_run(int end) {
  if (end) {
    for (uint32_t i = 0; i < npv; i++) pv[i]->ran = 0;
    npv = tick = 0;
    while (dead) {
      cd_view *v = dead;
      dead = v->next;
      cd_free(v);
    }
    return;
  }
  uint32_t a = npv;
  cd_state *s = changed;
  changed = changed_last = 0;
  while (s != CHANGED_END) {
    cd_state *next = s->changed;
    s->changed = 0;
    for (cd_link *l = s->first; l; l = l->next) {
      cd_view *v = l->v;
      if (v->queued || v->ran > s->tick) continue; /* collected, or ran after the change */
      v->queued = 1;
      pv_push(v);
    }
    s = next;
  }
  uint32_t b = npv;
  views_order(a, b);
  for (uint32_t i = a; i < b; i++) {
    cd_view *v = pv[i];
    v->queued = 0;
    if (!v->fn) continue; /* its region went, earlier in this round */
    v->ran = ++tick;
    uint32_t depth_was = open_depth;
    open_depth = v->depth; /* a region it shows, or a list it updates, is under its region */
#ifdef CD_STATE_CHECK
    cd_view *was = running;
    uint32_t base = running_base;
    running = v;
    running_base = nbuilding;
    v->fn(v->ctx);
    running = was;
    running_base = base;
#else
    v->fn(v->ctx);
#endif
    open_depth = depth_was;
  }
}

void cd_changed(cd_state *s) {
  s->ver++;
  s->tick = rendering ? ++tick : 0;
  if (s->changed) return; /* on the list already */
  s->changed = CHANGED_END;
  if (changed_last) changed_last->changed = s; else changed = s;
  changed_last = s;
  render_pass = run_renders;
  views_pass = views_run;
}

/* Every link of r's views out of its state's list, and the views freed (or, in a pass, dead). */
static void views_drop(cd_region *r) {
  for (cd_view *v = r->views, *next; v; v = next) {
    next = v->next;
    for (cd_link *l = v->links, *ln; l; l = ln) {
      ln = l->vnext;
      cd_state *s = l->s;
      if (l->prev) l->prev->next = l->next; else s->first = l->next;
      if (l->next) l->next->prev = l->prev; else s->last = l->prev;
      cd_free(l);
    }
    if (rendering) { /* pv may hold it: the pass's end frees it */
      v->fn = 0;
      v->next = dead;
      dead = v;
    } else {
      cd_free(v);
    }
  }
  r->views = 0;
}

void cd_watch(cd_state *s, cd_render_fn fn, void *ctx) {
  cd_region *r = recording;
  if (!r || releasing) __builtin_trap(); /* no region builds (a handler, a timer, a list row's create), or a cleanup */
  views_gone = views_drop;
  views_pass = views_run; /* a view made in a pass is in pv, which the pass's end empties */
  cd_view *v = r->views;
  while (v && (v->fn != fn || v->ctx != ctx)) v = v->next;
  if (v) {
    for (cd_link *l = v->links; l; l = l->vnext)
      if (l->s == s) return; /* it watches s already */
  } else {
    if (r->depth > 0xffff) __builtin_trap();
    v = cd_alloc(sizeof *v);
    *v = (cd_view){fn, ctx, r->views, 0, 0, (uint16_t)r->depth, 0};
    r->views = v;
    if (rendering) { /* made in a pass: it drew in its build, so only a change after now runs it */
      v->ran = ++tick;
      pv_push(v);
    }
  }
  cd_link *l = cd_alloc(sizeof *l);
  *l = (cd_link){s, v, s->last, 0, v->links};
  if (s->last) s->last->next = l; else s->first = l;
  s->last = l;
  v->links = l;
}

#ifdef CD_STATE_CHECK
/* The check build. A read while a build opened after the running view began (or with no view running)
 * is that build's, kept by the innermost region building; it is checked when that region's build ends.
 * Any other read, while a view runs, is the view's, checked now. */
static int view_watches(const cd_view *v, const cd_state *s) {
  for (const cd_link *l = v->links; l; l = l->vnext)
    if (l->s == s) return 1;
  return 0;
}
static int region_watches(const cd_region *r, const cd_state *s) {
  for (const cd_view *v = r->views; v; v = v->next)
    if (view_watches(v, s)) return 1;
  return 0;
}
void cd_read(const cd_state *s) {
  uint32_t lv = nbuilding;
  while (lv > running_base && !building[lv - 1]) lv--; /* a list's level is no region's */
  if (lv > running_base) {
    uint32_t i = lv - 1;
    if (nreads[i] == reads_cap[i]) {
      reads_cap[i] = reads_cap[i] ? reads_cap[i] * 2 : 8;
      reads[i] = cd_realloc(reads[i], reads_cap[i] * sizeof *reads[i]);
    }
    reads[i][nreads[i]++] = s;
  } else if (running && !view_watches(running, s)) {
    __builtin_trap(); /* a view read a state it does not watch */
  }
}
/* At the end of the build at level: each state it read is watched by its region, a region building
 * under it, or one of its r->up chain. */
static void reads_check(uint32_t level) {
  const cd_region *r = building[level];
  for (uint32_t k = 0; k < nreads[level]; k++) {
    const cd_state *s = reads[level][k];
    int ok = 0;
    for (const cd_region *u = r; u && !ok; u = u->up) ok = region_watches(u, s);
    for (uint32_t j = 0; j < level && !ok; j++) ok = building[j] && region_watches(building[j], s);
    if (!ok) __builtin_trap(); /* a build read a state that nothing around it watches */
  }
  nreads[level] = 0;
}
#endif

/* ---- pairs: a hash of (a, b) entries, open addressing on a alone ----
 * The one table type for cdom's own maps from a pair: cd_on's handlers by (id, event type), the timers
 * by (token, handle), and cd_dirty's pending asks. Every entry
 * with the same a lies in the probe run from a's home slot, so one walk to the first empty slot finds
 * all of them (pair_any), and a delete shifts the run back instead of leaving a tombstone. */
typedef struct { uint32_t a, b, used; void *p, *q; } pair_ent;
typedef struct { pair_ent *e; uint32_t cap, n; } pair_tab;
static uint32_t pair_home(const pair_tab *t, uint32_t a) { a *= 0x9e3779b1u; return (a ^ (a >> 15)) & (t->cap - 1); }
static pair_ent *pair_find(pair_tab *t, uint32_t a, uint32_t b) {
  if (!t->n) return 0;
  for (uint32_t m = t->cap - 1, i = pair_home(t, a); t->e[i].used; i = (i + 1) & m)
    if (t->e[i].a == a && t->e[i].b == b) return &t->e[i];
  return 0;
}
static pair_ent *pair_any(pair_tab *t, uint32_t a) {
  if (!t->n) return 0;
  for (uint32_t m = t->cap - 1, i = pair_home(t, a); t->e[i].used; i = (i + 1) & m)
    if (t->e[i].a == a) return &t->e[i];
  return 0;
}
static void pair_insert(pair_tab *t, pair_ent v) {
  uint32_t m = t->cap - 1, i = pair_home(t, v.a);
  while (t->e[i].used) i = (i + 1) & m;
  t->e[i] = v;
  t->n++;
}
/* Sets (a, b) to (p, q), replacing what it held. */
static void pair_put(pair_tab *t, uint32_t a, uint32_t b, void *p, void *q) {
  pair_ent *e = pair_find(t, a, b);
  if (e) { e->p = p; e->q = q; return; }
  if ((t->n + 1) * 2 > t->cap) { /* grow and rehash: at most half full */
    pair_ent *old = t->e;
    uint32_t ocap = t->cap;
    t->cap = ocap ? ocap * 2 : 64;
    t->e = cd_alloc(t->cap * sizeof(pair_ent));
    __builtin_memset(t->e, 0, t->cap * sizeof(pair_ent));
    t->n = 0;
    for (uint32_t i = 0; i < ocap; i++) if (old[i].used) pair_insert(t, old[i]);
    cd_free(old);
  }
  pair_insert(t, (pair_ent){a, b, 1, p, q});
}
/* Deletes e, then moves back each later entry of the run that its home allows to fill the hole. */
static void pair_del(pair_tab *t, pair_ent *e) {
  uint32_t m = t->cap - 1, i = (uint32_t)(e - t->e);
  t->n--;
  for (uint32_t j = i;;) {
    t->e[i].used = 0;
    for (;;) {
      j = (j + 1) & m;
      if (!t->e[j].used) return;
      uint32_t k = pair_home(t, t->e[j].a); /* it may move to i unless its home lies in (i, j] */
      if (i <= j ? (k <= i || k > j) : (k <= i && k > j)) break;
    }
    t->e[i] = t->e[j];
    i = j;
  }
}

/* ---- renders: cd_dirty's asks ----
 * asked maps a pending (fn, ctx) to its index in renders: the pair (ctx ^ fn * K, fn), which names one
 * (fn, ctx) and spreads one function's many ctx over the table. An index under render_at ran in this
 * pass, and an entry cd_undirty dropped has no fn: asked again, either is asked anew. The pass's end
 * empties the table, and drops one grown past its first size. */
static pair_tab asked;
static pair_ent *asked_find(cd_render_fn fn, void *ctx) {
  uint32_t f = (uint32_t)(uintptr_t)fn;
  pair_ent *e = pair_find(&asked, (uint32_t)(uintptr_t)ctx ^ f * 0x9e3779b1u, f);
  return e && (uintptr_t)e->p >= render_at && renders[(uintptr_t)e->p].fn ? e : 0;
}
void cd_dirty(cd_render_fn fn, void *ctx) {
  render_pass = run_renders;
  if (asked_find(fn, ctx)) return; /* pending */
  if (nrenders == renders_cap) {
    renders_cap = renders_cap ? renders_cap * 2 : 16;
    renders = cd_realloc(renders, renders_cap * sizeof(render_ent));
  }
  uint32_t f = (uint32_t)(uintptr_t)fn;
  pair_put(&asked, (uint32_t)(uintptr_t)ctx ^ f * 0x9e3779b1u, f, (void *)(uintptr_t)nrenders, 0);
  renders[nrenders++] = (render_ent){fn, ctx};
}
void cd_undirty(cd_render_fn fn, void *ctx) {
  pair_ent *e = asked_find(fn, ctx);
  if (e) renders[(uintptr_t)e->p].fn = 0;
}
static void asked_reset(void) {
  if (asked.cap > 64) {
    cd_free(asked.e);
    asked = (pair_tab){0};
  } else if (asked.n) {
    __builtin_memset(asked.e, 0, asked.cap * sizeof(pair_ent));
    asked.n = 0;
  }
}

/* ---- handlers: cd_on ----
 * handlers owns every (id, type) -> (fn, ctx); on_ids[id] says the id has at least one, so release()
 * looks in the table only for those. cd_event calls the handler for (record id, type) when there is
 * one, else cd_on_event. */
static pair_tab handlers;
static void on_drop(cd_id id) {
  for (pair_ent *e; (e = pair_any(&handlers, id));) pair_del(&handlers, e);
  on_ids[id] = 0;
}
void cd_on(cd_id id, uint32_t ev, cd_event_fn fn, void *ctx) {
  if (ev >= CD_EV_TYPES) __builtin_trap();
  int host = id >= CD_WINDOW && id <= CD_BODY;
  if (!host && (id < CD_FIRST_ID || id >= next_id || !alive[id])) __builtin_trap(); /* the mount, a reserved or a free id */
  if (!fn) {
    pair_ent *e = pair_find(&handlers, id, ev);
    if (!e) return;
    pair_del(&handlers, e);
    if (!pair_any(&handlers, id)) on_ids[id] = 0;
    cd_unlisten(id, ev);
    return;
  }
  if (id >= on_cap) {
    uint32_t old = on_cap;
    on_cap = id < 2048 ? 4096 : id * 2;
    on_ids = cd_realloc(on_ids, on_cap);
    __builtin_memset(on_ids + old, 0, on_cap - old);
  }
  on_ids[id] = 1;
  pair_put(&handlers, id, ev, (void *)fn, ctx);
  if (!host) cd_sel(id); /* cd_listen names the last node: make it id's own */
  cd_listen(id, ev);
}

/* ---- callback slots (cdom_cb.h): the one owner of cdom's own tokens ---- */
typedef struct { cd_cb_fn fn; void *ctx; uint16_t gen, live; } cb_slot;
static cb_slot *cb_slots;
static uint16_t *cb_free;
static uint32_t cb_n, cb_cap, cb_nfree;
#define CB_SLOTS_MAX 0xffffu /* slot 0xffff is never used, so no token is CD_FRAMES_TOKEN */
uint32_t cd_cb_new(cd_cb_fn fn, void *ctx) {
  uint32_t i;
  if (cb_nfree) i = cb_free[--cb_nfree];
  else {
    if (cb_n == CB_SLOTS_MAX) __builtin_trap();
    if (cb_n == cb_cap) {
      cb_cap = cb_cap ? cb_cap * 2 : 16;
      cb_slots = cd_realloc(cb_slots, cb_cap * sizeof(cb_slot));
      cb_free = cd_realloc(cb_free, cb_cap * sizeof(uint16_t));
    }
    i = cb_n++;
    cb_slots[i].gen = 0;
  }
  cb_slots[i].fn = fn;
  cb_slots[i].ctx = ctx;
  cb_slots[i].live = 1;
  return CD_TOKEN_CDOM | (uint32_t)cb_slots[i].gen << 16 | i;
}
static cb_slot *cb_named(uint32_t token) {
  uint32_t i = token & 0xffffu;
  if (token < CD_TOKEN_CDOM || i >= cb_n) return 0;
  cb_slot *c = &cb_slots[i];
  return c->live && c->gen == ((token >> 16) & 0x7fffu) ? c : 0;
}
static void cb_free_slot(cb_slot *c) {
  c->live = 0;
  c->gen = (c->gen + 1) & 0x7fffu;
  cb_free[cb_nfree++] = (uint16_t)(c - cb_slots);
}
static cb_slot *cb_read(uint32_t token, cd_cb_fn *fn, void **ctx) {
  cb_slot *c = cb_named(token);
  if (c) {
    *fn = c->fn;
    *ctx = c->ctx;
  }
  return c;
}
int cd_cb_get(uint32_t token, cd_cb_fn *fn, void **ctx) { return cb_read(token, fn, ctx) != 0; }
int cd_cb_take(uint32_t token, cd_cb_fn *fn, void **ctx) {
  cb_slot *c = cb_read(token, fn, ctx);
  if (c) cb_free_slot(c);
  return c != 0;
}
void cd_cb_drop(uint32_t token) {
  cb_slot *c = cb_named(token);
  if (c) cb_free_slot(c);
}

/* ---- timers ----
 * timers owns every timer started and not yet fired or stopped, as (token, the shim's setTimeout
 * handle): a token started twice is two entries, and cd_timer_stop clears every one of its token. */
static pair_tab timers;
static void timer_start(uint32_t ms, uint32_t token) { pair_put(&timers, token, j_timer(ms, token), 0, 0); }
void cd_timer_start(uint32_t ms, uint32_t token) {
  if (token >= CD_TOKEN_CDOM) __builtin_trap(); /* cdom's range */
  timer_start(ms, token);
}
uint32_t cd_timer_cb(uint32_t ms, cd_timer_fn fn, void *ctx) {
  uint32_t token = cd_cb_new((cd_cb_fn)fn, ctx);
  timer_start(ms, token);
  return token;
}
void cd_timer_stop(uint32_t token) {
  for (pair_ent *e; (e = pair_any(&timers, token));) {
    j_timer_stop(e->b);
    pair_del(&timers, e);
  }
  cd_cb_drop(token);
}

/* A slot's timer: the slot is emptied before fn runs, so fn may set it again. */
static void timer_slot_fired(void *ctx) {
  cd_timer_slot *t = ctx;
  t->token = 0;
  t->fn(t->ctx);
}
void cd_timer_set(cd_timer_slot *t, uint32_t ms, cd_timer_fn fn, void *ctx) {
  cd_timer_clear(t);
  t->fn = fn;
  t->ctx = ctx;
  t->token = cd_timer_cb(ms, timer_slot_fired, t);
}
void cd_timer_clear(cd_timer_slot *t) {
  if (t->token) cd_timer_stop(t->token);
  t->token = 0;
}

/* ---- window and time ---- */
static cd_id window_target(uint32_t ev) { return ev == CD_EV_RESIZE ? CD_WINDOW : CD_DOCUMENT; }
void cd_listen_window(uint32_t ev) { cd_listen(window_target(ev), ev); }
void cd_unlisten_window(uint32_t ev) { cd_unlisten(window_target(ev), ev); }
void cd_prevent_default(void) { inbox.flags |= CD_PREVENT; }
void cd_stop_propagation(void) { inbox.flags |= CD_STOP; }
/* Layout reads: the renders asked for so far run first, so the layout read is the state's. */
cd_id cd_hit(int32_t x, int32_t y) { renders_now(); return j_hit(x, y); }
void cd_viewport(cd_viewport_rec *v) { renders_now(); j_viewport(v); }
int cd_rect(cd_id id, double out[4]) { renders_now(); return (int)j_rect(id, out); }
void cd_raf_request(uint32_t token) { j_raf(token); }
double cd_now(void) { return j_now(); }
__attribute__((weak)) void cd_on_timer(uint32_t) { }
__attribute__((weak)) void cd_on_raf(uint32_t, double) { }
__attribute__((weak)) void cd_on_frame(double) { }
/* cd_frames' loop: frames_on is what the app asked for, frames_asked that a frame is requested, so a
 * second start (or a stop and start inside one frame) never requests a second one. */
static bool frames_on, frames_asked;
void cd_frames(bool on) {
  frames_on = on;
  if (frames_on && !frames_asked) { frames_asked = true; j_raf(CD_FRAMES_TOKEN); }
}
static void frame(double t) {
  frames_asked = false;
  if (!frames_on) return;
  cd_on_frame(t);
  cd_frames(frames_on);
}

/* ---- entry points ---- */
EXPORT("cd_layout") const uint32_t *cd_layout(void) { return layout; }

/* The word tools/cdlits overwrites with the hash of the page's string table; the shim compares it with
 * the table, so a page and a module from different builds fail at start. volatile and non-zero keep it a
 * real word in .data under LTO. */
volatile uint32_t cd_lits_stamp = 0x7374696c;
EXPORT("cd_lits") uint32_t cd_lits(void) { return cd_lits_stamp; }

uint32_t cd_seed;
/* cd_rand's state. An app does not reseed it: the one seed is cd_seed, which a page fixes in one line. */
static uint32_t rand_state;
uint32_t cd_rand(void) {
  rand_state ^= rand_state << 13;
  rand_state ^= rand_state >> 17;
  rand_state ^= rand_state << 5;
  return rand_state;
}
EXPORT("cd_start") void cd_start(uint32_t seed) {
  cd_seed = seed;
  rand_state = seed | 1; /* xorshift32 stays at 0 once it is 0, so seed 0 must not start it there */
  cd_at(CD_MOUNT, CD_APPEND); /* so an app starts building at the end of its mount, and calls cd_at only to build somewhere else */
  cd_main();
  cd_flush();
}

/* Every entry from JS ends in a flush, here and in cdom_net.c: an app never calls cd_flush.
 * cd_event returns the flags the handler set, which the shim acts on once C has returned. */
__attribute__((weak)) void cd_on_event(const cd_event_rec *) { }
EXPORT("cd_event") uint32_t cd_event(void) {
  inbox.flags = 0;
  uint32_t id = (uint32_t)inbox.id;
  pair_ent *h = id < on_cap && on_ids[id] ? pair_find(&handlers, id, (uint32_t)inbox.type) : 0;
  if (h) ((cd_event_fn)h->p)(&inbox, h->q);
  else cd_on_event(&inbox);
  cd_flush();
  return (uint32_t)inbox.flags;
}
/* A timer fired: it is no longer pending; an app token goes to cd_on_timer, cdom's to its callback
 * (taken, so the callback may start another in the same slot: a repeat). */
EXPORT("cd_timer") void cd_timer(uint32_t token, uint32_t handle) {
  pair_ent *e = pair_find(&timers, token, handle);
  if (e) pair_del(&timers, e);
  cd_cb_fn fn;
  void *ctx;
  if (token < CD_TOKEN_CDOM) cd_on_timer(token);
  else if (cd_cb_take(token, &fn, &ctx)) ((cd_timer_fn)fn)(ctx);
  cd_flush();
}
EXPORT("cd_raf") void cd_raf(uint32_t token, double t) {
  if (token == CD_FRAMES_TOKEN) frame(t); else cd_on_raf(token, t);
  cd_flush();
}

/* Bytes from JS (an encoded image, a network message, a stored value, a path): the shim asks for a
 * block of their size here, copies them in, then calls the export that hands them to the app, and that
 * export frees the block once the app's hook (or callback) and the flush after it returned. So nothing of
 * cdom's holds an answer's bytes past its answer: each is a block of its own, at most 4 KiB over its size
 * when large (memory, above), and its room serves the next block. */
EXPORT("cd_bytes_buf") void *cd_bytes_buf(uint32_t n) { return cd_alloc(n ? n : 1); }
__attribute__((weak)) void cd_on_encoded(uint32_t, const uint8_t *, uint32_t) { }
EXPORT("cd_encoded") void cd_encoded(uint32_t token, const uint8_t *p, uint32_t len) {
  cd_on_encoded(token, p, len);
  cd_flush();
  cd_free((void *)p);
}
__attribute__((weak)) void cd_on_image(uint32_t, uint32_t) { }
EXPORT("cd_image") void cd_image(uint32_t token, uint32_t ok) {
  cd_on_image(token, ok);
  cd_flush();
}

/* A first fill this large builds off-document and attaches once. */
#ifndef CD_DETACH_MIN
#define CD_DETACH_MIN 64
#endif

/* ---- keyed list diff: common prefix and suffix, then a key map and an
 * LIS so only nodes off the longest increasing run move ---- */
static uint32_t *scr_map, scr_map_cap;
static int32_t *scr_src, *scr_tails, *scr_pred;
static uint32_t scr_cap;

static void scratch(uint32_t m) {
  if (m > scr_cap) {
    scr_cap = m * 2;
    scr_src = cd_realloc(scr_src, scr_cap * 4);
    scr_tails = cd_realloc(scr_tails, scr_cap * 4);
    scr_pred = cd_realloc(scr_pred, scr_cap * 4);
  }
  uint32_t hc = 16;
  while (hc < m * 2) hc <<= 1;
  if (hc > scr_map_cap) {
    scr_map_cap = hc;
    scr_map = cd_realloc(scr_map, hc * 8);
  }
  __builtin_memset(scr_map, 0xff, hc * 8);
  scr_map_cap = hc; /* lookups use exactly hc slots */
}

static inline uint32_t khash(uint32_t k) { k *= 0x9e3779b1u; return k ^ (k >> 15); }

static void map_put(uint32_t key, uint32_t idx) {
  uint32_t m = scr_map_cap - 1, i = khash(key) & m;
  while (scr_map[i * 2 + 1] != 0xffffffffu) i = (i + 1) & m;
  scr_map[i * 2] = key;
  scr_map[i * 2 + 1] = idx;
}

static uint32_t map_get(uint32_t key) {
  uint32_t m = scr_map_cap - 1, i = khash(key) & m;
  for (;;) {
    uint32_t v = scr_map[i * 2 + 1];
    if (v == 0xffffffffu || scr_map[i * 2] == key) return v;
    i = (i + 1) & m;
  }
}

/* Marks LIS members of src[0..m) (entries < 0 skipped) by setting pred[i] = -2. */
static void lis_mark(const int32_t *src, uint32_t m, int32_t *tails, int32_t *pred) {
  uint32_t len = 0;
  for (uint32_t i = 0; i < m; i++) {
    int32_t v = src[i];
    pred[i] = -1;
    if (v < 0) continue;
    uint32_t lo = 0, hi = len;
    while (lo < hi) {
      uint32_t mid = (lo + hi) >> 1;
      if (src[tails[mid]] < v) lo = mid + 1; else hi = mid;
    }
    pred[i] = lo ? tails[lo - 1] : -1;
    tails[lo] = (int32_t)i;
    if (lo == len) len++;
  }
  int32_t k = len ? tails[len - 1] : -1;
  while (k >= 0) {
    int32_t p = pred[k];
    pred[k] = -2;
    k = p;
  }
}

/* destroy runs first, while the item's root id still names its node: what the item holds (a region it
 * opened, cleanups and all) goes before any of its ids, as a region's does. */
static void drop(cd_list *, cd_item *it, const cd_list_fns *f, int dom) {
  f->destroy(f->ctx, it);
  if (dom) cd_remove(it->node); else cd_id_free(it->node);
}

void cd_list_forget(cd_list *L, const cd_list_fns *f) {
  for (uint32_t i = 0; i < L->n; i++) {
    f->destroy(f->ctx, &L->items[i]);
    cd_id_free(L->items[i].node);
  }
  L->n = 0;
}

void cd_list_update(cd_list *L, const uint32_t *keys, uint32_t n, const cd_list_fns *f) {
  build_push(0); /* the items' ids are the list's, not a region's */
  cd_id end = L->end; /* where the tail is: before the first fixed sibling, or the parent's end */
  cd_item *old = L->items;
  uint32_t on = L->n;
  cd_item *nw = cd_alloc((n ? n : 1) * sizeof(cd_item));
  uint32_t os = 0, oe = on, ns = 0, ne = n;

  /* Trim matching heads and tails. A head that became the tail (or the
   * reverse) moves in one op, but only when another survivor sits next to it
   * in both lists: then it is out of order with every other survivor, so no
   * longest in-order run can keep it and the move is one the LIS would make. */
  for (;;) {
    while (os < oe && ns < ne && old[os].key == keys[ns]) {
      nw[ns] = old[os++];
      f->patch(f->ctx, ns, &nw[ns]);
      ns++;
    }
    while (os < oe && ns < ne && old[oe - 1].key == keys[ne - 1]) {
      nw[--ne] = old[--oe];
      f->patch(f->ctx, ne, &nw[ne]);
    }
    if (oe - os < 2 || ne - ns < 2) break;
    int head_to_tail = old[os].key == keys[ne - 1];
    int tail_to_head = old[oe - 1].key == keys[ns];
    if (head_to_tail && tail_to_head && oe - os > 2 && ne - ns > 2 && old[os + 1].key == keys[ns + 1]) {
      cd_move(old[os].node, ne < n ? nw[ne].node : end); /* a swap of the two ends */
      cd_move(old[oe - 1].node, old[os + 1].node);
      nw[--ne] = old[os++];
      f->patch(f->ctx, ne, &nw[ne]);
      nw[ns] = old[--oe];
      f->patch(f->ctx, ns, &nw[ns]);
      ns++;
      continue;
    }
    if (head_to_tail && old[os + 1].key == keys[ns]) {
      cd_move(old[os].node, ne < n ? nw[ne].node : end);
      nw[--ne] = old[os++];
      f->patch(f->ctx, ne, &nw[ne]);
      continue;
    }
    if (tail_to_head && old[oe - 2].key == keys[ne - 1]) {
      cd_move(old[oe - 1].node, old[os].node);
      nw[ns] = old[--oe];
      f->patch(f->ctx, ns, &nw[ns]);
      ns++;
      continue;
    }
    break;
  }

  if (os == oe) { /* only inserts */
    cd_id before = ne < n ? nw[ne].node : end;
    int off = L->owns_parent && on == 0 && n >= CD_DETACH_MIN;
    if (off) cd_detach(L->parent);
    for (uint32_t i = ns; i < ne; i++) {
      cd_at(L->parent, before);
      nw[i].key = keys[i];
      nw[i].node = f->create(f->ctx, i, &nw[i].data);
    }
    if (off) cd_attach(L->parent);
  } else if (ns == ne) { /* only removes */
    if (L->owns_parent && n == 0) {
      cd_clear(L->parent);
      for (uint32_t i = os; i < oe; i++) drop(L, &old[i], f, 0);
    } else {
      for (uint32_t i = os; i < oe; i++) drop(L, &old[i], f, 1);
    }
  } else {
    uint32_t m = ne - ns;
    scratch(m);
    for (uint32_t i = 0; i < m; i++) {
      map_put(keys[ns + i], ns + i);
      scr_src[i] = -1;
    }
    /* Replacing the whole list with all new keys: one clear beats n removes. */
    int whole = L->owns_parent && os == 0 && oe == on && ns == 0 && ne == n;
    if (whole)
      for (uint32_t i = 0; i < on && whole; i++) whole = map_get(old[i].key) == 0xffffffffu;
    if (whole) {
      cd_clear(L->parent);
      for (uint32_t i = 0; i < on; i++) drop(L, &old[i], f, 0);
      int off = n >= CD_DETACH_MIN;
      if (off) cd_detach(L->parent);
      for (uint32_t i = 0; i < n; i++) {
        cd_at(L->parent, end);
        nw[i].key = keys[i];
        nw[i].node = f->create(f->ctx, i, &nw[i].data);
      }
      if (off) cd_attach(L->parent);
    } else {
      int moved = 0;
      uint32_t last = 0;
      for (uint32_t i = os; i < oe; i++) {
        uint32_t j = map_get(old[i].key);
        if (j == 0xffffffffu) {
          drop(L, &old[i], f, 1);
          continue;
        }
        scr_src[j - ns] = (int32_t)i;
        nw[j] = old[i];
        f->patch(f->ctx, j, &nw[j]);
        if (j < last) moved = 1; else last = j;
      }
      if (moved) lis_mark(scr_src, m, scr_tails, scr_pred);
      for (uint32_t k = m; k-- > 0;) {
        uint32_t pos = ns + k;
        cd_id before = pos + 1 < n ? nw[pos + 1].node : end;
        if (scr_src[k] < 0) {
          cd_at(L->parent, before);
          nw[pos].key = keys[pos];
          nw[pos].node = f->create(f->ctx, pos, &nw[pos].data);
        } else if (moved && scr_pred[k] != -2) {
          cd_move(nw[pos].node, before);
        }
      }
    }
  }
  cd_free(old);
  L->items = nw;
  L->n = n;
  L->cap = n;
  build_pop();
}

/* ---- bound values: a node property that remembers what it last sent ----
 * seen says what last holds: 0 nothing yet, 1 a string good for the page (a literal's slot or a
 * read-only pointer, the same text whenever the id is), 2 a number, 3 bytes given to
 * cd_bound_set_bytes, kept in b->bytes (compared exactly: a hash could collide and leave the DOM wrong). */
enum { BOUND_TEXT, BOUND_ATTR, BOUND_CLASS, BOUND_STYLE, BOUND_BOOL, BOUND_PROP };
enum { SEEN_NONE, SEEN_ID, SEEN_NUM, SEEN_BYTES };
_Static_assert(SEEN_ID == 1, "cdom.h's inline cd_bound_set reads seen 1 as an id");
void cd_unbind(cd_bound *b) {
  cd_free(b->bytes);
  __builtin_memset(b, 0, sizeof *b);
}
static void unbind_cleanup(void *ctx) { cd_unbind(ctx); }
/* Each binder names its writer, so an app links the writers of the kinds it binds and no other. */
static void write_text(cd_id id, cd_str, cd_str v) { cd_set_text_node(id, v); }
static void write_attr(cd_id id, cd_str name, cd_str v) { cd_set_attr(id, name, v); }
static void write_class(cd_id id, cd_str, cd_str v) { cd_set_class(id, v); }
static void write_style(cd_id id, cd_str name, cd_str v) { cd_set_style(id, name, v); }
static void write_prop(cd_id id, cd_str name, cd_str v) { cd_set_str(id, name, v); }
static void bind(cd_bound *b, cd_id id, cd_str name, uint32_t kind, cd_bound_write write) {
  if (!id || name.v >> 31) __builtin_trap(); /* no node, or a runtime name read on use (it is kept) */
  b->id = id;
  b->name = name;
  b->kind = kind;
  b->write = write;
  b->seen = SEEN_NONE;
  b->blen = 0;
  if (recording) cd_on_free(0, unbind_cleanup, b); /* its node goes with the region: so does the binding */
}
void cd_bind_text(cd_bound *b, cd_id text_node) { bind(b, text_node, CD_NONE, BOUND_TEXT, write_text); }
void cd_bind_attr(cd_bound *b, cd_id el, cd_str name) { bind(b, el, name, BOUND_ATTR, write_attr); }
void cd_bind_class(cd_bound *b, cd_id el) { bind(b, el, CD_NONE, BOUND_CLASS, write_class); }
void cd_bind_style(cd_bound *b, cd_id el, cd_str prop) { bind(b, el, prop, BOUND_STYLE, write_style); }
void cd_bind_bool(cd_bound *b, cd_id el, cd_str prop) { bind(b, el, prop, BOUND_BOOL, 0); }
void cd_bind_prop(cd_bound *b, cd_id el, cd_str prop) { bind(b, el, prop, BOUND_PROP, write_prop); }
int cd_bound_live(const cd_bound *b) { return b->id != 0; }

static void bound_write(cd_bound *b, cd_str s) { b->write(b->id, b->name, s); }
void cd_bound_set_(cd_bound *b, cd_str s) {
  if (!b->id || b->kind == BOUND_BOOL) __builtin_trap(); /* unbound, or a bool (cd_bound_set_num) */
  if (s.v >> 31) __builtin_trap(); /* a runtime string: its id says nothing of its bytes (cd_bound_set_bytes) */
  /* the unchanged case returned in cdom.h's inline cd_bound_set */
  bound_write(b, s);
  b->seen = SEEN_ID;
  b->last = s.v;
}
/* b->bytes becomes p[0, len): what the node shows now. */
static void bound_keep(cd_bound *b, const char *p, uint32_t len) {
  if (len > b->bcap) {
    b->bcap = len;
    b->bytes = cd_realloc(b->bytes, len);
  }
  __builtin_memcpy(b->bytes, p, len);
  b->blen = len;
  b->seen = SEEN_BYTES;
}
void cd_bound_set_bytes(cd_bound *b, const char *p, uint32_t len) {
  if (!b->id || b->kind == BOUND_BOOL) __builtin_trap();
  if (b->seen == SEEN_BYTES && b->blen == len && !__builtin_memcmp(b->bytes, p, len)) return; /* nothing crosses */
  bound_keep(b, p, len);
  bound_write(b, cd_tmp(p, len));
}
void cd_bound_shows_bytes(cd_bound *b, const char *p, uint32_t len) {
  if (!b->id || b->kind == BOUND_BOOL) __builtin_trap(); /* unbound, or a bool */
  bound_keep(b, p, len);
}
void cd_bound_shows(cd_bound *b, cd_str s) {
  if (!b->id || s.v >> 31) __builtin_trap(); /* unbound, or a runtime string */
  b->seen = SEEN_ID;
  b->last = s.v;
}
void cd_bound_shows_num(cd_bound *b, uint32_t n) {
  if (!b->id) __builtin_trap();
  b->seen = SEEN_NUM;
  b->last = b->kind == BOUND_BOOL ? n != 0 : n;
}
void cd_bound_set_num(cd_bound *b, uint32_t n) {
  if (!b->id) __builtin_trap(); /* unbound */
  if (b->kind == BOUND_BOOL) n = n != 0; /* 1 and 5 are the same true */
  if (b->seen == SEEN_NUM && b->last == n) return;
  b->seen = SEEN_NUM;
  b->last = n;
  if (b->kind == BOUND_TEXT) cd_set_text_node_num(b->id, n);
  else if (b->kind == BOUND_BOOL) cd_set_bool(b->id, b->name, (int)n);
  else {
    char t[12];
    bound_write(b, cd_tmp(t, cd_fmt(t, (int32_t)n, 0, CD_UNIT_NONE)));
  }
}

/* ---- request slots ----
 * ctx is the generation in the high 16 bits and the index in the low 16: plain bits, never a pointer. */
void *cd_req_begin(cd_req *r, uint16_t index) {
  r->gen++;
  r->state = CD_REQ_LOADING;
  return (void *)(uintptr_t)((uint32_t)r->gen << 16 | index);
}
int cd_req_current(const cd_req *r, void *ctx, uint16_t *index) {
  uint32_t v = (uint32_t)(uintptr_t)ctx;
  if (r->state != CD_REQ_LOADING || (v >> 16) != r->gen) return 0;
  if (index) *index = (uint16_t)v;
  return 1;
}
void cd_req_end(cd_req *r, int ok) { r->state = ok ? CD_REQ_OK : CD_REQ_FAILED; }
void cd_req_drop(cd_req *r) { r->state = CD_REQ_IDLE; } /* the next begin moves the generation on */

#endif /* CDOM_HEAP_ONLY */
