#!/usr/bin/env python3
"""Generates one bench arm of cdom's bridge (the layer between C and the DOM) from the shipped
src/cdom.c, src/cdom.h and shim/cdom.js, which stay untouched.

    gen_arms.py <arm> <outdir> [--count]

writes <outdir>/cdom.c, cdom.h and cdom.js. Every arm keeps cdom's C API (cdom.h) and every C call
site: only the IMPORTS block of cdom.c (the j_* declarations) is replaced by the arm's own, plus,
for the fused arm, the additions listed under ARM e. --count wraps every import in a counter
(window.__cdomCross: calls per import, strings decoded, op-list records), for the untimed counting
pass only.

The arms (RESULTS.md says what each one measures):
  a     the shipped design: one import per DOM operation (port-fixes).
  list  the op list (cdom main's design, re-measured here): every void DOM operation is a 32-byte
        record in C memory, applied by one import, apply(), through one switch; any import that
        returns a value or reads the DOM, and every string batch, applies the pending list first.
  b     one generic dispatcher import, bridge(name, i0, i1, i2, i3, d): the name id picks a
        pre-resolved entry, which is a generic property set (node[prop] = arg), a generic method
        call (fn.call(target, arg, arg)), or, for the structural operations (open, close, clone,
        walk, ...), the shim's own function.
  c     a table of pre-resolved functions (the setters and methods themselves) called as
        fns[id].call(target, ...) through six imports, one per argument shape.
  d     imports bound at instantiation: Function.prototype.call.bind(Element.prototype.setAttribute)
        and friends, with nodes and strings mirrored into wasm externref tables so C can pass the
        receiver and the arguments; the list of natives is generated from the IMPORTS declarations
        through NATIVE below (numbers with a unit go through CSS Typed OM: CSS.px(v) and
        attributeStyleMap.set, since a native setProperty takes no number-plus-unit).
  e     fused operations (superinstructions) on top of a: a multi-property style op
        (cd_style_begin/num/str/end: one crossing, one node lookup, one .style), template slots
        sent with the clone that reads them (clone_slots), and freed ids sent in one call
        (free_n), the last two transparent to apps.
  f     a, with performance.now imported as Function.prototype.call.bind(Performance.prototype.now)
        and the performance object passed from an externref table: the one hot-path function with
        a Fast API C function ([NoAllocDirectCall], performance.idl:38); V8 takes it only behind
        --js-flags=--wasm-fast-api (an experimental flag, off in Chrome 156).
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ARMS = ("a", "list", "b", "c", "d", "e", "f")

# ---- the IMPORTS declarations, parsed ----
IMPORT_RE = re.compile(r'IMPORTS\("(\w+)"\)\s+([\w ]+?\**)\s*j_(\w+)\(([^)]*)\);', re.S)
BLOCK_START = '#define IMPORTS(n) __attribute__((import_module("cdom"), import_name(n)))\n'
BLOCK_END = 'IMPORTS("now") double j_now(void);\n'


class Import:
    def __init__(self, name, ret, fn, params):
        self.name, self.ret, self.fn = name, ret.strip(), fn
        self.params = []  # (type, name)
        params = " ".join(params.split())
        if params and params != "void":
            for p in params.split(","):
                m = re.match(r"(.*?)(\w+)$", p.strip())
                self.params.append((m.group(1).strip(), m.group(2)))

    def decl(self):
        return ", ".join(f"{t} {n}" if not t.endswith("*") else f"{t}{n}" for t, n in self.params) or "void"

    def slots(self):
        """Each parameter's slot in a generic call: i0..i3 for integers and pointers, d for a double."""
        out, k = [], 0
        for t, n in self.params:
            if t == "double":
                out.append("d")
            else:
                out.append(f"i{k}")
                k += 1
        if k > 4 or out.count("d") > 1:
            raise SystemExit(f"{self.name}: too many arguments for a generic call")
        return out

    def slot_args(self):
        """The arguments as the generic call passes them: (i0, i1, i2, i3, d)."""
        vals = {"i0": "0", "i1": "0", "i2": "0", "i3": "0", "d": "0"}
        for (t, n), s in zip(self.params, self.slots()):
            vals[s] = f"(uint32_t)(uintptr_t){n}" if t.endswith("*") else (n if s == "d" else f"(uint32_t){n}")
        return ", ".join(vals[s] for s in ("i0", "i1", "i2", "i3", "d"))


def parse_imports(c):
    start, end = c.index(BLOCK_START), c.index(BLOCK_END) + len(BLOCK_END)
    block = c[start:end]
    imports = [Import(*m.groups()) for m in IMPORT_RE.finditer(block)]
    if len(imports) != block.count('IMPORTS("'):
        raise SystemExit("an IMPORTS declaration did not parse")
    return start, end, block, imports


