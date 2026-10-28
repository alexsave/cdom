/* cdom core: allocator, string intern, op stream, node ids, events, keyed diff. */
#include "cdom.h"

#ifdef __wasm__
#define IMPORT(n) __attribute__((import_module("cdom"), import_name(n)))
#define IMPORTS(n) IMPORT(n)
#define EXPORT(n) __attribute__((export_name(n)))
extern unsigned char __heap_base;
static uintptr_t heap_start(void) { return (uintptr_t)&__heap_base; }
static uintptr_t heap_limit(void) { return (uintptr_t)__builtin_wasm_memory_size(0) << 16; }
static int grow(uint32_t pages) { return __builtin_wasm_memory_grow(0, pages) < 0; }
#else /* native (SSR): imports are plain externs; one lazily committed 4 GiB reservation is the linear memory */
#include <sys/mman.h>
#define IMPORT(n)
#define IMPORTS(n)
#define EXPORT(n)
static uintptr_t native_heap, native_used;
static uintptr_t heap_start(void) {
  void *p = mmap(0, (size_t)1 << 32, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
  if (p == MAP_FAILED) __builtin_trap();
  return native_heap = (uintptr_t)p;
}
static uintptr_t heap_limit(void) { return native_heap + native_used; }
static int grow(uint32_t pages) { native_used += (uintptr_t)pages << 16; return native_used > (uintptr_t)1 << 32; }
#endif

IMPORT("input") uint32_t js_input(cd_id id, char *dst, uint32_t cap);

/* ---- memory: power-of-two size classes over memory.grow ---- */
static uintptr_t heap_ptr, heap_end;
static void *free_lists[32];

void *cd_alloc(uint32_t n) {
  uint32_t c = 4;
  while ((1u << c) < n + 8) c++;
  void *p = free_lists[c];
  if (p) {
    free_lists[c] = *(void **)p;
    return p;
  }
  if (!heap_ptr) {
    heap_ptr = (heap_start() + 15) & ~(uintptr_t)15;
    heap_end = heap_limit();
  }
  uintptr_t need = heap_ptr + (1u << c);
  if (need > heap_end) {
    uint32_t pages = (uint32_t)((need - heap_end + 65535) >> 16);
    if (grow(pages)) __builtin_trap();
    heap_end += (uintptr_t)pages << 16;
  }
  uint32_t *h = (uint32_t *)heap_ptr;
  heap_ptr = need;
  h[0] = c;
  return h + 2;
}

static uint32_t cap_of(void *p) { return (1u << ((uint32_t *)p)[-2]) - 8; }

void cd_free(void *p) {
  if (!p) return;
  uint32_t c = ((uint32_t *)p)[-2];
  *(void **)p = free_lists[c];
  free_lists[c] = p;
}

void *cd_realloc(void *p, uint32_t n) {
  if (p && cap_of(p) >= n) return p;
  void *q = cd_alloc(n);
  if (p) {
    __builtin_memcpy(q, p, cap_of(p));
    cd_free(p);
  }
  return q;
}

/* ---- calls ----
 * Every DOM operation is its own import, called the moment C emits it. Only strings are
 * batched: a string crosses once (an id thereafter), and new strings are decoded together
 * right before the call that first needs them. Paired harness runs measured this against a
 * batched op list that the shim interpreted: the calls won on creates and clears
 * (PERF_LOG.md, the direct-call experiment), except for walks, which travel as one call. */
IMPORTS("strings") void j_strings(uint32_t n, uint32_t nbytes);
IMPORTS("at") void j_at(cd_id parent, cd_id before);
IMPORTS("open") void j_open(uint32_t tag);
IMPORTS("close") void j_close(void);
IMPORTS("text") void j_text(uint32_t s);
IMPORTS("text_num") void j_text_num(uint32_t n);
IMPORTS("attr") void j_attr(uint32_t name, uint32_t v);
IMPORTS("class") void j_class(uint32_t v);
IMPORTS("name") void j_name(cd_id id);
IMPORTS("listen") void j_listen(cd_id id);
IMPORTS("delegate") void j_delegate(uint32_t kind, uint32_t name, uint32_t capture);
IMPORTS("value") void j_value(uint32_t s);
IMPORTS("value_num") void j_value_num(uint32_t n);
IMPORTS("tpl_save") void j_tpl_save(uint32_t t);
IMPORTS("clone") void j_clone(uint32_t t);
IMPORTS("walk") void j_walk(cd_id from, uint32_t steps, uint32_t n);
IMPORTS("walk_value") void j_walk_value(cd_id from, uint32_t steps, uint32_t n, uint32_t s);
IMPORTS("walk_value_num") void j_walk_value_num(cd_id from, uint32_t steps, uint32_t n, uint32_t v);
IMPORTS("set_text") void j_set_text(cd_id id, uint32_t s);
IMPORTS("set_text_num") void j_set_text_num(cd_id id, uint32_t n);
IMPORTS("set_attr") void j_set_attr(cd_id id, uint32_t name, uint32_t v);
IMPORTS("set_class") void j_set_class(cd_id id, uint32_t v);
IMPORTS("remove") void j_remove(cd_id id);
IMPORTS("free") void j_free(cd_id id);
IMPORTS("move") void j_move(cd_id id, cd_id before);
IMPORTS("clear") void j_clear(cd_id id);
IMPORTS("detach") void j_detach(cd_id id);
IMPORTS("attach") void j_attach(cd_id id);
IMPORTS("host") void j_host(cd_id id, uint32_t kind, uint32_t name, const uint32_t *v, uint32_t nwords);

#define SB_CAP (1u << 18)
#define SM_CAP (1u << 12)

static uint8_t sbytes[SB_CAP];
static uint32_t smeta[SM_CAP * 3];
static uint32_t nsb, nstrs, ntmp;
static cd_event_rec inbox;
static uintptr_t layout[3]; /* uintptr_t is u32 in wasm; natively the sink reads real pointers */

/* Decodes the pending strings in JS. */
static void send_strings(void) {
  if (nstrs) j_strings(nstrs, nsb);
  nsb = nstrs = ntmp = 0;
}

/* ---- strings ---- */
typedef struct { uint32_t off, len, u16, hash; } str_ent;
static uint8_t *pool;
static uint32_t pool_len;
static str_ent *strs;
static uint32_t nstr_ids = 1; /* 0 is unused */
static uint8_t *sent;         /* JS holds the decoded string */
static uint32_t *htab, hcap;

static uint32_t hash_bytes(const char *s, uint32_t len) {
  uint32_t h = 2166136261u;
  for (uint32_t i = 0; i < len; i++) h = (h ^ (uint8_t)s[i]) * 16777619u;
  return h | 1;
}

static void htab_put(uint32_t h, uint32_t id) {
  uint32_t m = hcap - 1, i = h & m;
  while (htab[i]) i = (i + 1) & m;
  htab[i] = id;
}

static uint32_t push_str(cd_str s, uint32_t dest);
cd_str cd_intern(const char *s, uint32_t len) {
  uint32_t h = hash_bytes(s, len);
  if (hcap) {
    uint32_t m = hcap - 1;
    for (uint32_t i = h & m, id; (id = htab[i]); i = (i + 1) & m) {
      str_ent *e = &strs[id];
      if (e->hash == h && e->len == len && !__builtin_memcmp(pool + e->off, s, len)) return id;
    }
  }
  if (nstr_ids * 2 >= hcap) { /* grow and rehash */
    uint32_t ncap = hcap ? hcap * 2 : 1024;
    cd_free(htab);
    htab = cd_alloc(ncap * 4);
    __builtin_memset(htab, 0, ncap * 4);
    hcap = ncap;
    for (uint32_t id = 1; id < nstr_ids; id++) htab_put(strs[id].hash, id);
  }
  uint32_t id = nstr_ids++;
  strs = cd_realloc(strs, nstr_ids * sizeof(str_ent));
  sent = cd_realloc(sent, nstr_ids);
  sent[id] = 0;
  pool = cd_realloc(pool, pool_len + len);
  __builtin_memcpy(pool + pool_len, s, len);
  uint32_t u16 = 0;
  for (uint32_t i = 0; i < len; i++) {
    uint8_t c = (uint8_t)s[i];
    u16 += (c & 0xc0) != 0x80;
    u16 += c >= 0xf0; /* astral: a surrogate pair */
  }
  strs[id] = (str_ent){pool_len, len, u16, h};
  pool_len += len;
  htab_put(h, id);
#if CDOM_INTERN_TEXT
  /* Queued now, not at first use: strings are interned in bursts (a list's labels) and then
   * used one call each, so they decode together in the call before the first use. */
  sent[id] = 1;
  push_str(id, id);
#endif
  return id;
}

cd_str cd_internz(const char *s) {
  uint32_t n = 0;
  while (s[n]) n++;
  return cd_intern(s, n);
}

const char *cd_bytes(cd_str s, uint32_t *len) {
  *len = strs[s].len;
  return (const char *)pool + strs[s].off;
}

static uint32_t push_str(cd_str s, uint32_t dest) {
  str_ent *e = &strs[s];
  if (e->len > SB_CAP) __builtin_trap();
  if (nsb + e->len > SB_CAP || nstrs + 1 > SM_CAP) send_strings();
  uint32_t *m = &smeta[nstrs++ * 3];
  m[0] = dest;
  m[1] = e->len;
  m[2] = e->u16;
  __builtin_memcpy(sbytes + nsb, pool + e->off, e->len);
  nsb += e->len;
  return dest;
}

/* A name (tag, attribute): always cached in JS. */
static uint32_t ref_name(cd_str s) {
  if (!sent[s]) {
    sent[s] = 1;
    push_str(s, s);
  }
  return s;
}

/* Content: cached or per-op, by build flag. */
static uint32_t ref_text(cd_str s) {
#if CDOM_INTERN_TEXT
  return ref_name(s);
#else
  /* A per-op copy lands in the JS string table just above the interned ids, in a slot an id
   * interned later may take over: it is read only by the call its batch is sent for, and every
   * op sends its own batch (an interned string is queued for JS when interned or first used). */
  return push_str(s, nstr_ids + ntmp++);
#endif
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
/* Strings an op takes are queued by ref_*; this sends them, and any held walk, before the call. */
static inline void ready(void) { walk_done(); send_strings(); }

void cd_flush(void) { ready(); }

static cd_id at_parent = CD_DETACHED - 1, at_before = CD_DETACHED - 1;

void cd_at(cd_id parent, cd_id before) {
  if (parent == at_parent && before == at_before) return;
  ready(); j_at(parent, before);
  at_parent = parent;
  at_before = before;
}
void cd_open(cd_str tag) { uint32_t t = ref_name(tag); ready(); j_open(t); }
void cd_close(void) { ready(); j_close(); }
void cd_text(cd_str s) { uint32_t v = ref_text(s); ready(); j_text(v); }
void cd_text_num(uint32_t n) { ready(); j_text_num(n); }
void cd_attr(cd_str name, cd_str v) { uint32_t a = ref_name(name), b = ref_text(v); ready(); j_attr(a, b); }
void cd_class(cd_str v) { uint32_t a = ref_text(v); ready(); j_class(a); }
void cd_name(cd_id id) { ready(); j_name(id); }
/* Events: which ids listen to what is kept here, not in JS. The shim has one delegated
 * listener per event type or name at the mount, installed by the first listen of it; walking
 * up from the target it asks cd_wants of each node with an id, and posts the first that does. */
static void listen_kind(cd_id id, uint32_t kind, cd_str name);
void cd_listen(cd_id id, uint32_t ev) {
  if (ev >= CD_EV_CUSTOM) __builtin_trap();
  listen_kind(id, ev, 0);
}
void cd_listen_named(cd_id id, cd_str ev) { listen_kind(id, CD_EV_CUSTOM, ev); }
void cd_value(cd_str s) {
  uint32_t v = ref_text(s);
  send_strings();
  if (walking) { walking = 0; j_walk_value(walk_from, walk_steps, walk_n, v); } else j_value(v);
}
void cd_value_num(uint32_t n) {
  send_strings();
  if (walking) { walking = 0; j_walk_value_num(walk_from, walk_steps, walk_n, n); } else j_value_num(n);
}
/* Template slots: a slot is a text node of the template, named by an id of its own when it is
 * marked; cd_tpl_text sets it like any named text node. What each slot holds now (kind 0
 * unknown, 1 string id, 2 number) is kept so an unchanged value is not written again. */
static cd_id slot_new[CD_SLOTS], slot_id[CD_TPLS][CD_SLOTS]; /* the template being built's; each saved one's */
static uint32_t slot_val[CD_TPLS][CD_SLOTS];
static uint8_t slot_kind[CD_TPLS][CD_SLOTS];
void cd_tpl_save(uint32_t t) {
  int slots = 0;
  for (uint32_t k = 0; k < CD_SLOTS; k++) slots |= slot_new[k] != 0;
  if (slots) { /* a template with slots must have a number under CD_TPLS */
    if (t >= CD_TPLS) __builtin_trap();
    for (uint32_t k = 0; k < CD_SLOTS; k++)
      if (slot_id[t][k]) cd_id_free(slot_id[t][k]); /* t is built again: its old slots go */
  }
  if (t < CD_TPLS) {
    __builtin_memcpy(slot_id[t], slot_new, sizeof slot_new);
    __builtin_memset(slot_kind[t], 0, CD_SLOTS);
  }
  __builtin_memset(slot_new, 0, sizeof slot_new);
  /* a template is built off the document (cd_at(CD_DETACHED, 0) before its root), so its root
   * was never placed and the shim has nothing to take out */
  if (at_parent != CD_DETACHED && at_parent != CD_DETACHED - 1) __builtin_trap();
  ready(); j_tpl_save(t);
}
void cd_clone(uint32_t t) { ready(); j_clone(t); }
void cd_slot(uint32_t k) {
  if (k >= CD_SLOTS) __builtin_trap();
  if (!slot_new[k]) slot_new[k] = cd_id_new();
  ready(); j_name(slot_new[k]);
}
static cd_id slot_same(uint32_t t, uint32_t k, uint8_t kind, uint32_t v) {
  if (t >= CD_TPLS || k >= CD_SLOTS || !slot_id[t][k]) __builtin_trap(); /* not a slot of t */
  if (slot_kind[t][k] == kind && slot_val[t][k] == v) return 0;
  slot_kind[t][k] = kind;
  slot_val[t][k] = v;
  return slot_id[t][k];
}
void cd_tpl_text(uint32_t t, uint32_t k, cd_str s) {
  cd_id id = slot_same(t, k, 1, s);
  if (id) { uint32_t v = ref_text(s); ready(); j_set_text(id, v); }
}
void cd_tpl_text_num(uint32_t t, uint32_t k, uint32_t n) {
  cd_id id = slot_same(t, k, 2, n);
  if (id) { ready(); j_set_text_num(id, n); }
}
void cd_down(void) { walk_step(1); }
void cd_next(void) { walk_step(2); }
void cd_up(void) { walk_step(3); }
void cd_sel(cd_id id) { walk_done(); walking = 1; walk_from = id; walk_steps = walk_n = 0; }
void cd_set_text(cd_id id, cd_str s) { uint32_t v = ref_text(s); ready(); j_set_text(id, v); }
void cd_set_text_num(cd_id id, uint32_t n) { ready(); j_set_text_num(id, n); }
void cd_set_attr(cd_id id, cd_str name, cd_str v) { uint32_t a = ref_name(name), b = ref_text(v); ready(); j_set_attr(id, a, b); }
void cd_set_class(cd_id id, cd_str v) { uint32_t a = ref_text(v); ready(); j_set_class(id, a); }
void cd_move(cd_id id, cd_id before) { ready(); j_move(id, before); }
void cd_clear(cd_id id) { ready(); j_clear(id); }
void cd_detach(cd_id id) { ready(); j_detach(id); }
void cd_attach(cd_id id) { ready(); j_attach(id); }

/* ---- host: a widget op with a cdv value (format in cdom.h). The value is built in hv with
 * raw string ids; the builders check its shape as it grows (a stack of the values still owed
 * per level), and cd_host_end turns its string ids into sent ones in place, sends the strings
 * and passes the value's address and length to the host import. ---- */
#define HV_CAP (1u << 15)
static uint32_t hv[HV_CAP], nhv, hv_open, hv_bytes, hv_strs;
static uint32_t owed[CD_V_DEPTH + 1], in_obj[CD_V_DEPTH + 1], depth, keyed;
static inline uint32_t slen(cd_str s) { return strs[s].len; }

void cd_host(cd_id id, uint32_t kind, cd_str name) {
  if (hv_open) __builtin_trap(); /* the previous host op was never ended */
  hv_open = 1;
  hv[0] = id;
  hv[1] = kind;
  hv[2] = name;
  nhv = 3;
  hv_bytes = slen(name);
  hv_strs = 1;
  owed[0] = 1;
  in_obj[0] = 0;
  depth = keyed = 0;
}

static void hv_put(uint32_t w) {
  if (nhv >= HV_CAP) __builtin_trap(); /* larger than one host op may be */
  hv[nhv++] = w;
}
static void hv_str(cd_str s) {
  hv_put(s);
  hv_bytes += slen(s);
  hv_strs++;
}
/* a value starts: one must be owed here, and inside an object its key must come first */
static void v_begin(uint32_t tag) {
  if (!hv_open || !owed[depth] || in_obj[depth] != keyed) __builtin_trap();
  owed[depth]--;
  keyed = 0;
  hv_put(tag);
}
/* a value is complete: close every level that owes nothing more */
static void v_done(void) {
  while (depth && !owed[depth]) depth--;
}
static void v_open(uint32_t tag, uint32_t n) {
  v_begin(tag);
  hv_put(n);
  if (depth == CD_V_DEPTH) __builtin_trap();
  depth++;
  owed[depth] = n;
  in_obj[depth] = tag == CD_V_OBJECT;
  v_done();
}

void cd_v_null(void) { v_begin(CD_V_NULL); v_done(); }
void cd_v_bool(int b) { v_begin(b ? CD_V_TRUE : CD_V_FALSE); v_done(); }
void cd_v_u32(uint32_t v) { v_begin(CD_V_U32); hv_put(v); v_done(); }
void cd_v_i32(int32_t v) { v_begin(CD_V_I32); hv_put((uint32_t)v); v_done(); }
static void hv_f64(double v) {
  uint64_t b;
  __builtin_memcpy(&b, &v, 8);
  hv_put((uint32_t)b);
  hv_put((uint32_t)(b >> 32));
}
void cd_v_f64(double v) { v_begin(CD_V_F64); hv_f64(v); v_done(); }
void cd_v_str(cd_str s) { v_begin(CD_V_STR); hv_str(s); v_done(); }
void cd_v_f64s(const double *p, uint32_t n) {
  v_begin(CD_V_F64S);
  hv_put(n);
  for (uint32_t i = 0; i < n; i++) hv_f64(p[i]);
  v_done();
}
void cd_v_arr(uint32_t n) { v_open(CD_V_ARRAY, n); }
void cd_v_obj(uint32_t n) { v_open(CD_V_OBJECT, n); }
void cd_v_key(cd_str k) {
  if (!hv_open || !in_obj[depth] || keyed || !owed[depth]) __builtin_trap();
  keyed = 1;
  hv_str(k);
}

void cd_host_end(void) {
  if (!hv_open || depth || owed[0]) __builtin_trap(); /* not exactly one complete value */
  hv_open = 0;
  /* The value's strings must cross in one batch: a per-op (transient) id is reused after the
   * batch it came in. The queue is empty here when strings are per-op (every op sends its own
   * before its call), and holds only interned strings, which outlive any batch, otherwise. */
  if (hv_bytes > SB_CAP || hv_strs > SM_CAP) __builtin_trap();
  hv[2] = ref_name(hv[2]);
  /* walk the (well-formed) tree, sending each string: keys as names, STR values as text */
  uint32_t left[CD_V_DEPTH + 1], key[CD_V_DEPTH + 1], d = 0, i = 3;
  left[0] = 1;
  key[0] = 0;
  while (i < nhv) {
    while (!left[d]) d--;
    left[d]--;
    if (key[d]) { hv[i] = ref_name(hv[i]); i++; }
    uint32_t t = hv[i++];
    if (t == CD_V_U32 || t == CD_V_I32) i++;
    else if (t == CD_V_F64) i += 2;
    else if (t == CD_V_STR) { hv[i] = ref_text(hv[i]); i++; }
    else if (t == CD_V_F64S) i += 1 + 2 * hv[i];
    else if (t == CD_V_ARRAY || t == CD_V_OBJECT) {
      left[++d] = hv[i++];
      key[d] = t == CD_V_OBJECT;
    }
  }
  ready();
  j_host(hv[0], hv[1], hv[2], hv + 3, nhv - 3);
}

/* ---- node ids ---- */
static cd_id next_id = 1;
static cd_id *free_ids;
static uint32_t nfree;
static uint32_t *tags, tags_cap;
/* per id: the built-in event types it listens to (bit per CD_EV_*; bit CD_EV_CUSTOM: it
 * listens to some named event, held in the pairs below) */
static uint16_t *lmask;
typedef struct { cd_id id; cd_str name; } named_ev;
static named_ev *named;           /* (id, name) of every named listen */
static uint32_t nnamed, named_cap;
static uint32_t delegated;        /* built-in types with a listener at the mount */
static cd_str *dnames;            /* names with a listener at the mount */
static uint32_t ndnames;

cd_id cd_id_new(void) {
  if (nfree) return free_ids[--nfree];
  cd_id id = next_id++;
  if (id >= tags_cap) {
    tags_cap = tags_cap ? tags_cap * 2 : 4096;
    tags = cd_realloc(tags, tags_cap * 4);
    free_ids = cd_realloc(free_ids, tags_cap * 4);
    lmask = cd_realloc(lmask, tags_cap * 2);
  }
  lmask[id] = 0;
  return id;
}

static void release(cd_id id) {
  tags[id] = 0;
  if (lmask[id] >> CD_EV_CUSTOM & 1)
    for (uint32_t i = nnamed; i-- > 0;)
      if (named[i].id == id) named[i] = named[--nnamed];
  lmask[id] = 0;
  free_ids[nfree++] = id;
}

static void listen_kind(cd_id id, uint32_t kind, cd_str name) {
  int first;
  if (kind < CD_EV_CUSTOM) {
    static const char *const NAMES[] = {"click", "dblclick", "input", "change", "keydown", "keyup", "focusin",
                                        "focusout", "submit", "pointerdown", "pointerup", "pointermove"};
    first = !(delegated >> kind & 1);
    delegated |= 1u << kind;
    if (first) name = cd_internz(NAMES[kind]);
  } else {
    first = 1;
    for (uint32_t i = 0; i < ndnames && first; i++) first = dnames[i] != name;
    if (first) {
      dnames = cd_realloc(dnames, (ndnames + 1) * 4);
      dnames[ndnames++] = name;
    }
    uint32_t i = 0;
    while (i < nnamed && !(named[i].id == id && named[i].name == name)) i++;
    if (i == nnamed) {
      if (nnamed == named_cap) named = cd_realloc(named, (named_cap = named_cap ? named_cap * 2 : 16) * sizeof(named_ev));
      named[nnamed++] = (named_ev){id, name};
    }
  }
  lmask[id] |= (uint16_t)(1u << kind);
  /* named events are caught in the capture phase, so ones that do not bubble arrive too */
  if (first) { uint32_t n = ref_name(name); ready(); j_delegate(kind, n, kind == CD_EV_CUSTOM); }
  ready(); j_listen(id);
}

/* Does the node named id take events of this kind (and, for CD_EV_CUSTOM, this name)? */
EXPORT("cd_wants") int cd_wants(cd_id id, uint32_t kind, cd_str name) {
  if (id >= tags_cap || kind > CD_EV_CUSTOM || !(lmask[id] >> kind & 1)) return 0;
  if (kind < CD_EV_CUSTOM) return 1;
  for (uint32_t i = 0; i < nnamed; i++)
    if (named[i].id == id && named[i].name == name) return 1;
  return 0;
}

void cd_id_free(cd_id id) {
  ready(); j_free(id);
  release(id);
}

void cd_remove(cd_id id) {
  ready(); j_remove(id);
  release(id);
}

void cd_tag_set(cd_id id, uint32_t tag) { tags[id] = tag; }
uint32_t cd_tag(cd_id id) { return id < tags_cap ? tags[id] : 0; }

uint32_t cd_input_value(cd_id id, char *dst, uint32_t cap) { ready(); return js_input(id, dst, cap); }

/* ---- entry points ---- */
EXPORT("cd_layout") uintptr_t *cd_layout(void) {
  layout[0] = (uintptr_t)sbytes;
  layout[1] = (uintptr_t)smeta;
  layout[2] = (uintptr_t)&inbox;
  return layout;
}

uint32_t cd_seed;
EXPORT("cd_start") void cd_start(uint32_t seed) {
  cd_seed = seed;
  cd_main();
  cd_flush();
}

__attribute__((weak)) uint32_t cd_state_save(uint8_t *dst, uint32_t cap) { (void)dst; (void)cap; return 0; }
__attribute__((weak)) int cd_state_load(const uint8_t *src, uint32_t len) { (void)src; return len != 0; }

/* Hydration: the shim copies the page's state blob, as the base64 text the page holds, into
 * cd_state_buf; cd_start_state decodes it in place, loads it and renders the first frame over
 * the server's DOM. */
static uint8_t *state_buf;
EXPORT("cd_state_buf") uint8_t *cd_state_buf(uint32_t len) {
  cd_free(state_buf);
  return state_buf = cd_alloc(len);
}
/* Standard base64 with padding, decoded in place; the decoded length, or -1 when s is not
 * base64 (a length not a multiple of 4, a byte outside the alphabet, padding not at the end). */
static int32_t unbase64(uint8_t *s, uint32_t len) {
  if (len & 3) return -1;
  uint32_t o = 0;
  for (uint32_t i = 0; i < len; i += 4) {
    uint32_t v = 0, pad = 0;
    for (uint32_t k = 0; k < 4; k++) {
      uint8_t c = s[i + k];
      int32_t d = c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52
                : c == '+' ? 62 : c == '/' ? 63 : -1;
      /* '=' only in the last group's last place, or its last two */
      if (c == '=' && k >= 2 && i + 4 == len && (k == 3 || s[i + 3] == '=')) { pad++; d = 0; }
      else if (d < 0) return -1;
      v = v << 6 | (uint32_t)d;
    }
    s[o++] = (uint8_t)(v >> 16);
    if (pad < 2) s[o++] = (uint8_t)(v >> 8);
    if (pad < 1) s[o++] = (uint8_t)v;
  }
  return (int32_t)o;
}

EXPORT("cd_start_state") int cd_start_state(uint32_t len) {
  int32_t n = unbase64(state_buf, len);
  int bad = n < 0 ? -1 : cd_state_load(state_buf, (uint32_t)n);
  cd_free(state_buf);
  state_buf = 0;
  if (bad) return bad;
  cd_main();
  cd_flush();
  return 0;
}

EXPORT("cd_event") void cd_event(void) {
  cd_on_event(&inbox);
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

static void drop(cd_list *L, cd_item *it, const cd_list_fns *f, int dom) {
  if (dom) cd_remove(it->node); else cd_id_free(it->node);
  f->destroy(f->ctx, it);
  (void)L;
}

void cd_list_update(cd_list *L, const uint32_t *keys, uint32_t n, const cd_list_fns *f) {
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
      cd_move(old[os].node, ne < n ? nw[ne].node : 0); /* a swap of the two ends */
      cd_move(old[oe - 1].node, old[os + 1].node);
      nw[--ne] = old[os++];
      f->patch(f->ctx, ne, &nw[ne]);
      nw[ns] = old[--oe];
      f->patch(f->ctx, ns, &nw[ns]);
      ns++;
      continue;
    }
    if (head_to_tail && old[os + 1].key == keys[ns]) {
      cd_move(old[os].node, ne < n ? nw[ne].node : 0);
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
    cd_id before = ne < n ? nw[ne].node : 0;
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
        cd_at(L->parent, 0);
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
        cd_id before = pos + 1 < n ? nw[pos + 1].node : 0;
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
}
