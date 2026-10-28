/* cdom_html: the native sink (see cdom_html.h). Mirrors shim/cdom.js call for call. */
#include "cdom_html.h"
#include "cdom.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uintptr_t *cd_layout(void); /* cdom.c: string bytes, string meta, inbox */

#define DETACHED 0xffffffffu

static void fail(const char *what, const char *op) {
  fprintf(stderr, "cdom_html: %s (%s)\n", what, op);
  abort();
}

/* ---- arena: every node and value of one render ---- */
typedef struct chunk { struct chunk *next; size_t used, cap; } chunk;
static chunk *arena;

static void *ar(size_t n) {
  n = (n + 15) & ~(size_t)15;
  if (!arena || arena->used + n > arena->cap) {
    size_t cap = n > ((size_t)1 << 20) ? n : (size_t)1 << 20;
    chunk *c = malloc(sizeof(chunk) + 16 + cap);
    if (!c) fail("out of memory", "");
    c->next = arena;
    c->used = 0;
    c->cap = cap;
    arena = c;
  }
  void *p = (char *)(arena + 1) + 16 + arena->used;
  arena->used += n;
  return p;
}

static char *ar_copy(const char *s, uint32_t n) {
  char *d = ar(n ? n : 1);
  memcpy(d, s, n);
  return d;
}

/* ---- the tree ---- */
typedef struct { const char *n; uint32_t nlen; const char *v; uint32_t vlen; } hattr;
typedef struct hnode hnode;
struct hnode {
  hnode *parent, *first, *last, *prev, *next;
  hnode *dparent, *dnext; /* DETACH remembers the place */
  const char *tag;        /* element name bytes; 0 for a text node */
  uint32_t tlen;
  const char *text; /* text node value */
  uint32_t len;
  hattr *attrs;
  uint32_t nattr, cattr;
};

typedef struct { const char *p; uint32_t len; } hstr;
static hstr *strs; /* copies by id (an id above the interned ones is a per-op string's) */
static uint32_t strs_cap;
static hnode **ids, **tpl, **stack;
static uint32_t ids_cap, tpl_cap, stack_cap, sp;
static hnode *last, *root;
static uint32_t base = DETACHED, ref;
static uint64_t applied; /* DOM calls since the last reset */

static void *grow_array(void *a, uint32_t *cap, uint32_t need, size_t el) {
  if (need < *cap) return a;
  uint32_t n = *cap ? *cap : 64;
  while (n <= need) n *= 2;
  a = realloc(a, n * el);
  if (!a) fail("out of memory", "");
  memset((char *)a + *cap * el, 0, (n - *cap) * el);
  *cap = n;
  return a;
}

static hstr S(uint32_t s) {
  if (s >= strs_cap || !strs[s].p) fail("unknown string", "not sent before the call");
  return strs[s];
}

static hnode *id(uint32_t i, const char *op) {
  if (i >= ids_cap || !ids[i]) fail("no node with that id", op);
  return ids[i];
}

static hnode *new_elem(hstr tag) {
  hnode *n = ar(sizeof(hnode));
  memset(n, 0, sizeof *n);
  n->tag = tag.p;
  n->tlen = tag.len;
  return n;
}

static hnode *new_text(const char *s, uint32_t len) {
  hnode *n = ar(sizeof(hnode));
  memset(n, 0, sizeof *n);
  n->text = ar_copy(s, len);
  n->len = len;
  return n;
}

static uint32_t utoa(char *d, uint32_t v) {
  char t[10];
  uint32_t n = 0, k = 0;
  do t[n++] = (char)('0' + v % 10); while (v /= 10);
  while (n) d[k++] = t[--n];
  return k;
}

static void remove_attr(hnode *n, const char *name, uint32_t nlen);

static void unlink_node(hnode *n) {
  hnode *p = n->parent;
  if (!p) return;
  if (n->prev) n->prev->next = n->next; else p->first = n->next;
  if (n->next) n->next->prev = n->prev; else p->last = n->prev;
  n->parent = n->prev = n->next = 0;
}