# Operations that are a DOM write or read per call (the bridge's subject). The rest are batches
# (strings), callbacks and media (timer, raf, win, viewport, encode, image_bytes): direct in every arm.
DOM_OPS = {"at", "open", "open_svg", "close", "text", "text_num", "attr", "class", "name", "name_open",
           "listen", "value", "value_num", "tpl_save", "clone", "slot", "tpl_text", "tpl_text_num", "walk",
           "walk_value", "walk_value_num", "set_text", "set_text_num", "set_attr", "set_class", "style",
           "style_num", "remove", "free", "move", "clear", "detach", "attach", "hit", "rect", "now"}
# The shim's function for an import name (where they differ).
SHIM_FN = {"class": "setClass"}

# ---- arm b: generic entries. kind SET (target[member] = arg0) or CALL (member.call(target, arg0, arg1));
# target NODE (nodes[i0]), LAST (the node made or walked to last), STYLE (nodes[i0].style);
# an argument is S (stringOf), U (>>> 0), NU (d + stringOf: a number and its unit), E ("") of a slot.
GENERIC = {
    "set_text": ("SET", "NODE", "nodeValue", ["S1"]),
    "set_text_num": ("SET", "NODE", "nodeValue", ["U1"]),
    "set_attr": ("CALL", "NODE", "Element.prototype.setAttribute", ["S1", "S2"]),
    "set_class": ("SET", "NODE", "className", ["S1"]),
    "style": ("CALL", "STYLE", "CSSStyleDeclaration.prototype.setProperty", ["S1", "S2"]),
    "style_num": ("CALL", "STYLE", "CSSStyleDeclaration.prototype.setProperty", ["S1", "NU2"]),
    "clear": ("SET", "NODE", "textContent", ["E"]),
    "attr": ("CALL", "LAST", "Element.prototype.setAttribute", ["S0", "S1"]),
    "class": ("SET", "LAST", "className", ["S0"]),
    "value": ("SET", "LAST", "nodeValue", ["S0"]),
    "value_num": ("SET", "LAST", "nodeValue", ["U0"]),
}

# ---- arm c: the function table and the six call shapes ----
FNS = [  # (id, how JS resolves it)
    ("FN_NODE_VALUE", 'Object.getOwnPropertyDescriptor(Node.prototype, "nodeValue").set'),
    ("FN_CLASS_NAME", 'Object.getOwnPropertyDescriptor(Element.prototype, "className").set'),
    ("FN_TEXT_CONTENT", 'Object.getOwnPropertyDescriptor(Node.prototype, "textContent").set'),
    ("FN_SET_ATTRIBUTE", "Element.prototype.setAttribute"),
    ("FN_SET_PROPERTY", "CSSStyleDeclaration.prototype.setProperty"),
]
TABLE = {  # import -> (shape, fn id, target, the rest of the arguments)
    "set_text": ("call_s", "FN_NODE_VALUE", "id", ["s"]),
    "set_text_num": ("call_u", "FN_NODE_VALUE", "id", ["n"]),
    "set_attr": ("call_ss", "FN_SET_ATTRIBUTE", "id", ["name", "v"]),
    "set_class": ("call_s", "FN_CLASS_NAME", "id", ["v"]),
    "style": ("style_ss", "FN_SET_PROPERTY", "id", ["prop", "v"]),
    "style_num": ("style_sn", "FN_SET_PROPERTY", "id", ["prop", "v", "unit"]),
    "clear": ("call_e", "FN_TEXT_CONTENT", "id", []),
    "attr": ("call_ss", "FN_SET_ATTRIBUTE", "CD_TARGET_LAST", ["name", "v"]),
    "class": ("call_s", "FN_CLASS_NAME", "CD_TARGET_LAST", ["v"]),
    "value": ("call_s", "FN_NODE_VALUE", "CD_TARGET_LAST", ["s"]),
    "value_num": ("call_u", "FN_NODE_VALUE", "CD_TARGET_LAST", ["n"]),
}
SHAPES = {
    "call_s": ("uint32_t fn, cd_id target, uint32_t s", "fn, target, s", "fns[fn].call(targetOf(target), stringOf(s));"),
    "call_u": ("uint32_t fn, cd_id target, uint32_t n", "fn, target, n", "fns[fn].call(targetOf(target), n >>> 0);"),
    "call_ss": ("uint32_t fn, cd_id target, uint32_t a, uint32_t b", "fn, target, a, b",
                "fns[fn].call(targetOf(target), stringOf(a), stringOf(b));"),
    "call_e": ("uint32_t fn, cd_id target", "fn, target", 'fns[fn].call(targetOf(target), "");'),
    "style_ss": ("uint32_t fn, cd_id target, uint32_t a, uint32_t b", "fn, target, a, b",
                 "fns[fn].call(nodes[target].style, stringOf(a), stringOf(b));"),
    "style_sn": ("uint32_t fn, cd_id target, uint32_t a, double v, uint32_t unit", "fn, target, a, v, unit",
                 "fns[fn].call(nodes[target].style, stringOf(a), v + stringOf(unit));"),
}

