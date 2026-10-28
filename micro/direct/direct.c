/* Per-call cost of DOM work driven from wasm: the same DOM call through (a) a small JS arrow
 * import, (b) the browser's own method imported as Function.prototype.call.bind(method), with
 * nodes and strings as externrefs in a wasm table, and (c) one batched op-list apply. */
#define IMPORT(n) __attribute__((import_module("d"), import_name(n)))
typedef __externref_t ref;
IMPORT("a_value") void a_value(int node, int str);          /* arrow: nodes[node].nodeValue = strs[str] */
IMPORT("b_value") void b_value(ref node, ref s);            /* native nodeValue setter */
IMPORT("a_first") void a_first(int node);                   /* arrow: last = nodes[node].firstChild */
IMPORT("b_first") ref b_first(ref node);                    /* native firstChild getter */
IMPORT("a_clone") void a_clone(int node);                   /* arrow: last = nodes[node].cloneNode(true) */
IMPORT("b_clone") ref b_clone(ref node, int deep);          /* native cloneNode */
IMPORT("a_move") void a_move(int parent, int node);         /* arrow: parent.insertBefore(node, null) */
IMPORT("b_move") void b_move(ref parent, ref node, ref before); /* native insertBefore */
IMPORT("a_empty") void a_empty(int x);                      /* arrow that does nothing */
IMPORT("apply") void apply(int nops);                        /* op list of nops (op, a, b) records */
static ref T[0];
static int ops[3 * 4096];
__attribute__((export_name("grow"))) int grow(int n) { return __builtin_wasm_table_grow(T, __builtin_wasm_ref_null_extern(), n); }
__attribute__((export_name("put"))) void put(int i, ref x) { __builtin_wasm_table_set(T, i, x); }
/* slots: 0 text node, 1 string, 2 tr (with children), 3 tbody, 4 a row to move */
__attribute__((export_name("run"))) void run(int which, int n) {
  ref text = __builtin_wasm_table_get(T, 0), s = __builtin_wasm_table_get(T, 1), tr = __builtin_wasm_table_get(T, 2);
  ref body = __builtin_wasm_table_get(T, 3), row = __builtin_wasm_table_get(T, 4), nul = __builtin_wasm_ref_null_extern();
  switch (which) {
  case 0: for (int i = 0; i < n; i++) a_empty(i); break;
  case 1: for (int i = 0; i < n; i++) a_value(0, 1); break;
  case 2: for (int i = 0; i < n; i++) b_value(text, s); break;
  case 3: for (int i = 0; i < n; i++) a_first(2); break;
  case 4: for (int i = 0; i < n; i++) (void)b_first(tr); break;
  case 5: for (int i = 0; i < n; i++) a_clone(2); break;
  case 6: for (int i = 0; i < n; i++) (void)b_clone(tr, 1); break;
  case 7: for (int i = 0; i < n; i++) a_move(3, 4); break;
  case 8: for (int i = 0; i < n; i++) b_move(body, row, nul); break;
  default: /* 9 + k: op list, op k as in which = 1 + 2(k-1) */ {
    int k = which - 9;
    for (int done = 0; done < n;) {
      int m = n - done < 4096 ? n - done : 4096;
      for (int i = 0; i < m; i++) { ops[3 * i] = k; ops[3 * i + 1] = k == 3 ? 3 : (k == 1 ? 0 : 2); ops[3 * i + 2] = k == 3 ? 4 : 1; }
      apply(m); done += m;
    }
  }
  }
}
__attribute__((export_name("ops"))) int *ops_ptr(void) { return ops; }