/* parent.insertBefore(n, before), with the DOM's checks */
static void insert(hnode *p, hnode *n, hnode *before, const char *op) {
  if (!p || !p->tag) fail("insert under a missing node or a text node", op);
  for (hnode *a = p; a; a = a->parent)
    if (a == n) fail("insert a node into itself", op);
  if (before == n) before = n->next;
  if (before && before->parent != p) fail("insert before a node that is not a child", op);
  unlink_node(n);
  n->parent = p;
  n->next = before;
  n->prev = before ? before->prev : p->last;
  if (n->prev) n->prev->next = n; else p->first = n;
  if (before) before->prev = n; else p->last = n;
}

static int name_eq(const char *a, uint32_t al, const char *b, uint32_t bl) {
  if (al != bl) return 0;
  for (uint32_t i = 0; i < al; i++) {
    char x = a[i], y = b[i];
    if (x >= 'A' && x <= 'Z') x += 32;
    if (y >= 'A' && y <= 'Z') y += 32;
    if (x != y) return 0;
  }
  return 1;
}

static void set_attr(hnode *n, const char *name, uint32_t nlen, hstr v, const char *op) {
  if (!n || !n->tag) fail("attribute on a missing node or a text node", op);
  for (uint32_t i = 0; i < n->nattr; i++)
    if (name_eq(n->attrs[i].n, n->attrs[i].nlen, name, nlen)) {
      n->attrs[i].v = ar_copy(v.p, v.len);
      n->attrs[i].vlen = v.len;
      return;
    }
  if (n->nattr == n->cattr) {
    uint32_t c = n->cattr ? n->cattr * 2 : 4;
    hattr *a = ar(c * sizeof(hattr));
    if (n->nattr) memcpy(a, n->attrs, n->nattr * sizeof(hattr));
    n->attrs = a;
    n->cattr = c;
  }
  n->attrs[n->nattr++] = (hattr){name, nlen, ar_copy(v.p, v.len), v.len};
}

static void set_value(hnode *n, const char *s, uint32_t len, const char *op) { /* nodeValue = */
  if (!n) fail("value of a missing node", op);
  if (n->tag) return; /* nodeValue on an element does nothing */
  n->text = ar_copy(s, len);
  n->len = len;
}

static hnode *clone(const hnode *s) {
  hnode *n = ar(sizeof(hnode));
  memset(n, 0, sizeof *n);
  n->tag = s->tag;
  n->tlen = s->tlen;
  n->text = s->text; /* values are never written in place, so sharing is safe */
  n->len = s->len;
  if (s->nattr) {
    n->attrs = ar(s->nattr * sizeof(hattr));
    memcpy(n->attrs, s->attrs, s->nattr * sizeof(hattr));
    n->nattr = n->cattr = s->nattr;
  }
  for (const hnode *c = s->first; c; c = c->next) insert(n, clone(c), 0, "clone");
  return n;
}

static void place(hnode *n, const char *op) {
  if (sp) insert(stack[sp - 1], n, 0, op);
  else if (base != DETACHED) insert(id(base, op), n, ref ? id(ref, op) : 0, op);
}

static void push(hnode *n) {
  stack = grow_array(stack, &stack_cap, sp, sizeof(hnode *));
  stack[sp++] = n;
}

static void name(uint32_t i, hnode *n) {
  ids = grow_array(ids, &ids_cap, i, sizeof(hnode *));
  ids[i] = n;
}

static void host_op(uint32_t i, uint32_t kind, uint32_t nm, const uint32_t *a, uint32_t nargs);
static void host_reset(void);
static uint64_t dev_count;
static char dev_last[256];

