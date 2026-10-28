/* cdtc: the cdom template compiler. It turns a .cdt file (HTML plus state bindings,
 * events, conditionals and keyed lists) into C that emits cdom ops: a header with the
 * state types and prototypes, and a source file. One file, libc only.
 *
 *   <state name="Rows" global="S">          the model; global= also defines the instance
 *     u32 next_id = 1; u32 selected;        and cd_state_save/cd_state_load for it
 *     list rows: Row;                       a growable array of records
 *     record Row { u32 id; str label; }     fields are u32 or str (an interned cd_str)
 *   </state>
 *   <template name="row" item="Row *r" state="Rows *S" root="tr">   an item template
 *     <tr class:danger={r->id == S->selected} on:click={row_click}>
 *       <td>{r->id}</td><td><a>{r->label}</a></td> ...
 *   </template>
 *   <template name="page" state="Rows *S">  the page: built once under the mount
 *     ... <tbody>{#each S->rows as r (r->id)}<row/>{/each}</tbody> ...
 *   </template>
 *
 * Text {expr} binds a cd_str or a u32: a bare field of the item or the state is typed
 * from the state block, {n:expr} forces a number and {s:expr} a cd_str. attr={expr}
 * binds a whole cd_str value; class:name={cond} toggles a class on the static part;
 * on:event={handler} listens (one of the 12 built-in events; any other name, such as
 * on:value-change={h}, listens for that DOM event by name through cd_listen_named and is
 * dispatched on CD_EV_CUSTOM with the name's id in e->key); {#if c}<a/>{:else}<b/>{/if}
 * swaps one element. ref="name" on an element gives the template's view a cd_id field
 * holding its id (the page's view is page_v, declared in the header; a ref inside an
 * {#if} branch holds a freed id while the branch is gone). host on an element
 * (<canvas ref="chart" host>) makes it a widget's node: it is named, its static
 * attributes are written, and the template never puts or patches anything inside it.
 * Every static shape becomes a cloned template; the generated create clones it and
 * walks it with down/next/up to fill bindings, and patch emits only what changed.
 *
 * Usage: cdtc <in.cdt> -o <out-base>     writes <out-base>.h and <out-base>.c
 * Errors go to stderr as "<in>:<line>:<col>: <message>", exit status 1. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CDTC_VERSION "1"
#define MAXD 32      /* deepest element nesting in one template */
#define MAXTOG 8     /* class toggles on one element: 2^8 class strings at most */
#define MAXSLOT 255  /* listening elements in one file: the tag's top byte */

/* ---- input, errors, memory ---- */
static const char *in_path;
static char *src;
static size_t slen, P;

static void loc(size_t at, int *line, int *col) {
  *line = 1;
  *col = 1;
  for (size_t i = 0; i < at && i < slen; i++) {
    if (src[i] == '\n') { ++*line; *col = 1; } else ++*col;
  }
}

static void fail(size_t at, const char *fmt, ...) __attribute__((noreturn, format(printf, 2, 3)));
static void fail(size_t at, const char *fmt, ...) {
  int line, col;
  loc(at, &line, &col);
  fprintf(stderr, "%s:%d:%d: ", in_path, line, col);
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fputc('\n', stderr);
  exit(1);
}

static void *xrealloc(void *p, size_t n) {
  p = realloc(p, n ? n : 1);
  if (!p) { fprintf(stderr, "cdtc: out of memory\n"); exit(1); }
  return p;
}

static char *dupn(const char *s, size_t n) {
  char *d = xrealloc(0, n + 1);
  memcpy(d, s, n);
  d[n] = 0;
  return d;
}

#define PUSH(arr, n, cap, val)                                                    \
  do {                                                                           \
    if ((n) == (cap)) { (cap) = (cap) ? (cap) * 2 : 8; (arr) = xrealloc((arr), (size_t)(cap) * sizeof *(arr)); } \
    (arr)[(n)++] = (val);                                                        \
  } while (0)

typedef struct { int *v; int n, cap; } IntVec;

static int is_ws(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }
static int is_alpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
static int is_alnum(int c) { return is_alpha(c) || (c >= '0' && c <= '9'); }
static int at_str(const char *s) { size_t n = strlen(s); return P + n <= slen && !memcmp(src + P, s, n); }
static void skip_ws(void) { while (P < slen && is_ws(src[P])) P++; }

static int in_list(const char *s, const char *const *l) {
  for (; *l; l++)
    if (!strcmp(s, *l)) return 1;
  return 0;
}

static char *trim(const char *s) {
  while (is_ws(*s)) s++;
  size_t n = strlen(s);
  while (n && is_ws(s[n - 1])) n--;
  return dupn(s, n);
}

static int is_ident(const char *s) {
  if (!is_alpha(*s)) return 0;
  for (s++; *s; s++)
    if (!is_alnum(*s)) return 0;
  return 1;
}

/* ---- output ---- */
typedef struct { char *d; size_t n, cap; } Buf;

