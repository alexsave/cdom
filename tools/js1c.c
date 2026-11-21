/* js1c: a WebAssembly to JavaScript 1.0 compiler with an explicit stack (docs/js1-compile-design.md).
 *
 *   js1c [-d depth] [-a] [-s] [-q] in.wasm out.js
 *
 * in.wasm is a module already lowered by binaryen (the passes are in docs/js1-compile-design.md, section 1), so only
 * i32 and f64 values are left. out.js is JavaScript 1.0, as Netscape 2.02 runs it (docs/descent.md): functions at the
 * top level, no literals, no switch, no ===, no try, no typeof. It defines cdomFactoryJs1(imports) and sets
 * window.cdomFactory to it, as the IE 9 build does.
 *
 * -d depth: the longest chain of direct calls (default 8); -a: every function driven (none direct, for measuring);
 * -s: the data segments as one statement a word (default: a string of base 36 digits); -q: no summary on stderr.
 *
 * A function is direct (a plain JavaScript function, called directly) when it makes no indirect call, is on no cycle
 * of the call graph and has at most depth direct functions below it, inclusive; every other is driven: it never calls
 * a driven function itself, but spills its live locals onto the explicit stack, returns the callee's number to the
 * driver loop, and is entered again, with the resume flag, when the callee returns. Section 3 of the design. */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *in_name = "";
[[noreturn]] static void die(const char *fmt, ...);
static void die(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  fprintf(stderr, "js1c: %s: ", in_name);
  vfprintf(stderr, fmt, ap);
  fputc('\n', stderr);
  va_end(ap);
  exit(1);
}
static void *xalloc(size_t n) {
  void *p = calloc(1, n ? n : 1);
  if (!p) die("out of memory");
  return p;
}
static void *xgrow(void *p, size_t n) {
  p = realloc(p, n ? n : 1);
  if (!p) die("out of memory");
  return p;
}

/* Output buffers. */
typedef struct { char *p; size_t n, cap; } Buf;
static void bput(Buf *b, const char *s, size_t n) {
  if (b->n + n + 1 > b->cap) {
    b->cap = (b->n + n + 1) * 2;
    b->p = xgrow(b->p, b->cap);
  }
  memcpy(b->p + b->n, s, n);
  b->n += n;
  b->p[b->n] = 0;
}
static void bs(Buf *b, const char *s) { bput(b, s, strlen(s)); }
__attribute__((format(printf, 2, 3))) static void bf(Buf *b, const char *fmt, ...) {
  char tmp[512];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
  va_end(ap);
  if (n < 0 || (size_t)n >= sizeof tmp) die("internal: a formatted piece over 511 bytes");
  bput(b, tmp, (size_t)n);
}

/* Reading the binary. */
typedef struct { const uint8_t *p, *end; } Rd;
static uint8_t rb(Rd *r) {
  if (r->p >= r->end) die("truncated");
  return *r->p++;
}
static uint32_t ru(Rd *r) {
  uint32_t v = 0;
  for (int s = 0; s < 35; s += 7) {
    uint8_t c = rb(r);
    v |= (uint32_t)(c & 127) << s;
    if (!(c & 128)) return v;
  }
  die("a bad LEB128");
}
static int64_t rsl(Rd *r, int bits) {
  int64_t v = 0;
  int s = 0;
  uint8_t c;
  do {
    c = rb(r);
    if (s < 64) v |= (int64_t)((uint64_t)(c & 127) << s);
    s += 7;
  } while (c & 128);
  if (s < 64 && (c & 64)) v |= (int64_t)(~0ULL << s);
  (void)bits;
  return v;
}
static double rf64(Rd *r) {
  uint64_t u = 0;
  for (int i = 0; i < 8; i++) u |= (uint64_t)rb(r) << (8 * i);
  double d;
  memcpy(&d, &u, 8);
  return d;
}
static char *rname(Rd *r) {
  uint32_t n = ru(r);
  if ((size_t)(r->end - r->p) < n) die("truncated name");
  char *s = xalloc(n + 1);
  memcpy(s, r->p, n);
  r->p += n;
  return s;
}

enum { T_I32 = 0x7f, T_I64 = 0x7e, T_F32 = 0x7d, T_F64 = 0x7c, T_V128 = 0x7b, T_NONE = 0x40 };
static void check_type(uint8_t t, const char *where) {
  if (t == T_I32 || t == T_F64) return;
  if (t == T_I64) die("an i64 is left in %s: the module was not lowered (wasm-opt --i64-to-i32-lowering)", where);
  if (t == T_F32) die("an f32 in %s: js1c does not handle f32 yet", where);
  die("a value type 0x%02x in %s that js1c does not handle", t, where);
}

typedef struct { int np; uint8_t *p; uint8_t res; int canon; } FType;
typedef struct Stmt Stmt;
typedef struct {
  uint32_t type;
  int import;            /* an import: its module object's number, and its name */
  char *mod, *name;
  const uint8_t *code, *code_end;
  uint32_t nlocals;      /* params included */
  uint8_t *ltype;
  Stmt *body;
  int maxsp;             /* the temporaries the body uses */
  int *callees, ncallees; /* defined functions it calls */
  int indirect;          /* makes a call_indirect */
  int traps;             /* can trap by itself */
  int direct, height, state, maytrap, intable, exported, nsites, usesB;
} Func;
typedef struct { uint8_t ty; int mut; double f; int32_t i; } Global;
typedef struct { char *name; uint8_t kind; uint32_t idx; } Export;

static FType *types;
static uint32_t ntypes;
static Func *funcs;
static uint32_t nfuncs, nimports;
static Global *globals;
static uint32_t nglobals;
static Export *exports;
static uint32_t nexports;
static int has_memory;
static uint32_t mem_min, mem_max = 65536;
static int has_table;
static uint32_t table_min;
static int32_t *table;   /* function index or -1 */
static uint32_t table_n;
static int64_t start_fn = -1;
typedef struct { uint32_t off; const uint8_t *p; uint32_t n; } Seg;
static Seg *segs;
static uint32_t nsegs;
static char **mods;      /* import module names, one object each */
static int nmods;

static int depth_max = 8, all_driven = 0, data_stmts = 0, quiet = 0;

/* Expressions and statements. */
enum { E_CI, E_CF, E_LOC, E_TMP, E_GLB, E_UN, E_BIN, E_LOAD, E_SEL, E_MSZ };
typedef struct Expr {
  uint8_t k, ty, align;
  uint16_t op;
  int32_t i;
  uint32_t off;
  double f;
  struct Expr *a, *b, *c;
} Expr;
enum { L_BLOCK, L_LOOP, L_IF, L_FUNC };
typedef struct Label {
  uint8_t kind, rty;
  int id, slot, targeted, jsloop;
  struct Label **cross;
  int ncross;
  uint64_t *live;
} Label;
enum { S_SETL, S_SETT, S_SETG, S_STORE, S_CALL, S_CALLI, S_BLOCK, S_LOOP, S_IF, S_BR, S_BRTAB, S_RET, S_UNR, S_EXPR,
  S_FILL, S_COPY, S_GROW };
struct Stmt {
  uint8_t k, align;
  uint16_t op;
  int32_t i, j;
  uint32_t off;
  Expr *a, *b, *c;
  Expr **args;
  int nargs, rslot, site, lo, hi;
  Label *lab, **labs;
  Stmt *body, *els, *next;
  uint64_t *spill;
};

static Expr *ex_new(uint8_t k, uint8_t ty) {
  Expr *e = xalloc(sizeof *e);
  e->k = k;
  e->ty = ty;
  return e;
}
static Stmt *st_new(uint8_t k) {
  Stmt *s = xalloc(sizeof *s);
  s->k = k;
  s->rslot = -1;
  return s;
}

/* The opcodes that trap through a helper (JT set). */
static int op_traps(const Expr *e) {
  if (e->k == E_BIN && e->ty == T_I32 && (e->op == 0x6d || e->op == 0x6e || e->op == 0x6f || e->op == 0x70)) {
    if (e->b->k != E_CI || e->b->i == 0) return 1;
    return e->op == 0x6d && e->b->i == -1;
  }
  return e->k == E_UN && (e->op == 0xaa || e->op == 0xab);
}
static int ex_traps(const Expr *e) {
  if (!e) return 0;
  return op_traps(e) || ex_traps(e->a) || ex_traps(e->b) || ex_traps(e->c);
}
static int refs_local(const Expr *e, int x) {
  if (!e) return 0;
  if (e->k == E_LOC) return e->i == x;
  return refs_local(e->a, x) || refs_local(e->b, x) || refs_local(e->c, x);
}
static int refs_temp(const Expr *e, int t) {
  if (!e) return 0;
  if (e->k == E_TMP) return e->i == t;
  return refs_temp(e->a, t) || refs_temp(e->b, t) || refs_temp(e->c, t);
}
static int reads_mem(const Expr *e) {
  if (!e) return 0;
  if (e->k == E_LOAD || e->k == E_MSZ) return 1;
  return reads_mem(e->a) || reads_mem(e->b) || reads_mem(e->c);
}
static int reads_glob(const Expr *e, int g) {
  if (!e) return 0;
  if (e->k == E_GLB) return g < 0 || e->i == g;
  return reads_glob(e->a, g) || reads_glob(e->b, g) || reads_glob(e->c, g);
}

/* Building a function's tree from its code. */
typedef struct { Label *lab; Stmt *node; Stmt **tail; int height, unreach, skip, then_reach, in_else; } Ctrl;
typedef struct {
  Func *f;
  Expr **stk;
  int sp, cap;
  Ctrl *ctl;
  int nctl, capctl;
  int nlabels;
} Bld;
typedef struct { int local, temp, mem, glob; } Clob; /* glob: -1 none, -2 all */

static void append(Bld *b, Stmt *s) {
  Ctrl *c = &b->ctl[b->nctl - 1];
  *c->tail = s;
  c->tail = &s->next;
}
static Expr *temp_expr(int slot, uint8_t ty) {
  Expr *e = ex_new(E_TMP, ty);
  e->i = slot;
  return e;
}
static void note_sp(Bld *b, int n) {
  if (n > b->f->maxsp) b->f->maxsp = n;
}
static void push(Bld *b, Expr *e) {
  if (b->sp == b->cap) {
    b->cap = b->cap ? b->cap * 2 : 16;
    b->stk = xgrow(b->stk, sizeof *b->stk * (size_t)b->cap);
  }
  b->stk[b->sp++] = e;
  note_sp(b, b->sp);
}
static Expr *pop(Bld *b) {
  if (b->sp <= b->ctl[b->nctl - 1].height) die("internal: the value stack underflows");
  return b->stk[--b->sp];
}
static int stable(const Expr *e, int slot) { return e->k == E_CI || e->k == E_CF || (e->k == E_TMP && e->i == slot); }
static int in_temp(const Expr *e, int slot) { return e->k == E_TMP && e->i == slot; }
static int clobbered(const Expr *e, const Clob *c) {
  if (c->local >= 0 && refs_local(e, c->local)) return 1;
  if (c->temp >= 0 && refs_temp(e, c->temp)) return 1;
  if (c->mem && reads_mem(e)) return 1;
  if (c->glob != -1 && reads_glob(e, c->glob == -2 ? -1 : c->glob)) return 1;
  return 0;
}
/* Puts into their temporaries the stack's values that the coming statement would change (all of them with every),
 * and the values below those that read their temporaries, bottom up, so each is computed before it changes. */