void cd_html_reset(void) {
  while (arena) {
    chunk *c = arena->next;
    free(arena);
    arena = c;
  }
  if (ids_cap) memset(ids, 0, ids_cap * sizeof(hnode *));
  if (tpl_cap) memset(tpl, 0, tpl_cap * sizeof(hnode *));
  sp = 0;
  last = root = 0;
  base = DETACHED;
  ref = 0;
  applied = 0;
  dev_count = 0;
  dev_last[0] = 0;
  host_reset();
  name(0, new_elem((hstr){"div", 3}));
}

uint64_t cd_html_ops(void) { return applied; }
uint64_t cd_html_dev_count(void) { return dev_count; }
const char *cd_html_dev_last(void) { return dev_last; }
void j_dev_warn(const char *msg, uint32_t len); /* cdom.c's import in a CDOM_DEV build */
void j_dev_warn(const char *msg, uint32_t len) {
  fprintf(stderr, "cdom dev: %.*s\n", (int)len, msg);
  uint32_t n = len < sizeof dev_last - 1 ? len : (uint32_t)sizeof dev_last - 1;
  memcpy(dev_last, msg, n);
  dev_last[n] = 0;
  dev_count++;
}

/* ---- the imports: cdom.c calls each one as it emits the op (see the IMPORTS list there) ---- */

/* every DOM call is counted, as the shim would make it; strings and input are transport */
static void op(void) {
  if (!arena) cd_html_reset();
  applied++;
}

void j_strings(uint32_t nstrs, uint32_t nbytes) { /* each is copied to its id */
  uintptr_t *L = cd_layout();
  const uint8_t *sb = (const uint8_t *)L[0];
  const uint32_t *sm = (const uint32_t *)L[1];
  if (!arena) cd_html_reset();
  for (uint32_t i = 0, off = 0; i < nstrs; i++) {
    uint32_t dest = sm[i * 3], len = sm[i * 3 + 1];
    const char *p = (const char *)sb + off;
    off += len;
    if (off > nbytes) fail("string bytes overrun", "strings");
    strs = grow_array(strs, &strs_cap, dest, sizeof(hstr));
    char *c = malloc(len ? len : 1);
    if (!c) fail("out of memory", "strings");
    memcpy(c, p, len);
    free((void *)strs[dest].p);
    strs[dest] = (hstr){c, len};
  }
}

static char num[10];
void j_at(cd_id p, cd_id b) { op(); base = p; ref = b; }
void j_open(uint32_t t) {
  op();
  hnode *el = new_elem(S(t));
  if (sp) insert(stack[sp - 1], el, 0, "open");
  push(el);
  last = el;
}
void j_close(void) {
  op();
  if (!sp) fail("close without open", "close");
  hnode *el = stack[--sp];
  if (!sp) { root = el; place(el, "close"); }
}
void j_text(uint32_t s) { op(); hstr v = S(s); last = new_text(v.p, v.len); place(last, "text"); }
void j_text_num(uint32_t n) { op(); last = new_text(num, utoa(num, n)); place(last, "text_num"); }
void j_attr(uint32_t a, uint32_t v) { op(); hstr nm = S(a); set_attr(last, nm.p, nm.len, S(v), "attr"); }
void j_class(uint32_t v) { op(); set_attr(last, "class", 5, S(v), "class"); }
void j_name(cd_id i) { op(); name(i, last); }
void j_listen(cd_id i) { op(); name(i, last); }
/* the mount's listener: the page's resume call installs the same ones (cd_html_resume_events) */
static struct { hstr name; uint32_t capture; } evs[64];
static uint32_t nevs;
void j_delegate(uint32_t kind, uint32_t ev, uint32_t capture) {
  op();
  (void)kind;
  hstr s = S(ev);
  if (!s.len) fail("an empty event name", "delegate");
  for (uint32_t i = 0; i < s.len; i++) {
    char c = s.p[i];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' || c == ':'))
      fail("an event name with a character outside [A-Za-z0-9_.:-]", "delegate");
  }
  if (nevs == 64) fail("more than 64 event types", "delegate");
  evs[nevs].name = s;
  evs[nevs++].capture = capture;
}
uint32_t cd_html_resume_events(char *dst, uint32_t cap) {
  uint32_t n = 0;
  for (uint32_t pass = 0; pass < 2; pass++) {
    if (pass && n < cap) dst[n] = ',';
    n += pass;
    if (n < cap) dst[n] = '[';
    n++;
    for (uint32_t i = 0, k = 0; i < nevs; i++) {
      if (evs[i].capture != pass) continue;
      const char *q = k++ ? ",'" : "'";
      for (; *q; q++, n++) if (n < cap) dst[n] = *q;
      for (uint32_t j = 0; j < evs[i].name.len; j++, n++) if (n < cap) dst[n] = evs[i].name.p[j];
      if (n < cap) dst[n] = '\'';
      n++;
    }
    if (n < cap) dst[n] = ']';
    n++;
  }
  return n;
}
void j_value(uint32_t s) { op(); hstr v = S(s); set_value(last, v.p, v.len, "value"); }
void j_value_num(uint32_t n) { op(); set_value(last, num, utoa(num, n), "value_num"); }
void j_tpl_save(uint32_t t) {
  op();
  if (!root) fail("template save with no closed root", "tpl_save");
  tpl = grow_array(tpl, &tpl_cap, t, sizeof(hnode *));
  tpl[t] = root;
}
void j_clone(uint32_t t) { /* at depth 0 its close places it; inside an open element it is appended, like open */
  op();
  if (t >= tpl_cap || !tpl[t]) fail("clone of a missing template", "clone");
  hnode *el = clone(tpl[t]);
  if (sp) insert(stack[sp - 1], el, 0, "clone");
  push(el);
  last = el;
}
/* n steps of 2 bits (1 firstChild, 2 nextSibling, 3 parentNode) from the node named from, or
 * from the last node when from is 0xffffffff */