# ---- arm d: natives. C name -> (C signature, how JS binds it). Bound with Function.prototype.call.bind
# unless the binding says otherwise; the needed list is what DIRECT_NATIVE's imports use.
NATIVE = {
    "n_node_value": ("void n_node_value(cd_ref node, cd_ref value)", 'call.bind(setter(Node.prototype, "nodeValue"))'),
    "n_class_name": ("void n_class_name(cd_ref element, cd_ref value)", 'call.bind(setter(Element.prototype, "className"))'),
    "n_text_content": ("void n_text_content(cd_ref node, cd_ref value)", 'call.bind(setter(Node.prototype, "textContent"))'),
    "n_set_attribute": ("void n_set_attribute(cd_ref element, cd_ref name, cd_ref value)", "call.bind(Element.prototype.setAttribute)"),
    "n_style": ("cd_ref n_style(cd_ref element)", 'call.bind(getter(HTMLElement.prototype, "style"))'),
    "n_set_property": ("void n_set_property(cd_ref style, cd_ref property, cd_ref value)",
                       "call.bind(CSSStyleDeclaration.prototype.setProperty)"),
    "n_style_map": ("cd_ref n_style_map(cd_ref element)", 'call.bind(getter(HTMLElement.prototype, "attributeStyleMap"))'),
    "n_style_map_set": ("void n_style_map_set(cd_ref map, cd_ref property, cd_ref value)",
                        "call.bind(StylePropertyMap.prototype.set)"),
}
# Typed OM factories per cdom unit (cdom.h CD_UNIT_*): CSS.px(v) and friends, called with no receiver.
UNITS = ["number", "px", "percent", "deg", "em", "rem", "vw", "vh", "ms", "s", "rad"]
for u in UNITS:
    NATIVE[f"n_css_{u}"] = (f"cd_ref n_css_{u}(double v)", f"CSS.{u}")
DIRECT_NATIVE = {  # import -> C body over the natives
    "set_text": "n_node_value(node_ref(id), str_ref(s));",
    "set_attr": "n_set_attribute(node_ref(id), str_ref(name), str_ref(v));",
    "set_class": "n_class_name(node_ref(id), str_ref(v));",
    "style": "n_set_property(n_style(node_ref(id)), str_ref(prop), str_ref(v));",
    "clear": "n_text_content(node_ref(id), str_ref(0));",
}