static void out(Buf *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void out(Buf *b, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int k = vsnprintf(0, 0, fmt, ap);
  va_end(ap);
  if (b->n + (size_t)k + 1 > b->cap) {
    while (b->n + (size_t)k + 1 > b->cap) b->cap = b->cap ? b->cap * 2 : 4096;
    b->d = xrealloc(b->d, b->cap);
  }
  va_start(ap, fmt);
  vsnprintf(b->d + b->n, (size_t)k + 1, fmt, ap);
  va_end(ap);
  b->n += (size_t)k;
}

/* ---- the model ---- */
enum { T_U32 = 1, T_STR, T_LIST };
typedef struct { char *name; int type; char *rec; int reci; long long init; int has_init; size_t at; } Field;
typedef struct { char *name; Field *f; int n, cap; size_t at; } Record;
static char *st_name, *st_global;
static size_t st_at;
static int have_state;
static Field *sf;
static int nsf, csf;
static Record *recs;
static int nrec, crec;

enum { N_ELEM = 1, N_TEXT, N_BIND, N_IF, N_EACH };
enum { A_STATIC = 1, A_BIND, A_TOGGLE, A_EVENT, A_REF, A_HOST };
typedef struct { int kind; char *name, *val; size_t at; int ev; } Attr;
typedef struct {
  int kind, tpl, parent;
  size_t at;
  char *tag; /* ELEM */
  char *ref; /* ELEM: ref="name", the view field holding its id */
  int host;  /* ELEM: host, a widget's node: named, opaque to the template */
  Attr *a;
  int na, cap_a, self_close;
  int *k; /* children */
  int nk;
  char *text; /* TEXT: decoded bytes */
  char *expr; /* BIND: the expression; IF: the condition; EACH: the list expression */
  int bt;     /* BIND: 'n' or 's' */
  int br[2];  /* IF: then and else roots, -1 for none */
  char *evar, *ekey, *eref; /* EACH */
  size_t eref_at;
  int efield, eref_t, ei, eowner; /* eowner: the record holding the list, -1 for the state */
  /* analysis */
  int ln, frag, depth, path[MAXD], unstable, named, slot, ntog, nattrb, owns;
} Node;
static Node *N;
static int nn, cn;

typedef struct {
  char *name, *ivar, *itype, *svar, *stype, *rootdecl;
  size_t at, item_at, state_at, root_at;
  int root, rec, page, used, first, last; /* nodes [first, last) */
} Tpl;
static Tpl *T;
static int nt, ct;

typedef struct { int tpl, root, ifn, br, tid, has_free, has_patch; } Frag;
static Frag *F;
static int nf, cfr;

static const char *const EVENTS[] = {"click", "dblclick", "input", "change", "keydown", "keyup", "focusin",
                                     "focusout", "submit", "pointerdown", "pointerup", "pointermove", 0};
static const char *const EVENT_IDS[] = {"CLICK", "DBLCLICK", "INPUT", "CHANGE", "KEYDOWN", "KEYUP", "FOCUSIN",
                                        "FOCUSOUT", "SUBMIT", "POINTERDOWN", "POINTERUP", "POINTERMOVE"};
static const char *const VOIDS[] = {"area", "base", "br", "col", "embed", "hr", "img", "input", "link",
                                    "meta", "source", "track", "wbr", 0};
/* start tags that close an open <p> (HTML's "close a p element" list) */
static const char *const P_CLOSERS[] = {"address", "article", "aside", "blockquote", "details", "div", "dl",
  "fieldset", "figcaption", "figure", "footer", "form", "h1", "h2", "h3", "h4", "h5", "h6", "header",
  "hgroup", "hr", "main", "menu", "nav", "ol", "p", "pre", "section", "table", "ul", 0};
static const char *const P_SCOPE[] = {"button", "table", "td", "th", "caption", "template", 0};
static const char *const TABLE_KIDS[] = {"caption", "colgroup", "thead", "tbody", "tfoot", 0};
static const char *const SECTION_KIDS[] = {"tr", 0};
static const char *const ROW_KIDS[] = {"td", "th", 0};

static int new_node(int kind, int tpl, int parent, size_t at) {
  Node x;
  memset(&x, 0, sizeof x);
  x.kind = kind;
  x.tpl = tpl;
  x.parent = parent;
  x.at = at;
  x.br[0] = x.br[1] = -1;
  x.ln = x.frag = x.slot = -1;
  PUSH(N, nn, cn, x);
  return nn - 1;
}

/* ---- HTML placement rules: what the parser would rearrange ---- */
typedef struct { const char *tag; size_t at; } Open;
static Open *ostk;
static int osp, ocap;

static int table_ctx(const char *t) {
  return !strcmp(t, "table") || !strcmp(t, "thead") || !strcmp(t, "tbody") || !strcmp(t, "tfoot") || !strcmp(t, "tr");
}

static void check_elem(const char *ptag, const char *tag, size_t at) {
  if (!ptag) return;
  const char *const *ok = !strcmp(ptag, "table") ? TABLE_KIDS
                          : (!strcmp(ptag, "thead") || !strcmp(ptag, "tbody") || !strcmp(ptag, "tfoot")) ? SECTION_KIDS
                          : !strcmp(ptag, "tr") ? ROW_KIDS : 0;
  if (!ok || in_list(tag, ok)) return;
  if (!strcmp(ptag, "table") && !strcmp(tag, "tr")) fail(at, "<tr> directly inside <table>: the HTML parser wraps it in a <tbody>");
  char allowed[128] = "";
  for (const char *const *l = ok; *l; l++) {
    strcat(allowed, l == ok ? "<" : ", <");
    strcat(allowed, *l);
    strcat(allowed, ">");
  }
  fail(at, "<%s> directly inside <%s> is moved by the HTML parser (only %s belong%s there)", tag, ptag, allowed,
       ok[1] ? "" : "s");
}

static void check_text(const char *ptag, size_t at) {
  if (ptag && table_ctx(ptag)) fail(at, "text directly inside <%s> is moved out of the table by the HTML parser", ptag);
}

static int is_open(const char *tag) {
  for (int i = osp; i-- > 0;)
    if (!strcmp(ostk[i].tag, tag)) return 1;
  return 0;
}

/* ---- lexing ---- */
static char *read_brace(void) { /* at '{': the content up to the matching '}' */
  size_t at = P++, s = P;
  int depth = 1;
  while (P < slen) {
    char c = src[P];
    if (c == '"' || c == '\'') {
      for (P++; P < slen && src[P] != c; P++)
        if (src[P] == '\\') P++;
      P++;
      continue;
    }
    if (c == '{') depth++;
    else if (c == '}' && !--depth) break;
    P++;
  }
  if (P >= slen) fail(at, "{ is never closed");
  char *r = dupn(src + s, P - s);
  P++;
  return r;
}

static void put_utf8(char *d, size_t *n, unsigned long c) {
  if (c < 0x80) d[(*n)++] = (char)c;
  else if (c < 0x800) { d[(*n)++] = (char)(0xc0 | c >> 6); d[(*n)++] = (char)(0x80 | (c & 63)); }
  else if (c < 0x10000) {
    d[(*n)++] = (char)(0xe0 | c >> 12); d[(*n)++] = (char)(0x80 | ((c >> 6) & 63)); d[(*n)++] = (char)(0x80 | (c & 63));
  } else {
    d[(*n)++] = (char)(0xf0 | c >> 18); d[(*n)++] = (char)(0x80 | ((c >> 12) & 63));
    d[(*n)++] = (char)(0x80 | ((c >> 6) & 63)); d[(*n)++] = (char)(0x80 | (c & 63));
  }
}

/* Character references: &amp; &lt; &gt; &quot; &apos; &nbsp; &#N; &#xN;. A bare & stays. */
static char *decode(size_t at, size_t len) {
  const char *s = src + at;
  char *d = xrealloc(0, len + 1);
  size_t n = 0;
  for (size_t i = 0; i < len; i++) {
    if (s[i] != '&') { d[n++] = s[i]; continue; }
    size_t j = i + 1;
    while (j < len && j - i < 12 && (is_alnum(s[j]) || s[j] == '#')) j++;
    if (j >= len || s[j] != ';' || j == i + 1) { d[n++] = '&'; continue; }
    const char *e = s + i + 1;
    size_t el = j - i - 1;
    unsigned long c = 0;
    if (el == 3 && !memcmp(e, "amp", 3)) c = '&';
    else if (el == 2 && !memcmp(e, "lt", 2)) c = '<';
    else if (el == 2 && !memcmp(e, "gt", 2)) c = '>';
    else if (el == 4 && !memcmp(e, "quot", 4)) c = '"';
    else if (el == 4 && !memcmp(e, "apos", 4)) c = '\'';
    else if (el == 4 && !memcmp(e, "nbsp", 4)) c = 0xa0;
    else if (e[0] == '#') {
      char *end;
      c = (el > 1 && (e[1] == 'x' || e[1] == 'X')) ? strtoul(e + 2, &end, 16) : strtoul(e + 1, &end, 10);
      if (end != s + j || !c || c > 0x10ffff) fail(at + i, "bad character reference &%.*s;", (int)el, e);
    } else fail(at + i, "unknown character reference &%.*s;", (int)el, e);
    if (c > 0x7f) d = xrealloc(d, len + 4 + n);
    put_utf8(d, &n, c);
    i = j;
  }
  d[n] = 0;
  return d;
}

static char *read_tag_name(size_t at) {
  size_t s = P;
  while (P < slen && (is_alnum(src[P]) || src[P] == '-')) P++;
  if (P == s) fail(at, "expected a tag name after <");
  for (size_t i = s; i < P; i++)
    if (src[i] >= 'A' && src[i] <= 'Z') fail(at, "tag names are lower case: <%.*s>", (int)(P - s), src + s);
  return dupn(src + s, P - s);
}

/* Attributes up to '>' or '/>'. kind: A_STATIC for quoted or bare values, A_BIND for {expr}. */
static void parse_attrs(Attr **a, int *na, int *ca, int *self_close, size_t tag_at) {
  for (;;) {
    skip_ws();
    if (P >= slen) fail(tag_at, "this tag is never finished with >");
    if (src[P] == '>') { P++; *self_close = 0; return; }
    if (at_str("/>")) { P += 2; *self_close = 1; return; }
    size_t at = P;
    while (P < slen && !is_ws(src[P]) && !strchr("=>/\"'{", src[P])) P++;
    if (P == at) fail(at, "expected an attribute name");
    Attr x = {A_STATIC, dupn(src + at, P - at), 0, at, -1};
    skip_ws();
    if (P < slen && src[P] == '=') {
      P++;
      skip_ws();
      if (P < slen && (src[P] == '"' || src[P] == '\'')) {
        char q = src[P++];
        size_t s = P;
        while (P < slen && src[P] != q) P++;
        if (P >= slen) fail(at, "attribute value is never closed");
        x.val = decode(s, P - s);
        P++;
      } else if (P < slen && src[P] == '{') {
        x.kind = A_BIND;
        x.val = trim(read_brace());
        if (!*x.val) fail(at, "empty binding in attribute %s", x.name);
      } else {
        size_t s = P;
        while (P < slen && !is_ws(src[P]) && src[P] != '>' && !at_str("/>")) P++;
        if (P == s) fail(at, "attribute %s has no value after =", x.name);
        x.val = decode(s, P - s);
      }
    } else {
      x.val = dupn("", 0);
    }
    for (int i = 0; i < *na; i++)
      if (!strcmp((*a)[i].name, x.name)) fail(at, "duplicate attribute %s", x.name);
    PUSH(*a, *na, *ca, x);
  }
}

/* ---- the template body parser ---- */
enum { END_EOF, END_TAG, END_ELSE, END_IF, END_EACH };
static size_t term_at;
static char *term_name;

static int parse_content(int tpl, int parent, IntVec *kids, int check);

/* A close tag that is not the one wanted: name what is wrong with it when we can. */
static void stray_close(void) {
  if (in_list(term_name, VOIDS) && !is_open(term_name))
    fail(term_at, "<%s> is a void element and cannot have children", term_name);
  if (!is_open(term_name)) fail(term_at, "</%s> closes nothing: no <%s> is open", term_name, term_name);
}

static const char *ptag(int check) { return check && osp ? ostk[osp - 1].tag : 0; }

static int reserved_field(const char *s);

static void classify_attrs(int me) {
  Node *x = &N[me];
  int has_class_bind = 0;
  for (int i = 0; i < x->na; i++) {
    Attr *a = &x->a[i];
    if (!strncmp(a->name, "on:", 3)) {
      int e = 0;
      while (EVENTS[e] && strcmp(EVENTS[e], a->name + 3)) e++;
      if (!EVENTS[e]) { /* any other name is a custom event: cd_listen_named */
        const char *c = a->name + 3;
        int okname = is_alpha(*c) && *c != '_';
        for (; *c && okname; c++) okname = is_alnum(*c) || *c == '-' || *c == '.' || *c == ':';
        if (!okname)
          fail(a->at, "bad event name \"%s\": a custom event's name is a letter, then letters, digits, -, _, . or :", a->name + 3);
        e = -1;
      }
      if (a->kind != A_BIND || !is_ident(a->val)) fail(a->at, "%s needs a handler name: %s={handler}", a->name, a->name);
      a->kind = A_EVENT;
      a->ev = e;
    } else if (!strcmp(a->name, "ref")) {
      if (a->kind != A_STATIC || !is_ident(a->val)) fail(a->at, "ref needs an identifier: ref=\"name\"");
      if (reserved_field(a->val)) fail(a->at, "ref=\"%s\" clashes with a field cdtc generates; pick another name", a->val);
      a->kind = A_REF;
      x->ref = a->val;
    } else if (!strcmp(a->name, "host")) {
      if (a->kind != A_STATIC || *a->val) fail(a->at, "host takes no value: <%s ref=\"name\" host>", x->tag);
      a->kind = A_HOST;
      x->host = 1;
    } else if (!strncmp(a->name, "class:", 6)) {
      if (a->kind != A_BIND) fail(a->at, "%s needs a condition: %s={cond}", a->name, a->name);
      if (!a->name[6]) fail(a->at, "class: needs a class name");
      a->kind = A_TOGGLE;
      if (++x->ntog > MAXTOG) fail(a->at, "more than %d class toggles on one element", MAXTOG);
    } else if (a->kind == A_BIND) {
      if (!strcmp(a->name, "class")) has_class_bind = 1;
      x->nattrb++;
    }
  }
  if (has_class_bind && x->ntog) fail(x->at, "class={...} and class: toggles cannot be mixed on one element");
  if (x->host && !x->ref) fail(x->at, "a host element needs ref=\"name\" so the app can reach its widget");
}

/* names cdtc gives view fields: root, key, idx, id<n>, c<n>, t<n>, a<n>_<n>, if<n>_b, if<n>_root, list<n> */
static int reserved_field(const char *s) {
  if (!strcmp(s, "root") || !strcmp(s, "key") || !strcmp(s, "idx")) return 1;
  static const char *const P[] = {"id", "c", "t", "a", "if", "list", 0};
  for (const char *const *p = P; *p; p++) {
    size_t n = strlen(*p);
    if (!strncmp(s, *p, n) && s[n] >= '0' && s[n] <= '9') return 1;
  }
  return !strncmp(s, "cdt_", 4);
}

static void parse_element(int tpl, int parent, IntVec *kids, int check) {
  size_t at = P++;
  char *tag = read_tag_name(at);
  int me = new_node(N_ELEM, tpl, parent, at);
  N[me].tag = tag;
  {
    Attr *a = 0;
    int na = 0, ca = 0, sc = 0;
    parse_attrs(&a, &na, &ca, &sc, at);
    N[me].a = a;
    N[me].na = na;
    N[me].cap_a = ca;
    N[me].self_close = sc;
  }
  classify_attrs(me);
  check_elem(ptag(check), tag, at);
  if (in_list(tag, P_CLOSERS))
    for (int i = osp; i-- > 0;) {
      if (!strcmp(ostk[i].tag, "p")) fail(at, "<%s> inside <p>: the HTML parser closes the <p> first", tag);
      if (in_list(ostk[i].tag, P_SCOPE)) break;
    }
  PUSH(kids->v, kids->n, kids->cap, me);
  if (in_list(tag, VOIDS) || N[me].self_close) return;
  Open o = {tag, at};
  PUSH(ostk, osp, ocap, o);
  IntVec ck = {0, 0, 0};
  int t = parse_content(tpl, me, &ck, 1);
  if (t == END_TAG && !strcmp(term_name, tag)) {
    if (N[me].host && ck.n)
      fail(N[ck.v[0]].at, "<%s host> belongs to its widget: the template cannot put anything inside it", tag);
    osp--;
    N[me].k = ck.v;
    N[me].nk = ck.n;
    return;
  }
  if (t == END_TAG) stray_close();
  fail(at, "<%s> is never closed", tag);
}

static int one_elem(IntVec *v, size_t at, const char *what) {
  if (v->n != 1 || N[v->v[0]].kind != N_ELEM) fail(at, "a %s branch must be exactly one element", what);
  return v->v[0];
}

static void parse_if(int tpl, int parent, IntVec *kids, size_t at, const char *cond) {
  int me = new_node(N_IF, tpl, parent, at);
  N[me].expr = trim(cond);
  if (!*N[me].expr) fail(at, "{#if} needs a condition");
  PUSH(kids->v, kids->n, kids->cap, me);
  IntVec a = {0, 0, 0}, b = {0, 0, 0};
  int t = parse_content(tpl, me, &a, 1), has_else = 0;
  size_t else_at = 0;
  if (t == END_ELSE) {
    has_else = 1;
    else_at = term_at;
    t = parse_content(tpl, me, &b, 1);
  }
  if (t != END_IF) {
    if (t == END_TAG) stray_close();
    if (t == END_ELSE) fail(term_at, "{:else} appears twice in one {#if}");
    fail(at, "{#if} is never closed");
  }
  N[me].br[0] = one_elem(&a, at, "{#if}");
  if (has_else) N[me].br[1] = one_elem(&b, else_at, "{:else}");
}

static void parse_each(int tpl, int parent, IntVec *kids, size_t at, const char *body) {
  const char *as = strstr(body, " as ");
  const char *form = "{#each} must read {#each S->list as item (key)}";
  if (!as) fail(at, "%s", form);
  int me = new_node(N_EACH, tpl, parent, at);
  N[me].expr = trim(dupn(body, (size_t)(as - body)));
  const char *s = as + 4;
  while (is_ws(*s)) s++;
  const char *v = s;
  while (is_alnum(*s)) s++;
  if (s == v) fail(at, "%s", form);
  N[me].evar = dupn(v, (size_t)(s - v));
  while (is_ws(*s)) s++;
  const char *close = strrchr(s, ')');
  if (*s != '(' || !close) fail(at, "%s", form);
  for (const char *c = close + 1; *c; c++)
    if (!is_ws(*c)) fail(at, "%s", form);
  N[me].ekey = trim(dupn(s + 1, (size_t)(close - s - 1)));
  if (!*N[me].ekey || !*N[me].expr) fail(at, "%s", form);
  PUSH(kids->v, kids->n, kids->cap, me);
  IntVec b = {0, 0, 0};
  int t = parse_content(tpl, me, &b, 0);
  if (t != END_EACH) {
    if (t == END_TAG) stray_close();
    fail(at, "{#each} is never closed");
  }
  Node *r = b.n == 1 ? &N[b.v[0]] : 0;
  if (!r || r->kind != N_ELEM || !r->self_close || r->na)
    fail(at, "{#each} body must be one template reference like <row/>");
  N[me].eref = r->tag;
  N[me].eref_at = r->at;
}

static int parse_content(int tpl, int parent, IntVec *kids, int check) {
  for (;;) {
    if (P >= slen) return END_EOF;
    if (at_str("<!--")) {
      size_t at = P;
      char *e = strstr(src + P + 4, "-->");
      if (!e) fail(at, "comment is never closed");
      P = (size_t)(e - src) + 3;
      continue;
    }
    if (at_str("</")) {
      term_at = P;
      P += 2;
      term_name = read_tag_name(term_at);
      skip_ws();
      if (P >= slen || src[P] != '>') fail(term_at, "expected > to end </%s", term_name);
      P++;
      return END_TAG;
    }
    if (src[P] == '<') { parse_element(tpl, parent, kids, check); continue; }
    if (src[P] == '{') {
      size_t at = P;
      char *in = read_brace(), *t = trim(in);
      if (!strncmp(t, "#if", 3) && (is_ws(t[3]) || !t[3])) { parse_if(tpl, parent, kids, at, t + 3); continue; }
      if (!strncmp(t, "#each", 5) && (is_ws(t[5]) || !t[5])) { parse_each(tpl, parent, kids, at, t + 5); continue; }
      if (!strcmp(t, ":else")) { term_at = at; return END_ELSE; }
      if (!strcmp(t, "/if")) { term_at = at; return END_IF; }
      if (!strcmp(t, "/each")) { term_at = at; return END_EACH; }
      if (t[0] == '#' || t[0] == ':' || t[0] == '/') fail(at, "unknown directive {%s}", t);
      check_text(ptag(check), at);
      int me = new_node(N_BIND, tpl, parent, at);
      if ((t[0] == 'n' || t[0] == 's') && t[1] == ':') {
        N[me].bt = t[0];
        N[me].expr = trim(t + 2);
      } else {
        N[me].expr = t;
      }
      if (!*N[me].expr) fail(at, "empty binding {}");
      PUSH(kids->v, kids->n, kids->cap, me);
      continue;
    }
    /* text: up to the next tag or brace. Whitespace runs holding a newline are
     * dropped at either end, so indentation between tags makes no nodes. */
    size_t s = P;
    while (P < slen && src[P] != '<' && src[P] != '{') P++;
    size_t a = s, z = P;
    int nl = 0;
    while (a < z && is_ws(src[a])) nl |= src[a++] == '\n';
    if (!nl) a = s;
    nl = 0;
    while (z > a && is_ws(src[z - 1])) nl |= src[--z] == '\n';
    if (!nl) z = P;
    if (a == z) continue;
    int blank = 1;
    for (size_t i = a; i < z; i++) blank &= is_ws(src[i]);
    const char *pt = ptag(check);
    if (blank && (parent < 0 || !check || (pt && table_ctx(pt)))) continue;
    check_text(pt, a);
    char *text = decode(a, z - a);
    int last = kids->n ? kids->v[kids->n - 1] : -1;
    if (last >= 0 && N[last].kind == N_TEXT) { /* text, a comment, more text: one node */
      char *m = xrealloc(0, strlen(N[last].text) + strlen(text) + 1);
      strcpy(m, N[last].text);
      strcat(m, text);
      N[last].text = m;
      continue;
    }
    int me = new_node(N_TEXT, tpl, parent, a);
    N[me].text = text;
    PUSH(kids->v, kids->n, kids->cap, me);
  }
}

/* "Type *name" */
static void parse_decl(const char *s, size_t at, const char *what, char **type, char **var) {
  const char *p = s, *t0, *v0;
  while (is_ws(*p)) p++;
  t0 = p;
  while (is_alnum(*p)) p++;
  *type = dupn(t0, (size_t)(p - t0));
  while (is_ws(*p)) p++;
  if (*p != '*' || !**type) fail(at, "%s must read \"Type *name\"", what);
  p++;
  while (is_ws(*p)) p++;
  v0 = p;
  while (is_alnum(*p)) p++;
  *var = dupn(v0, (size_t)(p - v0));
  while (is_ws(*p)) p++;
  if (*p || !is_ident(*var) || !is_ident(*type)) fail(at, "%s must read \"Type *name\"", what);
}

static void parse_template(void) {
  size_t at = P;
  P += 9;
  Attr *a = 0;
  int na = 0, ca = 0, sc = 0;
  parse_attrs(&a, &na, &ca, &sc, at);
  if (sc) fail(at, "<template/> has no body");
  Tpl t;
  memset(&t, 0, sizeof t);
  t.at = at;
  t.rec = -1;
  for (int i = 0; i < na; i++) {
    if (a[i].kind != A_STATIC) fail(a[i].at, "<template> attributes are plain strings");
    if (!strcmp(a[i].name, "name")) t.name = a[i].val;
    else if (!strcmp(a[i].name, "item")) { parse_decl(a[i].val, a[i].at, "item", &t.itype, &t.ivar); t.item_at = a[i].at; }
    else if (!strcmp(a[i].name, "state")) { parse_decl(a[i].val, a[i].at, "state", &t.stype, &t.svar); t.state_at = a[i].at; }
    else if (!strcmp(a[i].name, "root")) { t.rootdecl = a[i].val; t.root_at = a[i].at; }
    else fail(a[i].at, "unknown <template> attribute %s", a[i].name);
  }
  if (!t.name || !is_ident(t.name)) fail(at, "<template> needs name=\"identifier\"");
  for (int i = 0; i < nt; i++)
    if (!strcmp(T[i].name, t.name)) fail(at, "template \"%s\" is declared twice", t.name);
  if (!t.stype) fail(at, "template \"%s\" needs state=\"Type *name\"", t.name);
  t.page = !t.itype;
  t.first = nn;
  int ti = nt;
  PUSH(T, nt, ct, t);
  IntVec top = {0, 0, 0};
  Open o = {"template", at};
  osp = 0;
  PUSH(ostk, osp, ocap, o);
  int r = parse_content(ti, -1, &top, 1);
  if (!(r == END_TAG && !strcmp(term_name, "template"))) {
    if (r == END_TAG) stray_close();
    if (r == END_ELSE) fail(term_at, "{:else} without {#if}");
    if (r == END_IF) fail(term_at, "{/if} without {#if}");
    if (r == END_EACH) fail(term_at, "{/each} without {#each}");
    fail(at, "<template> is never closed");
  }
  osp = 0;
  if (top.n != 1 || N[top.v[0]].kind != N_ELEM) fail(at, "template \"%s\" must have exactly one root element", T[ti].name);
  T[ti].root = top.v[0];
  T[ti].last = nn;
  if (T[ti].rootdecl && strcmp(T[ti].rootdecl, N[top.v[0]].tag))
    fail(N[top.v[0]].at, "template \"%s\" declares root=\"%s\" but its root is <%s>", T[ti].name, T[ti].rootdecl, N[top.v[0]].tag);
}

/* ---- the state block: a tiny C-like declaration list ---- */
static size_t st_end;

static void st_skip(void) {
  for (;;) {
    while (P < st_end && is_ws(src[P])) P++;
    if (P + 1 < st_end && src[P] == '/' && src[P + 1] == '/') {
      while (P < st_end && src[P] != '\n') P++;
    } else if (P + 1 < st_end && src[P] == '/' && src[P + 1] == '*') {
      size_t at = P;
      char *e = strstr(src + P + 2, "*/");
      if (!e || (size_t)(e - src) >= st_end) fail(at, "comment is never closed");
      P = (size_t)(e - src) + 2;
    } else return;
  }
}

static char *st_ident(const char *what) {
  st_skip();
  size_t s = P;
  if (P < st_end && is_alpha(src[P]))
    while (P < st_end && is_alnum(src[P])) P++;
  if (P == s) fail(s, "expected %s", what);
  return dupn(src + s, P - s);
}

static void st_expect(char c) {
  st_skip();
  if (P >= st_end || src[P] != c) fail(P, "expected '%c'", c);
  P++;
}

static int st_peek(char c) {
  st_skip();
  return P < st_end && src[P] == c;
}

static void field_unique(Field *f, int n, const char *name, size_t at) {
  for (int i = 0; i < n; i++)
    if (!strcmp(f[i].name, name)) fail(at, "field %s is declared twice", name);
}

static void parse_state(void) {
  size_t at = P;
  if (have_state) fail(at, "a file has one <state> block");
  have_state = 1;
  st_at = at;
  P += 6;
  Attr *a = 0;
  int na = 0, ca = 0, sc = 0;
  parse_attrs(&a, &na, &ca, &sc, at);
  for (int i = 0; i < na; i++) {
    if (a[i].kind != A_STATIC) fail(a[i].at, "<state> attributes are plain strings");
    if (!strcmp(a[i].name, "name")) st_name = a[i].val;
    else if (!strcmp(a[i].name, "global")) st_global = a[i].val;
    else fail(a[i].at, "unknown <state> attribute %s", a[i].name);
  }
  if (!st_name || !is_ident(st_name)) fail(at, "<state> needs name=\"identifier\"");
  if (st_global && !is_ident(st_global)) fail(at, "global= must be an identifier");
  if (sc) { st_end = P; return; }
  char *e = strstr(src + P, "</state>");
  if (!e) fail(at, "<state> is never closed");
  st_end = (size_t)(e - src);
  for (;;) {
    st_skip();
    if (P >= st_end) break;
    size_t dat = P;
    char *kw = st_ident("u32, str, list or record");
    if (!strcmp(kw, "u32") || !strcmp(kw, "str")) {
      Field f = {st_ident("a field name"), kw[0] == 'u' ? T_U32 : T_STR, 0, -1, 0, 0, dat};
      field_unique(sf, nsf, f.name, dat);
      if (st_peek('=')) {
        P++;
        st_skip();
        size_t s = P;
        while (P < st_end && src[P] >= '0' && src[P] <= '9') P++;
        if (P == s || f.type != T_U32) fail(s, "only a u32 takes an initial value, a decimal number");
        f.init = strtoll(src + s, 0, 10);
        if (f.init > 0xffffffffLL) fail(s, "initial value does not fit a u32");
        f.has_init = 1;
      }
      st_expect(';');
      PUSH(sf, nsf, csf, f);
    } else if (!strcmp(kw, "list")) {
      Field f = {st_ident("a list name"), T_LIST, 0, -1, 0, 0, dat};
      field_unique(sf, nsf, f.name, dat);
      st_expect(':');
      f.rec = st_ident("a record name");
      st_expect(';');
      PUSH(sf, nsf, csf, f);
    } else if (!strcmp(kw, "record")) {
      Record r = {st_ident("a record name"), 0, 0, 0, dat};
      for (int i = 0; i < nrec; i++)
        if (!strcmp(recs[i].name, r.name)) fail(dat, "record %s is declared twice", r.name);
      if (!strcmp(r.name, st_name)) fail(dat, "record %s has the state's name", r.name);
      st_expect('{');
      while (!st_peek('}')) {
        size_t fat = P;
        char *ty = st_ident("u32, str or list");
        if (strcmp(ty, "u32") && strcmp(ty, "str") && strcmp(ty, "list"))
          fail(fat, "record fields are u32, str or list, not %s", ty);
        Field f = {st_ident("a field name"), ty[0] == 'u' ? T_U32 : ty[0] == 's' ? T_STR : T_LIST, 0, -1, 0, 0, fat};
        field_unique(r.f, r.n, f.name, fat);
        if (f.type == T_LIST) {
          st_expect(':');
          f.rec = st_ident("a record name");
        }
        st_expect(';');
        PUSH(r.f, r.n, r.cap, f);
        st_skip();
        if (P >= st_end) fail(dat, "record %s is never closed", r.name);
      }
      P++;
      if (st_peek(';')) P++;
      if (!r.n) fail(dat, "record %s has no fields", r.name);
      PUSH(recs, nrec, crec, r);
    } else {
      fail(dat, "unknown declaration %s: expected u32, str, list or record", kw);
    }
  }
  P = st_end + 8;
}

/* ---- resolving names and types ---- */
static int find_rec(const char *name) {
  for (int i = 0; i < nrec; i++)
    if (!strcmp(recs[i].name, name)) return i;
  return -1;
}

static int find_field(Field *f, int n, const char *name) {
  for (int i = 0; i < n; i++)
    if (!strcmp(f[i].name, name)) return i;
  return -1;
}

/* "a->b" or "a.b" naming a field of the item or the state: its type, else 0 */
static int field_type(const Tpl *t, const char *e) {
  const char *p = e, *a0, *b0;
  a0 = p;
  while (is_alnum(*p)) p++;
  char *a = dupn(a0, (size_t)(p - a0));
  while (is_ws(*p)) p++;
  if (p[0] == '-' && p[1] == '>') p += 2;
  else if (p[0] == '.') p++;
  else return 0;
  while (is_ws(*p)) p++;
  b0 = p;
  while (is_alnum(*p)) p++;
  char *b = dupn(b0, (size_t)(p - b0));
  while (is_ws(*p)) p++;
  if (*p || !*a || !*b) return 0;
  if (t->ivar && !strcmp(a, t->ivar) && t->rec >= 0) {
    int i = find_field(recs[t->rec].f, recs[t->rec].n, b);
    return i < 0 ? 0 : recs[t->rec].f[i].type;
  }
  if (!strcmp(a, t->svar)) {
    int i = find_field(sf, nsf, b);
    return i < 0 ? 0 : sf[i].type;
  }
  return 0;
}

static Field *ag_f(int a);
static int ag_n(int a);

typedef struct { char *name; char *sig; size_t at; } Handler;
static Handler *H;
static int nh, chd;
static int nslots;
static int *slot_node; /* slot -> node */
static int nslot_cap;

static int find_tpl(const char *name) {
  for (int i = 0; i < nt; i++)
    if (!strcmp(T[i].name, name)) return i;
  return -1;
}

static void resolve(void) {
  if (!have_state) fail(0, "a file needs a <state> block");
  for (int r = 0; r < nrec; r++)
    for (int i = 0; i < recs[r].n; i++) {
      Field *f = &recs[r].f[i];
      if (f->type != T_LIST) continue;
      f->reci = find_rec(f->rec);
      if (f->reci < 0) fail(f->at, "list %s holds an unknown record %s", f->name, f->rec);
      char buf[300];
      snprintf(buf, sizeof buf, "%s_n", f->name);
      if (find_field(recs[r].f, recs[r].n, buf) >= 0) fail(f->at, "list %s needs the name %s for its count", f->name, buf);
      snprintf(buf, sizeof buf, "%s_cap", f->name);
      if (find_field(recs[r].f, recs[r].n, buf) >= 0) fail(f->at, "list %s needs the name %s for its capacity", f->name, buf);
    }
  for (int i = 0; i < nsf; i++) {
    if (sf[i].type != T_LIST) continue;
    sf[i].reci = find_rec(sf[i].rec);
    if (sf[i].reci < 0) fail(sf[i].at, "list %s holds an unknown record %s", sf[i].name, sf[i].rec);
    char buf[300];
    snprintf(buf, sizeof buf, "%s_n", sf[i].name);
    if (find_field(sf, nsf, buf) >= 0) fail(sf[i].at, "list %s needs the name %s for its count", sf[i].name, buf);
    snprintf(buf, sizeof buf, "%s_cap", sf[i].name);
    if (find_field(sf, nsf, buf) >= 0) fail(sf[i].at, "list %s needs the name %s for its capacity", sf[i].name, buf);
  }
  int page = -1;
  for (int ti = 0; ti < nt; ti++) {
    Tpl *t = &T[ti];
    if (strcmp(t->stype, st_name)) fail(t->state_at, "state type %s is not the state block's %s", t->stype, st_name);
    if (t->page) {
      if (page >= 0) fail(t->at, "a file has one page template (a template without item=); \"%s\" is the second", t->name);
      page = ti;
    } else {
      t->rec = find_rec(t->itype);
      if (t->rec < 0) fail(t->item_at, "item type %s is not a record of the state", t->itype);
    }
  }
  if (page < 0) fail(0, "a file needs a page template (a template without item=)");
  for (int ti = 0; ti < nt; ti++) {
    Tpl *t = &T[ti];
    for (int n = t->first; n < t->last; n++) {
      Node *x = &N[n];
      if (x->kind == N_BIND && !x->bt) {
        int ty = field_type(t, x->expr);
        if (ty == T_U32) x->bt = 'n';
        else if (ty == T_STR) x->bt = 's';
        else fail(x->at, "cannot infer the type of {%s}: write {n:%s} for a u32 or {s:%s} for a cd_str", x->expr, x->expr, x->expr);
      }
      if (x->kind == N_ELEM && x->ref)
        for (int m = t->first; m < n; m++)
          if (N[m].kind == N_ELEM && N[m].ref && !strcmp(N[m].ref, x->ref))
            fail(x->at, "ref=\"%s\" is used twice in template \"%s\"", x->ref, t->name);
      if (x->kind == N_ELEM)
        for (int i = 0; i < x->na; i++) {
          Attr *a = &x->a[i];
          if (a->kind == A_BIND && field_type(t, a->val) == T_U32)
            fail(a->at, "attribute %s binds a u32; attribute values are cd_str", a->name);
          if (a->kind != A_EVENT) continue;
          char sig[512];
          if (t->page) snprintf(sig, sizeof sig, "%s *, const cd_event_rec *", t->stype);
          else snprintf(sig, sizeof sig, "%s *, %s *, const cd_event_rec *", t->stype, t->itype);
          int h = 0;
          while (h < nh && strcmp(H[h].name, a->val)) h++;
          if (h == nh) {
            Handler hd = {a->val, dupn(sig, strlen(sig)), a->at};
            PUSH(H, nh, chd, hd);
          } else if (strcmp(H[h].sig, sig)) {
            fail(a->at, "handler %s is used from a page and from an item template; it needs one signature", a->val);
          }
          if (x->slot < 0) {
            if (nslots == MAXSLOT) fail(x->at, "more than %d listening elements in one file", MAXSLOT);
            x->slot = ++nslots;
            if (nslots >= nslot_cap) {
              nslot_cap = nslot_cap ? nslot_cap * 2 : 16;
              slot_node = xrealloc(slot_node, (size_t)nslot_cap * sizeof(int));
            }
            slot_node[nslots] = n;
          }
        }
      if (x->kind != N_EACH) continue;
      for (int p = x->parent; p >= 0; p = N[p].parent)
        if (N[p].kind == N_IF) fail(x->at, "{#each} cannot be inside {#if}");
      Node *par = &N[x->parent];
      if (par->k[par->nk - 1] != n) fail(x->at, "{#each} must be the last child of its element");
      x->owns = par->nk == 1;
      /* S->list on the page, item->list (a list of the item's record) in an item template */
      const char *e = x->expr, *base = t->page ? t->svar : t->ivar;
      x->eowner = t->page ? -1 : t->rec;
      Field *lf = ag_f(x->eowner);
      size_t sl = strlen(base);
      if (strncmp(e, base, sl) || strncmp(e + sl, "->", 2) || (x->efield = find_field(lf, ag_n(x->eowner), e + sl + 2)) < 0 ||
          lf[x->efield].type != T_LIST)
        fail(x->at, "{#each} needs a list of the %s, like %s->list (got %s)", t->page ? "state" : "item", base, e);
      lf += x->efield;
      x->eref_t = find_tpl(x->eref);
      if (x->eref_t < 0) fail(x->eref_at, "unknown template <%s/>", x->eref);
      Tpl *r = &T[x->eref_t];
      if (r->page) fail(x->eref_at, "<%s/> is the page; an {#each} needs an item template", x->eref);
      if (r->rec != lf->reci)
        fail(x->eref_at, "template %s takes a %s but %s holds %s", r->name, r->itype, lf->name, lf->rec);
      if (r->used) fail(x->eref_at, "template %s is already used by another {#each}", r->name);
      r->used = n + 1;
      check_elem(par->tag, N[r->root].tag, x->eref_at);
    }
  }
  for (int ti = 0; ti < nt; ti++)
    if (!T[ti].page && !T[ti].used) fail(T[ti].at, "template \"%s\" is not used by any {#each}", T[ti].name);
  for (int ti = 0; ti < nt; ti++) { /* every item template hangs off the page through its {#each} chain */
    int u = ti, k = 0;
    while (!T[u].page && k++ <= nt) u = N[T[u].used - 1].tpl;
    if (!T[u].page) fail(T[ti].at, "template \"%s\" never reaches the page: its {#each} chain loops", T[ti].name);
  }
}

/* the template that owns item template t's {#each}, and how deep t sits under the page */
static int owner_of(int t) { return N[T[t].used - 1].tpl; }
static int tdepth(int t) { return T[t].page ? 0 : 1 + tdepth(owner_of(t)); }
static int t_lists_of(int t) { /* how many {#each} template t holds */
  int k = 0;
  for (int n = T[t].first; n < T[t].last; n++) k += N[n].kind == N_EACH && N[n].frag >= 0;
  return k;
}
static int listens(int t) {
  for (int m = T[t].first; m < T[t].last; m++)
    if (N[m].frag >= 0 && N[m].slot > 0) return 1;
  return 0;
}
/* does an event need to find the record an {#each}'s view shows: some template at or under it listens */
static int needs_find(int x) {
  int r = N[x].eref_t;
  if (listens(r)) return 1;
  for (int n = T[r].first; n < T[r].last; n++)
    if (N[n].kind == N_EACH && N[n].frag >= 0 && needs_find(n)) return 1;
  return 0;
}

/* ---- analysis: fragments, static paths, which nodes need ids ---- */
static void assign(int n, int frag, const int *path, int depth, int unstable, int *ln) {
  Node *x = &N[n];
  x->ln = (*ln)++;
  x->frag = frag;
  x->depth = depth;
  memcpy(x->path, path, (size_t)depth * sizeof(int));
  x->unstable = unstable;
  if (x->kind != N_ELEM) return;
  if (depth + 1 >= MAXD) fail(x->at, "elements nest deeper than %d", MAXD);
  int si = 0, seen_if = 0, p2[MAXD];
  memcpy(p2, path, (size_t)depth * sizeof(int));
  for (int i = 0; i < x->nk; i++) {
    int c = N[n].k[i];
    if (N[c].kind == N_IF) {
      seen_if = 1;
      N[c].ln = (*ln)++;
      N[c].frag = frag;
      for (int b = 0; b < 2; b++) {
        if (N[c].br[b] < 0) continue;
        Frag f = {N[n].tpl, N[c].br[b], c, b, 0, 0, 0};
        PUSH(F, nf, cfr, f);
        assign(N[c].br[b], nf - 1, p2, 0, 0, ln);
      }
    } else if (N[c].kind == N_EACH) {
      N[c].ln = (*ln)++;
      N[c].frag = frag;
    } else {
      p2[depth] = si++;
      assign(c, frag, p2, depth + 1, unstable || seen_if, ln);
    }
  }
}

static int is_froot(int n) { return N[n].kind == N_ELEM && N[n].frag >= 0 && F[N[n].frag].root == n; }

static void analyse(void) {
  for (int ti = 0; ti < nt; ti++) { /* fragment ti is template ti's root; branches follow */
    Frag f = {ti, T[ti].root, -1, 0, 0, 0, 0};
    PUSH(F, nf, cfr, f);
  }
  for (int ti = 0; ti < nt; ti++) {
    int ln = 0, path[MAXD];
    assign(T[ti].root, ti, path, 0, 0, &ln);
  }
  for (int n = 0; n < nn; n++) {
    Node *x = &N[n];
    if (x->frag < 0 || is_froot(n)) continue;
    if (x->kind == N_ELEM) {
      if (x->slot > 0 || x->ref) x->named = 1;
      for (int i = 0; i < x->nk; i++)
        if (N[x->k[i]].kind == N_IF || N[x->k[i]].kind == N_EACH) x->named = 1;
      if (x->unstable && (x->ntog || x->nattrb)) x->named = 1;
    }
    if (x->kind == N_BIND && x->unstable) x->named = 1;
  }
  /* an {#if} inserts before the first static sibling after it: that sibling needs an id */
  for (int n = 0; n < nn; n++) {
    if (N[n].kind != N_ELEM || N[n].frag < 0) continue;
    for (int i = 0; i < N[n].nk; i++) {
      if (N[N[n].k[i]].kind != N_IF) continue;
      for (int j = i + 1; j < N[n].nk; j++) {
        int c = N[n].k[j];
        if (N[c].kind == N_IF) continue;
        if (N[c].kind != N_EACH) N[c].named = 1;
        break;
      }
    }
  }
  int tid = 0;
  for (int f = 0; f < nf; f++)
    if (f >= nt || !T[f].page) F[f].tid = ++tid; /* every fragment but the page itself is cloned */
}

/* ---- code generation ---- */
static char **lits;
static int nlit, clit;

static int lit(const char *s) {
  for (int i = 0; i < nlit; i++)
    if (!strcmp(lits[i], s)) return i;
  PUSH(lits, nlit, clit, dupn(s, strlen(s)));
  return nlit - 1;
}

static void out_cstr(Buf *b, const char *s) {
  out(b, "\"");
  for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
    if (*p == '"' || *p == '\\') out(b, "\\%c", *p);
    else if (*p >= 0x20 && *p < 0x7f) out(b, "%c", *p);
    else out(b, "\\%03o", *p);
  }
  out(b, "\"");
}