static void walk(cd_id from, uint32_t steps, uint32_t n, const char *call) {
  hnode *el = from == 0xffffffffu ? last : id(from, call);
  for (; n; n--, steps >>= 2) {
    if (!el) fail("walk from nothing", call);
    uint32_t k = steps & 3;
    el = k == 1 ? el->first : k == 2 ? el->next : el->parent;
  }
  last = el;
}
void j_walk(cd_id from, uint32_t steps, uint32_t n) { op(); walk(from, steps, n, "walk"); }
void j_walk_value(cd_id from, uint32_t steps, uint32_t n, uint32_t s) {
  op(); walk(from, steps, n, "walk_value");
  hstr v = S(s); set_value(last, v.p, v.len, "walk_value");
}
void j_walk_value_num(cd_id from, uint32_t steps, uint32_t n, uint32_t v) {
  op(); walk(from, steps, n, "walk_value_num");
  set_value(last, num, utoa(num, v), "walk_value_num");
}
void j_set_text(cd_id i, uint32_t s) { op(); hstr v = S(s); set_value(id(i, "set_text"), v.p, v.len, "set_text"); }
void j_set_text_num(cd_id i, uint32_t n) { op(); set_value(id(i, "set_text_num"), num, utoa(num, n), "set_text_num"); }
void j_set_attr(cd_id i, uint32_t a, uint32_t v) { op(); hnode *el = id(i, "set_attr"); hstr nm = S(a); set_attr(el, nm.p, nm.len, S(v), "set_attr"); }
void j_set_class(cd_id i, uint32_t v) { op(); set_attr(id(i, "set_class"), "class", 5, S(v), "set_class"); }
void j_remove(cd_id i) { op(); unlink_node(id(i, "remove")); ids[i] = 0; }
void j_free(cd_id i) { op(); if (i < ids_cap) ids[i] = 0; }
void j_move(cd_id i, cd_id b) {
  op();
  hnode *el = id(i, "move");
  if (!el->parent) fail("move of a node with no parent", "move");
  insert(el->parent, el, b ? id(b, "move") : 0, "move");
}
void j_clear(cd_id i) { /* textContent = "" */
  op();
  hnode *el = id(i, "clear");
  if (!el->tag) { el->len = 0; return; }
  while (el->first) unlink_node(el->first);
}
void j_detach(cd_id i) {
  op();
  hnode *el = id(i, "detach");
  el->dparent = el->parent;
  el->dnext = el->next;
  unlink_node(el);
}
void j_attach(cd_id i) {
  op();
  hnode *el = id(i, "attach");
  insert(el->dparent, el, el->dnext, "attach");
  el->dparent = el->dnext = 0;
}
void j_host(cd_id i, uint32_t kind, uint32_t nm, const uint32_t *v, uint32_t nwords) { op(); host_op(i, kind, nm, v, nwords); }
uint32_t js_input(cd_id i, char *dst, uint32_t cap) { /* the value attribute, as an unedited input's .value */
  hnode *n = id(i, "input");
  for (uint32_t k = 0; k < n->nattr; k++)
    if (name_eq(n->attrs[k].n, n->attrs[k].nlen, "value", 5)) {
      uint32_t m = n->attrs[k].vlen < cap ? n->attrs[k].vlen : cap;
      memcpy(dst, n->attrs[k].v, m);
      return m;
    }
  return 0;
}

