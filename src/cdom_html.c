/* cdom_html: the native sink (see cdom_html.h). Mirrors shim/cdom.js op for op. */
#include "cdom_html.h"
#include "cdom.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uintptr_t *cd_layout(void); /* cdom.c: ops, args, string bytes, string meta, inbox */

/* The op numbers of cdom.c (its enum is private; the shim repeats them too). */
enum {
  OP_AT = 1, OP_OPEN, OP_CLOSE, OP_TEXT, OP_TEXT_NUM, OP_ATTR, OP_CLASS, OP_NAME, OP_LISTEN,
  OP_VALUE, OP_VALUE_NUM, OP_TPL_SAVE, OP_CLONE, OP_DOWN, OP_NEXT, OP_UP, OP_SET_TEXT,
  OP_SET_TEXT_NUM, OP_SET_ATTR, OP_SET_CLASS, OP_REMOVE, OP_FREE, OP_MOVE, OP_CLEAR, OP_DETACH,
  OP_ATTACH, OP_SEL
};
#define TRANSIENT 0x40000000u
#define DETACHED 0xffffffffu

static void fail(const char *what, uint32_t op) {
  fprintf(stderr, "cdom_html: %s (op %u)\n", what, op);
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
    if (!c) fail("out of memory", 0);
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
static hstr *strs, *tmp; /* strs: persistent copies by id; tmp: this batch's transients */
static uint32_t strs_cap, tmp_cap;
static hnode **ids, **tpl, **stack;
static uint32_t ids_cap, tpl_cap, stack_cap, sp;
static hnode *last, *root;
static uint32_t base = DETACHED, ref;

static void *grow_array(void *a, uint32_t *cap, uint32_t need, size_t el) {
  if (need < *cap) return a;
  uint32_t n = *cap ? *cap : 64;
  while (n <= need) n *= 2;
  a = realloc(a, n * el);
  if (!a) fail("out of memory", 0);
  memset((char *)a + *cap * el, 0, (n - *cap) * el);
  *cap = n;
  return a;
}

static hstr S(uint32_t s) {
  if (s >= TRANSIENT) {
    if (s - TRANSIENT >= tmp_cap) fail("unknown transient string", OP_TEXT);
    return tmp[s - TRANSIENT];
  }
  if (s >= strs_cap || !strs[s].p) fail("unknown string", OP_TEXT);
  return strs[s];
}

static hnode *id(uint32_t i, uint32_t op) {
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

static void unlink_node(hnode *n) {
  hnode *p = n->parent;
  if (!p) return;
  if (n->prev) n->prev->next = n->next; else p->first = n->next;
  if (n->next) n->next->prev = n->prev; else p->last = n->prev;
  n->parent = n->prev = n->next = 0;
}

/* parent.insertBefore(n, before), with the DOM's checks */
static void insert(hnode *p, hnode *n, hnode *before, uint32_t op) {
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

static void set_attr(hnode *n, const char *name, uint32_t nlen, hstr v, uint32_t op) {
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

static void set_value(hnode *n, const char *s, uint32_t len, uint32_t op) { /* nodeValue = */
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
  for (const hnode *c = s->first; c; c = c->next) insert(n, clone(c), 0, OP_CLONE);
  return n;
}

static void place(hnode *n, uint32_t op) {
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
  name(0, new_elem((hstr){"div", 3}));
}

void js_apply(uint32_t nops, uint32_t nargs, uint32_t nstrs, uint32_t nbytes) {
  uintptr_t *L = cd_layout();
  const uint8_t *ops = (const uint8_t *)L[0], *sb = (const uint8_t *)L[2];
  const uint32_t *a = (const uint32_t *)L[1], *sm = (const uint32_t *)L[3];
  if (!arena) cd_html_reset();
  /* strings: persistent ones are copied, transients point into this batch */
  for (uint32_t i = 0, off = 0; i < nstrs; i++) {
    uint32_t dest = sm[i * 3], len = sm[i * 3 + 1];
    const char *p = (const char *)sb + off;
    off += len;
    if (off > nbytes) fail("string bytes overrun", 0);
    if (dest >= TRANSIENT) {
      tmp = grow_array(tmp, &tmp_cap, dest - TRANSIENT, sizeof(hstr));
      tmp[dest - TRANSIENT] = (hstr){p, len};
    } else {
      strs = grow_array(strs, &strs_cap, dest, sizeof(hstr));
      char *c = malloc(len ? len : 1);
      if (!c) fail("out of memory", 0);
      memcpy(c, p, len);
      free((void *)strs[dest].p);
      strs[dest] = (hstr){c, len};
    }
  }
  uint32_t q = 0, n;
  char num[10];
  hnode *el;
  for (uint32_t p = 0; p < nops; p++) {
    uint32_t op = ops[p];
    switch (op) {
    case OP_AT: base = a[q++]; ref = a[q++]; break;
    case OP_OPEN:
      el = new_elem(S(a[q++]));
      if (sp) insert(stack[sp - 1], el, 0, op);
      push(el);
      last = el;
      break;
    case OP_CLOSE:
      if (!sp) fail("close without open", op);
      el = stack[--sp];
      if (!sp) { root = el; place(el, op); }
      break;
    case OP_TEXT: { hstr s = S(a[q++]); last = new_text(s.p, s.len); place(last, op); break; }
    case OP_TEXT_NUM: last = new_text(num, utoa(num, a[q++])); place(last, op); break;
    case OP_ATTR: { hstr nm = S(a[q++]); set_attr(last, nm.p, nm.len, S(a[q++]), op); break; }
    case OP_CLASS: set_attr(last, "class", 5, S(a[q++]), op); break;
    case OP_NAME: name(a[q++], last); break;
    case OP_LISTEN: name(a[q++], last); q++; break;
    case OP_VALUE: { hstr s = S(a[q++]); set_value(last, s.p, s.len, op); break; }
    case OP_VALUE_NUM: set_value(last, num, utoa(num, a[q++]), op); break;
    case OP_TPL_SAVE:
      if (!root) fail("template save with no closed root", op);
      tpl = grow_array(tpl, &tpl_cap, a[q], sizeof(hnode *));
      tpl[a[q++]] = root;
      unlink_node(root);
      break;
    case OP_CLONE: /* opened, never inserted until its CLOSE at depth 0 places it */
      n = a[q++];
      if (n >= tpl_cap || !tpl[n]) fail("clone of a missing template", op);
      el = clone(tpl[n]);
      push(el);
      last = el;
      break;
    case OP_DOWN: if (!last) fail("down from nothing", op); last = last->first; break;
    case OP_NEXT: if (!last) fail("next from nothing", op); last = last->next; break;
    case OP_UP: if (!last) fail("up from nothing", op); last = last->parent; break;
    case OP_SET_TEXT: { el = id(a[q++], op); hstr s = S(a[q++]); set_value(el, s.p, s.len, op); break; }
    case OP_SET_TEXT_NUM: el = id(a[q++], op); set_value(el, num, utoa(num, a[q++]), op); break;
    case OP_SET_ATTR: { el = id(a[q++], op); hstr nm = S(a[q++]); set_attr(el, nm.p, nm.len, S(a[q++]), op); break; }
    case OP_SET_CLASS: el = id(a[q++], op); set_attr(el, "class", 5, S(a[q++]), op); break;
    case OP_REMOVE: n = a[q++]; unlink_node(id(n, op)); ids[n] = 0; break;
    case OP_FREE: n = a[q++]; if (n < ids_cap) ids[n] = 0; break;
    case OP_MOVE:
      el = id(a[q++], op);
      n = a[q++];
      if (!el->parent) fail("move of a node with no parent", op);
      insert(el->parent, el, n ? id(n, op) : 0, op);
      break;
    case OP_CLEAR: /* textContent = "" */
      el = id(a[q++], op);
      if (!el->tag) { el->len = 0; break; }
      while (el->first) unlink_node(el->first);
      break;
    case OP_DETACH:
      el = id(a[q++], op);
      el->dparent = el->parent;
      el->dnext = el->next;
      unlink_node(el);
      break;
    case OP_ATTACH:
      el = id(a[q++], op);
      insert(el->dparent, el, el->dnext, op);
      el->dparent = el->dnext = 0;
      break;
    case OP_SEL: last = id(a[q++], op); break;
    default: fail("bad op", op);
    }
  }
  if (q != nargs) fail("argument count mismatch", 0);
  if (tmp_cap) memset(tmp, 0, tmp_cap * sizeof(hstr));
}

uint32_t js_value(cd_id i, char *dst, uint32_t cap) { /* the value attribute, as an unedited input's .value */
  hnode *n = id(i, 0);
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
        if (n->text[i] == '<' && n->text[i + 1] == '/') fail("\"</\" inside script or style text", 0);
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
    if (n->first) fail("children under a void element", 0);
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

static void write_children(wbuf *w, const hnode *p) {
  for (const hnode *c = p->first; c; c = c->next) {
    if (!c->tag && c->prev && !c->prev->tag) put(w, "<!---->", 7);
    write_node(w, c);
  }
}

uint32_t cd_html_write(char *dst, uint32_t cap) {
  if (!arena) cd_html_reset();
  wbuf w = {dst, cap, 0};
  write_children(&w, ids[0]);
  return w.n;
}
