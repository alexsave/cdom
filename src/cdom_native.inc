/* Experiment (branch direct-calls), arm (b) "direct-native": every op calls the browser's own
 * DOM method, imported as Function.prototype.call.bind(method), so no JS frame sits between
 * wasm and the DOM. Nodes, strings, templates and the walk state live in externref tables.
 * Included by cdom.c in place of the op-stream emitters when CDOM_NATIVE is 1. */
typedef __externref_t ref;
#define NIMP(n) __attribute__((import_module("n"), import_name(n)))
NIMP("createElement") ref n_create_element(ref doc, ref tag);
NIMP("createTextNode") ref n_create_text(ref doc, ref s);
NIMP("createTextNode") ref n_create_text_num(ref doc, uint32_t n);
NIMP("appendChild") ref n_append(ref parent, ref child);
NIMP("insertBefore") ref n_insert(ref parent, ref child, ref before);
NIMP("setAttribute") void n_set_attr(ref el, ref name, ref value);
NIMP("className") void n_set_class(ref el, ref value);
NIMP("nodeValue") void n_set_value(ref node, ref value);
NIMP("nodeValue") void n_set_value_num(ref node, uint32_t n);
NIMP("textContent") void n_set_content(ref node, ref value);
NIMP("cloneNode") ref n_clone(ref node, uint32_t deep);
NIMP("firstChild") ref n_first(ref node);
NIMP("nextSibling") ref n_next(ref node);
NIMP("parentNode") ref n_parent(ref node);
NIMP("remove") void n_remove(ref el);
NIMP("set") uint32_t n_reflect_set(ref obj, ref key, uint32_t v); /* Reflect.set: the $i expando */
NIMP("listen") void n_listen(uint32_t type);                     /* arrow: installs one delegated listener */

/* Tables: nodes by id, strings by id, templates, template slots (t * CD_SLOTS + k), the open
 * element stack, pending slots, parent/next of detached nodes, and registers. */
static ref TN[0], TS[0], TT[0], TSL[0], TST[0], TP[0], TDP[0], TDN[0], TR[0];
enum { R_LAST, R_ROOT, R_DOC, R_KEY_I, R_EMPTY, R_NREGS };
#define TGET(t, i) __builtin_wasm_table_get(t, i)
#define TSET(t, i, v) __builtin_wasm_table_set(t, i, v)
#define NUL __builtin_wasm_ref_null_extern()
#define FIT(t, n) do { uint32_t n_ = (n); if (n_ > (uint32_t)__builtin_wasm_table_size(t)) \
  (void)__builtin_wasm_table_grow(t, NUL, n_ * 2 - (uint32_t)__builtin_wasm_table_size(t)); } while (0)

static uint16_t *lmask; static uint32_t lmask_cap; static uint8_t listening[16];
EXPORT("cd_lmask") uint16_t *cd_lmask(void) { return lmask; }
EXPORT("cd_node") ref cd_node(uint32_t id) { return id < (uint32_t)__builtin_wasm_table_size(TN) ? TGET(TN, id) : NUL; }
EXPORT("cd_str_put") void cd_str_put(uint32_t id, ref s) { FIT(TS, id + 1); TSET(TS, id, s); }
EXPORT("cd_init") void cd_init(ref doc, ref mount, ref key_i, ref empty) {
  FIT(TR, R_NREGS); FIT(TN, 1);
  TSET(TR, R_DOC, doc); TSET(TR, R_KEY_I, key_i); TSET(TR, R_EMPTY, empty); TSET(TN, 0, mount);
}

/* A string must be in the table before a call takes it: new ones are decoded at once. */
static ref S_(cd_str s) {
  if (!sent[s]) { sent[s] = 1; push_str(s, s); cd_flush(); }
  return TGET(TS, s);
}
static uint32_t sp, base = CD_DETACHED, before_id;
static ref last(void) { return TGET(TR, R_LAST); }
static void set_last(ref v) { TSET(TR, R_LAST, v); }
static void place(ref n) {
  if (sp) n_append(TGET(TST, sp - 1), n);
  else if (base != CD_DETACHED) n_insert(TGET(TN, base), n, before_id ? TGET(TN, before_id) : NUL);
}
static void push(ref el) { FIT(TST, sp + 1); TSET(TST, sp++, el); set_last(el); }