static int use_cls, use_str;
static const Tpl *cur; /* the template being generated */

/* user text inside a generated C comment: a star-slash in it would end the comment early */
static char *cmt(const char *s) {
  char *d = xrealloc(0, strlen(s) * 2 + 1), *o = d;
  for (; *s; s++) {
    *o++ = *s;
    if (s[0] == '*' && s[1] == '/') *o++ = ' ';
  }
  *o = 0;
  return d;
}

static const char *vname(const Tpl *t) { static char b[300]; snprintf(b, sizeof b, "%s_view", t->name); return b; }

static void params(Buf *b, const Tpl *t) {
  if (t->page) out(b, "%s *%s, %s_view *cdt_v", t->stype, t->svar, t->name);
  else out(b, "%s *%s, %s *%s, %s_view *cdt_v", t->stype, t->svar, t->itype, t->ivar, t->name);
}

static void args(Buf *b, const Tpl *t) {
  if (t->page) out(b, "%s, cdt_v", t->svar);
  else out(b, "%s, %s, cdt_v", t->svar, t->ivar);
}

static void voids(Buf *b, const Tpl *t) {
  out(b, "  (void)%s;", t->svar);
  if (!t->page) out(b, " (void)%s;", t->ivar);
  out(b, " (void)cdt_v;\n");
}