/* ---- writing ---- */
typedef struct { char *d; uint32_t cap, n; } wbuf;

static void put(wbuf *w, const char *s, uint32_t len) {
  if (w->n < w->cap) memcpy(w->d + w->n, s, len < w->cap - w->n ? len : w->cap - w->n);
  w->n += len;
}

static void put_lower(wbuf *w, const char *s, uint32_t len) {
  for (uint32_t i = 0; i < len; i++) {
    char c = s[i];
    if (c >= 'A' && c <= 'Z') c += 32;
    put(w, &c, 1);
  }
}

static void put_esc(wbuf *w, const char *s, uint32_t len) {
  uint32_t from = 0;
  for (uint32_t i = 0; i < len; i++) {
    const char *e;
    switch (s[i]) {
    case '&': e = "&amp;"; break;
    case '<': e = "&lt;"; break;
    case '>': e = "&gt;"; break;
    case '"': e = "&quot;"; break;
    default: continue;
    }
    put(w, s + from, i - from);
    put(w, e, (uint32_t)strlen(e));
    from = i + 1;
  }
  put(w, s + from, len - from);
}

static int is(const hnode *n, const char *t) { return name_eq(n->tag, n->tlen, t, (uint32_t)strlen(t)); }

static int is_void(const hnode *n) {
  static const char *V[] = {"br", "hr", "img", "input", "meta", "link", "area", "base", "col",
                            "embed", "source", "track", "wbr"};
  for (uint32_t i = 0; i < sizeof V / sizeof *V; i++)
    if (is(n, V[i])) return 1;
  return 0;
}

static void write_children(wbuf *w, const hnode *p);

static void write_node(wbuf *w, const hnode *n) {
  if (!n->tag) {
    const hnode *p = n->parent;
    if (p && p->tag && (is(p, "script") || is(p, "style"))) {
      for (uint32_t i = 0; i + 1 < n->len; i++)
        if (n->text[i] == '<' && n->text[i + 1] == '/') fail("\"</\" inside script or style text", "write");
      put(w, n->text, n->len);
    } else {
      put_esc(w, n->text, n->len);
    }
    return;
  }
  put(w, "<", 1);
  put_lower(w, n->tag, n->tlen);
  for (uint32_t i = 0; i < n->nattr; i++) {
    put(w, " ", 1);
    put_lower(w, n->attrs[i].n, n->attrs[i].nlen);
    put(w, "=\"", 2);
    put_esc(w, n->attrs[i].v, n->attrs[i].vlen);
    put(w, "\"", 1);
  }
  put(w, ">", 1);
  if (is_void(n)) {
    if (n->first) fail("children under a void element", "write");
    return;
  }
  if ((is(n, "pre") || is(n, "textarea") || is(n, "listing")) && n->first && !n->first->tag &&
      n->first->len && n->first->text[0] == '\n')
    put(w, "\n", 1);
  write_children(w, n);
  put(w, "</", 2);
  put_lower(w, n->tag, n->tlen);
  put(w, ">", 1);
}