static void flush(Bld *b, const Clob *c, int every) {
  int n = b->sp;
  if (!n) return;
  char *mark = xalloc((size_t)n);
  for (int i = 0; i < n; i++) mark[i] = !stable(b->stk[i], i) && (every || clobbered(b->stk[i], c));
  for (int i = n - 1; i >= 0; i--)
    if (mark[i])
      for (int h = 0; h < i; h++)
        if (!mark[h] && !stable(b->stk[h], h) && refs_temp(b->stk[h], i)) mark[h] = 1;
  for (int i = 0; i < n; i++)
    if (mark[i]) {
      Stmt *s = st_new(S_SETT);
      s->i = i;
      s->a = b->stk[i];
      append(b, s);
      b->stk[i] = temp_expr(i, b->stk[i]->ty);
    }
  free(mark);
}
static void flush_all(Bld *b) {
  Clob c = { -1, -1, 0, -1 };
  flush(b, &c, 1);
}
/* The top value into its own temporary (for a br_if or br_table's value, which stays on the stack). */
static void settle_top(Bld *b) {
  int k = b->sp - 1;
  if (stable(b->stk[k], k)) return;
  Clob c = { -1, k, 0, -1 };
  Expr *e = b->stk[k];
  b->sp--;
  flush(b, &c, 0);
  b->sp++;
  Stmt *s = st_new(S_SETT);
  s->i = k;
  s->a = e;
  append(b, s);
  b->stk[k] = temp_expr(k, e->ty);
}
static uint8_t block_type(Rd *r) {
  uint8_t t = *r->p;
  if (t == T_NONE) {
    r->p++;
    return 0;
  }
  if (t == T_I32 || t == T_I64 || t == T_F32 || t == T_F64 || t == T_V128) {
    r->p++;
    check_type(t, "a block type");
    return t;
  }
  int64_t ti = rsl(r, 33);
  if (ti < 0 || (uint64_t)ti >= ntypes) die("a bad block type");
  if (types[ti].np) die("a block with parameters (multivalue): not handled");
  return types[ti].res;
}
static Label *label_new(Bld *b, uint8_t kind, uint8_t rty, int slot) {
  Label *l = xalloc(sizeof *l);
  l->kind = kind;
  l->rty = rty;
  l->slot = slot;
  l->id = ++b->nlabels;
  return l;
}
static void push_ctrl(Bld *b, Label *l, Stmt *node, Stmt **tail) {
  if (b->nctl == b->capctl) {
    b->capctl = b->capctl ? b->capctl * 2 : 16;
    b->ctl = xgrow(b->ctl, sizeof *b->ctl * (size_t)b->capctl);
  }
  Ctrl *c = &b->ctl[b->nctl++];
  memset(c, 0, sizeof *c);
  c->lab = l;
  c->node = node;
  c->tail = tail;
  c->height = b->sp;
}
static Label *br_target(Bld *b, uint32_t depth) {
  if (depth >= (uint32_t)b->nctl) die("a branch out of the function");
  Label *l = b->ctl[b->nctl - 1 - depth].lab;
  l->targeted = 1;
  return l;
}
/* The value a branch to l carries, or NULL. */
static Expr *br_value(Bld *b, Label *l, int keep) {
  if (l->kind == L_LOOP || !l->rty) return NULL;
  if (!keep) return pop(b);
  settle_top(b);
  return b->stk[b->sp - 1];
}
static void memarg(Rd *r, uint8_t *align, uint32_t *off) {
  uint32_t a = ru(r);
  if (a & 64) die("a memory index (multi-memory): not handled");
  *align = (uint8_t)a;
  *off = ru(r);
}
static void skip_imm(Rd *r, uint8_t op) {
  switch (op) {
  case 0x0c: case 0x0d: case 0x10: case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x41: case 0xd2:
    ru(r); return;
  case 0x11: ru(r); ru(r); return;
  case 0x0e: { uint32_t n = ru(r); for (uint32_t i = 0; i <= n; i++) ru(r); return; }
  case 0x1c: { uint32_t n = ru(r); for (uint32_t i = 0; i < n; i++) rb(r); return; }
  case 0x3f: case 0x40: rb(r); return;
  case 0x42: rsl(r, 64); return;
  case 0x43: for (int i = 0; i < 4; i++) rb(r); return;
  case 0x44: for (int i = 0; i < 8; i++) rb(r); return;
  case 0xfc: {
    uint32_t sub = ru(r);
    if (sub == 10) { rb(r); rb(r); }
    else if (sub == 11) rb(r);
    else if (sub == 8) { ru(r); rb(r); }
    else if (sub == 9) ru(r);
    return;
  }
  }
  if (op >= 0x28 && op <= 0x3e) { ru(r); ru(r); return; }
}
static void add_callee(Func *f, int g) {
  for (int i = 0; i < f->ncallees; i++)
    if (f->callees[i] == g) return;
  f->callees = xgrow(f->callees, sizeof(int) * (size_t)(f->ncallees + 1));
  f->callees[f->ncallees++] = g;
}
static Expr *un(uint16_t op, uint8_t ty, Expr *a) {
  Expr *e = ex_new(E_UN, ty);
  e->op = op;
  e->a = a;
  return e;
}
static Expr *bin(uint16_t op, uint8_t ty, Expr *a, Expr *b) {
  Expr *e = ex_new(E_BIN, ty);
  e->op = op;
  e->a = a;
  e->b = b;
  return e;
}
static void end_reach(Bld *b, Ctrl *c) {
  /* After a block, the code is reached when its end was, or a branch goes to its end. */
  int reached = !c->unreach || (c->lab->kind != L_LOOP && c->lab->targeted);
  if (c->lab->kind == L_IF) reached = reached || c->then_reach || !c->in_else;
  Ctrl *p = &b->ctl[b->nctl - 1];
  if (!reached) p->unreach = 1;
}
static void build(Func *f) {
  Bld bb = { .f = f }, *b = &bb;
  Rd rd = { f->code, f->code_end }, *r = &rd;
  FType *ft = &types[f->type];
  Label *fl = label_new(b, L_FUNC, ft->res, 0);
  push_ctrl(b, fl, NULL, &f->body);
  for (;;) {
    uint8_t op = rb(r);
    Ctrl *c = &b->ctl[b->nctl - 1];
    if (c->unreach) {
      if (op == 0x02 || op == 0x03 || op == 0x04) {
        block_type(r);
        c->skip++;
        continue;
      }
      if (op == 0x05 && !c->skip) goto do_else;
      if (op == 0x0b) {
        if (c->skip) {
          c->skip--;
          continue;
        }
        goto do_end;
      }
      skip_imm(r, op);
      continue;
    }
    switch (op) {
    case 0x00: append(b, st_new(S_UNR)); f->traps = 1; c->unreach = 1; break;
    case 0x01: break;
    case 0x02: case 0x03: case 0x04: {
      uint8_t rty = block_type(r);
      Expr *cond = op == 0x04 ? pop(b) : NULL;
      flush_all(b);
      Stmt *s = st_new(op == 0x02 ? S_BLOCK : op == 0x03 ? S_LOOP : S_IF);
      Label *l = label_new(b, op == 0x02 ? L_BLOCK : op == 0x03 ? L_LOOP : L_IF, rty, b->sp);
      s->lab = l;
      s->a = cond;
      append(b, s);
      push_ctrl(b, l, s, &s->body);
      if (rty) note_sp(b, b->sp + 1);
      break;
    }
    case 0x05:
    do_else: {
      if (c->lab->kind != L_IF || c->in_else) die("an else outside an if");
      if (!c->unreach && c->lab->rty) {
        Expr *v = pop(b);
        if (!in_temp(v, c->height)) {
          Stmt *s = st_new(S_SETT);
          s->i = c->height;
          s->a = v;
          append(b, s);
        }
      }
      c->then_reach = !c->unreach;
      b->sp = c->height;
      c->unreach = 0;
      c->skip = 0;
      c->in_else = 1;
      c->tail = &c->node->els;
      break;
    }
    case 0x0b:
    do_end: {
      if (!c->unreach && c->lab->rty) {
        Expr *v = pop(b);
        if (c->lab->kind == L_FUNC) {
          Stmt *s = st_new(S_RET);
          s->a = v;
          append(b, s);
        } else if (!in_temp(v, c->height)) {
          Stmt *s = st_new(S_SETT);
          s->i = c->height;
          s->a = v;
          append(b, s);
        }
      } else if (!c->unreach && c->lab->kind == L_FUNC) append(b, st_new(S_RET));
      if (c->lab->kind == L_FUNC) {
        if (r->p != r->end) die("code after the function's end");
        free(b->stk);
        free(b->ctl);
        return;
      }
      Ctrl done = *c;
      b->nctl--;
      b->sp = done.height;
      if (done.lab->rty) push(b, temp_expr(done.height, done.lab->rty));
      end_reach(b, &done);
      break;
    }
    case 0x0c: {
      Label *l = br_target(b, ru(r));
      Stmt *s = st_new(S_BR);
      s->lab = l;
      s->a = br_value(b, l, 0);
      append(b, s);
      c->unreach = 1;
      break;
    }
    case 0x0d: {
      Label *l = br_target(b, ru(r));
      Expr *cond = pop(b);
      Stmt *s = st_new(S_BR);
      s->lab = l;
      s->a = br_value(b, l, 1);
      s->b = cond;
      append(b, s);
      break;
    }
    case 0x0e: {
      uint32_t n = ru(r);
      Stmt *s = st_new(S_BRTAB);
      s->labs = xalloc(sizeof(Label *) * (n + 1));
      for (uint32_t i = 0; i <= n; i++) s->labs[i] = br_target(b, ru(r));
      s->j = (int32_t)n;
      settle_top(b);
      Expr *idx = pop(b);
      s->b = idx;
      s->a = br_value(b, s->labs[n], 0);
      append(b, s);
      c->unreach = 1;
      break;
    }
    case 0x0f: {
      Stmt *s = st_new(S_RET);
      if (ft->res) s->a = pop(b);
      append(b, s);
      c->unreach = 1;
      break;
    }
    case 0x10: case 0x11: {
      uint32_t ti, fi = 0;
      Expr *idx = NULL;
      if (op == 0x10) {
        fi = ru(r);
        if (fi >= nfuncs) die("a call of function %u, which does not exist", fi);
        ti = funcs[fi].type;
      } else {
        ti = ru(r);
        if (ru(r)) die("a call_indirect of a second table");
        if (ti >= ntypes) die("a bad type index");
        idx = pop(b);
        f->indirect = 1;
        f->traps = 1;
      }
      FType *ct = &types[ti];
      Stmt *s = st_new(op == 0x10 ? S_CALL : S_CALLI);
      s->i = (int32_t)fi;
      s->j = (int32_t)ti;
      s->c = idx;
      s->nargs = ct->np;
      s->args = xalloc(sizeof(Expr *) * (size_t)(ct->np + 1));
      for (int i = ct->np - 1; i >= 0; i--) s->args[i] = pop(b);
      if (ct->res) s->rslot = b->sp;
      Clob cl = { -1, s->rslot, 1, -2 };
      flush(b, &cl, 0);
      append(b, s);
      if (ct->res) push(b, temp_expr(s->rslot, ct->res));
      if (op == 0x10 && fi >= nimports) add_callee(f, (int)fi);
      break;
    }
    case 0x1a: {
      Expr *e = pop(b);
      if (ex_traps(e)) {
        Stmt *s = st_new(S_EXPR);
        s->a = e;
        append(b, s);
      }
      break;
    }
    case 0x1b: case 0x1c: {
      if (op == 0x1c) {
        uint32_t n = ru(r);
        for (uint32_t i = 0; i < n; i++) check_type(rb(r), "a select");
      }
      Expr *cnd = pop(b), *y = pop(b), *x = pop(b);
      Expr *e = ex_new(E_SEL, x->ty);
      e->a = cnd;
      e->b = x;
      e->c = y;
      push(b, e);
      break;
    }
    case 0x20: {
      uint32_t x = ru(r);
      if (x >= f->nlocals) die("a bad local index");
      Expr *e = ex_new(E_LOC, f->ltype[x]);
      e->i = (int32_t)x;
      push(b, e);
      break;
    }
    case 0x21: case 0x22: {
      uint32_t x = ru(r);
      if (x >= f->nlocals) die("a bad local index");
      Expr *v = pop(b);
      Clob cl = { (int)x, -1, 0, -1 };
      flush(b, &cl, 0);
      Stmt *s = st_new(S_SETL);
      s->i = (int32_t)x;
      s->a = v;
      append(b, s);
      if (op == 0x22) {
        Expr *e = ex_new(E_LOC, f->ltype[x]);
        e->i = (int32_t)x;
        push(b, e);
      }
      break;
    }
    case 0x23: {
      uint32_t g = ru(r);
      if (g >= nglobals) die("a bad global index");
      Expr *e = ex_new(E_GLB, globals[g].ty);
      e->i = (int32_t)g;
      push(b, e);
      break;
    }
    case 0x24: {
      uint32_t g = ru(r);
      if (g >= nglobals) die("a bad global index");
      Expr *v = pop(b);
      Clob cl = { -1, -1, 0, (int)g };
      flush(b, &cl, 0);
      Stmt *s = st_new(S_SETG);
      s->i = (int32_t)g;
      s->a = v;
      append(b, s);
      break;
    }
    case 0x28: case 0x2b: case 0x2c: case 0x2d: case 0x2e: case 0x2f: {
      Expr *e = ex_new(E_LOAD, op == 0x2b ? T_F64 : T_I32);
      e->op = op;
      memarg(r, &e->align, &e->off);
      e->a = pop(b);
      push(b, e);
      break;
    }
    case 0x36: case 0x39: case 0x3a: case 0x3b: {
      Stmt *s = st_new(S_STORE);
      s->op = op;
      memarg(r, &s->align, &s->off);
      s->b = pop(b);
      s->a = pop(b);
      Clob cl = { -1, -1, 1, -1 };
      flush(b, &cl, 0);
      append(b, s);
      break;
    }
    case 0x3f: rb(r); push(b, ex_new(E_MSZ, T_I32)); break;
    case 0x40: {
      rb(r);
      Stmt *s = st_new(S_GROW);
      s->a = pop(b);
      s->rslot = b->sp;
      Clob cl = { -1, s->rslot, 1, -1 };
      flush(b, &cl, 0);
      append(b, s);
      push(b, temp_expr(s->rslot, T_I32));
      break;
    }
    case 0x41: { Expr *e = ex_new(E_CI, T_I32); e->i = (int32_t)rsl(r, 32); push(b, e); break; }
    case 0x44: { Expr *e = ex_new(E_CF, T_F64); e->f = rf64(r); push(b, e); break; }
    case 0x45: case 0x67: case 0x68: case 0x69: case 0xc0: case 0xc1: push(b, un(op, T_I32, pop(b))); break;
    case 0x99: case 0x9a: case 0x9b: case 0x9c: case 0x9d: case 0x9e: case 0x9f: push(b, un(op, T_F64, pop(b))); break;
    case 0xaa: case 0xab: f->traps = 1; push(b, un(op, T_I32, pop(b))); break;
    case 0xb7: case 0xb8: push(b, un(op, T_F64, pop(b))); break;
    case 0xfc: {
      uint32_t sub = ru(r);
      if (sub == 2 || sub == 3) push(b, un((uint16_t)(0x100 | sub), T_I32, pop(b)));
      else if (sub == 10 || sub == 11) {
        if (sub == 10) { rb(r); rb(r); } else rb(r);
        Stmt *s = st_new(sub == 10 ? S_COPY : S_FILL);
        s->c = pop(b);
        s->b = pop(b);
        s->a = pop(b);
        Clob cl = { -1, -1, 1, -1 };
        flush(b, &cl, 0);
        append(b, s);
      } else die("the opcode 0xfc %u, which js1c does not handle", sub);
      break;
    }
    default:
      if ((op >= 0x46 && op <= 0x4f) || (op >= 0x6a && op <= 0x78)) {
        Expr *y = pop(b), *x = pop(b);
        if (op >= 0x6d && op <= 0x70) f->traps = 1;
        push(b, bin(op, T_I32, x, y));
      } else if (op >= 0x61 && op <= 0x66) {
        Expr *y = pop(b), *x = pop(b);
        push(b, bin(op, T_I32, x, y));
      } else if (op >= 0xa0 && op <= 0xa6) {
        Expr *y = pop(b), *x = pop(b);
        push(b, bin(op, T_F64, x, y));
      } else if ((op >= 0x29 && op <= 0x2a) || (op >= 0x30 && op <= 0x35) || (op >= 0x37 && op <= 0x38) ||
                 (op >= 0x3c && op <= 0x3e) || op == 0x42 || (op >= 0x50 && op <= 0x5a) || (op >= 0x79 && op <= 0x8a) ||
                 (op >= 0xa7 && op <= 0xb6 && op != 0xaa && op != 0xab) || op >= 0xbc)
        die("the opcode 0x%02x (i64, f32 or a reinterpretation), which the lowering should have removed", op);
      else if (op >= 0x43 && op <= 0x44) die("an f32 constant");
      else if (op >= 0x5b && op <= 0x60) die("an f32 comparison: js1c does not handle f32 yet");
      else if (op >= 0x8b && op <= 0x98) die("an f32 operation: js1c does not handle f32 yet");
      else die("the opcode 0x%02x, which js1c does not handle", op);
    }
  }
}