static void fname(Buf *b, int f, const char *what) {
  const Tpl *t = &T[F[f].tpl];
  if (f == F[f].tpl && !t->page && (!strcmp(what, "create") || !strcmp(what, "patch"))) out(b, "%s_%s", t->name, what);
  else out(b, "%s_f%d_%s", t->name, N[F[f].root].ln, what);
}

/* the id expression of a node, or 0 when it has none */
static const char *idx(int n) {
  static char b[4][300];
  static int k;
  char *s = b[k++ & 3];
  if (is_froot(n)) {
    int f = N[n].frag;
    if (F[f].ifn < 0) return "cdt_v->root";
    snprintf(s, 300, "cdt_v->if%d_root", N[F[f].ifn].ln);
    return s;
  }
  if (!N[n].named) return 0;
  if (N[n].ref) snprintf(s, 300, "cdt_v->%s", N[n].ref);
  else snprintf(s, 300, "cdt_v->id%d", N[n].ln);
  return s;
}

static void walk(Buf *b, const int *from, int fl, const int *to, int tl) {
  int c = 0;
  while (c < fl && c < tl && from[c] == to[c]) c++;
  if (c == fl) {
    for (int d = c; d < tl; d++) {
      out(b, " cd_down();");
      for (int i = 0; i < to[d]; i++) out(b, " cd_next();");
    }
    return;
  }
  for (int i = 0; i < fl - c - 1; i++) out(b, " cd_up();");
  for (int i = from[c]; i < to[c]; i++) out(b, " cd_next();");
  for (int d = c + 1; d < tl; d++) {
    out(b, " cd_down();");
    for (int i = 0; i < to[d]; i++) out(b, " cd_next();");
  }
}

static int walk_cost(const int *from, int fl, const int *to, int tl) {
  int c = 0, k = 0;
  while (c < fl && c < tl && from[c] == to[c]) c++;
  if (c == fl) {
    for (int d = c; d < tl; d++) k += 1 + to[d];
    return k;
  }
  k = fl - c - 1 + to[c] - from[c];
  for (int d = c + 1; d < tl; d++) k += 1 + to[d];
  return k;
}

static void bits(Buf *b, int n) {
  int t = 0;
  out(b, "(");
  for (int i = 0; i < N[n].na; i++) {
    Attr *a = &N[n].a[i];
    if (a->kind != A_TOGGLE) continue;
    out(b, "%s((%s) ? %uu : 0u)", t ? " | " : "", a->val, 1u << t);
    t++;
  }
  out(b, ")");
}

static void cls(Buf *b, int n, const char *v) {
  out(b, "cdt_cls(%s_k%d_c, %s_k%d_s, %s)", cur->name, N[n].ln, cur->name, N[n].ln, v);
}

static void tagval(Buf *b, int n) {
  if (cur->page) out(b, "%du << 24", N[n].slot);
  else out(b, "%du << 24 | (uint32_t)(cdt_v - %s_views)", N[n].slot, cur->name);
}

static void listen(Buf *b, int n, const char *id, const char *ind) {
  for (int i = 0; i < N[n].na; i++) {
    Attr *a = &N[n].a[i];
    if (a->kind != A_EVENT) continue;
    if (a->ev >= 0) out(b, "%scd_listen(%s, CD_EV_%s);\n", ind, id, EVENT_IDS[a->ev]);
    else out(b, "%scd_listen_named(%s, cdt_s[%d]); /* %s */\n", ind, id, lit(a->name + 3), cmt(a->name + 3));
  }
  out(b, "%scd_tag_set(%s, ", ind, id);
  tagval(b, n);
  out(b, ");\n");
}

static void frag_nodes(int n, IntVec *v) {
  PUSH(v->v, v->n, v->cap, n);
  if (N[n].kind != N_ELEM) return;
  for (int i = 0; i < N[n].nk; i++) {
    int c = N[n].k[i];
    if (N[c].kind == N_IF || N[c].kind == N_EACH) PUSH(v->v, v->n, v->cap, c);
    else frag_nodes(c, v);
  }
}

static const char *anchor(int ifn) { /* the first present node after an {#if}, or 0 */
  static char b[4096];
  int par = N[ifn].parent, i = 0;
  while (N[par].k[i] != ifn) i++;
  b[0] = 0;
  int open = 0;
  for (int j = i + 1; j < N[par].nk; j++) {
    int c = N[par].k[j];
    char t[128];
    if (N[c].kind == N_IF) {
      snprintf(t, sizeof t, "cdt_v->if%d_root ? cdt_v->if%d_root : (", N[c].ln, N[c].ln);
      strcat(b, t);
      open++;
      continue;
    }
    if (N[c].kind != N_EACH) {
      strcat(b, idx(c));
      for (; open; open--) strcat(b, ")");
      return b;
    }
    break;
  }
  strcat(b, "0");
  for (; open; open--) strcat(b, ")");
  return b;
}

static void cond_b(Buf *b, int ifn) {
  out(b, "((%s) ? 1u : %s)", N[ifn].expr, N[ifn].br[1] >= 0 ? "2u" : "0u");
}

static int branch_frag(int ifn, int br) {
  for (int f = 0; f < nf; f++)
    if (F[f].ifn == ifn && F[f].br == br) return f;
  return -1;
}

static void insert_branches(Buf *b, int ifn, const char *ind) {
  for (int br = 0; br < 2; br++) {
    int f = branch_frag(ifn, br);
    if (f < 0) continue;
    out(b, "%s%sif (cdt_b == %d) { cd_at(%s, %s); ", ind, br ? "else " : "", br + 1, idx(N[ifn].parent), anchor(ifn));
    fname(b, f, "create");
    out(b, "(");
    args(b, cur);
    out(b, "); }\n");
  }
}

/* ---- the static shape of a fragment, for its template ---- */
static void shape(Buf *b, int n) {
  Node *x = &N[n];
  if (x->kind == N_TEXT) { out(b, "  cd_text(cdt_s[%d]);\n", lit(x->text)); return; }
  if (x->kind == N_BIND) { out(b, "  cd_text(cdt_s[%d]);\n", lit(" ")); return; }
  out(b, "  cd_open(cdt_s[%d]);", lit(x->tag));
  for (int i = 0; i < x->na; i++) {
    Attr *a = &x->a[i];
    if (a->kind == A_STATIC && !strcmp(a->name, "class")) out(b, " cd_class(cdt_s[%d]);", lit(a->val));
    else if (a->kind == A_STATIC) out(b, " cd_attr(cdt_s[%d], cdt_s[%d]);", lit(a->name), lit(a->val));
    else if (a->kind == A_BIND) out(b, " cd_attr(cdt_s[%d], cdt_s[%d]);", lit(a->name), lit(""));
  }
  out(b, "\n");
  for (int i = 0; i < x->nk; i++)
    if (N[x->k[i]].kind != N_IF && N[x->k[i]].kind != N_EACH) shape(b, x->k[i]);
  out(b, "  cd_close();\n");
}