def c_arm_block(arm, block, imports):
    """The replacement for the IMPORTS block of cdom.c."""
    by = {i.name: i for i in imports}
    out = [BLOCK_START, f"/* bench arm {arm} (micro/bridge/gen_arms.py): this block replaces the shipped IMPORTS. */\n"]
    direct = lambda i: f'IMPORTS("{i.name}") {i.ret} j_{i.fn}({i.decl()});\n'
    if arm in ("a", "e"):
        return block  # the shipped declarations, verbatim
    if arm == "f":
        out += [direct(i) for i in imports if i.name != "now"]
        out.append("""typedef __externref_t cd_ref;
static cd_ref ref_misc[0]; /* 0: the page's performance object */
__attribute__((export_name("cd_ref_misc"))) void cd_ref_misc(uint32_t i, cd_ref r) {
  if (i >= __builtin_wasm_table_size(ref_misc)) __builtin_wasm_table_grow(ref_misc, __builtin_wasm_ref_null_extern(), i + 1);
  __builtin_wasm_table_set(ref_misc, i, r);
}
IMPORTS("n_now") double n_now(cd_ref performance); /* Function.prototype.call.bind(Performance.prototype.now) */
static inline double j_now(void) { return n_now(__builtin_wasm_table_get(ref_misc, 0)); }
""")
        return "".join(out)
    if arm == "list":
        listed = [i for i in imports if i.name in DOM_OPS and i.ret == "void" and i.name != "rect"]
        out.append("""typedef struct { uint32_t op, i0, i1, i2, i3, pad; double d; } list_rec; /* 32 bytes */
#define LIST_CAP 4096
static list_rec list_buf[LIST_CAP];
static uint32_t list_n;
IMPORTS("apply") void j_apply(const list_rec *ops, uint32_t n);
static void list_flush(void) {
  if (!list_n) return;
  uint32_t n = list_n;
  list_n = 0;
  j_apply(list_buf, n);
}
static inline list_rec *list_push(uint32_t op) {
  if (list_n == LIST_CAP) list_flush();
  list_rec *r = &list_buf[list_n++];
  r->op = op;
  return r;
}
""")
        out.append("enum { " + ", ".join(f"OP_{i.name.upper()}" for i in listed) + " };\n")
        for i in imports:
            if i in listed:
                sets = []
                for (t, n), s in zip(i.params, i.slots()):
                    sets.append(f"r->{s} = {n};" if s == "d" else f"r->{s} = (uint32_t){n};")
                if sets:
                    out.append(f"static inline void j_{i.fn}({i.decl()}) {{ list_rec *r = list_push(OP_{i.name.upper()}); "
                               + " ".join(sets) + " }\n")
                else:
                    out.append(f"static inline void j_{i.fn}({i.decl()}) {{ (void)list_push(OP_{i.name.upper()}); }}\n")
            else:  # applies the pending list first
                out.append(f'IMPORTS("{i.name}") {i.ret} jx_{i.fn}({i.decl()});\n')
                call = f"jx_{i.fn}({', '.join(n for _, n in i.params)})"
                ret = "" if i.ret == "void" else "return "
                out.append(f"static inline {i.ret} j_{i.fn}({i.decl()}) {{ list_flush(); {ret}{call}; }}\n")
        return "".join(out)
    if arm == "b":
        routed = [i for i in imports if i.name in DOM_OPS]
        out.append('IMPORTS("bridge") double j_bridge(uint32_t name, uint32_t i0, uint32_t i1, uint32_t i2, uint32_t i3, double d);\n')
        out.append("enum { " + ", ".join(f"BR_{i.name.upper()}" for i in routed) + " };\n")
        for i in imports:
            if i in routed:
                call = f"j_bridge(BR_{i.name.upper()}, {i.slot_args()})"
                body = call + ";" if i.ret == "void" else f"return ({i.ret}){call};"
                out.append(f"static inline {i.ret} j_{i.fn}({i.decl()}) {{ {body} }}\n")
            else:
                out.append(direct(i))
        return "".join(out)
    if arm == "c":
        out.append("#define CD_TARGET_LAST 0xffffffffu /* the node made or walked to last */\n")
        out.append("enum { " + ", ".join(f for f, _ in FNS) + " };\n")
        for shape, (decl, _, _) in SHAPES.items():
            out.append(f'IMPORTS("{shape}") void j_{shape}({decl});\n')
        for i in imports:
            if i.name in TABLE:
                shape, fn, target, rest = TABLE[i.name]
                out.append(f"static inline void j_{i.fn}({i.decl()}) {{ j_{shape}({', '.join([fn, target] + rest)}); }}\n")
            else:
                out.append(direct(i))
        return "".join(out)
    if arm == "d":
        out.append("""typedef __externref_t cd_ref;
/* The nodes C names and the strings JS holds, mirrored as references for the natives: the shim
 * puts each one in as it names or decodes it (cd_ref_node, cd_ref_str). */
static cd_ref ref_nodes[0], ref_strs[0], ref_tmps[0];
static void ref_put(int which, uint32_t i, cd_ref r) {
  if (which == 0) {
    if (i >= __builtin_wasm_table_size(ref_nodes)) __builtin_wasm_table_grow(ref_nodes, __builtin_wasm_ref_null_extern(), i + 1024);
    __builtin_wasm_table_set(ref_nodes, i, r);
  } else if (which == 1) {
    if (i >= __builtin_wasm_table_size(ref_strs)) __builtin_wasm_table_grow(ref_strs, __builtin_wasm_ref_null_extern(), i + 1024);
    __builtin_wasm_table_set(ref_strs, i, r);
  } else {
    if (i >= __builtin_wasm_table_size(ref_tmps)) __builtin_wasm_table_grow(ref_tmps, __builtin_wasm_ref_null_extern(), i + 1024);
    __builtin_wasm_table_set(ref_tmps, i, r);
  }
}
__attribute__((export_name("cd_ref_node"))) void cd_ref_node(uint32_t id, cd_ref r) { ref_put(0, id, r); }
__attribute__((export_name("cd_ref_str"))) void cd_ref_str(uint32_t id, cd_ref r) {
  if (id >= 0x40000000u) ref_put(2, id - 0x40000000u, r); else ref_put(1, id, r);
}
static inline cd_ref node_ref(cd_id id) { return __builtin_wasm_table_get(ref_nodes, id); }
static inline cd_ref str_ref(uint32_t s) {
  return s >= 0x40000000u ? __builtin_wasm_table_get(ref_tmps, s - 0x40000000u) : __builtin_wasm_table_get(ref_strs, s);
}
""")
        for name, (sig, _) in NATIVE.items():
            out.append(f'IMPORTS("{name}") {sig};\n')
        units = " ".join(f"case {k}: return n_css_{u}(v);" for k, u in enumerate(UNITS))
        out.append(f"static cd_ref css_unit(double v, uint32_t unit) {{ switch (unit) {{ {units} }} __builtin_trap(); }}\n")
        for i in imports:
            if i.name in DIRECT_NATIVE:
                out.append(f"static inline void j_{i.fn}({i.decl()}) {{ {DIRECT_NATIVE[i.name]} }}\n")
            else:
                out.append(direct(i))
        # numbers with a unit: C passes the unit's enum (cd_set_style_num is patched to call this one)
        out.append("static inline void j_style_num_unit(cd_id id, uint32_t prop, double v, uint32_t unit) {\n"
                   "  n_style_map_set(n_style_map(node_ref(id)), str_ref(prop), css_unit(v, unit));\n}\n")
        return "".join(out)
    raise SystemExit(f"no arm {arm}")