/* Bit sets of a function's variables: its locals, then its temporaries. */
static int nvars, nwords;
static uint64_t *bset(void) { return xalloc(sizeof(uint64_t) * (size_t)nwords); }
static void bsetcopy(uint64_t *d, const uint64_t *s) { memcpy(d, s, sizeof(uint64_t) * (size_t)nwords); }
static void bor(uint64_t *d, const uint64_t *s) { for (int i = 0; i < nwords; i++) d[i] |= s[i]; }
static int beq(const uint64_t *a, const uint64_t *b) { return !memcmp(a, b, sizeof(uint64_t) * (size_t)nwords); }
static void bset1(uint64_t *d, int v) { d[v >> 6] |= 1ULL << (v & 63); }
static void bclr(uint64_t *d, int v) { d[v >> 6] &= ~(1ULL << (v & 63)); }
static int bhas(const uint64_t *d, int v) { return (int)(d[v >> 6] >> (v & 63) & 1); }
static int cur_nlocals;
static void uses(uint64_t *d, const Expr *e) {
  if (!e) return;
  if (e->k == E_LOC) bset1(d, e->i);
  else if (e->k == E_TMP) bset1(d, cur_nlocals + e->i);
  uses(d, e->a);
  uses(d, e->b);
  uses(d, e->c);
}
static uint64_t *live_list(Stmt *s, const uint64_t *out);
static uint64_t *target_live(Label *l, const Expr *val) {
  uint64_t *t = bset();
  if (l->kind != L_FUNC) {
    bsetcopy(t, l->live);
    if (val) bclr(t, cur_nlocals + l->slot);
  }
  return t;
}
static uint64_t *live_stmt(Stmt *s, const uint64_t *cur) {
  uint64_t *r = bset();
  switch (s->k) {
  case S_SETL: bsetcopy(r, cur); bclr(r, s->i); uses(r, s->a); break;
  case S_SETT: bsetcopy(r, cur); bclr(r, cur_nlocals + s->i); uses(r, s->a); break;
  case S_SETG: case S_STORE: case S_EXPR: case S_FILL: case S_COPY:
    bsetcopy(r, cur); uses(r, s->a); uses(r, s->b); uses(r, s->c); break;
  case S_GROW: bsetcopy(r, cur); bclr(r, cur_nlocals + s->rslot); uses(r, s->a); break;
  case S_CALL: case S_CALLI:
    bsetcopy(r, cur);
    if (s->rslot >= 0) bclr(r, cur_nlocals + s->rslot);
    if (!s->spill) s->spill = bset();
    bsetcopy(s->spill, r);
    for (int i = 0; i < s->nargs; i++) uses(r, s->args[i]);
    uses(r, s->c);
    break;
  case S_BLOCK: {
    if (!s->lab->live) s->lab->live = bset();
    bsetcopy(s->lab->live, cur);
    uint64_t *x = live_list(s->body, cur);
    bsetcopy(r, x);
    free(x);
    break;
  }
  case S_LOOP: {
    if (!s->lab->live) s->lab->live = bset();
    memset(s->lab->live, 0, sizeof(uint64_t) * (size_t)nwords);
    for (;;) {
      uint64_t *x = live_list(s->body, cur);
      int same = beq(x, s->lab->live);
      bsetcopy(s->lab->live, x);
      free(x);
      if (same) break;
    }
    bsetcopy(r, s->lab->live);
    break;
  }
  case S_IF: {
    if (!s->lab->live) s->lab->live = bset();
    bsetcopy(s->lab->live, cur);
    uint64_t *x = live_list(s->body, cur), *y = live_list(s->els, cur);
    bsetcopy(r, x);
    bor(r, y);
    free(x);
    free(y);
    uses(r, s->a);
    break;
  }
  case S_BR: {
    uint64_t *t = target_live(s->lab, s->a);
    bsetcopy(r, t);
    free(t);
    uses(r, s->a);
    if (s->b) {
      bor(r, cur);
      uses(r, s->b);
    }
    break;
  }
  case S_BRTAB:
    for (int i = 0; i <= s->j; i++) {
      uint64_t *t = target_live(s->labs[i], s->a);
      bor(r, t);
      free(t);
    }
    uses(r, s->a);
    uses(r, s->b);
    break;
  case S_RET: uses(r, s->a); break;
  case S_UNR: break;
  }
  return r;
}
static uint64_t *live_list(Stmt *s, const uint64_t *out) {
  int n = 0;
  for (Stmt *t = s; t; t = t->next) n++;
  Stmt **v = xalloc(sizeof(Stmt *) * (size_t)(n + 1));
  n = 0;
  for (Stmt *t = s; t; t = t->next) v[n++] = t;
  uint64_t *cur = bset();
  bsetcopy(cur, out);
  for (int i = n - 1; i >= 0; i--) {
    uint64_t *x = live_stmt(v[i], cur);
    free(cur);
    cur = x;
  }
  free(v);
  return cur;
}

/* Numbering the call sites of a driven function: a site is a call of a driven function or an indirect call. */
static int is_site(const Stmt *s) {
  if (s->k == S_CALLI) return 1;
  return s->k == S_CALL && s->i >= (int32_t)nimports && !funcs[s->i].direct;
}
static int site_n;
static void number_list(Stmt *s);
static void number(Stmt *s) {
  s->lo = s->hi = 0;
  if (is_site(s)) {
    s->site = s->lo = s->hi = ++site_n;
    return;
  }
  if (s->k == S_BLOCK || s->k == S_LOOP || s->k == S_IF) {
    int before = site_n;
    number_list(s->body);
    if (s->k == S_IF) number_list(s->els);
    if (site_n > before) {
      s->lo = before + 1;
      s->hi = site_n;
    }
  }
}
static void number_list(Stmt *s) {
  for (; s; s = s->next) number(s);
}
static int list_lo(Stmt *s) {
  for (; s; s = s->next)
    if (s->lo) return s->lo;
  return 0;
}
static int list_hi(Stmt *s) {
  int h = 0;
  for (; s; s = s->next)
    if (s->hi) h = s->hi;
  return h;
}

/* Names. A function's variables are a letter, then digits: never a reserved word, never a global (all J...). */
static void vname(Buf *b, int v) {
  bf(b, "%c", 'a' + v % 26);
  if (v >= 26) bf(b, "%d", v / 26);
}
static void lname(Buf *b, int x) { vname(b, x); }
static void tname(Buf *b, int t) { vname(b, cur_nlocals + t); }

/* Expressions. want: W_I a signed 32-bit integer, W_U the same bits as an unsigned number, W_C a condition, W_F a
 * double. Every piece is written as an atom (in parentheses where it is compound). */