static const char *static_class(int n) {
  for (int i = 0; i < N[n].na; i++)
    if (N[n].a[i].kind == A_STATIC && !strcmp(N[n].a[i].name, "class")) return N[n].a[i].val;
  return 0;
}

/* the class strings of a toggled element: the static part plus each subset of toggles */
static void class_table(Buf *b, int n) {
  const char *st = static_class(n);
  int k = N[n].ntog;
  out(b, "static const char *const %s_k%d_s[%d] = {", cur->name, N[n].ln, 1 << k);
  for (int m = 0; m < 1 << k; m++) {
    char s[4096];
    snprintf(s, sizeof s, "%s", st ? st : "");
    for (int i = 0, t = 0; i < N[n].na; i++) {
      if (N[n].a[i].kind != A_TOGGLE) continue;
      if (m >> t++ & 1) {
        if (*s) strncat(s, " ", sizeof s - strlen(s) - 1);
        strncat(s, N[n].a[i].name + 6, sizeof s - strlen(s) - 1);
      }
    }
    out(b, "%s", m ? ", " : "");
    out_cstr(b, s);
  }
  out(b, "};\nstatic cd_str %s_k%d_c[%d];\n", cur->name, N[n].ln, 1 << k);
  use_cls = 1;
}

/* attribute bindings and class at create time (last is the element) */
static void elem_values(Buf *b, int n, const char *ind, int clone) {
  Node *x = &N[n];
  int class_done = 0;
  for (int i = 0; i < x->na; i++) {
    Attr *a = &x->a[i];
    int is_class = (a->kind == A_STATIC && !strcmp(a->name, "class")) || a->kind == A_TOGGLE;
    if (is_class && x->ntog && !class_done) {
      class_done = 1;
      out(b, "%scdt_v->c%d = ", ind, x->ln);
      bits(b, n);
      out(b, ";\n");
      char v[32];
      snprintf(v, sizeof v, "cdt_v->c%d", x->ln);
      if (clone || !static_class(n)) out(b, "%sif (%s) cd_class(", ind, v);
      else out(b, "%scd_class(", ind);
      cls(b, n, v);
      out(b, ");\n");
      continue;
    }
    if (clone) {
      if (a->kind == A_BIND) {
        out(b, "%scd_attr(cdt_s[%d], cdt_str(cdt_v->a%d_%d = (%s)));\n", ind, lit(a->name), x->ln, i, a->val);
        use_str = 1;
      }
      continue;
    }
    if (a->kind == A_STATIC && !strcmp(a->name, "class")) out(b, "%scd_class(cdt_s[%d]);\n", ind, lit(a->val));
    else if (a->kind == A_STATIC) out(b, "%scd_attr(cdt_s[%d], cdt_s[%d]);\n", ind, lit(a->name), lit(a->val));
    else if (a->kind == A_BIND) {
      out(b, "%scd_attr(cdt_s[%d], cdt_str(cdt_v->a%d_%d = (%s)));\n", ind, lit(a->name), x->ln, i, a->val);
      use_str = 1;
    }
  }
}

static void name_it(Buf *b, int n, const char *ind) {
  const char *id = idx(n);
  if (!id) return;
  if (N[n].kind == N_ELEM && N[n].slot > 0) listen(b, n, id, ind);
  else out(b, "%scd_name(%s);\n", ind, id);
}

static void bind_value(Buf *b, int n, const char *ind, int clone) {
  Node *x = &N[n];
  if (x->bt == 's') {
    out(b, "%s%s(cdt_str(cdt_v->t%d = (%s)));\n", ind, clone ? "cd_value" : "cd_text", x->ln, x->expr);
    use_str = 1;
  } else {
    out(b, "%s%s(cdt_v->t%d = (uint32_t)(%s));\n", ind, clone ? "cd_value_num" : "cd_text_num", x->ln, x->expr);
  }
}

static int unnamed_root = -1; /* the page root when nothing names or reaches it */

/* Does anything need the page root's id: its own listeners, values, branches or lists,
 * or a patch that walks from it? */
static int root_used(int f) {
  int r = F[f].root;
  if (N[r].slot > 0 || N[r].ntog || N[r].nattrb || N[r].ref) return 1;
  IntVec v = {0, 0, 0};
  frag_nodes(r, &v);
  int used = 0;
  for (int i = 0; i < v.n; i++) {
    int n = v.v[i];
    if ((N[n].kind == N_IF || N[n].kind == N_EACH) && N[n].parent == r) used = 1;
    if (!idx(n) && (N[n].kind == N_BIND || (N[n].kind == N_ELEM && (N[n].ntog || N[n].nattrb)))) used = 1;
  }
  free(v.v);
  return used;
}

/* Direct build: the page, and its branches on the first render, emitted in document
 * order with real values, so a hydrating shim adopts every node in turn. */
static void direct(Buf *b, int n, int depth) {
  char ind[80];
  memset(ind, ' ', sizeof ind);
  ind[2 + 2 * depth < 78 ? 2 + 2 * depth : 78] = 0;
  Node *x = &N[n];
  const char *id = idx(n);
  if (x->kind == N_TEXT) {
    out(b, "%scd_text(cdt_s[%d]);\n", ind, lit(x->text));
    if (id) out(b, "%s%s = cd_id_new();\n", ind, id), name_it(b, n, ind);
    return;
  }
  if (x->kind == N_BIND) {
    bind_value(b, n, ind, 0);
    if (id) out(b, "%s%s = cd_id_new();\n", ind, id), name_it(b, n, ind);
    return;
  }
  if (x->kind == N_IF) {
    out(b, "%s{\n%s  uint32_t cdt_b = ", ind, ind);
    cond_b(b, n);
    out(b, ";\n%s  cdt_v->if%d_b = cdt_b;\n", ind, x->ln);
    for (int br = 0; br < 2; br++) {
      if (x->br[br] < 0) continue;
      out(b, "%s  %sif (cdt_b == %d) {\n", ind, br ? "else " : "", br + 1);
      direct(b, x->br[br], depth + 2);
      out(b, "%s  }\n", ind);
    }
    out(b, "%s}\n", ind);
    return;
  }
  if (x->kind == N_EACH) {
    out(b, "%s%s_list%d.parent = %s;\n", ind, cur->name, x->ei, idx(x->parent));
    out(b, "%s%s_list%d.owns_parent = %d;\n", ind, cur->name, x->ei, x->owns);
    return;
  }
  if (n == unnamed_root) id = 0;
  if (id) out(b, "%s%s = cd_id_new();\n", ind, id);
  if (id && x->ref && is_froot(n)) out(b, "%scdt_v->%s = %s;\n", ind, x->ref, id);
  out(b, "%scd_open(cdt_s[%d]);\n", ind, lit(x->tag));
  if (id) name_it(b, n, ind);
  elem_values(b, n, ind, 0);
  /* an {#if}'s later siblings are anchors: zero the roots it may read before they exist */
  for (int i = 0; i < x->nk; i++)
    if (N[x->k[i]].kind == N_IF) out(b, "%scdt_v->if%d_root = 0;\n", ind, N[x->k[i]].ln);
  for (int i = 0; i < x->nk; i++) direct(b, x->k[i], depth);
  out(b, "%scd_close();\n", ind);
}

static int create_action(int n) {
  Node *x = &N[n];
  if (x->kind == N_BIND) return 1;
  if (x->kind == N_TEXT) return x->named;
  return x->kind == N_ELEM && (x->named || x->slot > 0 || x->ntog || x->nattrb);
}

/* Clone build: a fragment's template is cloned, walked to fill bindings, closed,
 * and its {#if} branches are inserted after it closes. */
static void clone_create(Buf *b, int f) {
  const Tpl *t = &T[F[f].tpl];
  int root = F[f].root;
  if (f == F[f].tpl) out(b, "cd_id ");
  else out(b, "static cd_id ");
  fname(b, f, "create");
  out(b, "(");
  params(b, t);
  out(b, ") {\n");
  voids(b, t);
  IntVec v = {0, 0, 0};
  frag_nodes(root, &v);
  out(b, "  cd_id cdt_root = %s = cd_id_new();\n", idx(root));
  if (N[root].ref) out(b, "  cdt_v->%s = cdt_root;\n", N[root].ref);
  for (int i = 0; i < v.n; i++)
    if (N[v.v[i]].kind == N_IF) out(b, "  cdt_v->if%d_root = 0;\n", N[v.v[i]].ln);
  out(b, "  cd_clone(%d);\n", F[f].tid);
  if (N[root].slot > 0) listen(b, root, "cdt_root", "  ");
  else out(b, "  cd_name(cdt_root);\n");
  elem_values(b, root, "  ", 1);
  int pos[MAXD], pl = 0;
  for (int i = 1; i < v.n; i++) {
    int n = v.v[i];
    if (N[n].kind == N_IF || N[n].kind == N_EACH || !create_action(n)) continue;
    out(b, " ");
    if (pl && 1 + walk_cost(0, 0, N[n].path, N[n].depth) < walk_cost(pos, pl, N[n].path, N[n].depth)) {
      out(b, " cd_sel(cdt_root);"); /* back to the root is shorter than climbing */
      walk(b, 0, 0, N[n].path, N[n].depth);
    } else {
      walk(b, pos, pl, N[n].path, N[n].depth);
    }
    out(b, "\n");
    memcpy(pos, N[n].path, (size_t)N[n].depth * sizeof(int));
    pl = N[n].depth;
    const char *id = idx(n);
    if (N[n].kind == N_BIND) bind_value(b, n, "  ", 1);
    if (id) out(b, "  %s = cd_id_new();\n", id), name_it(b, n, "  ");
    if (N[n].kind == N_ELEM) elem_values(b, n, "  ", 1);
  }
  out(b, "  cd_close();\n");
  for (int i = 0; i < v.n; i++) {
    int n = v.v[i];
    if (N[n].kind != N_IF) continue;
    out(b, "  {\n    uint32_t cdt_b = ");
    cond_b(b, n);
    out(b, ";\n    cdt_v->if%d_b = cdt_b;\n", N[n].ln);
    insert_branches(b, n, "    ");
    out(b, "  }\n");
  }
  if (f == F[f].tpl && t_lists_of(f)) { /* the item's own lists, filled once it is in place */
    for (int n = t->first; n < t->last; n++) {
      if (N[n].kind != N_EACH || N[n].frag < 0) continue;
      out(b, "  cdt_v->list%d = (cd_list){0};\n  cdt_v->list%d.parent = %s;\n  cdt_v->list%d.owns_parent = %d;\n", N[n].ei, N[n].ei,
          idx(N[n].parent), N[n].ei, N[n].owns);
    }
    out(b, "  %s_lists(%s, %s, cdt_v);\n", t->name, t->svar, t->ivar);
  }
  out(b, "  return cdt_root;\n}\n\n");
  free(v.v);
}

/* the walk to a node without an id: select the fragment root, then down/next */
static void reach(Buf *b, int n) {
  out(b, "cd_sel(%s);", idx(F[N[n].frag].root));
  walk(b, 0, 0, N[n].path, N[n].depth);
  out(b, " ");
}

static void patch_body(Buf *b, int f) {
  IntVec v = {0, 0, 0};
  frag_nodes(F[f].root, &v);
  for (int i = 0; i < v.n; i++) {
    int n = v.v[i];
    Node *x = &N[n];
    const char *id = idx(n);
    if (x->kind == N_ELEM && x->ntog) {
      out(b, "  {\n    uint32_t cdt_c = ");
      bits(b, n);
      out(b, ";\n    if (cdt_c != cdt_v->c%d) {\n      cdt_v->c%d = cdt_c;\n      ", x->ln, x->ln);
      if (id) out(b, "cd_set_class(%s, ", id);
      else reach(b, n), out(b, "cd_class(");
      cls(b, n, "cdt_c");
      out(b, ");\n    }\n  }\n");
    }
    if (x->kind == N_ELEM)
      for (int k = 0; k < x->na; k++) {
        Attr *a = &x->a[k];
        if (a->kind != A_BIND) continue;
        out(b, "  {\n    cd_str cdt_a = (%s);\n    if (cdt_a != cdt_v->a%d_%d) {\n      cdt_v->a%d_%d = cdt_a;\n      ",
            a->val, x->ln, k, x->ln, k);
        if (id) out(b, "cd_set_attr(%s, cdt_s[%d], cdt_str(cdt_a));\n", id, lit(a->name));
        else reach(b, n), out(b, "cd_attr(cdt_s[%d], cdt_str(cdt_a));\n", lit(a->name));
        out(b, "    }\n  }\n");
      }
    if (x->kind == N_BIND) {
      int s = x->bt == 's';
      out(b, "  {\n    %s cdt_t = %s(%s);\n    if (cdt_t != cdt_v->t%d) {\n      cdt_v->t%d = cdt_t;\n      ",
          s ? "cd_str" : "uint32_t", s ? "" : "(uint32_t)", x->expr, x->ln, x->ln);
      if (id) out(b, "%s(%s, %s);\n", s ? "cd_set_text" : "cd_set_text_num", id, s ? "cdt_str(cdt_t)" : "cdt_t");
      else reach(b, n), out(b, "%s(%s);\n", s ? "cd_value" : "cd_value_num", s ? "cdt_str(cdt_t)" : "cdt_t");
      out(b, "    }\n  }\n");
    }
    if (x->kind == N_IF) {
      out(b, "  {\n    uint32_t cdt_b = ");
      cond_b(b, n);
      out(b, ";\n    if (cdt_b != cdt_v->if%d_b) {\n      if (cdt_v->if%d_b) {\n        cd_remove(cdt_v->if%d_root);\n", x->ln, x->ln, x->ln);
      for (int br = 0; br < 2; br++) {
        int g = branch_frag(n, br);
        if (g < 0 || !F[g].has_free) continue;
        out(b, "        if (cdt_v->if%d_b == %d) ", x->ln, br + 1);
        fname(b, g, "free");
        out(b, "(cdt_v);\n");
      }
      out(b, "        cdt_v->if%d_root = 0;\n      }\n      cdt_v->if%d_b = cdt_b;\n", x->ln, x->ln);
      insert_branches(b, n, "      ");
      out(b, "    }");
      for (int br = 0; br < 2; br++) {
        int g = branch_frag(n, br);
        if (g < 0 || !F[g].has_patch) continue;
        out(b, " else if (cdt_b == %d) {\n      ", br + 1);
        fname(b, g, "patch");
        out(b, "(");
        args(b, cur);
        out(b, ");\n    }");
      }
      out(b, "\n  }\n");
    }
  }
  free(v.v);
}