# ---- arm e: the fused operations, appended to cdom.c and cdom.h ----
E_C = r"""
/* ---- bench arm e: fused operations (micro/bridge/gen_arms.py) ----
 * A multi-property style: cd_style_begin(id), then any number of cd_style_num / cd_style_str, then
 * cd_style_end, which crosses once. The properties are set in the order given (a transition stays
 * before the transform it times). Contract: the one-off strings (cd_tmp) of one fused style must
 * all be made after the op before it crossed and are consumed by it; a string batch sent in
 * between (a full buffer) traps, as tmp_check does for a single op. */
typedef struct { uint32_t prop, value, kind, pad; double v; } style_item; /* kind 0: v + unit string; 1: string */
#define STYLE_ITEMS 32
static style_item style_items[STYLE_ITEMS];
static uint32_t style_count, style_batch;
static cd_id style_node;
IMPORTS("style_n") void j_style_n(cd_id id, const style_item *items, uint32_t n);
void cd_style_begin(cd_id id) {
  if (style_count) __builtin_trap();
  style_node = id;
  style_batch = batches;
}
void cd_style_num(cd_str prop, double v, uint32_t unit) {
  if (style_count == STYLE_ITEMS || unit >= CD_UNITS) __builtin_trap();
  if (!unit_ids[unit]) unit_ids[unit] = cd_internz(UNIT_STR[unit]);
  style_items[style_count++] = (style_item){ref_name(prop), ref_name(unit_ids[unit]), 0, 0, v};
}
void cd_style_str(cd_str prop, cd_str v) {
  if (style_count == STYLE_ITEMS) __builtin_trap();
  uint32_t b = ref_text(v), a = ref_name(prop);
  style_items[style_count++] = (style_item){a, b, 1, 0, 0};
}
void cd_style_end(void) {
  if (!style_count) return;
  if (batches != style_batch) __builtin_trap();
  ready();
  j_style_n(style_node, style_items, style_count);
  style_count = 0;
}
"""
E_C_PENDING = r"""
/* bench arm e: template slots set ahead of their clone, and freed ids, cross later in one call:
 * with the clone (clone_slots), or before whatever op crosses next (ready). */
typedef struct { uint32_t t, k, kind, v; } slot_set; /* kind 1 string id, 2 number */
#define SLOT_SETS 64
#define FREE_Q 1024
static slot_set slot_sets[SLOT_SETS];
static uint32_t n_slot_sets;
static cd_id free_q[FREE_Q];
static uint32_t n_free_q, batches;
IMPORTS("apply_slots") void j_slots(const slot_set *sets, uint32_t n);
IMPORTS("clone_slots") void j_clone_slots(uint32_t t, const slot_set *sets, uint32_t n);
IMPORTS("free_n") void j_free_n(const cd_id *ids, uint32_t n);
static void fused_pending(void) {
  if (n_free_q) { j_free_n(free_q, n_free_q); n_free_q = 0; }
  if (n_slot_sets) { j_slots(slot_sets, n_slot_sets); n_slot_sets = 0; }
}
"""
E_H = r"""
/* bench arm e (micro/bridge/gen_arms.py): one style op setting several properties of one node. */
void cd_style_begin(cd_id id);
void cd_style_num(cd_str prop, double v, uint32_t unit);
void cd_style_str(cd_str prop, cd_str v);
void cd_style_end(void);
#define CDOM_FUSED 1
"""


def sub_once(text, old, new, what):
    if text.count(old) != 1:
        raise SystemExit(f"{what}: anchor found {text.count(old)} times: {old!r}")
    return text.replace(old, new)