/* An empty text node, which HTML cannot carry, is written as <!--e-->, and a text node right
 * after a (non-empty) text node is written after <!---->, so the parser keeps the two apart.
 * Resuming (cdom.h) reads them back: <!--e--> is the empty text node, <!----> is nothing.
 * Inside script, style, textarea and title a comment would be text, so none is written there
 * (their texts merge). */
static void write_children(wbuf *w, const hnode *p) {
  int raw = is(p, "script") || is(p, "style") || is(p, "textarea") || is(p, "title");
  for (const hnode *c = p->first; c; c = c->next) {
    if (raw || c->tag) write_node(w, c);
    else if (!c->len) put(w, "<!--e-->", 8);
    else {
      if (c->prev && !c->prev->tag && c->prev->len) put(w, "<!---->", 7);
      write_node(w, c);
    }
  }
}

uint32_t cd_html_node(cd_id i, char *dst, uint32_t cap) {
  if (!arena || i >= ids_cap || !ids[i]) return 0;
  wbuf w = {dst, cap, 0};
  write_node(&w, ids[i]);
  return w.n;
}

uint32_t cd_html_write(char *dst, uint32_t cap) {
  if (!arena) cd_html_reset();
  wbuf w = {dst, cap, 0};
  write_children(&w, ids[0]);
  return w.n;
}

/* ---- HOST: the cdv decoder and printer (format in cdom.h) ---- */
enum { V_NULL, V_FALSE, V_TRUE, V_U32, V_I32, V_F64, V_STR, V_F64S, V_ARRAY, V_OBJECT };

static void remove_attr(hnode *n, const char *name, uint32_t nlen) {
  for (uint32_t i = 0; i < n->nattr; i++)
    if (name_eq(n->attrs[i].n, n->attrs[i].nlen, name, nlen)) {
      memmove(n->attrs + i, n->attrs + i + 1, (n->nattr - i - 1) * sizeof(hattr));
      n->nattr--;
      return;
    }
}

/* A double as JavaScript's String(v) writes it: the shortest digits that read back as v,
 * placed by the ECMAScript Number::toString rules. */
static void put_f64(wbuf *w, double v) {
  if (v != v) { put(w, "NaN", 3); return; }
  if (v == 0) { put(w, "0", 1); return; }
  if (v < 0) { put(w, "-", 1); v = -v; }
  if (v > 1.7976931348623157e308) { put(w, "Infinity", 8); return; }
  char e[40], d[20];
  int k = 0, x = 0;
  for (int p = 1; p <= 17; p++) {
    snprintf(e, sizeof e, "%.*e", p - 1, v);
    if (strtod(e, 0) == v || p == 17) break;
  }
  for (const char *c = e; *c && *c != 'e'; c++)
    if (*c >= '0' && *c <= '9') d[k++] = *c;
  x = atoi(strchr(e, 'e') + 1);
  while (k > 1 && d[k - 1] == '0') k--;
  int n = x + 1; /* the decimal point sits after n digits */
  if (k <= n && n <= 21) {
    put(w, d, (uint32_t)k);
    for (int i = k; i < n; i++) put(w, "0", 1);
  } else if (0 < n && n <= 21) {
    put(w, d, (uint32_t)n); put(w, ".", 1); put(w, d + n, (uint32_t)(k - n));
  } else if (-6 < n && n <= 0) {
    put(w, "0.", 2);
    for (int i = n; i < 0; i++) put(w, "0", 1);
    put(w, d, (uint32_t)k);
  } else {
    put(w, d, 1);
    if (k > 1) { put(w, ".", 1); put(w, d + 1, (uint32_t)(k - 1)); }
    char t[8];
    int m = snprintf(t, sizeof t, "e%c%d", n - 1 < 0 ? '-' : '+', n - 1 < 0 ? 1 - n : n - 1);
    put(w, t, (uint32_t)m);
  }
}