static void patch_fn(Buf *b, int f) {
  const Tpl *t = &T[F[f].tpl];
  out(b, "%s", f == F[f].tpl ? "void " : "static void ");
  fname(b, f, "patch");
  out(b, "(");
  params(b, t);
  out(b, ") {\n");
  voids(b, t);
  patch_body(b, f);
  if (f == F[f].tpl && !t->page && t_lists_of(f)) out(b, "  %s_lists(%s, %s, cdt_v);\n", t->name, t->svar, t->ivar);
  out(b, "}\n\n");
}

static int frag_has_patch(int f) {
  IntVec v = {0, 0, 0};
  frag_nodes(F[f].root, &v);
  int r = 0;
  for (int i = 0; i < v.n; i++) {
    Node *x = &N[v.v[i]];
    if (x->kind == N_BIND || x->kind == N_IF || (x->kind == N_ELEM && (x->ntog || x->nattrb))) r = 1;
  }
  free(v.v);
  return r;
}

static int frag_has_free(int f) {
  IntVec v = {0, 0, 0};
  frag_nodes(F[f].root, &v);
  int r = 0;
  for (int i = 1; i < v.n; i++)
    if (N[v.v[i]].named || N[v.v[i]].kind == N_IF) r = 1;
  free(v.v);
  return r;
}

static void free_fn(Buf *b, int f) {
  out(b, "static void ");
  fname(b, f, "free");
  out(b, "(%s *cdt_v) {\n", vname(cur));
  IntVec v = {0, 0, 0};
  frag_nodes(F[f].root, &v);
  for (int i = 1; i < v.n; i++) {
    int n = v.v[i];
    if (N[n].named) out(b, "  cd_id_free(%s);\n", idx(n));
    if (N[n].kind != N_IF) continue;
    for (int br = 0; br < 2; br++) {
      int g = branch_frag(n, br);
      if (g < 0) continue;
      out(b, "  %sif (cdt_v->if%d_b == %d) { ", br ? "else " : "", N[n].ln, br + 1);
      if (F[g].has_free) fname(b, g, "free"), out(b, "(cdt_v); ");
      out(b, "cd_id_free(cdt_v->if%d_root); }\n", N[n].ln);
    }
  }
  out(b, "}\n\n");
  free(v.v);
}

static void view_struct(Buf *b, const Tpl *t, int ti) {
  out(b, "typedef struct %s_view {\n  cd_id root;\n", t->name);
  if (!t->page) out(b, "  uint32_t key, idx; /* the item's key and its index at the last render */\n");
  if (!t->page && !T[owner_of(ti)].page) out(b, "  uint32_t pvi; /* the view of the item whose list holds this one */\n");
  if (!t->page)
    for (int n = t->first; n < t->last; n++)
      if (N[n].kind == N_EACH && N[n].frag >= 0) out(b, "  cd_list list%d; /* {#each %s} */\n", N[n].ei, cmt(N[n].expr));
  for (int n = t->first; n < t->last; n++) {
    Node *x = &N[n];
    if (x->frag < 0 || N[n].tpl != ti) continue;
    if (x->ref) out(b, "  cd_id %s; /* ref */\n", x->ref);
    else if (x->named && !is_froot(n)) out(b, "  cd_id id%d;\n", x->ln);
    if (x->kind == N_ELEM && x->ntog) out(b, "  uint32_t c%d;\n", x->ln);
    if (x->kind == N_ELEM)
      for (int i = 0; i < x->na; i++)
        if (x->a[i].kind == A_BIND) out(b, "  cd_str a%d_%d;\n", x->ln, i);
    if (x->kind == N_BIND) out(b, "  %s t%d;\n", x->bt == 's' ? "cd_str" : "uint32_t", x->ln);
    if (x->kind == N_IF) out(b, "  uint32_t if%d_b;\n  cd_id if%d_root;\n", x->ln, x->ln);
  }
  out(b, "} %s_view;\n", t->name);
}

static const char *ctype(int ty) { return ty == T_STR ? "cd_str" : "uint32_t"; }

/* ---- the state's code. An aggregate is the state (-1) or a record (0..nrec-1); every one
 * reachable from the state gets a blob size, writer and reader, and one holding lists a
 * drop that frees them. ---- */
static const char *ag_name(int a) { return a < 0 ? st_name : recs[a].name; }
static Field *ag_f(int a) { return a < 0 ? sf : recs[a].f; }
static int ag_n(int a) { return a < 0 ? nsf : recs[a].n; }
static int ag_lists(int a) {
  for (int i = 0; i < ag_n(a); i++)
    if (ag_f(a)[i].type == T_LIST) return 1;
  return 0;
}
static int *reached;
static void mark_reach(int a) {
  for (int i = 0; i < ag_n(a); i++) {
    Field *f = &ag_f(a)[i];
    if (f->type == T_LIST && !reached[f->reci]) {
      reached[f->reci] = 1;
      mark_reach(f->reci);
    }
  }
}
static int ag_min(int a) { /* the fewest blob bytes one record can take */
  int k = 0;
  for (int i = 0; i < ag_n(a); i++) k += ag_f(a)[i].type == T_STR ? 2 : 4;
  return k;
}

static void list_helpers(Buf *h, Buf *c, int a, Field *f) {
  const char *O = ag_name(a), *L = f->name, *R = f->rec;
  int deep = ag_lists(f->reci);
  out(h, "void %s_%s_reserve(%s *s, uint32_t n);\n", O, L, O);
  out(h, "%s *%s_%s_push(%s *s); /* a zeroed record at the end */\n", R, O, L, O);
  out(h, "void %s_%s_remove(%s *s, uint32_t i);%s\n", O, L, O, deep ? " /* frees the record's lists */" : "");
  out(c, "void %s_%s_reserve(%s *s, uint32_t n) {\n", O, L, O);
  out(c, "  if (n <= s->%s_cap) return;\n  uint32_t c = s->%s_cap ? s->%s_cap : 16;\n  while (c < n) c *= 2;\n", L, L, L);
  out(c, "  s->%s = cd_realloc(s->%s, c * (uint32_t)sizeof(%s));\n  s->%s_cap = c;\n}\n\n", L, L, R, L);
  out(c, "%s *%s_%s_push(%s *s) {\n  if (s->%s_n == s->%s_cap) %s_%s_reserve(s, s->%s_n + 1);\n", R, O, L, O, L, L, O, L, L);
  out(c, "  %s *r = &s->%s[s->%s_n++];\n  *r = (%s){0};\n  return r;\n}\n\n", R, L, L, R);
  out(c, "void %s_%s_remove(%s *s, uint32_t i) {\n  if (i >= s->%s_n) return;\n", O, L, O, L);
  if (deep) out(c, "  cdt_drop_%s(&s->%s[i]);\n", R, L);
  out(c, "  for (uint32_t j = i + 1; j < s->%s_n; j++) s->%s[j - 1] = s->%s[j];\n  s->%s_n--;\n}\n\n", L, L, L, L);
}

static void ag_code(Buf *c, int a) {
  const char *A = ag_name(a);
  Field *F = ag_f(a);
  int n = ag_n(a);
  /* size */
  out(c, "static uint64_t cdt_need_%s(const %s *r) {\n  uint64_t need = %d;\n  (void)r;\n", A, A, ag_min(a));
  for (int i = 0; i < n; i++) {
    if (F[i].type == T_STR)
      out(c, "  {\n    uint32_t k = cdt_len(r->%s);\n    if (k > 0xffff) return 0xffffffffffffffffull;\n    need += k;\n  }\n", F[i].name);
    else if (F[i].type == T_LIST) {
      out(c, "  for (uint32_t i = 0; i < r->%s_n; i++) {\n    uint64_t k = cdt_need_%s(&r->%s[i]);\n", F[i].name, F[i].rec, F[i].name);
      out(c, "    if (k == 0xffffffffffffffffull) return k;\n    need += k;\n  }\n");
    }
  }
  out(c, "  return need;\n}\n\n");
  /* writer */
  out(c, "static uint8_t *cdt_put_%s(uint8_t *d, const %s *r) {\n  (void)r;\n", A, A);
  for (int i = 0; i < n; i++) {
    if (F[i].type == T_U32) out(c, "  d = cdt_put32(d, r->%s);\n", F[i].name);
    else if (F[i].type == T_STR) out(c, "  d = cdt_putstr(d, r->%s);\n", F[i].name);
    else
      out(c, "  d = cdt_put32(d, r->%s_n);\n  for (uint32_t i = 0; i < r->%s_n; i++) d = cdt_put_%s(d, &r->%s[i]);\n", F[i].name,
          F[i].name, F[i].rec, F[i].name);
  }
  out(c, "  return d;\n}\n\n");
  /* reader: with r 0 it only checks */
  out(c, "static int cdt_walk_%s(%s *r, const uint8_t *b, uint32_t len, uint32_t *o) {\n  uint32_t k;\n  (void)r; (void)b; (void)k;\n", A, A);
  for (int i = 0; i < n; i++) {
    const char *f = F[i].name;
    if (F[i].type == T_U32) out(c, "  if (len - *o < 4) return -1;\n  if (r) r->%s = cdt_get32(b + *o);\n  *o += 4;\n", f);
    else if (F[i].type == T_STR)
      out(c, "  if (len - *o < 2) return -1;\n  k = cdt_get16(b + *o);\n  *o += 2;\n  if (len - *o < k) return -1;\n"
             "  if (r) r->%s = cd_intern((const char *)b + *o, k);\n  *o += k;\n", f);
    else {
      int sub = F[i].reci, deep = ag_lists(sub);
      out(c, "  if (len - *o < 4) return -1;\n  k = cdt_get32(b + *o);\n  *o += 4;\n  if (k > (len - *o) / %d) return -1;\n", ag_min(sub));
      out(c, "  if (r) {\n");
      if (deep) out(c, "    for (uint32_t i = 0; i < r->%s_n; i++) cdt_drop_%s(&r->%s[i]);\n", f, F[i].rec, f);
      out(c, "    %s_%s_reserve(r, k);\n    r->%s_n = k;\n", A, f, f);
      if (deep) out(c, "    for (uint32_t i = 0; i < k; i++) r->%s[i] = (%s){0};\n", f, F[i].rec);
      out(c, "  }\n  for (uint32_t i = 0; i < k; i++)\n    if (cdt_walk_%s(r ? &r->%s[i] : 0, b, len, o)) return -1;\n", F[i].rec, f);
    }
  }
  out(c, "  return 0;\n}\n\n");
  if (a >= 0 && ag_lists(a)) {
    out(c, "static void cdt_drop_%s(%s *r) {\n", A, A);
    for (int i = 0; i < n; i++) {
      if (F[i].type != T_LIST) continue;
      if (ag_lists(F[i].reci)) out(c, "  for (uint32_t i = 0; i < r->%s_n; i++) cdt_drop_%s(&r->%s[i]);\n", F[i].name, F[i].rec, F[i].name);
      out(c, "  cd_free(r->%s);\n  r->%s = 0;\n  r->%s_n = r->%s_cap = 0;\n", F[i].name, F[i].name, F[i].name, F[i].name);
    }
    out(c, "}\n\n");
  }
}