enum { W_I, W_U, W_C, W_F };
static void ex(Buf *b, const Expr *e, int want);
static int is_cmp(const Expr *e) {
  if (e->k == E_UN && e->op == 0x45) return 1;
  return e->k == E_BIN && ((e->op >= 0x46 && e->op <= 0x4f) || (e->op >= 0x61 && e->op <= 0x66));
}
static void fconst(Buf *b, double d) {
  if (d != d) { bs(b, "(0/0)"); return; }
  if (d == 1.0 / 0.0) { bs(b, "(1/0)"); return; }
  if (d == -1.0 / 0.0) { bs(b, "(-1/0)"); return; }
  if (d == 0 && 1 / d < 0) { bs(b, "(-0)"); return; }
  char t[64];
  snprintf(t, sizeof t, "%.17g", d);
  /* The shortest of %.15g to %.17g that reads back the same. */
  for (int p = 15; p <= 17; p++) {
    char u[64];
    snprintf(u, sizeof u, "%.*g", p, d);
    if (strtod(u, NULL) == d) {
      memcpy(t, u, sizeof t);
      break;
    }
  }
  if (d < 0) bf(b, "(%s)", t);
  else bs(b, t);
}
static int simple_addr(const Expr *e) {
  return e->k == E_LOC || e->k == E_TMP || e->k == E_CI || e->k == E_GLB;
}
/* The byte address of a load or store, as a signed integer expression. */
static void addr(Buf *b, const Expr *a, uint32_t off) {
  if (a->k == E_CI) {
    bf(b, "%u", (uint32_t)a->i + off);
    return;
  }
  if (!off) {
    ex(b, a, W_I);
    return;
  }
  bs(b, "(");
  ex(b, a, W_I);
  bf(b, "+%u)", off);
}
static void load(Buf *b, const Expr *e) {
  const Expr *a = e->a;
  uint32_t off = e->off;
  int nat = e->op == 0x28 ? 2 : e->op == 0x2b ? 3 : (e->op == 0x2c || e->op == 0x2d) ? 0 : 1;
  int aligned = e->align >= (nat > 2 ? 2 : nat);
  if (e->op == 0x2b) {
    bs(b, "JLd(");
    addr(b, a, off);
    bs(b, ")");
    return;
  }
  if (e->op == 0x28) {
    if (!aligned) {
      bs(b, "JLw(");
      addr(b, a, off);
      bs(b, ")");
      return;
    }
    if (a->k == E_CI) {
      bf(b, "(JM[%u]|0)", ((uint32_t)a->i + off) >> 2);
      return;
    }
    bs(b, "(JM[");
    addr(b, a, off);
    bs(b, ">>2]|0)");
    return;
  }
  int sgn = e->op == 0x2c || e->op == 0x2e;
  if (!aligned || !simple_addr(a)) {
    bs(b, nat == 0 ? (sgn ? "JLbs(" : "JLbu(") : aligned ? (sgn ? "JLhs(" : "JLhu(") : (sgn ? "JLgs(" : "JLgu("));
    addr(b, a, off);
    bs(b, ")");
    return;
  }
  if (a->k == E_CI) {
    uint32_t x = (uint32_t)a->i + off, sh = (x & (nat ? 2u : 3u)) << 3;
    if (sgn) bf(b, "(JM[%u]<<%u>>%u)", x >> 2, (nat ? 16 : 24) - sh, nat ? 16 : 24);
    else bf(b, "(JM[%u]>>>%u&%u)", x >> 2, sh, nat ? 65535 : 255);
    return;
  }
  Buf x = { 0 };
  addr(&x, a, off);
  if (sgn) bf(b, "(JM[%s>>2]<<(%u-((%s&%u)<<3))>>%u)", x.p, nat ? 16 : 24, x.p, nat ? 2 : 3, nat ? 16 : 24);
  else bf(b, "(JM[%s>>2]>>>((%s&%u)<<3)&%u)", x.p, x.p, nat ? 2 : 3, nat ? 65535 : 255);
  free(x.p);
}
static void call2(Buf *b, const char *fn, const Expr *x, int wx, const Expr *y, int wy) {
  bs(b, fn);
  bs(b, "(");
  ex(b, x, wx);
  if (y) {
    bs(b, ",");
    ex(b, y, wy);
  }
  bs(b, ")");
}
static void bin_i32(Buf *b, const Expr *e) {
  const Expr *x = e->a, *y = e->b;
  int yc = y->k == E_CI;
  int32_t k = y->i;
  const char *o = NULL;
  switch (e->op) {
  case 0x6a: o = "+"; break;
  case 0x6b: o = "-"; break;
  case 0x71: o = "&"; break;
  case 0x72: o = "|"; break;
  case 0x73: o = "^"; break;
  case 0x74: o = "<<"; break;
  case 0x75: o = ">>"; break;
  }
  if (e->op == 0x6a || e->op == 0x6b) {
    /* Netscape 3.04 converts to 32 bits right only from -2^31 to 2^32 - 1 and gives NaN outside
     * (docs/js1-compile-design.md section 12), and the sum or difference of two i32 reaches -2^32 to 2^32. With a
     * constant the range is known and needs no reduction: x + c for c >= 0, and (x >>> 0) - c for c > 0, stay in it
     * (and a negative constant turns one into the other). Otherwise 2^32 is added and the sum reduced modulo 2^32. */
    int sub = e->op == 0x6b;
    if (yc || (!sub && x->k == E_CI)) {
      const Expr *v = yc ? x : y;
      int64_t c = yc ? (sub ? -(int64_t)k : (int64_t)k) : (int64_t)x->i;
      if (c >= 0) {
        bs(b, "(");
        ex(b, v, W_I);
        bf(b, "+%lld|0)", (long long)c);
      } else {
        bs(b, "(");
        ex(b, v, W_U);
        bf(b, "-%lld|0)", (long long)-c);
      }
      return;
    }
    bs(b, "((");
    ex(b, x, W_I);
    bs(b, sub ? "-" : "+");
    ex(b, y, W_I);
    bs(b, "+4294967296)%4294967296|0)");
    return;
  }
  if (o) {
    bs(b, "(");
    ex(b, x, W_I);
    bs(b, o);
    if (yc && (e->op == 0x74 || e->op == 0x75)) bf(b, "%d", k & 31);
    else ex(b, y, W_I);
    bs(b, ")");
    return;
  }
  switch (e->op) {
  case 0x76:
    bs(b, "(");
    ex(b, x, W_I);
    bs(b, ">>>");
    if (yc) bf(b, "%d", k & 31);
    else ex(b, y, W_I);
    bs(b, "|0)");
    return;
  case 0x6c:
    /* By a small constant c the product is exact (under 2^52); for |c| > 1 it is moved up by a multiple of 2^32 to
     * no less than 0 and reduced modulo 2^32, since Netscape 3.04 gives NaN outside -2^31 to 2^32 - 1. */
    if ((yc && k > -(1 << 21) && k < (1 << 21)) || (x->k == E_CI && x->i > -(1 << 21) && x->i < (1 << 21))) {
      const Expr *v = yc ? x : y;
      int32_t c = yc ? k : x->i;
      int64_t up = 4294967296LL * (((int64_t)(c < 0 ? -c : c) + 1) / 2);
      bs(b, c >= -1 && c <= 1 ? "(" : "((");
      ex(b, v, W_I);
      if (c >= -1 && c <= 1) bf(b, "*%d|0)", c);
      else bf(b, "*%d+%lld)%%4294967296|0)", c, (long long)up);
      return;
    }
    call2(b, "JMu", x, W_I, y, W_I);
    return;
  case 0x6d:
    if (yc && k != 0 && k != -1) {
      bs(b, "(");
      ex(b, x, W_I);
      bf(b, "/%d|0)", k);
      return;
    }
    call2(b, "JDs", x, W_I, y, W_I);
    return;
  case 0x6e:
    if (yc && k != 0) {
      bs(b, "(");
      ex(b, x, W_U);
      bf(b, "/%u|0)", (uint32_t)k);
      return;
    }
    call2(b, "JDu", x, W_I, y, W_I);
    return;
  case 0x6f:
    if (yc && k != 0) {
      bs(b, "(");
      ex(b, x, W_I);
      if (k < 0) bf(b, "%%(%d)|0)", k);
      else bf(b, "%%%d|0)", k);
      return;
    }
    call2(b, "JRs", x, W_I, y, W_I);
    return;
  case 0x70:
    if (yc && k != 0) {
      bs(b, "(");
      ex(b, x, W_U);
      bf(b, "%%%u|0)", (uint32_t)k);
      return;
    }
    call2(b, "JRu", x, W_I, y, W_I);
    return;
  case 0x77: call2(b, "JRl", x, W_I, y, W_I); return;
  case 0x78: call2(b, "JRr", x, W_I, y, W_I); return;
  }
  die("internal: i32 binary 0x%02x", e->op);
}
static void cmp(Buf *b, const Expr *e) {
  if (e->k == E_UN) { /* eqz */
    bs(b, "(");
    ex(b, e->a, W_I);
    bs(b, "==0)");
    return;
  }
  static const char *ops[] = { "==", "!=", "<", "<", ">", ">", "<=", "<=", ">=", ">=" };
  if (e->op >= 0x61) {
    static const char *fo[] = { "==", "!=", "<", ">", "<=", ">=" };
    bs(b, "(");
    ex(b, e->a, W_F);
    bs(b, fo[e->op - 0x61]);
    ex(b, e->b, W_F);
    bs(b, ")");
    return;
  }
  int i = e->op - 0x46, u = i >= 2 && (i & 1);
  bs(b, "(");
  ex(b, e->a, u ? W_U : W_I);
  bs(b, ops[i]);
  ex(b, e->b, u ? W_U : W_I);
  bs(b, ")");
}
static void ex(Buf *b, const Expr *e, int want) {
  if (want == W_C) {
    /* x != 0 and eqz(eqz(x)) are x itself as a condition. */
    if (e->k == E_BIN && e->op == 0x47 && e->b->k == E_CI && e->b->i == 0) { ex(b, e->a, W_C); return; }
    if (is_cmp(e)) { cmp(b, e); return; }
  }
  if (is_cmp(e)) {
    bs(b, "(");
    cmp(b, e);
    bs(b, "?1:0)");
    return;
  }
  if (want == W_U && e->ty == T_I32) {
    if (e->k == E_CI) { bf(b, "%u", (uint32_t)e->i); return; }
    if (e->k == E_BIN && e->op == 0x76) {
      bs(b, "(");
      ex(b, e->a, W_I);
      bs(b, ">>>");
      if (e->b->k == E_CI) bf(b, "%d", e->b->i & 31);
      else ex(b, e->b, W_I);
      bs(b, ")");
      return;
    }
    bs(b, "(");
    ex(b, e, W_I);
    bs(b, ">>>0)");
    return;
  }
  switch (e->k) {
  case E_CI:
    if (e->i < 0) bf(b, "(%d)", e->i);
    else bf(b, "%d", e->i);
    return;
  case E_CF: fconst(b, e->f); return;
  case E_LOC: lname(b, e->i); return;
  case E_TMP: tname(b, e->i); return;
  case E_GLB: bf(b, "Jg%d", e->i); return;
  case E_MSZ: bs(b, "JMp"); return;
  case E_LOAD: load(b, e); return;
  case E_SEL:
    bs(b, "(");
    ex(b, e->a, W_C);
    bs(b, "?");
    ex(b, e->b, e->ty == T_F64 ? W_F : W_I);
    bs(b, ":");
    ex(b, e->c, e->ty == T_F64 ? W_F : W_I);
    bs(b, ")");
    return;
  case E_UN:
    switch (e->op) {
    case 0x67: call2(b, "JCz", e->a, W_I, NULL, 0); return;
    case 0x68: call2(b, "JCt", e->a, W_I, NULL, 0); return;
    case 0x69: call2(b, "JPc", e->a, W_I, NULL, 0); return;
    case 0xc0: bs(b, "("); ex(b, e->a, W_I); bs(b, "<<24>>24)"); return;
    case 0xc1: bs(b, "("); ex(b, e->a, W_I); bs(b, "<<16>>16)"); return;
    case 0x99: call2(b, "Math.abs", e->a, W_F, NULL, 0); return;
    case 0x9a: bs(b, "(-"); ex(b, e->a, W_F); bs(b, ")"); return;
    case 0x9b: call2(b, "Math.ceil", e->a, W_F, NULL, 0); return;
    case 0x9c: call2(b, "Math.floor", e->a, W_F, NULL, 0); return;
    case 0x9d: call2(b, "JTr", e->a, W_F, NULL, 0); return;
    case 0x9e: call2(b, "JNr", e->a, W_F, NULL, 0); return;
    case 0x9f: call2(b, "Math.sqrt", e->a, W_F, NULL, 0); return;
    case 0xaa: call2(b, "JTs", e->a, W_F, NULL, 0); return;
    case 0xab: call2(b, "JTu", e->a, W_F, NULL, 0); return;
    case 0x102: call2(b, "JSs", e->a, W_F, NULL, 0); return;
    case 0x103: call2(b, "JSu", e->a, W_F, NULL, 0); return;
    case 0xb7: ex(b, e->a, W_I); return;
    case 0xb8: ex(b, e->a, W_U); return;
    }
    die("internal: unary 0x%02x", e->op);
  case E_BIN:
    if (e->ty == T_I32) { bin_i32(b, e); return; }
    switch (e->op) {
    case 0xa0: case 0xa1: case 0xa2: case 0xa3: {
      static const char o[] = "+-*/";
      bs(b, "(");
      ex(b, e->a, W_F);
      bf(b, "%c", o[e->op - 0xa0]);
      ex(b, e->b, W_F);
      bs(b, ")");
      return;
    }
    case 0xa4: call2(b, "JMn", e->a, W_F, e->b, W_F); return;
    case 0xa5: call2(b, "JMx", e->a, W_F, e->b, W_F); return;
    case 0xa6: call2(b, "JCs", e->a, W_F, e->b, W_F); return;
    }
    die("internal: f64 binary 0x%02x", e->op);
  }
  die("internal: expression kind %d", e->k);
}
static void exv(Buf *b, const Expr *e) { ex(b, e, e->ty == T_F64 ? W_F : W_I); }