void cd_at(cd_id parent, cd_id before) { base = parent; before_id = before; }
void cd_open(cd_str tag) {
  ref el = n_create_element(TGET(TR, R_DOC), S_(tag));
  if (sp) n_append(TGET(TST, sp - 1), el);
  push(el);
}
void cd_close(void) { ref el = TGET(TST, --sp); if (!sp) { TSET(TR, R_ROOT, el); place(el); } }
void cd_text(cd_str s) { ref t = n_create_text(TGET(TR, R_DOC), S_(s)); set_last(t); place(t); }
void cd_text_num(uint32_t n) { ref t = n_create_text_num(TGET(TR, R_DOC), n); set_last(t); place(t); }
void cd_attr(cd_str name, cd_str v) { n_set_attr(last(), S_(name), S_(v)); }
void cd_class(cd_str v) { n_set_class(last(), S_(v)); }
void cd_name(cd_id id) { FIT(TN, id + 1); TSET(TN, id, last()); }
void cd_listen(cd_id id, uint32_t ev) {
  FIT(TN, id + 1); TSET(TN, id, last());
  n_reflect_set(last(), TGET(TR, R_KEY_I), id);
  if (id >= lmask_cap) {
    uint32_t c = lmask_cap ? lmask_cap : 1024; while (c <= id) c *= 2;
    uint16_t *g = cd_alloc(c * 2); __builtin_memset(g, 0, c * 2);
    if (lmask) { __builtin_memcpy(g, lmask, lmask_cap * 2); cd_free(lmask); }
    lmask = g; lmask_cap = c;
  }
  lmask[id] |= (uint16_t)(1u << ev);
  if (!listening[ev]) { listening[ev] = 1; n_listen(ev); }
}
void cd_value(cd_str s) { n_set_value(last(), S_(s)); }
void cd_value_num(uint32_t n) { n_set_value_num(last(), n); }
static uint32_t slot_val[CD_TPLS][CD_SLOTS];
static uint8_t slot_kind[CD_TPLS][CD_SLOTS];
static int slot_same(uint32_t t, uint32_t k, uint8_t kind, uint32_t v) {
  if (t >= CD_TPLS || k >= CD_SLOTS) __builtin_trap();
  if (slot_kind[t][k] == kind && slot_val[t][k] == v) return 1;
  slot_kind[t][k] = kind; slot_val[t][k] = v;
  return 0;
}
void cd_tpl_save(uint32_t t) {
  if (t >= CD_TPLS) __builtin_trap();
  __builtin_memset(slot_kind[t], 0, CD_SLOTS);
  ref root = TGET(TR, R_ROOT);
  FIT(TT, t + 1); TSET(TT, t, root);
  FIT(TSL, (t + 1) * CD_SLOTS); FIT(TP, CD_SLOTS);
  for (uint32_t k = 0; k < CD_SLOTS; k++) { TSET(TSL, t * CD_SLOTS + k, TGET(TP, k)); TSET(TP, k, NUL); }
  n_remove(root);
}
void cd_clone(uint32_t t) { push(n_clone(TGET(TT, t), 1)); }
void cd_slot(uint32_t k) { if (k >= CD_SLOTS) __builtin_trap(); FIT(TP, CD_SLOTS); TSET(TP, k, last()); }
void cd_tpl_text(uint32_t t, uint32_t k, cd_str s) { if (!slot_same(t, k, 1, s)) n_set_value(TGET(TSL, t * CD_SLOTS + k), S_(s)); }
void cd_tpl_text_num(uint32_t t, uint32_t k, uint32_t n) { if (!slot_same(t, k, 2, n)) n_set_value_num(TGET(TSL, t * CD_SLOTS + k), n); }
void cd_down(void) { set_last(n_first(last())); }
void cd_next(void) { set_last(n_next(last())); }
void cd_up(void) { set_last(n_parent(last())); }
void cd_sel(cd_id id) { set_last(TGET(TN, id)); }
void cd_set_text(cd_id id, cd_str s) { n_set_value(TGET(TN, id), S_(s)); }
void cd_set_text_num(cd_id id, uint32_t n) { n_set_value_num(TGET(TN, id), n); }
void cd_set_attr(cd_id id, cd_str name, cd_str v) { n_set_attr(TGET(TN, id), S_(name), S_(v)); }
void cd_set_class(cd_id id, cd_str v) { n_set_class(TGET(TN, id), S_(v)); }
void cd_move(cd_id id, cd_id before) { ref el = TGET(TN, id); n_insert(n_parent(el), el, before ? TGET(TN, before) : NUL); }
void cd_clear(cd_id id) { n_set_content(TGET(TN, id), TGET(TR, R_EMPTY)); }
void cd_detach(cd_id id) {
  ref el = TGET(TN, id);
  FIT(TDP, id + 1); FIT(TDN, id + 1);
  TSET(TDP, id, n_parent(el)); TSET(TDN, id, n_next(el));
  n_remove(el);
}
void cd_attach(cd_id id) { n_insert(TGET(TDP, id), TGET(TN, id), TGET(TDN, id)); TSET(TDP, id, NUL); TSET(TDN, id, NUL); }
static void native_forget(cd_id id) { TSET(TN, id, NUL); if (id < lmask_cap) lmask[id] = 0; }