static double word_f64(const uint32_t *a) {
  uint64_t b = (uint64_t)a[0] | (uint64_t)a[1] << 32;
  double v;
  memcpy(&v, &b, 8);
  return v;
}

static uint32_t need(uint32_t *q, uint32_t nargs) { /* the next word, bounds-checked */
  if (*q >= nargs) fail("value runs past its words", "host");
  return (*q)++;
}

/* Decodes one value at a[*q], advancing *q. With w, prints it: null, true, false, numbers as
 * String(number) would, "string" (with \ and " escaped), f64[...], [...], {key:value,...};
 * with js set, a string prints bare and a container prints nothing (String() of a scalar). */
static void cdv(const uint32_t *a, uint32_t *q, uint32_t nargs, wbuf *w, int js, int depth) {
  if (depth > 64) fail("value nests deeper than 64", "host");
  uint32_t t = a[need(q, nargs)], n;
  char num[16];
  switch (t) {
  case V_NULL: if (w) put(w, "null", 4); break;
  case V_FALSE: if (w) put(w, "false", 5); break;
  case V_TRUE: if (w) put(w, "true", 4); break;
  case V_U32: n = a[need(q, nargs)]; if (w) put(w, num, (uint32_t)snprintf(num, sizeof num, "%u", n)); break;
  case V_I32: n = a[need(q, nargs)]; if (w) put(w, num, (uint32_t)snprintf(num, sizeof num, "%d", (int32_t)n)); break;
  case V_F64: need(q, nargs); need(q, nargs); if (w) put_f64(w, word_f64(a + *q - 2)); break;
  case V_STR: {
    hstr s = S(a[need(q, nargs)]);
    if (!w) break;
    if (js) { put(w, s.p, s.len); break; }
    put(w, "\"", 1);
    for (uint32_t i = 0; i < s.len; i++) {
      if (s.p[i] == '"' || s.p[i] == '\\') put(w, "\\", 1);
      put(w, s.p + i, 1);
    }
    put(w, "\"", 1);
    break;
  }
  case V_F64S:
    n = a[need(q, nargs)];
    if (n > (nargs - *q) / 2) fail("value runs past its words", "host");
    if (w && !js) put(w, "f64[", 4);
    for (uint32_t i = 0; i < n; i++) {
      if (w && !js) { if (i) put(w, ",", 1); put_f64(w, word_f64(a + *q)); }
      *q += 2;
    }
    if (w && !js) put(w, "]", 1);
    break;
  case V_ARRAY: case V_OBJECT:
    n = a[need(q, nargs)];
    if (w && !js) put(w, t == V_ARRAY ? "[" : "{", 1);
    for (uint32_t i = 0; i < n; i++) {
      if (w && !js && i) put(w, ",", 1);
      if (t == V_OBJECT) {
        hstr k = S(a[need(q, nargs)]);
        if (w && !js) { put(w, k.p, k.len); put(w, ":", 1); }
      }
      cdv(a, q, nargs, js ? 0 : w, js, depth + 1);
    }
    if (w && !js) put(w, t == V_ARRAY ? "]" : "}", 1);
    break;
  default: fail("bad value tag", "host");
  }
}

static uint64_t host_count[5];
static wbuf host_last;

static void host_reset(void) { memset(host_count, 0, sizeof host_count); host_last.n = 0; }

uint64_t cd_html_host_ops(uint32_t kind) { return kind < 5 ? host_count[kind] : 0; }

uint32_t cd_html_host_last(char *dst, uint32_t cap) {
  uint32_t n = host_last.n < host_last.cap ? host_last.n : host_last.cap;
  if (cap) memcpy(dst, host_last.d, n < cap ? n : cap);
  return host_last.n;
}