/* Statements. */
typedef struct {
  Func *f;
  int fi, driven;
  Label **labs;
  int nlabs, cap;
  int usesB;
} Em;
static void em_list(Em *m, Buf *b, Stmt *s, int guarded, int hi);
static void lpush(Em *m, Label *l) {
  if (m->nlabs == m->cap) {
    m->cap = m->cap ? m->cap * 2 : 16;
    m->labs = xgrow(m->labs, sizeof(Label *) * (size_t)m->cap);
  }
  m->labs[m->nlabs++] = l;
}
static void ret_seq(Em *m, Buf *b, const Expr *v) {
  if (m->driven) {
    if (v) {
      bs(b, "JR=");
      exv(b, v);
      bs(b, ";");
    }
    bs(b, "JQ=1;return JS[JP-1];");
  } else if (v) {
    bs(b, "return ");
    exv(b, v);
    bs(b, ";");
  } else bs(b, "return 0;"); /* never a bare return: Netscape 2.02 refuses a function that has both (tools/js10.mjs) */
}
static void add_cross(Label *w, Label *t) {
  for (int i = 0; i < w->ncross; i++)
    if (w->cross[i] == t) return;
  w->cross = xgrow(w->cross, sizeof(Label *) * (size_t)(w->ncross + 1));
  w->cross[w->ncross++] = t;
}
static void jump(Em *m, Buf *b, Label *l, const Expr *v) {
  if (l->kind == L_FUNC) {
    ret_seq(m, b, v);
    return;
  }
  if (v && !in_temp(v, l->slot)) {
    tname(b, l->slot);
    bs(b, "=");
    exv(b, v);
    bs(b, ";");
  }
  int inner = 0;
  for (int i = m->nlabs - 1; i >= 0 && m->labs[i] != l; i--)
    if (m->labs[i]->jsloop) {
      inner++;
      add_cross(m->labs[i], l);
    }
  if (!inner) {
    bs(b, l->kind == L_LOOP ? "continue;" : "break;");
    return;
  }
  m->usesB = 1;
  bf(b, "B=%d;break;", l->id);
}
/* After the JavaScript loop of w closes: where a branch crossed it, carry it on to the next loop out. */
static void cross_check(Em *m, Buf *b, Label *w) {
  if (!w->ncross) return;
  Label *e = NULL;
  for (int i = m->nlabs - 1; i >= 0; i--)
    if (m->labs[i]->jsloop) {
      e = m->labs[i];
      break;
    }
  if (!e) die("internal: a branch crosses no loop");
  int here = 0, further = 0;
  for (int i = 0; i < w->ncross; i++) {
    if (w->cross[i] == e) here = 1;
    else further = 1;
  }
  const char *act = e->kind == L_LOOP ? "continue;" : "break;";
  if (here && !further) bf(b, "if(B){B=0;%s}", act);
  else if (!here) bs(b, "if(B)break;");
  else bf(b, "if(B){if(B==%d){B=0;%s}break;}", e->id, act);
}
static int ends_flow(Stmt *s) {
  if (!s) return 0;
  while (s->next) s = s->next;
  return (s->k == S_BR && !s->b) || s->k == S_BRTAB || s->k == S_RET || s->k == S_UNR;
}
static void brtab_tree(Em *m, Buf *b, Stmt *s, const char *iv, int *start, Label **tg, int lo, int hi) {
  if (hi - lo == 1) {
    jump(m, b, tg[lo], s->a);
    return;
  }
  int mid = (lo + hi) / 2;
  bf(b, "if(%s<%d){", iv, start[mid]);
  brtab_tree(m, b, s, iv, start, tg, lo, mid);
  bs(b, "}else{");
  brtab_tree(m, b, s, iv, start, tg, mid, hi);
  bs(b, "}");
}
static void spill_restore(Em *m, Buf *b, Stmt *s, int restore) {
  int n = 0;
  for (int v = 0; v < nvars; v++)
    if (bhas(s->spill, v)) {
      if (restore) {
        vname(b, v);
        bf(b, "=JS[JP+%d];", n);
      } else {
        bf(b, "JS[JP+%d]=", n);
        vname(b, v);
        bs(b, ";");
      }
      n++;
    }
  if (!restore) bf(b, "JS[JP+%d]=%d;JS[JP+%d]=%d;JP=JP+%d;", n, s->site, n + 1, m->fi + 1, n + 2);
  (void)m;
}
static int spill_count(const Stmt *s) {
  int n = 0;
  for (int v = 0; v < nvars; v++) n += bhas(s->spill, v);
  return n;
}
static void args_list(Buf *b, Stmt *s) {
  for (int i = 0; i < s->nargs; i++) {
    if (i) bs(b, ",");
    exv(b, s->args[i]);
  }
}
static int is_ident(const char *s);
/* The imports binaryen's i64 lowering adds for a reinterpretation (env.wasm2js_scratch_*), which the runtime has. */
static const char *scratch(const Func *g) {
  static const char *names[][2] = { { "wasm2js_scratch_store_f64", "Jzsf" }, { "wasm2js_scratch_load_f64", "Jzlf" },
    { "wasm2js_scratch_store_i32", "Jzsi" }, { "wasm2js_scratch_load_i32", "Jzli" } };
  if (strcmp(g->mod, "env")) return NULL;
  for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
    if (!strcmp(g->name, names[i][0])) return names[i][1];
  if (!strncmp(g->name, "wasm2js_", 8)) die("the import env.%s (an f32 reinterpretation?): not handled", g->name);
  return NULL;
}
/* A name a member can be written with a dot: an identifier and no reserved word, nor a method of JavaScript 1.2 and
 * later that the gate refuses by its name (call and apply). */
static int js_name(const char *s) {
  static const char *no[] = { "break", "case", "catch", "class", "const", "continue", "default", "delete", "do", "else",
    "enum", "export", "extends", "false", "finally", "for", "function", "if", "import", "in", "instanceof", "new", "null",
    "return", "super", "switch", "this", "throw", "true", "try", "typeof", "var", "void", "while", "with", "call",
    "apply", "abstract", "boolean", "byte", "char", "double", "final", "float", "goto", "implements", "int", "interface",
    "long", "native", "package", "private", "protected", "public", "short", "static", "synchronized", "throws",
    "transient", "volatile", NULL };
  if (!is_ident(s)) return 0;
  for (int i = 0; no[i]; i++)
    if (!strcmp(s, no[i])) return 0;
  return 1;
}
static void em_call(Em *m, Buf *b, Stmt *s) {
  if (s->k == S_CALL && s->i < (int32_t)nimports) {
    Func *g = &funcs[s->i];
    FType *t = &types[g->type];
    const char *z = scratch(g);
    if (z) { /* binaryen's reinterpretations through a scratch double: the runtime's own */
      if (s->rslot >= 0) {
        tname(b, s->rslot);
        bs(b, "=");
      }
      bs(b, z);
      bs(b, "(");
      args_list(b, s);
      bs(b, ");");
      return;
    }
    if (s->rslot >= 0) {
      tname(b, s->rslot);
      bs(b, "=");
    }
    if (js_name(g->name)) bf(b, "Ji%d.%s(", g->import, g->name);
    else {
      bf(b, "Ji%d[\"", g->import);
      bs(b, g->name);
      bs(b, "\"](");
    }
    args_list(b, s);
    bs(b, ")");
    if (s->rslot >= 0) bs(b, t->res == T_F64 ? "-0" : "|0");
    bs(b, ";");
    return;
  }
  if (s->k == S_CALL && funcs[s->i].direct) {
    if (s->rslot >= 0) {
      tname(b, s->rslot);
      bs(b, "=");
    }
    bf(b, "Jd%d(", s->i);
    args_list(b, s);
    bs(b, ");");
    if (funcs[s->i].maytrap) bs(b, "if(JT)return 0;");
    return;
  }
  if (!m->driven) die("internal: a direct function with a site");
  bs(b, "if(!R){");
  if (s->k == S_CALLI) {
    m->usesB = 1;
    bs(b, "B=JE[");
    ex(b, s->c, W_I);
    bf(b, "];if(!B||JY[B]!=%d){JT=1;return 0;}", types[s->j].canon);
  }
  FType *t = &types[s->j];
  for (int i = 0; i < s->nargs; i++) {
    bf(b, "Ja%d=", i);
    exv(b, s->args[i]);
    bs(b, ";");
  }
  (void)t;
  spill_restore(m, b, s, 0);
  if (s->k == S_CALLI) bs(b, "return B;}");
  else bf(b, "return %d;}", s->i + 1);
  bf(b, "R=0;JP=JP-%d;", spill_count(s) + 2);
  spill_restore(m, b, s, 1);
  if (s->rslot >= 0) {
    tname(b, s->rslot);
    bs(b, "=JR;");
  }
}
static int stmt_traps(const Stmt *s) {
  if (s->k == S_BLOCK || s->k == S_LOOP || s->k == S_IF) return ex_traps(s->a);
  if (ex_traps(s->a) || ex_traps(s->b) || ex_traps(s->c)) return 1;
  for (int i = 0; i < s->nargs; i++)
    if (ex_traps(s->args[i])) return 1;
  return 0;
}
static void em_stmt(Em *m, Buf *b, Stmt *s, int guarded) {
  switch (s->k) {
  case S_SETL: lname(b, s->i); bs(b, "="); exv(b, s->a); bs(b, ";"); break;
  case S_SETT: tname(b, s->i); bs(b, "="); exv(b, s->a); bs(b, ";"); break;
  case S_SETG: bf(b, "Jg%d=", s->i); exv(b, s->a); bs(b, ";"); break;
  case S_STORE:
    if (s->op == 0x36 && s->align >= 2) {
      if (s->a->k == E_CI) bf(b, "JM[%u]=", ((uint32_t)s->a->i + s->off) >> 2);
      else {
        bs(b, "JM[");
        addr(b, s->a, s->off);
        bs(b, ">>2]=");
      }
      ex(b, s->b, W_I);
      bs(b, ";");
      break;
    }
    bs(b, s->op == 0x36 ? "JWw(" : s->op == 0x39 ? "JWd(" : s->op == 0x3a ? "JWb(" : "JWh(");
    addr(b, s->a, s->off);
    bs(b, ",");
    exv(b, s->b);
    bs(b, ");");
    break;
  case S_CALL: case S_CALLI: em_call(m, b, s); break;
  case S_EXPR: bs(b, "B="); exv(b, s->a); bs(b, ";B=0;"); m->usesB = 1; break;
  case S_FILL: case S_COPY:
    bs(b, s->k == S_FILL ? "JMf(" : "JMc(");
    ex(b, s->a, W_I);
    bs(b, ",");
    ex(b, s->b, W_I);
    bs(b, ",");
    ex(b, s->c, W_I);
    bs(b, ");");
    break;
  case S_GROW: tname(b, s->rslot); bs(b, "=JMg("); ex(b, s->a, W_I); bs(b, ");"); break;
  case S_UNR: bs(b, "JT=1;return 0;"); break;
  case S_RET: ret_seq(m, b, s->a); break;
  case S_BR:
    if (s->b) {
      bs(b, "if(");
      ex(b, s->b, W_C);
      bs(b, "){");
      jump(m, b, s->lab, s->a);
      bs(b, "}");
    } else jump(m, b, s->lab, s->a);
    break;
  case S_BRTAB: {
    int n = s->j;
    Buf iv = { 0 };
    ex(&iv, s->b, W_I);
    /* Runs of the same target, each with its first index. */
    int *start = xalloc(sizeof(int) * (size_t)(n + 1));
    Label **tg = xalloc(sizeof(Label *) * (size_t)(n + 1));
    int runs = 0;
    for (int i = 0; i < n; i++)
      if (!runs || tg[runs - 1] != s->labs[i]) {
        start[runs] = i;
        tg[runs++] = s->labs[i];
      }
    if (!n) jump(m, b, s->labs[0], s->a);
    else {
      bf(b, "if((%s>>>0)>=%d){", iv.p, n);
      jump(m, b, s->labs[n], s->a);
      bs(b, "}else{");
      brtab_tree(m, b, s, iv.p, start, tg, 0, runs);
      bs(b, "}");
    }
    free(start);
    free(tg);
    free(iv.p);
    break;
  }
  case S_BLOCK: case S_LOOP: case S_IF: {
    Label *l = s->lab;
    l->jsloop = l->targeted;
    if (l->jsloop) bs(b, "while(1){");
    lpush(m, l);
    if (s->k == S_IF) {
      int tl = list_lo(s->body), el = list_lo(s->els);
      bs(b, "if(");
      if (guarded && tl && el) bf(b, "R?R<=%d:", list_hi(s->body));
      else if (guarded && tl) bs(b, "R||");
      else if (guarded && el) bs(b, "!R&&");
      ex(b, s->a, W_C);
      bs(b, "){");
      em_list(m, b, s->body, guarded && tl, list_hi(s->body));
      bs(b, "}");
      if (s->els) {
        bs(b, "else{");
        em_list(m, b, s->els, guarded && el, list_hi(s->els));
        bs(b, "}");
      }
    } else em_list(m, b, s->body, guarded, s->hi);
    m->nlabs--;
    if (l->jsloop) {
      if (s->k == S_IF || !ends_flow(s->body)) bs(b, "break;");
      bs(b, "}");
      cross_check(m, b, l);
    }
    break;
  }
  }
  if (stmt_traps(s)) bs(b, "if(JT)return 0;");
}
static void em_list(Em *m, Buf *b, Stmt *s, int guarded, int hi) {
  int open = 0;
  for (; s; s = s->next) {
    if (!guarded) {
      em_stmt(m, b, s, 0);
      continue;
    }
    if (!s->lo) {
      if (!open) bs(b, "if(!R){");
      open = 1;
      em_stmt(m, b, s, 0);
      continue;
    }
    if (open) bs(b, "}");
    open = 0;
    if (s->hi < hi) {
      bf(b, "if(R<=%d){", s->hi);
      em_stmt(m, b, s, 1);
      bs(b, "}");
    } else {
      em_stmt(m, b, s, 1);
      guarded = 0; /* past the statement that holds the list's last site, R is 0 */
    }
  }
  if (open) bs(b, "}");
}