static void gen_state(Buf *h, Buf *c) {
  reached = xrealloc(0, (size_t)(nrec ? nrec : 1) * sizeof(int));
  memset(reached, 0, (size_t)(nrec ? nrec : 1) * sizeof(int));
  mark_reach(-1);
  for (int r = 0; r < nrec; r++) {
    out(h, "typedef struct %s {", recs[r].name);
    for (int i = 0; i < recs[r].n; i++) {
      Field *f = &recs[r].f[i];
      if (f->type == T_LIST) out(h, " struct %s *%s; uint32_t %s_n, %s_cap;", f->rec, f->name, f->name, f->name);
      else out(h, " %s %s;", ctype(f->type), f->name);
    }
    out(h, " } %s;\n", recs[r].name);
  }
  out(h, "typedef struct %s {\n", st_name);
  for (int i = 0; i < nsf; i++) {
    if (sf[i].type == T_LIST) out(h, "  %s *%s;\n  uint32_t %s_n, %s_cap;\n", sf[i].rec, sf[i].name, sf[i].name, sf[i].name);
    else out(h, "  %s %s;\n", ctype(sf[i].type), sf[i].name);
  }
  out(h, "} %s;\n", st_name);
  out(h, "/* initial values; a str holds 0 until set, and 0 renders as \"\" */\n#define %s_INIT {", st_name);
  int any = 0;
  for (int i = 0; i < nsf; i++)
    if (sf[i].has_init) out(h, "%s.%s = %lld", any++ ? ", " : "", sf[i].name, sf[i].init);
  out(h, "%s}\n", any ? "" : "0");
  if (st_global) out(h, "extern %s %s;\n", st_name, st_global);
  /* which encoders the layout needs: a list's count is a u32 */
  int strs = 0, u32s = 0;
  for (int a = -1; a < nrec; a++) {
    if (a >= 0 && !reached[a]) continue;
    for (int i = 0; i < ag_n(a); i++) {
      strs |= ag_f(a)[i].type == T_STR;
      u32s |= ag_f(a)[i].type != T_STR;
    }
  }
  for (int a = -1; a < nrec; a++) /* prototypes: records may refer to each other in any order */
    if (a < 0 || reached[a]) {
      const char *A = ag_name(a);
      out(c, "static uint64_t cdt_need_%s(const %s *r);\nstatic uint8_t *cdt_put_%s(uint8_t *d, const %s *r);\n", A, A, A, A);
      out(c, "static int cdt_walk_%s(%s *r, const uint8_t *b, uint32_t len, uint32_t *o);\n", A, A);
      if (a >= 0 && ag_lists(a)) out(c, "static void cdt_drop_%s(%s *r);\n", A, A);
    }
  out(c, "\n");
  for (int a = -1; a < nrec; a++) {
    if (a >= 0 && !reached[a]) continue;
    for (int i = 0; i < ag_n(a); i++)
      if (ag_f(a)[i].type == T_LIST) list_helpers(h, c, a, &ag_f(a)[i]);
  }
  out(h, "/* the state as a fixed little-endian blob: fields in declaration order, a u32 as 4 bytes,\n"
         " * a str as a u16 length then its bytes, a list as a u32 count then its records */\n");
  out(h, "uint32_t %s_save(const %s *s, uint8_t *dst, uint32_t cap); /* bytes, or the need when > cap */\n", st_name, st_name);
  out(h, "int %s_load(%s *s, const uint8_t *src, uint32_t len); /* 0 on success; s untouched on failure */\n", st_name, st_name);
  if (strs || u32s) {
    out(c, "static uint8_t *cdt_put16(uint8_t *d, uint32_t v) { d[0] = (uint8_t)v; d[1] = (uint8_t)(v >> 8); return d + 2; }\n");
    out(c, "static uint32_t cdt_get16(const uint8_t *s) { return s[0] | (uint32_t)s[1] << 8; }\n");
  }
  if (u32s) {
    out(c, "static uint8_t *cdt_put32(uint8_t *d, uint32_t v) { return cdt_put16(cdt_put16(d, v), v >> 16); }\n");
    out(c, "static uint32_t cdt_get32(const uint8_t *s) { return cdt_get16(s) | cdt_get16(s + 2) << 16; }\n");
  }
  if (strs) {
    out(c, "static uint32_t cdt_len(cd_str s) { uint32_t n = 0; if (s) cd_bytes(s, &n); return n; }\n");
    out(c, "static uint8_t *cdt_putstr(uint8_t *d, cd_str s) {\n  uint32_t n = cdt_len(s);\n  const char *b = n ? cd_bytes(s, &n) : \"\";\n");
    out(c, "  d = cdt_put16(d, n);\n  for (uint32_t i = 0; i < n; i++) d[i] = (uint8_t)b[i];\n  return d + n;\n}\n");
  }
  out(c, "\n");
  for (int a = -1; a < nrec; a++)
    if (a < 0 || reached[a]) ag_code(c, a);
  out(c, "uint32_t %s_save(const %s *s, uint8_t *dst, uint32_t cap) {\n  uint64_t need = cdt_need_%s(s);\n", st_name, st_name, st_name);
  out(c, "  if (need > 0xfffffffeu) return 0xffffffffu;\n  if (need > cap) return (uint32_t)need;\n");
  out(c, "  cdt_put_%s(dst, s);\n  return (uint32_t)need;\n}\n\n", st_name);
  out(c, "int %s_load(%s *s, const uint8_t *src, uint32_t len) { /* one pass to check, one to fill */\n  uint32_t o = 0;\n", st_name, st_name);
  out(c, "  if (cdt_walk_%s(0, src, len, &o) || o != len) return -1;\n  o = 0;\n  return cdt_walk_%s(s, src, len, &o);\n}\n\n", st_name, st_name);
  if (st_global) {
    out(c, "uint32_t cd_state_save(uint8_t *dst, uint32_t cap) { return %s_save(&%s, dst, cap); }\n", st_name, st_global);
    out(c, "int cd_state_load(const uint8_t *src, uint32_t len) { return %s_load(&%s, src, len); }\n\n", st_name, st_global);
  }
}

/* the record an {#each}'s view i shows, found by the key it was made with: for a list of the
 * state directly, for an item's list through the parent item's own find */
static void find_fn(Buf *c, int n) {
  Node *x = &N[n];
  Tpl *t = &T[N[n].tpl], *r = &T[x->eref_t];
  Field *lf = &ag_f(x->eowner)[x->efield];
  const char *O = t->name, *L = lf->name, *R = lf->rec, *SV = t->svar;
  int e = x->ei;
  out(c, "static %s *%s_find%d(%s *%s, uint32_t cdt_i) { /* the item a view shows */\n", R, O, e, st_name, SV);
  out(c, "  if (cdt_i >= %s_nviews) return 0;\n  %s_view *cdt_v = &%s_views[cdt_i];\n", r->name, r->name, r->name);
  const char *P = SV;
  if (!t->page) {
    Node *up = &N[t->used - 1];
    out(c, "  %s *cdt_p = %s_find%d(%s, cdt_v->pvi);\n  if (!cdt_p) return 0;\n", t->itype, T[up->tpl].name, up->ei, SV);
    P = "cdt_p";
  }
  out(c, "  if (cdt_v->idx < %s->%s_n && %s_key%d(%s, &%s->%s[cdt_v->idx]) == cdt_v->key) return &%s->%s[cdt_v->idx];\n", P, L, O, e,
      SV, P, L, P, L);
  out(c, "  for (uint32_t k = 0; k < %s->%s_n; k++)\n    if (%s_key%d(%s, &%s->%s[k]) == cdt_v->key) return &%s->%s[k];\n", P, L, O, e,
      SV, P, L, P, L);
  out(c, "  return 0;\n}\n");
}

/* An item template's own lists: each view holds a cd_list per {#each}; the callbacks read
 * the state, the parent record and the parent view from a context set before each update
 * (updates of one {#each} never nest: a template's lists hold other templates). */
static void item_lists(Buf *c, int ti) {
  Tpl *t = &T[ti];
  const char *O = t->name, *ST = st_name, *SV = t->svar, *IV = t->ivar;
  Buf up = {0, 0, 0};
  for (int n = t->first; n < t->last; n++) {
    if (N[n].kind != N_EACH || N[n].frag < 0) continue;
    Node *x = &N[n];
    Tpl *r = &T[x->eref_t];
    Field *lf = &ag_f(x->eowner)[x->efield];
    const char *L = lf->name, *R = lf->rec;
    int e = x->ei;
    out(c, "\n/* {#each %s as %s (%s)} <%s/> */\n", cmt(x->expr), x->evar, cmt(x->ekey), r->name);
    out(c, "static struct { %s *S; %s *r; uint32_t pvi; } %s_ctx%d;\nstatic uint32_t *%s_keys%d, %s_kcap%d;\n", ST, t->itype, O, e, O,
        e, O, e);
    out(c, "static uint32_t %s_key%d(%s *%s, %s *%s) { (void)%s; (void)%s; return (uint32_t)(%s); }\n", O, e, ST, SV, R, x->evar, SV,
        x->evar, x->ekey);
    out(c, "static cd_id %s_ec%d(void *cdt_ctx, uint32_t cdt_i, uint32_t *cdt_data) {\n  (void)cdt_ctx;\n", O, e);
    out(c, "  uint32_t cdt_vi = %s_view_new();\n  %s_view *cdt_v = &%s_views[cdt_vi];\n", r->name, r->name, r->name);
    out(c, "  cdt_v->key = %s_keys%d[cdt_i];\n  cdt_v->idx = cdt_i;\n  cdt_v->pvi = %s_ctx%d.pvi;\n  *cdt_data = cdt_vi;\n", O, e, O, e);
    out(c, "  return %s_create(%s_ctx%d.S, &%s_ctx%d.r->%s[cdt_i], cdt_v);\n}\n", r->name, O, e, O, e, L);
    out(c, "static void %s_ep%d(void *cdt_ctx, uint32_t cdt_i, cd_item *cdt_it) {\n  (void)cdt_ctx;\n", O, e);
    out(c, "  %s_view *cdt_v = &%s_views[cdt_it->data];\n  cdt_v->idx = cdt_i;\n", r->name, r->name);
    out(c, "  %s_patch(%s_ctx%d.S, &%s_ctx%d.r->%s[cdt_i], cdt_v);\n}\n", r->name, O, e, O, e, L);
    out(c, "static void %s_ed%d(void *cdt_ctx, cd_item *cdt_it) { (void)cdt_ctx; %s_release(cdt_it->data); }\n", O, e, r->name);
    out(c, "static cd_list_fns %s_fns%d = {0, %s_ec%d, %s_ep%d, %s_ed%d};\n", O, e, O, e, O, e, O, e);
    if (needs_find(n)) find_fn(c, n);
    out(&up, "  {\n    uint32_t cdt_n = %s->%s_n;\n", IV, L);
    out(&up, "    if (cdt_n > %s_kcap%d) {\n      %s_kcap%d = cdt_n * 2;\n      %s_keys%d = cd_realloc(%s_keys%d, %s_kcap%d * 4);\n    }\n", O, e,
        O, e, O, e, O, e, O, e);
    out(&up, "    for (uint32_t cdt_i = 0; cdt_i < cdt_n; cdt_i++) %s_keys%d[cdt_i] = %s_key%d(%s, &%s->%s[cdt_i]);\n", O, e, O, e, SV, IV, L);
    out(&up, "    %s_ctx%d.S = %s;\n    %s_ctx%d.r = %s;\n    %s_ctx%d.pvi = (uint32_t)(cdt_v - %s_views);\n", O, e, SV, O, e, IV, O, e, O);
    out(&up, "    cd_list_update(&cdt_v->list%d, %s_keys%d, cdt_n, &%s_fns%d);\n  }\n", e, O, e, O, e);
  }
  out(c, "static void %s_lists(%s *%s, %s *%s, %s_view *cdt_v) {\n  (void)%s;\n%s}\n\n", O, ST, SV, t->itype, IV, O, SV, up.d);
}

/* a view goes back to its free list: its lists' items first, then the ids it named */
static void release_fn(Buf *c, int ti) {
  Tpl *t = &T[ti];
  out(c, "static void %s_release(uint32_t cdt_vi) {\n  %s_view *cdt_v = &%s_views[cdt_vi];\n  (void)cdt_v;\n", t->name, t->name, t->name);
  for (int n = t->first; n < t->last; n++) {
    if (N[n].kind != N_EACH || N[n].frag < 0) continue;
    out(c, "  for (uint32_t i = 0; i < cdt_v->list%d.n; i++) {\n", N[n].ei);
    out(c, "    cd_id_free(cdt_v->list%d.items[i].node);\n    %s_release(cdt_v->list%d.items[i].data);\n  }\n", N[n].ei, T[N[n].eref_t].name,
        N[n].ei);
    out(c, "  cd_free(cdt_v->list%d.items);\n  cdt_v->list%d = (cd_list){0};\n", N[n].ei, N[n].ei);
  }
  if (F[ti].has_free) out(c, "  %s_f%d_free(cdt_v);\n", t->name, N[t->root].ln);
  out(c, "  %s_vfree[%s_nvfree++] = cdt_vi;\n}\n\n", t->name, t->name);
}

static void gen_item(Buf *h, Buf *c, int ti) {
  Tpl *t = &T[ti];
  cur = t;
  view_struct(h, t, ti);
  out(h, "cd_id %s_create(", t->name);
  params(h, t);
  out(h, ");\nvoid %s_patch(", t->name);
  params(h, t);
  out(h, ");\n");
  out(c, "/* ---- template %s: <%s> per %s ---- */\n", t->name, N[t->root].tag, t->itype);
  out(c, "static %s_view *%s_views;\nstatic uint32_t %s_nviews, %s_vcap, *%s_vfree, %s_nvfree;\n", t->name, t->name, t->name,
      t->name, t->name, t->name);
  out(c, "static uint32_t %s_view_new(void) {\n  if (%s_nvfree) return %s_vfree[--%s_nvfree];\n", t->name, t->name, t->name, t->name);
  out(c, "  if (%s_nviews == %s_vcap) {\n    %s_vcap = %s_vcap ? %s_vcap * 2 : 1024;\n", t->name, t->name, t->name, t->name, t->name);
  out(c, "    %s_views = cd_realloc(%s_views, %s_vcap * (uint32_t)sizeof(%s_view));\n", t->name, t->name, t->name, t->name);
  out(c, "    %s_vfree = cd_realloc(%s_vfree, %s_vcap * 4);\n  }\n  return %s_nviews++;\n}\n", t->name, t->name, t->name, t->name);
  for (int n = t->first; n < t->last; n++)
    if (N[n].frag >= 0 && N[n].kind == N_ELEM && N[n].ntog) class_table(c, n);
  out(c, "\n");
  if (t_lists_of(ti)) item_lists(c, ti);
  for (int f = nf; f-- > 0;) {
    if (F[f].tpl != ti) continue;
    if (F[f].has_free) free_fn(c, f);
    clone_create(c, f);
    if (F[f].has_patch) patch_fn(c, f);
  }
  release_fn(c, ti);
}