static int truthy(const uint32_t *a, uint32_t at) { /* JS truthiness of a scalar value */
  switch (a[at]) {
  case V_NULL: case V_FALSE: return 0;
  case V_U32: case V_I32: return a[at + 1] != 0;
  case V_F64: { double v = word_f64(a + at + 1); return v == v && v != 0; }
  case V_STR: return S(a[at + 1]).len != 0;
  default: return 1;
  }
}

/* select.value = v: the options (also inside optgroups) whose value is v get selected */
static void select_options(hnode *p, hstr v) {
  for (hnode *o = p->first; o; o = o->next) {
    if (!o->tag) continue;
    if (is(o, "optgroup")) { select_options(o, v); continue; }
    if (!is(o, "option")) continue;
    hstr ov = {"", 0};
    int has = 0;
    for (uint32_t k = 0; k < o->nattr; k++)
      if (name_eq(o->attrs[k].n, o->attrs[k].nlen, "value", 5)) { ov = (hstr){o->attrs[k].v, o->attrs[k].vlen}; has = 1; }
    if (!has && o->first && !o->first->tag) ov = (hstr){o->first->text, o->first->len};
    if (ov.len == v.len && !memcmp(ov.p, v.p, v.len)) set_attr(o, "selected", 8, (hstr){"", 0}, "host");
    else remove_attr(o, "selected", 8);
  }
}

/* PROP on the server: the state a no-JS page should show, as markup. */
static void prop(hnode *n, hstr name, const uint32_t *a, uint32_t at, uint32_t end) {
  if (!n->tag) return;
  static const char *const BOOLS[] = {"checked", "selected", "disabled"};
  for (uint32_t i = 0; i < 3; i++)
    if (name_eq(name.p, name.len, BOOLS[i], (uint32_t)strlen(BOOLS[i]))) {
      if (truthy(a, at)) set_attr(n, BOOLS[i], (uint32_t)strlen(BOOLS[i]), (hstr){"", 0}, "host");
      else remove_attr(n, BOOLS[i], (uint32_t)strlen(BOOLS[i]));
      return;
    }
  if (!name_eq(name.p, name.len, "value", 5)) return;
  char tb[512];
  wbuf tw = {tb, sizeof tb, 0};
  uint32_t q = at;
  cdv(a, &q, end, &tw, 1, 0);
  if (tw.n > sizeof tb) fail("value property longer than 512 bytes", "host");
  hstr v = {ar_copy(tb, tw.n), tw.n};
  if (is(n, "textarea")) {
    while (n->first) unlink_node(n->first);
    if (v.len) insert(n, new_text(v.p, v.len), 0, "host");
  } else if (is(n, "select")) {
    select_options(n, v);
  } else {
    set_attr(n, "value", 5, v, "host");
  }
}

static void host_op(uint32_t node_id, uint32_t kind, uint32_t nm, const uint32_t *a, uint32_t nargs) {
  hnode *n = id(node_id, "host");
  hstr name = S(nm);
  if (kind > 4) fail("bad host kind", "host");
  host_count[kind]++;
  if (!host_last.cap) {
    host_last.cap = 1 << 16;
    host_last.d = malloc(host_last.cap);
    if (!host_last.d) fail("out of memory", "host");
  }
  uint32_t q;
  for (;;) { /* print, and print again into a larger buffer when it did not fit */
    host_last.n = 0;
    q = 0;
    char head[32];
    put(&host_last, head, (uint32_t)snprintf(head, sizeof head, "%u %u ", kind, node_id));
    put(&host_last, name.p, name.len);
    put(&host_last, " ", 1);
    cdv(a, &q, nargs, &host_last, 0, 0);
    if (host_last.n <= host_last.cap) break;
    host_last.cap = host_last.n;
    host_last.d = realloc(host_last.d, host_last.cap);
    if (!host_last.d) fail("out of memory", "host");
  }
  if (q != nargs) fail("value is not exactly the words passed", "host");
  if (kind == 3) prop(n, name, a, 0, nargs);
}