def gen_c(arm, c):
    start, end, block, imports = parse_imports(c)
    c = c[:start] + c_arm_block(arm, block, imports) + c[end:]
    if arm == "list":
        c = sub_once(c, 'IMPORT("input") uint32_t js_input(cd_id id, char *dst, uint32_t cap);',
                     'IMPORT("input") uint32_t js_input_raw(cd_id id, char *dst, uint32_t cap);\n'
                     'static void list_flush(void);\n'
                     'static inline uint32_t js_input(cd_id id, char *dst, uint32_t cap) { list_flush(); return js_input_raw(id, dst, cap); }',
                     "list input")
        c = sub_once(c, "void cd_flush(void) { ready(); }", "void cd_flush(void) { ready(); list_flush(); }", "list flush")
    if arm == "d":
        c = sub_once(c, "ready(); j_style_num(id, a, v, u);", "(void)u; ready(); j_style_num_unit(id, a, v, unit);", "d style_num")
    if arm == "e":
        c = sub_once(c, "static inline void ready(void) { walk_done(); send_strings(); }",
                     "static inline void ready(void) { walk_done(); send_strings(); fused_pending(); }", "e ready")
        c = sub_once(c, "#define SB_CAP (1u << 18)\n", E_C_PENDING + "\n#define SB_CAP (1u << 18)\n", "e pending")
        c = sub_once(c, "  if (nstrs) j_strings(nstrs, nsb);\n", "  if (nstrs) { j_strings(nstrs, nsb); batches++; }\n", "e batches")
        c = sub_once(c, "  uint32_t v = ref_text(s); ready(); j_tpl_text(t, k, v);\n",
                     "  uint32_t v = ref_text(s);\n"
                     "  if (n_slot_sets == SLOT_SETS) ready();\n"
                     "  slot_sets[n_slot_sets++] = (slot_set){t, k, 1, v};\n", "e tpl_text")
        c = sub_once(c, "  ready(); j_tpl_text_num(t, k, n);\n",
                     "  if (n_slot_sets == SLOT_SETS) ready();\n"
                     "  slot_sets[n_slot_sets++] = (slot_set){t, k, 2, n};\n", "e tpl_text_num")
        c = sub_once(c, "void cd_clone(uint32_t t) { if (svg_depth) svg_depth++; ready(); j_clone(t); }",
                     "void cd_clone(uint32_t t) {\n"
                     "  if (svg_depth) svg_depth++;\n"
                     "  walk_done(); send_strings();\n"
                     "  if (n_free_q) { j_free_n(free_q, n_free_q); n_free_q = 0; }\n"
                     "  if (n_slot_sets) { j_clone_slots(t, slot_sets, n_slot_sets); n_slot_sets = 0; } else j_clone(t);\n"
                     "}", "e clone")
        c = sub_once(c, "void cd_id_free(cd_id id) {\n  ready(); j_free(id);\n",
                     "void cd_id_free(cd_id id) {\n  if (n_free_q == FREE_Q) ready();\n  free_q[n_free_q++] = id;\n", "e free")
        c += E_C
    return c


def gen_h(arm, h):
    if arm != "e":
        return h
    if not h.endswith("\n#endif\n"):
        raise SystemExit("cdom.h: no closing #endif")
    return h[: -len("#endif\n")] + E_H + "\n#endif\n"


# ---- the shim side ----
JS_COMMON = r"""
  // ---- bench arm %(arm)s (micro/bridge/gen_arms.py): the imports this arm adds or replaces ----
  let armF64 = null; // a Float64Array over wasm memory, made again after it grows
  function f64() {
    if (!armF64 || armF64.buffer !== memory.buffer) {
      armF64 = new Float64Array(memory.buffer);
    }
    return armF64;
  }
"""
JS_ARM = {}
JS_ARM["a"] = r"""
  function armImports(table) {
    return table;
  }
  function armReady() {}
"""
JS_ARM["e"] = r"""
  // Several style properties of one node: (property, value) items in C memory, 24 bytes each:
  // property id, value id (a string, or the unit after a number), kind (0 number, 1 string), the number.
  function style_n(id, itemsPtr, count) {
    if (buffer !== memory.buffer) {
      views();
    }
    const numbers = f64();
    const style = nodes[id].style;
    for (let p = itemsPtr >> 2, end = p + count * 6; p < end; p += 6) {
      const property = stringOf(u32[p]);
      if (u32[p + 2]) {
        style.setProperty(property, stringOf(u32[p + 1]));
      } else {
        style.setProperty(property, numbers[(p + 4) >> 1] + stringOf(u32[p + 1]));
      }
    }
  }
  // Template slot texts set ahead of their clone: (template, slot, kind, value), 16 bytes each.
  function apply_slots(setsPtr, count) {
    if (buffer !== memory.buffer) {
      views();
    }
    for (let p = setsPtr >> 2, end = p + count * 4; p < end; p += 4) {
      const node = templateSlots[u32[p]][u32[p + 1]];
      node.nodeValue = u32[p + 2] === 1 ? stringOf(u32[p + 3]) : u32[p + 3];
    }
  }
  function clone_slots(templateId, setsPtr, count) {
    apply_slots(setsPtr, count);
    clone(templateId);
  }
  function free_n(idsPtr, count) {
    if (buffer !== memory.buffer) {
      views();
    }
    for (let p = idsPtr >> 2, end = p + count; p < end; p++) {
      free(u32[p]);
    }
  }
  function armImports(table) {
    return Object.assign(table, { style_n, apply_slots, clone_slots, free_n });
  }
  function armReady() {}
"""
JS_ARM["f"] = r"""
  function armImports(table) {
    delete table.now;
    table.n_now = Function.prototype.call.bind(Performance.prototype.now);
    return table;
  }
  function armReady() {
    exports.cd_ref_misc(0, performance);
  }
"""