/* Analysis: which functions are direct, their heights, which can trap. */
static void visit(int fi) {
  Func *f = &funcs[fi];
  if (f->state == 2) return;
  if (f->state == 1) { /* a cycle: every function on it is driven */
    f->direct = 0;
    f->height = 1 << 20;
    return;
  }
  f->state = 1;
  int h = 1, ok = !f->indirect && !all_driven;
  for (int i = 0; i < f->ncallees; i++) {
    int g = f->callees[i];
    visit(g);
    if (funcs[g].state == 1 || !funcs[g].direct) ok = 0;
    else if (funcs[g].height + 1 > h) h = funcs[g].height + 1;
  }
  if (f->state == 1 && f->height == (1 << 20)) ok = 0;
  if (h > depth_max) ok = 0;
  f->direct = ok;
  f->height = ok ? h : 0;
  f->state = 2;
}
static void analyse(void) {
  for (uint32_t i = nimports; i < nfuncs; i++) {
    funcs[i].direct = 1;
    funcs[i].state = 0;
    funcs[i].height = 0;
  }
  for (uint32_t i = nimports; i < nfuncs; i++) visit((int)i);
  /* A second pass: a function that called one found on a cycle only later must be driven too. */
  for (int changed = 1; changed;) {
    changed = 0;
    for (uint32_t i = nimports; i < nfuncs; i++) {
      Func *f = &funcs[i];
      if (!f->direct) continue;
      for (int k = 0; k < f->ncallees; k++)
        if (!funcs[f->callees[k]].direct) {
          f->direct = 0;
          f->height = 0;
          changed = 1;
          break;
        }
    }
  }
  for (uint32_t i = nimports; i < nfuncs; i++) funcs[i].maytrap = funcs[i].traps;
  for (int changed = 1; changed;) {
    changed = 0;
    for (uint32_t i = nimports; i < nfuncs; i++) {
      Func *f = &funcs[i];
      if (f->maytrap) continue;
      for (int k = 0; k < f->ncallees; k++)
        if (funcs[f->callees[k]].maytrap) {
          f->maytrap = 1;
          changed = 1;
          break;
        }
    }
  }
}

/* The JavaScript 1.0 runtime: the helpers the output calls. Kept in this file so one tool writes one file. No ++ or --
 * anywhere in the output: Netscape 3.04 refuses a "--" at column 254 of a line ("missing ) after for-loop control.",
 * tools/legacy/js11.mjs's seam rule), and i=i+1 costs the same, so js1c writes none rather than counting columns. */
static const char *RUNTIME =
  "var JM=new Object(),JS=new Object(),JE=new Object(),JY=new Object(),JF=new Object();\n"
  "var JP=0,JQ=0,JR=0,JT=0,JX=0,JN=0,JW=0,JB=-1,JMp=0,JMmax=65536,JO=0,JL2=Math.log(2),JPT=0,JK=0;\n"
  "function JMu(a,b){var l=(a&65535)*(b&65535),h=((a>>>16)*(b&65535)&65535)+((a&65535)*(b>>>16)&65535);"
  "return (l+h*65536)%4294967296|0;}\n"
  "function JDs(a,b){if(b==0||(a==-2147483648&&b==-1)){JT=1;return 0;}return a/b|0;}\n"
  "function JDu(a,b){if(b==0){JT=1;return 0;}return (a>>>0)/(b>>>0)|0;}\n"
  "function JRs(a,b){if(b==0){JT=1;return 0;}return a%b|0;}\n"
  "function JRu(a,b){if(b==0){JT=1;return 0;}return (a>>>0)%(b>>>0)|0;}\n"
  "function JRl(x,k){k=k&31;return x<<k|x>>>(32-k);}\n"
  "function JRr(x,k){k=k&31;return x>>>k|x<<(32-k);}\n"
  "function JCz(x){var n=0;if(x==0)return 32;while(x>0){n=n+1;x=x<<1;}return n;}\n"
  "function JCt(x){var n=0;if(x==0)return 32;while((x&1)==0){n=n+1;x=x>>1;}return n;}\n"
  "function JPc(x){var n=0;x=x>>>0;while(x!=0){n=n+1;x=(x&(x-1))>>>0;}return n;}\n"
  "function JLbu(a){return JM[a>>2]>>>((a&3)<<3)&255;}\n"
  "function JLbs(a){return JM[a>>2]<<(24-((a&3)<<3))>>24;}\n"
  "function JLhu(a){return JM[a>>2]>>>((a&2)<<3)&65535;}\n"
  "function JLhs(a){return JM[a>>2]<<(16-((a&2)<<3))>>16;}\n"
  "function JLgu(a){if(a&1)return JLbu(a)|JLbu(a+1)<<8;return JLhu(a);}\n"
  "function JLgs(a){return JLgu(a)<<16>>16;}\n"
  "function JLw(a){var s=(a&3)<<3,i=a>>2;if(s==0)return JM[i]|0;return JM[i]>>>s|JM[i+1]<<(32-s);}\n"
  "function JWb(a,v){var i=a>>2,s=(a&3)<<3;JM[i]=JM[i]&~(255<<s)|(v&255)<<s;}\n"
  "function JWh(a,v){var i=a>>2,s=(a&2)<<3;if(a&1){JWb(a,v);JWb(a+1,v>>8);return;}JM[i]=JM[i]&~(65535<<s)|(v&65535)<<s;}\n"
  "function JWw(a,v){var s=(a&3)<<3,i=a>>2;if(s==0){JM[i]=v|0;return;}JM[i]=JM[i]&~(-1<<s)|v<<s;"
  "JM[i+1]=JM[i+1]&-1<<s|v>>>(32-s);}\n"
  "function JP2(e){var i,v;if(!JPT){JPT=new Object();v=1;for(i=0;i<=1023;i=i+1){JPT[i+1074]=v;v=v*2;}"
  "v=1;for(i=0;i>=-1074;i=i-1){JPT[i+1074]=v;v=v/2;}}return JPT[e+1074];}\n"
  "function JWD(lo,hi){var e=hi>>>20&2047,m=(hi&1048575)*4294967296+(lo>>>0),s=1;if(hi<0)s=-1;"
  "if(e==2047){if(m)return 0/0;return s/0;}if(e==0)return s*m*JP2(-1074);return s*(m/4503599627370496+1)*JP2(e-1023);}\n"
  "function JDW(x){var s=0,e,m,h;if(x!=x){JX=2146959360;return 0;}if(x<0||(x==0&&1/x<0)){s=-2147483648;x=-x;}"
  "if(x==1/0){JX=s|2146435072;return 0;}if(x==0){JX=s;return 0;}e=Math.floor(Math.log(x)/JL2);"
  "if(e>1023)e=1023;if(e<-1074)e=-1074;while(e>-1074&&JP2(e)>x)e=e-1;while(e<1023&&JP2(e+1)<=x)e=e+1;"
  "if(e<-1022){m=x*JP2(1022)*4503599627370496;e=0;}else{m=(x/JP2(e)-1)*4503599627370496;e=e+1023;}"
  "h=Math.floor(m/4294967296);JX=s|e<<20|h;return m-h*4294967296|0;}\n"
  "function JLd(a){return JWD(JLw(a),JLw(a+4));}\n"
  "function JWd(a,x){var lo=JDW(x);JWw(a,lo);JWw(a+4,JX);}\n"
  "function JTr(x){if(x<0)return Math.ceil(x);return Math.floor(x);}\n"
  "function JNr(x){var r=Math.floor(x),d=x-r;if(x!=x||r==x)return x;if(d>0.5)r=r+1;"
  "else if(d==0.5){if(r-2*Math.floor(r/2)!=0)r=r+1;}if(r==0&&x<0)return -0;return r;}\n"
  "function JMn(a,b){if(a!=a)return a;if(b!=b)return b;if(a==b){if(a==0&&1/a<0)return a;return b;}if(a<b)return a;return b;}\n"
  "function JMx(a,b){if(a!=a)return a;if(b!=b)return b;if(a==b){if(a==0&&1/a>0)return a;return b;}if(a>b)return a;return b;}\n"
  "function JCs(a,b){if(a<0||(a==0&&1/a<0))a=-a;if(b<0||(b==0&&1/b<0))return -a;return a;}\n"
  "function JTs(x){if(x>-2147483649&&x<2147483648)return x|0;JT=1;return 0;}\n"
  "function JTu(x){if(x>-1&&x<4294967296)return x|0;JT=1;return 0;}\n"
  "function JSs(x){if(x!=x)return 0;if(x<=-2147483648)return -2147483648;if(x>=2147483647)return 2147483647;return x|0;}\n"
  "function JSu(x){if(x!=x||x<=-1)return 0;if(x>=4294967295)return -1;return x|0;}\n"
  "function JMg(n){var o=JMp,i;n=n>>>0;if(JMp+n>JMmax)return -1;for(i=JMp*16384;i<(JMp+n)*16384;i=i+1)JM[i]=0;JMp=JMp+n;return o;}\n"
  "function JMf(d,v,n){var e=(d>>>0)+(n>>>0);v=v&255;d=d>>>0;while(d<e&&(d&3)!=0){JWb(d,v);d=d+1;}"
  "v=v|v<<8|v<<16|v<<24;while(d+4<=e){JM[d>>2]=v;d=d+4;}while(d<e){JWb(d,v);d=d+1;}}\n"
  "function JMc(d,s,n){var i;d=d>>>0;s=s>>>0;n=n>>>0;if(((d|s|n)&3)==0){if(d<=s){for(i=0;i<n;i=i+4)JM[(d+i)>>2]=JM[(s+i)>>2]|0;}"
  "else{for(i=n-4;i>=0;i=i-4)JM[(d+i)>>2]=JM[(s+i)>>2]|0;}return;}"
  "if(d<=s){for(i=0;i<n;i=i+1)JWb(d+i,JLbu(s+i));}else{for(i=n-1;i>=0;i=i-1)JWb(d+i,JLbu(s+i));}}\n"
  "var JZ0=0,JZ1=0;function Jzsf(x){JZ0=JDW(x);JZ1=JX;}function Jzlf(){return JWD(JZ0,JZ1);}\n"
  "function Jzsi(k,v){if(k)JZ1=v|0;else JZ0=v|0;}function Jzli(k){if(k)return JZ1;return JZ0;}\n"
  "function Jgo(n){var k=JB;while(n){if(k==0){JN=n;return 1;}k=k-1;n=JF[n]();}JN=0;return 0;}\n"
  "function Jmore(){if(Jgo(JN))return 1;JP=JW;JQ=0;JO.js1_paused=0;if(JT)JO.js1_trap=1;JO.js1_result=JR;return 0;}\n"
  "function Jdz(s){var i=0,n=s.length,j,t,d,k=JK;while(i<n){j=s.indexOf(\",\",i);t=s.substring(i,j);d=t.indexOf(\".\");"
  "if(d>=0){k=k+parseInt(t.substring(0,d),36);t=t.substring(d+1,t.length);}JM[k]=parseInt(t,36)|0;k=k+1;i=j+1;}JK=k;}\n";