static void gen_page(Buf *h, Buf *c, int ti) {
  Tpl *t = &T[ti];
  cur = t;
  const char *P = t->name, *SV = t->svar, *ST = t->stype;
  out(h, "void %s_init(void);              /* interns strings, builds every template */\n", P);
  out(h, "void %s_create(%s *%s);  /* builds the page under the mount; call once */\n", P, ST, SV);
  out(h, "void %s_render(%s *%s);  /* patches the page and its lists to the state */\n", P, ST, SV);
  out(h, "void %s_dispatch(const cd_event_rec *e); /* calls the handler an event names */\n", P);
  out(c, "/* ---- the page: %s ---- */\n", P);
  view_struct(h, t, ti);
  out(h, "extern %s_view %s_v; /* the page's ids: ref=\"name\" fields reach named nodes */\n", P, P);
  out(c, "%s_view %s_v;\n", P, P);
  if (nslots) out(c, "static %s *%s_S;\n", ST, P);
  for (int n = t->first; n < t->last; n++)
    if (N[n].frag >= 0 && N[n].kind == N_ELEM && N[n].ntog) class_table(c, n);
  /* the lists */
  for (int n = t->first; n < t->last; n++) {
    if (N[n].kind != N_EACH || N[n].frag < 0) continue;
    Node *x = &N[n];
    Tpl *r = &T[x->eref_t];
    const char *L = sf[x->efield].name, *R = sf[x->efield].rec;
    int e = x->ei;
    out(c, "\n/* {#each %s as %s (%s)} <%s/> */\n", cmt(x->expr), x->evar, cmt(x->ekey), r->name);
    out(c, "static cd_list %s_list%d;\nstatic uint32_t *%s_keys%d, %s_kcap%d;\n", P, e, P, e, P, e);
    out(c, "static uint32_t %s_key%d(%s *%s, %s *%s) { (void)%s; (void)%s; return (uint32_t)(%s); }\n", P, e, ST, SV, R, x->evar, SV,
        x->evar, x->ekey);
    out(c, "static cd_id %s_ec%d(void *cdt_ctx, uint32_t cdt_i, uint32_t *cdt_data) {\n  %s *%s = cdt_ctx;\n", P, e, ST, SV);
    out(c, "  uint32_t cdt_vi = %s_view_new();\n  %s_view *cdt_v = &%s_views[cdt_vi];\n", r->name, r->name, r->name);
    out(c, "  cdt_v->key = %s_keys%d[cdt_i];\n  cdt_v->idx = cdt_i;\n  *cdt_data = cdt_vi;\n", P, e);
    out(c, "  return %s_create(%s, &%s->%s[cdt_i], cdt_v);\n}\n", r->name, SV, SV, L);
    out(c, "static void %s_ep%d(void *cdt_ctx, uint32_t cdt_i, cd_item *cdt_it) {\n  %s *%s = cdt_ctx;\n", P, e, ST, SV);
    out(c, "  %s_view *cdt_v = &%s_views[cdt_it->data];\n  cdt_v->idx = cdt_i;\n", r->name, r->name);
    out(c, "  %s_patch(%s, &%s->%s[cdt_i], cdt_v);\n}\n", r->name, SV, SV, L);
    out(c, "static void %s_ed%d(void *cdt_ctx, cd_item *cdt_it) { (void)cdt_ctx; %s_release(cdt_it->data); }\n", P, e, r->name);
    out(c, "static cd_list_fns %s_fns%d = {0, %s_ec%d, %s_ep%d, %s_ed%d};\n", P, e, P, e, P, e, P, e);
    if (needs_find(n)) find_fn(c, n);
  }
  out(c, "\n");
  for (int f = nf; f-- > 0;) {
    if (F[f].tpl != ti || f == ti) continue;
    if (F[f].has_free) free_fn(c, f);
    clone_create(c, f);
    if (F[f].has_patch) patch_fn(c, f);
  }
  /* init: strings, then every template's shape */
  Buf init = {0, 0, 0};
  for (int f = 0; f < nf; f++) {
    if (!F[f].tid) continue;
    if (F[f].ifn < 0) out(&init, "  cd_at(CD_DETACHED, 0); /* template %d: %s */\n", F[f].tid, T[F[f].tpl].name);
    else out(&init, "  cd_at(CD_DETACHED, 0); /* template %d: %s, the %s branch of {#if %s} */\n", F[f].tid, T[F[f].tpl].name,
             F[f].br ? "else" : "then", cmt(N[F[f].ifn].expr));
    shape(&init, F[f].root);
    out(&init, "  cd_tpl_save(%d);\n", F[f].tid);
  }
  /* create */
  Buf cr = {0, 0, 0};
  unnamed_root = root_used(ti) ? -1 : t->root;
  direct(&cr, t->root, 0);
  unnamed_root = -1;
  /* render */
  Buf rd = {0, 0, 0};
  patch_body(&rd, ti);
  for (int n = t->first; n < t->last; n++) {
    if (N[n].kind != N_EACH || N[n].frag < 0) continue;
    Node *x = &N[n];
    const char *L = sf[x->efield].name;
    int e = x->ei;
    out(&rd, "  {\n    uint32_t cdt_n = %s->%s_n;\n", SV, L);
    out(&rd, "    if (cdt_n > %s_kcap%d) {\n      %s_kcap%d = cdt_n * 2;\n      %s_keys%d = cd_realloc(%s_keys%d, %s_kcap%d * 4);\n    }\n", P,
        e, P, e, P, e, P, e, P, e);
    out(&rd, "    for (uint32_t cdt_i = 0; cdt_i < cdt_n; cdt_i++) %s_keys%d[cdt_i] = %s_key%d(%s, &%s->%s[cdt_i]);\n", P, e, P, e, SV, SV, L);
    out(&rd, "    %s_fns%d.ctx = %s;\n    cd_list_update(&%s_list%d, %s_keys%d, cdt_n, &%s_fns%d);\n  }\n", P, e, SV, P, e, P, e, P, e);
  }
  /* the strings exist now: emit the table, its helpers, init, create, render */
  int empty = use_str ? lit("") : 0; /* before the table is written */
  Buf top = {0, 0, 0};
  out(&top, "/* ---- strings, interned once by %s_init ---- */\nstatic const char *const cdt_lit[%d] = {", P, nlit ? nlit : 1);
  for (int i = 0; i < nlit; i++) {
    out(&top, "%s", i ? ", " : "");
    out_cstr(&top, lits[i]);
  }
  out(&top, "%s};\nstatic cd_str cdt_s[%d];\n", nlit ? "" : "\"\"", nlit ? nlit : 1);
  if (use_cls) out(&top, "static cd_str cdt_cls(cd_str *c, const char *const *s, uint32_t b) { return c[b] ? c[b] : (c[b] = cd_internz(s[b])); }\n");
  if (use_str) out(&top, "static cd_str cdt_str(cd_str s) { return s ? s : cdt_s[%d]; }\n", empty);
  out(&top, "\n");
  out(c, "void %s_init(void) {\n  for (uint32_t i = 0; i < %d; i++) cdt_s[i] = cd_internz(cdt_lit[i]);\n", P, nlit ? nlit : 1);
  out(c, "%s}\n\n", init.d ? init.d : "");
  out(c, "void %s_create(%s *%s) {\n  %s_view *cdt_v = &%s_v;\n", P, ST, SV, P, P);
  if (nslots) out(c, "  %s_S = %s;\n", P, SV);
  out(c, "  (void)%s;\n  cd_at(CD_MOUNT, 0);\n%s}\n\n", SV, cr.d);
  out(c, "void %s_render(%s *%s) {\n  %s_view *cdt_v = &%s_v;\n  (void)%s; (void)cdt_v;\n%s}\n\n", P, ST, SV, P, P, SV, rd.d ? rd.d : "");
  /* dispatch */
  out(c, "void %s_dispatch(const cd_event_rec *e) {\n", P);
  if (!nslots) out(c, "  (void)e;\n");
  else {
    out(c, "  uint32_t cdt_t = cd_tag((cd_id)e->id);\n  %s *%s = %s_S;\n  if (!%s) return;\n  switch (cdt_t >> 24) {\n", ST, SV, P, SV);
    for (int s = 1; s <= nslots; s++) {
      int n = slot_node[s];
      Tpl *o = &T[N[n].tpl];
      out(c, "  case %d: { /* %s: <%s> */\n", s, o->name, N[n].tag);
      if (!o->page) {
        Node *each = &N[o->used - 1];
        out(c, "    %s *%s = %s_find%d(%s, cdt_t & 0xffffffu);\n    if (!%s) return;\n", o->itype, o->ivar, T[each->tpl].name, each->ei,
            SV, o->ivar);
      }
      out(c, "    switch (e->type) {\n");
      int custom = 0;
      for (int i = 0; i < N[n].na; i++) {
        Attr *a = &N[n].a[i];
        if (a->kind != A_EVENT) continue;
        if (a->ev < 0) { custom = 1; continue; }
        if (o->page) out(c, "    case CD_EV_%s: %s(%s, e); break;\n", EVENT_IDS[a->ev], a->val, SV);
        else out(c, "    case CD_EV_%s: %s(%s, %s, e); break;\n", EVENT_IDS[a->ev], a->val, SV, o->ivar);
      }
      if (custom) { /* named events: e->key is the name's string id */
        out(c, "    case CD_EV_CUSTOM:\n");
        for (int i = 0; i < N[n].na; i++) {
          Attr *a = &N[n].a[i];
          if (a->kind != A_EVENT || a->ev >= 0) continue;
          out(c, "      if ((cd_str)e->key == cdt_s[%d]) ", lit(a->name + 3));
          if (o->page) out(c, "%s(%s, e); /* %s */\n", a->val, SV, cmt(a->name + 3));
          else out(c, "%s(%s, %s, e); /* %s */\n", a->val, SV, o->ivar, cmt(a->name + 3));
        }
        out(c, "      break;\n");
      }
      out(c, "    default: break;\n    }\n    break;\n  }\n");
    }
    out(c, "  default: break;\n  }\n");
  }
  out(c, "}\n");
  /* the string table goes first: everything above refers to it */
  Buf all = {0, 0, 0};
  out(&all, "%s%s", top.d, c->d ? c->d : "");
  *c = all;
}

static void generate(const char *base) {
  const char *slash = strrchr(in_path, '/');
  const char *srcname = slash ? slash + 1 : in_path;
  const char *bs = strrchr(base, '/');
  const char *bname = bs ? bs + 1 : base;
  char guard[256];
  size_t g = 0;
  g += (size_t)snprintf(guard, sizeof guard, "CDT_");
  for (const char *p = bname; *p && g < sizeof guard - 3; p++) guard[g++] = is_alnum(*p) ? (char)(*p >= 'a' && *p <= 'z' ? *p - 32 : *p) : '_';
  guard[g] = 0;
  Buf h = {0, 0, 0}, c = {0, 0, 0}, body = {0, 0, 0};
  out(&h, "/* Generated by cdtc %s from %s. Edit the template, not this file. */\n#ifndef %s_H\n#define %s_H\n#include \"cdom.h\"\n\n",
      CDTC_VERSION, srcname, guard, guard);
  out(&c, "/* Generated by cdtc %s from %s. Edit the template, not this file. */\n#include \"%s.h\"\n\n", CDTC_VERSION, srcname, bname);
  out(&h, "/* ---- state ---- */\n");
  if (st_global) out(&body, "%s %s = %s_INIT;\n\n", st_name, st_global, st_name);
  gen_state(&h, &body);
  for (int f = 0; f < nf; f++) F[f].has_free = 0;
  for (int f = nf; f-- > 0;) {
    F[f].has_free = frag_has_free(f);
    F[f].has_patch = f < nt || frag_has_patch(f); /* a template's patch is public: always there */
  }
  for (int ti = 0; ti < nt; ti++) { /* each template numbers its own lists */
    int ne = 0;
    for (int n = T[ti].first; n < T[ti].last; n++)
      if (N[n].kind == N_EACH && N[n].frag >= 0) N[n].ei = ne++;
  }
  for (int n = 0; n < nn; n++) /* finds call their parents' finds: declare them all first */
    if (N[n].kind == N_EACH && N[n].frag >= 0 && needs_find(n))
      out(&body, "static %s *%s_find%d(%s *%s, uint32_t cdt_i);\n", ag_f(N[n].eowner)[N[n].efield].rec, T[N[n].tpl].name, N[n].ei, st_name,
          T[N[n].tpl].svar);
  if (nh) {
    out(&h, "\n/* ---- handlers: the app defines these ---- */\n");
    for (int i = 0; i < nh; i++) {
      /* the signature: from the template using it */
      int ti = -1;
      for (int m = 0; m < nn && ti < 0; m++)
        if (N[m].kind == N_ELEM && N[m].frag >= 0)
          for (int k = 0; k < N[m].na; k++)
            if (N[m].a[k].kind == A_EVENT && !strcmp(N[m].a[k].val, H[i].name)) ti = N[m].tpl;
      Tpl *t = &T[ti];
      if (t->page) out(&h, "void %s(%s *%s, const cd_event_rec *e);\n", H[i].name, t->stype, t->svar);
      else out(&h, "void %s(%s *%s, %s *%s, const cd_event_rec *e);\n", H[i].name, t->stype, t->svar, t->itype, t->ivar);
    }
  }
  out(&h, "\n/* ---- templates ---- */\n");
  int maxd = 0; /* items deepest first: a template's code calls the templates its lists hold */
  for (int ti = 0; ti < nt; ti++)
    if (tdepth(ti) > maxd) maxd = tdepth(ti);
  for (int d = maxd; d > 0; d--)
    for (int ti = 0; ti < nt; ti++)
      if (!T[ti].page && tdepth(ti) == d) gen_item(&h, &body, ti);
  for (int ti = 0; ti < nt; ti++)
    if (T[ti].page) gen_page(&h, &body, ti);
  out(&h, "\n#endif\n");
  out(&c, "%s", body.d);
  char path[4096];
  snprintf(path, sizeof path, "%s.h", base);
  FILE *fh = fopen(path, "wb");
  if (!fh || fwrite(h.d, 1, h.n, fh) != h.n || fclose(fh)) { fprintf(stderr, "cdtc: cannot write %s\n", path); exit(1); }
  snprintf(path, sizeof path, "%s.c", base);
  FILE *fc = fopen(path, "wb");
  if (!fc || fwrite(c.d, 1, c.n, fc) != c.n || fclose(fc)) { fprintf(stderr, "cdtc: cannot write %s\n", path); exit(1); }
}

int main(int argc, char **argv) {
  const char *base = 0;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "-o") && i + 1 < argc) base = argv[++i];
    else if (!in_path && argv[i][0] != '-') in_path = argv[i];
    else { in_path = 0; break; }
  }
  if (!in_path || !base) {
    fprintf(stderr, "usage: cdtc <in.cdt> -o <out-base>   (writes <out-base>.h and <out-base>.c)\n");
    return 2;
  }
  FILE *f = fopen(in_path, "rb");
  if (!f) { fprintf(stderr, "cdtc: cannot read %s\n", in_path); return 1; }
  size_t cap = 0;
  for (;;) {
    if (slen + 65536 > cap) src = xrealloc(src, cap = (slen + 65536) * 2);
    size_t k = fread(src + slen, 1, cap - slen - 1, f);
    slen += k;
    if (!k) break;
  }
  fclose(f);
  src[slen] = 0;
  if (strlen(src) != slen) fail(strlen(src), "NUL byte in the source");
  for (;;) {
    skip_ws();
    if (P >= slen) break;
    if (at_str("<!--")) {
      size_t at = P;
      char *e = strstr(src + P + 4, "-->");
      if (!e) fail(at, "comment is never closed");
      P = (size_t)(e - src) + 3;
    } else if (at_str("<state") && (is_ws(src[P + 6]) || src[P + 6] == '>')) parse_state();
    else if (at_str("<template") && (is_ws(src[P + 9]) || src[P + 9] == '>')) parse_template();
    else fail(P, "expected <state> or <template>");
  }
  resolve();
  analyse();
  generate(base);
  return 0;
}