def js_list(imports):
    listed = [i for i in imports if i.name in DOM_OPS and i.ret == "void" and i.name != "rect"]
    cases = []
    for k, i in enumerate(listed):
        args = []
        for s in i.slots():
            args.append("numbers[(p + 6) >> 1]" if s == "d" else f"i32[p + {1 + int(s[1])}]")
        cases.append(f"        case {k}: {SHIM_FN.get(i.name, i.name)}({', '.join(args)}); break;")
    names = ", ".join(i.name for i in listed)
    return r"""
  // The op list: count records of 8 words at recordsPtr (op, i0, i1, i2, i3, pad, d), applied in
  // order by one switch (cdom main's design). Ops: %s.
  function apply(recordsPtr, count) {
    if (buffer !== memory.buffer) {
      views();
    }
    const numbers = f64();
    COUNT_RECORDS
    for (let p = recordsPtr >> 2, end = p + count * 8; p < end; p += 8) {
      switch (u32[p]) {
%s
      }
    }
  }
  function armImports(table) {
    const listed = [%s];
    for (const name of listed) {
      delete table[name];
    }
    table.apply = apply;
    return table;
  }
  function armReady() {}
""" % (names, "\n".join(cases), ", ".join(f'"{i.name}"' for i in listed))


def js_bridge(imports):
    routed = [i for i in imports if i.name in DOM_OPS]
    rows = []
    for i in routed:
        if i.name in GENERIC:
            kind, target, member, args = GENERIC[i.name]
            codes = []
            for a in args:
                if a == "E":
                    codes.append("ARG_EMPTY")
                else:
                    code = {"S": "ARG_STRING", "U": "ARG_U32", "NU": "ARG_NUMBER_UNIT"}[a.rstrip("0123")]
                    codes.append(f"{code} | {a[-1]}")
            while len(codes) < 2:
                codes.append("ARG_NONE")
            m = f'"{member}"' if kind == "SET" else member
            rows.append(f"      {{ kind: BRIDGE_{kind}, target: TARGET_{target}, member: {m}, first: {codes[0]}, second: {codes[1]} }}, // {i.name}")
        else:  # the shim's own function, its arguments taken back out of the slots
            params = ", ".join(i.slots())
            rows.append(f"      {{ kind: BRIDGE_OP, op: (i0, i1, i2, i3, d) => {SHIM_FN.get(i.name, i.name)}({params}) }}, // {i.name}")
    return r"""
  // One generic import: bridge(name, i0, i1, i2, i3, d). The name id picks an entry resolved once
  // at start: a property set (target[member] = first), a method call (member.call(target, first,
  // second)), or one of the shim's structural functions. An argument code is a kind (bits 2 up)
  // and the slot it reads (bits 0-1: i0..i3).
  const BRIDGE_OP = 0;
  const BRIDGE_SET = 1;
  const BRIDGE_CALL = 2;
  const TARGET_NODE = 0;
  const TARGET_LAST = 1;
  const TARGET_STYLE = 2;
  const ARG_NONE = 0 << 2;
  const ARG_STRING = 1 << 2;
  const ARG_U32 = 2 << 2;
  const ARG_NUMBER_UNIT = 3 << 2; // d and the unit string in the slot
  const ARG_EMPTY = 4 << 2;
  let bridgeEntries;
  function bridgeArg(code, i0, i1, i2, i3, d) {
    const slot = code & 3;
    const value = slot === 0 ? i0 : slot === 1 ? i1 : slot === 2 ? i2 : i3;
    switch (code & ~3) {
      case ARG_STRING: return stringOf(value);
      case ARG_U32: return value >>> 0;
      case ARG_NUMBER_UNIT: return d + stringOf(value);
      case ARG_EMPTY: return "";
      default: return undefined;
    }
  }
  function bridge(name, i0, i1, i2, i3, d) {
    const entry = bridgeEntries[name];
    if (entry.kind === BRIDGE_OP) {
      return entry.op(i0, i1, i2, i3, d);
    }
    let target;
    if (entry.target === TARGET_LAST) {
      target = last;
    } else if (entry.target === TARGET_STYLE) {
      target = nodes[i0].style;
    } else {
      target = nodes[i0];
    }
    const first = bridgeArg(entry.first, i0, i1, i2, i3, d);
    if (entry.kind === BRIDGE_SET) {
      target[entry.member] = first;
    } else {
      entry.member.call(target, first, bridgeArg(entry.second, i0, i1, i2, i3, d));
    }
    return 0;
  }
  function armImports(table) {
    bridgeEntries = [
%s
    ];
    for (const name of [%s]) {
      delete table[name];
    }
    table.bridge = bridge;
    return table;
  }
  function armReady() {}
""" % ("\n".join(rows), ", ".join(f'"{i.name}"' for i in routed))