static void b36(Buf *b, uint32_t v) {
  char t[16];
  int n = 0;
  do {
    t[n++] = "0123456789abcdefghijklmnopqrstuvwxyz"[v % 36];
    v /= 36;
  } while (v);
  while (n) bput(b, &t[--n], 1);
}

static int is_ident(const char *s) {
  if (!*s || (*s >= '0' && *s <= '9')) return 0;
  for (const char *p = s; *p; p++)
    if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_')) return 0;
  return 1;
}

int main(int argc, char **argv) {
  int a = 1;
  for (; a < argc && argv[a][0] == '-'; a++) {
    if (!strcmp(argv[a], "-d") && a + 1 < argc) depth_max = atoi(argv[++a]);
    else if (!strcmp(argv[a], "-a")) all_driven = 1;
    else if (!strcmp(argv[a], "-s")) data_stmts = 1;
    else if (!strcmp(argv[a], "-q")) quiet = 1;
    else {
      fprintf(stderr, "usage: js1c [-d depth] [-a] [-s] [-q] in.wasm out.js\n");
      return 2;
    }
  }
  if (argc - a != 2) {
    fprintf(stderr, "usage: js1c [-d depth] [-a] [-s] [-q] in.wasm out.js\n");
    return 2;
  }
  in_name = argv[a];
  if (depth_max < 1) depth_max = 1;
  FILE *fp = fopen(argv[a], "rb");
  if (!fp) die("cannot open it");
  fseek(fp, 0, SEEK_END);
  long sz = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  if (sz < 8) die("not a wasm module");
  uint8_t *buf = xalloc((size_t)sz);
  if (fread(buf, 1, (size_t)sz, fp) != (size_t)sz) die("cannot read it");
  fclose(fp);
  if (memcmp(buf, "\0asm\1\0\0\0", 8)) die("not a wasm module (version 1)");
  Rd r = { buf + 8, buf + sz };
  uint32_t ndefined = 0, ncode = 0;
  uint32_t *deftypes = NULL;
  while (r.p < r.end) {
    uint8_t id = rb(&r);
    uint32_t len = ru(&r);
    if ((size_t)(r.end - r.p) < len) die("a section runs past the end");
    Rd s = { r.p, r.p + len };
    r.p += len;
    switch (id) {
    case 0: break;
    case 1:
      ntypes = ru(&s);
      types = xalloc(sizeof *types * ntypes);
      for (uint32_t i = 0; i < ntypes; i++) {
        if (rb(&s) != 0x60) die("a type that is not a function type");
        FType *t = &types[i];
        t->np = (int)ru(&s);
        t->p = xalloc((size_t)t->np);
        for (int k = 0; k < t->np; k++) {
          t->p[k] = rb(&s);
          check_type(t->p[k], "a function type");
        }
        uint32_t nr = ru(&s);
        if (nr > 1) die("a function with %u results (multivalue): not handled", nr);
        t->res = nr ? rb(&s) : 0;
        if (nr) check_type(t->res, "a function type");
        /* The same signature, the same number, for call_indirect's check. */
        t->canon = (int)i;
        for (uint32_t k = 0; k < i; k++)
          if (types[k].np == t->np && types[k].res == t->res && !memcmp(types[k].p, t->p, (size_t)t->np)) {
            t->canon = types[k].canon;
            break;
          }
      }
      break;
    case 2: {
      uint32_t n = ru(&s);
      for (uint32_t i = 0; i < n; i++) {
        char *mod = rname(&s), *name = rname(&s);
        uint8_t kind = rb(&s);
        if (kind != 0) die("an import of %s.%s that is not a function (kind %u): not handled", mod, name, kind);
        uint32_t ti = ru(&s);
        if (ti >= ntypes) die("a bad type index");
        funcs = xgrow(funcs, sizeof *funcs * (nfuncs + 1));
        Func *f = &funcs[nfuncs++];
        memset(f, 0, sizeof *f);
        f->type = ti;
        f->mod = mod;
        f->name = name;
        int k = 0;
        for (; k < nmods; k++)
          if (!strcmp(mods[k], mod)) break;
        if (k == nmods) {
          mods = xgrow(mods, sizeof(char *) * (size_t)(nmods + 1));
          mods[nmods++] = mod;
        }
        f->import = k;
        nimports++;
      }
      break;
    }
    case 3:
      ndefined = ru(&s);
      deftypes = xalloc(sizeof(uint32_t) * ndefined);
      for (uint32_t i = 0; i < ndefined; i++) {
        deftypes[i] = ru(&s);
        if (deftypes[i] >= ntypes) die("a bad type index");
      }
      funcs = xgrow(funcs, sizeof *funcs * (nfuncs + ndefined));
      for (uint32_t i = 0; i < ndefined; i++) {
        Func *f = &funcs[nfuncs + i];
        memset(f, 0, sizeof *f);
        f->type = deftypes[i];
        f->import = -1;
      }
      nfuncs += ndefined;
      break;
    case 4: {
      uint32_t n = ru(&s);
      if (n > 1) die("more than one table");
      if (n) {
        if (rb(&s) != 0x70) die("a table that is not of funcref");
        uint8_t fl = rb(&s);
        table_min = ru(&s);
        if (fl & 1) ru(&s);
        has_table = 1;
      }
      break;
    }
    case 5: {
      uint32_t n = ru(&s);
      if (n > 1) die("more than one memory");
      if (n) {
        uint8_t fl = rb(&s);
        if (fl & ~1) die("a shared or 64-bit memory");
        mem_min = ru(&s);
        if (fl & 1) mem_max = ru(&s);
        has_memory = 1;
      }
      break;
    }
    case 6:
      nglobals = ru(&s);
      globals = xalloc(sizeof *globals * nglobals);
      for (uint32_t i = 0; i < nglobals; i++) {
        Global *g = &globals[i];
        g->ty = rb(&s);
        check_type(g->ty, "a global");
        g->mut = rb(&s);
        uint8_t op = rb(&s);
        if (op == 0x41) g->i = (int32_t)rsl(&s, 32);
        else if (op == 0x44) g->f = rf64(&s);
        else die("a global whose initializer is not a constant");
        if (rb(&s) != 0x0b) die("a global initializer of more than one instruction");
      }
      break;
    case 7:
      nexports = ru(&s);
      exports = xalloc(sizeof *exports * nexports);
      for (uint32_t i = 0; i < nexports; i++) {
        exports[i].name = rname(&s);
        exports[i].kind = rb(&s);
        exports[i].idx = ru(&s);
      }
      break;
    case 8: start_fn = ru(&s); break;
    case 9: {
      uint32_t n = ru(&s);
      for (uint32_t i = 0; i < n; i++) {
        uint32_t fl = ru(&s);
        if (fl != 0) die("an element segment of kind %u: only active segments of function indices are handled", fl);
        if (rb(&s) != 0x41) die("an element offset that is not i32.const");
        uint32_t off = (uint32_t)rsl(&s, 32);
        if (rb(&s) != 0x0b) die("a bad element offset");
        uint32_t k = ru(&s);
        if (off + k > table_n) {
          table = xgrow(table, sizeof(int32_t) * (off + k));
          for (uint32_t t = table_n; t < off + k; t++) table[t] = -1;
          table_n = off + k;
        }
        for (uint32_t t = 0; t < k; t++) table[off + t] = (int32_t)ru(&s);
      }
      break;
    }
    case 10:
      ncode = ru(&s);
      if (ncode != ndefined) die("the code section does not match the function section");
      for (uint32_t i = 0; i < ncode; i++) {
        uint32_t n = ru(&s);
        Rd body = { s.p, s.p + n };
        s.p += n;
        Func *f = &funcs[nimports + i];
        FType *t = &types[f->type];
        uint32_t ng = ru(&body), nl = (uint32_t)t->np;
        const uint8_t *save = body.p;
        for (uint32_t k = 0; k < ng; k++) {
          nl += ru(&body);
          uint8_t ty = rb(&body);
          check_type(ty, "a local");
        }
        f->nlocals = nl;
        f->ltype = xalloc(nl);
        memcpy(f->ltype, t->p, (size_t)t->np);
        body.p = save;
        uint32_t at = (uint32_t)t->np;
        for (uint32_t k = 0; k < ng; k++) {
          uint32_t c = ru(&body);
          uint8_t ty = rb(&body);
          for (uint32_t q = 0; q < c; q++) f->ltype[at++] = ty;
        }
        f->code = body.p;
        f->code_end = body.end;
      }
      break;
    case 11: {
      uint32_t n = ru(&s);
      for (uint32_t i = 0; i < n; i++) {
        uint32_t fl = ru(&s);
        if (fl == 1) die("a passive data segment: not handled");
        if (fl == 2) ru(&s);
        if (rb(&s) != 0x41) die("a data offset that is not i32.const");
        uint32_t off = (uint32_t)rsl(&s, 32);
        if (rb(&s) != 0x0b) die("a bad data offset");
        uint32_t k = ru(&s);
        if ((size_t)(s.end - s.p) < k) die("a data segment runs past its section");
        segs = xgrow(segs, sizeof *segs * (nsegs + 1));
        segs[nsegs++] = (Seg){ off, s.p, k };
        s.p += k;
      }
      break;
    }
    case 12: break;
    default: die("a section %u, which js1c does not know", id);
    }
  }
  if (ncode != ndefined) die("no code section");

  for (uint32_t i = nimports; i < nfuncs; i++) build(&funcs[i]);
  for (uint32_t i = 0; i < table_n; i++)
    if (table[i] >= 0) {
      if ((uint32_t)table[i] >= nfuncs) die("a table element names no function");
      funcs[table[i]].intable = 1;
    }
  for (uint32_t i = 0; i < nexports; i++)
    if (exports[i].kind == 0) {
      if (exports[i].idx >= nfuncs) die("an export names no function");
      if (exports[i].idx < nimports) die("an export of an import: not handled");
      funcs[exports[i].idx].exported = 1;
    }
  analyse();
  for (uint32_t i = 0; i < table_n; i++)
    if (table[i] >= 0 && (uint32_t)table[i] < nimports) die("an import in the table: not handled");

  Buf out = { 0 };
  bf(&out, "/* %s: compiled to JavaScript 1.0 by tools/js1c (docs/js1-compile-design.md), do not edit. */\n", in_name);
  bs(&out, RUNTIME);
  bf(&out, "JMp=%u;", mem_min);
  if (mem_max != 65536) bf(&out, "JMmax=%u;", mem_max);
  bs(&out, "\n");
  for (int k = 0; k < nmods; k++) bf(&out, "var Ji%d=0;\n", k);
  for (uint32_t g = 0; g < nglobals; g++) {
    bf(&out, "var Jg%u=", g);
    if (globals[g].ty == T_F64) fconst(&out, globals[g].f);
    else bf(&out, "%d", globals[g].i);
    bs(&out, ";\n");
  }
  int ndirect = 0, ndriven = 0, nsites_all = 0, hmax = 0;
  for (uint32_t i = nimports; i < nfuncs; i++) {
    Func *f = &funcs[i];
    FType *t = &types[f->type];
    cur_nlocals = (int)f->nlocals;
    nvars = (int)f->nlocals + f->maxsp;
    nwords = (nvars + 63) / 64;
    if (!nwords) nwords = 1;
    site_n = 0;
    if (!f->direct) number_list(f->body);
    f->nsites = site_n;
    nsites_all += site_n;
    uint64_t *empty = bset();
    uint64_t *in = live_list(f->body, empty);
    Em m = { .f = f, .fi = (int)i, .driven = !f->direct };
    Buf body = { 0 };
    em_list(&m, &body, f->body, f->nsites > 0, f->nsites);
    Buf head = { 0 };
    if (f->direct) {
      ndirect++;
      if (f->height > hmax) hmax = f->height;
      bf(&head, "function Jd%u(", i);
      for (int k = 0; k < t->np; k++) {
        if (k) bs(&head, ",");
        lname(&head, k);
      }
      bs(&head, "){");
    } else {
      ndriven++;
      bf(&head, "function Jf%u(){", i);
    }
    /* The variables: the locals (the direct one's parameters are its own), the temporaries, R and B. */
    int first = 1;
    for (int v = f->direct ? t->np : 0; v < nvars; v++) {
      bs(&head, first ? "var " : ",");
      first = 0;
      vname(&head, v);
    }
    if (f->nsites) {
      bs(&head, first ? "var " : ",");
      first = 0;
      bs(&head, "R=0");
    }
    if (m.usesB) {
      bs(&head, first ? "var " : ",");
      first = 0;
      bs(&head, "B=0");
    }
    if (!first) bs(&head, ";");
    Buf init = { 0 };
    if (!f->direct)
      for (int k = 0; k < t->np; k++) {
        lname(&init, k);
        bf(&init, "=Ja%d;", k);
      }
    for (int v = t->np; v < (int)f->nlocals; v++)
      if (bhas(in, v)) {
        lname(&init, v);
        bs(&init, "=0;");
      }
    if (f->nsites) {
      bs(&head, "if(JQ){JQ=0;R=JS[JP-2];}");
      if (init.n) {
        bs(&head, "else{");
        bs(&head, init.p);
        bs(&head, "}");
      }
    } else if (init.p) bs(&head, init.p);
    bput(&out, head.p, head.n);
    if (body.p) bput(&out, body.p, body.n);
    bs(&out, "}\n");
    free(head.p);
    free(body.p);
    free(init.p);
    free(empty);
    free(in);
    /* A direct function in the table or behind the driver gets a driven wrapper. */
    if (f->direct && f->intable) {
      bf(&out, "function Jf%u(){", i);
      if (t->res) bs(&out, "JR=");
      bf(&out, "Jd%u(", i);
      for (int k = 0; k < t->np; k++) bf(&out, "%sJa%d", k ? "," : "", k);
      bs(&out, ");if(JT)return 0;JQ=1;return JS[JP-1];}\n");
    }
  }
  /* Export wrappers. */
  for (uint32_t e = 0; e < nexports; e++) {
    if (exports[e].kind != 0) continue;
    uint32_t fi = exports[e].idx;
    Func *f = &funcs[fi];
    FType *t = &types[f->type];
    bf(&out, "function Jx%u(", e);
    for (int k = 0; k < t->np; k++) bf(&out, "%s%c", k ? "," : "", 'a' + k % 26);
    if (t->np > 26) die("an export of more than 26 parameters");
    if (f->direct) {
      bs(&out, "){var r;JT=0;JO.js1_trap=0;r=");
      bf(&out, "Jd%u(", fi);
      for (int k = 0; k < t->np; k++) bf(&out, "%s%c%s", k ? "," : "", 'a' + k, t->p[k] == T_F64 ? "-0" : "|0");
      bs(&out, ");if(JT){JO.js1_trap=1;return 0;}");
      bs(&out, t->res ? "return r;}\n" : "return 0;}\n");
    } else {
      bs(&out, "){var p=JP;JT=0;JO.js1_trap=0;");
      for (int k = 0; k < t->np; k++) bf(&out, "Ja%d=%c%s;", k, 'a' + k, t->p[k] == T_F64 ? "-0" : "|0");
      bf(&out, "JS[p]=0;JS[p+1]=0;JP=p+2;JQ=0;if(Jgo(%u)){JW=p;JO.js1_paused=1;return 0;}JP=p;JQ=0;", fi + 1);
      bs(&out, "if(JT){JO.js1_trap=1;return 0;}");
      bs(&out, t->res ? "return JR;}\n" : "return 0;}\n");
    }
  }
  /* Memory: the data's words. */
  uint32_t dend = 0;
  for (uint32_t i = 0; i < nsegs; i++)
    if (segs[i].off + segs[i].n > dend) dend = segs[i].off + segs[i].n;
  uint32_t nw = (dend + 3) / 4;
  uint32_t *words = xalloc(sizeof(uint32_t) * (nw + 1));
  for (uint32_t i = 0; i < nsegs; i++)
    for (uint32_t k = 0; k < segs[i].n; k++) {
      uint32_t x = segs[i].off + k;
      words[x >> 2] = (words[x >> 2] & ~(255u << (8 * (x & 3)))) | (uint32_t)segs[i].p[k] << (8 * (x & 3));
    }
  int ninit = 0;
  bs(&out, "function Jdata0(){");
  if (data_stmts) {
    int inthis = 0;
    for (uint32_t w = 0; w < nw; w++) {
      if (!words[w]) continue;
      if (inthis == 512) {
        bf(&out, "}\nfunction Jdata%d(){", ++ninit);
        inthis = 0;
      }
      bf(&out, "JM[%u]=%d;", w, (int32_t)words[w]);
      inthis++;
    }
  } else {
    Buf chunk = { 0 };
    uint32_t last = 0;
    int started = 0;
    for (uint32_t w = 0; w < nw; w++) {
      if (!words[w]) continue;
      if (!started) {
        bf(&out, "JK=%u;", w);
        started = 1;
        last = w;
      } else if (w > last + 1) {
        b36(&chunk, w - last - 1);
        bs(&chunk, ".");
      }
      b36(&chunk, words[w]);
      bs(&chunk, ",");
      last = w;
      if (chunk.n >= 4000) {
        bs(&out, "Jdz(\"");
        bs(&out, chunk.p);
        bs(&out, "\");");
        chunk.n = 0;
      }
    }
    if (chunk.n) {
      bs(&out, "Jdz(\"");
      bs(&out, chunk.p);
      bs(&out, "\");");
    }
    free(chunk.p);
  }
  bs(&out, "}\n");
  /* The factory. */
  bs(&out, "function cdomFactoryJs1(imp){var x;JM=new Object();JS=new Object();JP=0;JT=0;");
  for (int k = 0; k < nmods; k++) {
    if (js_name(mods[k])) bf(&out, "Ji%d=imp.%s;", k, mods[k]);
    else bf(&out, "Ji%d=imp[\"%s\"];", k, mods[k]);
  }
  /* Every word of the initial memory is written before the data: Netscape 2.02 dies on undefined | 0 ("33041 is not a
   * number.", docs/js1-compile-design.md section 12), so a word nothing wrote must not be read as undefined. Written in
   * order from 0, which also keeps a modern engine's object an array (rec(1000) 9 times faster in node than a dictionary
   * whose first number key is the data's, high above the stack). memory.grow writes its new words the same way (JMg). */
  bf(&out, "for(x=0;x<%u;x=x+1)JM[x]=0;", (unsigned)((uint64_t)mem_min * 16384));
  for (int k = 0; k <= ninit; k++) bf(&out, "Jdata%d();", k);
  for (uint32_t g = 0; g < nglobals; g++) {
    bf(&out, "Jg%u=", g);
    if (globals[g].ty == T_F64) fconst(&out, globals[g].f);
    else bf(&out, "%d", globals[g].i);
    bs(&out, ";");
  }
  bf(&out, "JMp=%u;\n", mem_min);
  for (uint32_t i = nimports; i < nfuncs; i++)
    if (!funcs[i].direct || funcs[i].intable) bf(&out, "JF[%u]=Jf%u;", i + 1, i);
  bs(&out, "\n");
  for (uint32_t i = 0; i < table_n; i++)
    if (table[i] >= 0) bf(&out, "JE[%u]=%d;JY[%d]=%d;", i, table[i] + 1, table[i] + 1, types[funcs[table[i]].type].canon);
  bs(&out, "\nJO=new Object();x=new Object();x.words=JM;JO.memory=x;JO.js1_trap=0;JO.js1_paused=0;JO.js1_result=0;JO.js1_more=Jmore;");
  for (uint32_t e = 0; e < nexports; e++) {
    if (exports[e].kind != 0) continue;
    if (js_name(exports[e].name)) bf(&out, "JO.%s=Jx%u;", exports[e].name, e);
    else bf(&out, "JO[\"%s\"]=Jx%u;", exports[e].name, e);
  }
  if (start_fn >= 0) {
    if ((uint64_t)start_fn < nimports) die("a start function that is an import");
    for (uint32_t e = 0; e < nexports; e++) (void)e;
    bf(&out, "Jstart%u();", (unsigned)start_fn);
  }
  bs(&out, "return JO;}\n");
  if (start_fn >= 0) {
    Func *f = &funcs[start_fn];
    if (f->direct) bf(&out, "function Jstart%u(){Jd%u();}\n", (unsigned)start_fn, (unsigned)start_fn);
    else bf(&out, "function Jstart%u(){var p=JP;JS[p]=0;JS[p+1]=0;JP=p+2;JQ=0;Jgo(%u);JP=p;JQ=0;}\n", (unsigned)start_fn,
            (unsigned)start_fn + 1);
  }
  bs(&out, "window.cdomFactory=cdomFactoryJs1;\n");
  (void)has_memory;
  (void)has_table;
  (void)table_min;

  FILE *o = fopen(argv[a + 1], "wb");
  if (!o) die("cannot write %s", argv[a + 1]);
  if (fwrite(out.p, 1, out.n, o) != out.n) die("cannot write %s", argv[a + 1]);
  fclose(o);
  int bound = 4 + hmax;
  if (!quiet)
    fprintf(stderr, "js1c: %s: %u functions (%d direct, %d driven, %d call sites), %u bytes; JS depth bound %d\n",
            argv[a + 1], nfuncs - nimports, ndirect, ndriven, nsites_all, (unsigned)out.n, bound);
  if (bound > 32) die("the JavaScript call depth could reach %d, over the bound of 32", bound);
  return 0;
}