def js_table():
    fns = ",\n".join(f"      {how}, // {fid}" for fid, how in FNS)
    shapes = "\n".join(f"  function {s}({js}) {{\n    {body}\n  }}" for s, (_, js, body) in SHAPES.items())
    return r"""
  // A table of the setters and methods themselves, called as fns[id].call(target, ...) through one
  // import per argument shape; target -1 is the node made or walked to last.
  let fns;
  function targetOf(target) {
    return target === -1 ? last : nodes[target];
  }
%s
  function armImports(table) {
    fns = [
%s
    ];
    for (const name of [%s]) {
      delete table[name];
    }
    return Object.assign(table, { %s });
  }
  function armReady() {}
""" % (shapes, fns, ", ".join(f'"{n}"' for n in TABLE), ", ".join(SHAPES))


def js_native():
    binds = "\n".join(f"      {n}: {how}," for n, (_, how) in NATIVE.items())
    return r"""
  // Natives bound at instantiation (Function.prototype.call.bind(method): the first argument is the
  // receiver), with nodes and strings mirrored into wasm's reference tables as the shim names or
  // decodes them.
  const call = Function.prototype.call;
  function setter(proto, name) {
    return Object.getOwnPropertyDescriptor(proto, name).set;
  }
  function getter(proto, name) {
    return Object.getOwnPropertyDescriptor(proto, name).get;
  }
  function mirrored(fn) {
    return (id, ...rest) => {
      fn(id, ...rest);
      exports.cd_ref_node(id, nodes[id] ?? null);
    };
  }
  function armKeep(id, value) {
    if (exports) {
      exports.cd_ref_str(id, value);
    }
  }
  function armImports(table) {
    for (const name of [%s]) {
      delete table[name];
    }
    for (const name of ["name", "name_open", "listen", "remove", "free"]) {
      table[name] = mirrored(table[name]);
    }
    return Object.assign(table, {
%s
    });
  }
  function armReady() {
    exports.cd_ref_str(0, "");
  }
""" % (", ".join(f'"{n}"' for n in list(DIRECT_NATIVE) + ["style_num"]), binds)


JS_COUNT = r"""
  // --count: every import call counted by name, with the strings decoded and the op-list records.
  const crossings = globalThis.__cdomCross = Object.create(null);
  function counted(table) {
    for (const name of Object.keys(table)) {
      const fn = table[name];
      table[name] = function (...args) {
        crossings[name] = (crossings[name] | 0) + 1;
        return fn.apply(this, args);
      };
    }
    return table;
  }
"""


def gen_js(arm, js, imports, count):
    block = JS_COMMON % {"arm": arm}
    block += {"list": lambda: js_list(imports), "b": lambda: js_bridge(imports), "c": js_table,
              "d": js_native}.get(arm, lambda: JS_ARM[arm])()
    block = block.replace("COUNT_RECORDS", "crossings.$records = (crossings.$records | 0) + count;" if count else "")
    if count:
        block += JS_COUNT
    js = sub_once(js, "  async function start(url, mount, env, options) {", block + "\n  async function start(url, mount, env, options) {", "js block")
    wrap = "counted(armImports(imports.cdom))" if count else "armImports(imports.cdom)"
    js = sub_once(js, "      env: env || {},\n    };\n", f"      env: env || {{}},\n    }};\n    imports.cdom = {wrap};\n", "js imports")
    js = sub_once(js, "    exports.cd_start(", "    armReady();\n    exports.cd_start(", "js ready")
    if arm == "d":
        js = sub_once(js, "  function keep(id, value) {\n", "  function keep(id, value) {\n    armKeep(id, value);\n", "js keep")
    if count:
        js = sub_once(js, "  function decode(count, byteCount) {\n",
                      "  function decode(count, byteCount) {\n    globalThis.__cdomCross.$strings = (globalThis.__cdomCross.$strings | 0) + count;\n", "js decode count")
    return js


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    count = "--count" in sys.argv
    if len(args) != 2 or args[0] not in ARMS:
        raise SystemExit(__doc__)
    arm, out = args[0], Path(args[1])
    c = (ROOT / "src/cdom.c").read_text()
    h = (ROOT / "src/cdom.h").read_text()
    js = (ROOT / "shim/cdom.js").read_text()
    _, _, _, imports = parse_imports(c)
    out.mkdir(parents=True, exist_ok=True)
    (out / "cdom.c").write_text(gen_c(arm, c))
    (out / "cdom.h").write_text(gen_h(arm, h))
    (out / "cdom.js").write_text(gen_js(arm, js, imports, count))


if __name__ == "__main__":
    main()
