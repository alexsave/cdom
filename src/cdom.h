/* cdom: C owns the tree, state and diffing; a fixed JS shim applies a
 * batched binary op stream to the real DOM and feeds events back. */
#ifndef CDOM_H
#define CDOM_H
#include <stdint.h>

/* Content strings (text, class values) cross once and are cached in JS by id
 * when 1; when 0 every use re-sends the bytes. Tag and attribute names are
 * always cached: they are the enum. */
#ifndef CDOM_INTERN_TEXT
#define CDOM_INTERN_TEXT 1
#endif

/* Resuming a server-rendered page (below) when 1. With 0 the virtual first render, the path
 * tree, the resolver and the event matcher are compiled out (a client-rendered page does not
 * pay for them), and the module exports neither cd_start_state nor cd_state_buf: it starts only
 * with cd_start, and a page that calls cdom.resume needs a build with 1. Events cross in the same
 * form either way. */
#ifndef CDOM_RESUME
#define CDOM_RESUME 1
#endif

typedef uint32_t cd_id;  /* node id; 0 is the mount element */
typedef uint32_t cd_str; /* interned string id */

#define CD_MOUNT 0u
#define CD_DETACHED 0xffffffffu

enum { CD_EV_CLICK, CD_EV_DBLCLICK, CD_EV_INPUT, CD_EV_CHANGE, CD_EV_KEYDOWN, CD_EV_KEYUP,
       CD_EV_FOCUSIN, CD_EV_FOCUSOUT, CD_EV_SUBMIT, CD_EV_POINTERDOWN, CD_EV_POINTERUP,
       CD_EV_POINTERMOVE, CD_EV_CUSTOM };

/* memory */
void *cd_alloc(uint32_t n);
void *cd_realloc(void *p, uint32_t n);
void cd_free(void *p);

/* strings */
cd_str cd_intern(const char *s, uint32_t len);
cd_str cd_internz(const char *s);
const char *cd_bytes(cd_str s, uint32_t *len);

/* node ids */
cd_id cd_id_new(void);
void cd_id_free(cd_id id);           /* JS drops its reference; no DOM change */
void cd_tag_set(cd_id id, uint32_t tag); /* app data per node, for events */
uint32_t cd_tag(cd_id id);

/* building: nodes go to (parent, before) when their root closes */
void cd_at(cd_id parent, cd_id before); /* before 0 = append; parent CD_DETACHED = keep off-DOM */
void cd_open(cd_str tag);
void cd_close(void);
void cd_text(cd_str s);
void cd_text_num(uint32_t n);
void cd_attr(cd_str name, cd_str value);
void cd_class(cd_str value);
void cd_name(cd_id id);              /* remember the last node as id */
void cd_listen(cd_id id, uint32_t ev); /* name the last node and deliver ev for it */
/* name the last node and deliver DOM events of that name for it (a web component's
 * events): they arrive as CD_EV_CUSTOM with e->key holding the name's cd_str id. The shim
 * listens in the capture phase at the mount, so events that do not bubble arrive too. */
void cd_listen_named(cd_id id, cd_str event);
void cd_value(cd_str s);             /* last.nodeValue */
void cd_value_num(uint32_t n);
void cd_tpl_save(uint32_t t);        /* the last closed root becomes template t */
void cd_clone(uint32_t t);           /* open a deep clone of template t */
/* Template slots: while a template is being built, cd_slot(k) marks the last node (a text node)
 * as slot k (k < CD_SLOTS). cd_tpl_text sets that text in template t itself, so the following
 * cd_clone(t) copies it in and the clone needs no walk. A slot keeps its last value: set every
 * slot before each clone. A template with slots has t < CD_TPLS. Each slot takes a node id of
 * its own (the core names the slot's text node with it), given back when t is built again. */
#define CD_TPLS 64
#define CD_SLOTS 16
void cd_slot(uint32_t k);
void cd_tpl_text(uint32_t t, uint32_t k, cd_str s);
void cd_tpl_text_num(uint32_t t, uint32_t k, uint32_t n);
void cd_down(void);                  /* last = last.firstChild */
void cd_next(void);                  /* last = last.nextSibling */
void cd_up(void);                    /* last = last.parentNode */
void cd_sel(cd_id id);               /* last = the node named id; then walk with down/next/up */

/* updating */
void cd_set_text(cd_id id, cd_str s); /* id is a text node */
void cd_set_text_num(cd_id id, uint32_t n);
void cd_set_attr(cd_id id, cd_str name, cd_str value);
void cd_set_class(cd_id id, cd_str value);
void cd_remove(cd_id id);            /* detach and free the id */
void cd_move(cd_id id, cd_id before);
void cd_clear(cd_id id);             /* drop all children; caller frees their ids */
void cd_detach(cd_id id);            /* take a node out of the document, remembering its place */
void cd_attach(cd_id id);            /* put it back where it was */
void cd_flush(void);

/* events: the app implements this; the shim fills the record */
/* id is the nearest listening ancestor-or-self of the target. For CD_EV_CUSTOM, key is
 * the event name's cd_str id (cd_listen_named) and x, y, button and mods are 0. path holds the
 * element-child indices from that node down toward the target, one byte per
 * level (byte 0 = which child of the listener), for depth levels (max 4). */
typedef struct { int32_t type, id, x, y, button, key, mods, path, depth; } cd_event_rec;
void cd_on_event(const cd_event_rec *e);
void cd_main(void);                  /* the app's entry, called once at start */
uint32_t cd_input_value(cd_id id, char *dst, uint32_t cap); /* copy an input's value on demand */

/* hosting a third-party widget or web component on a C-owned node: one generic op.
 * cd_host(id, kind, name) begins it, one value follows (built with the cd_v_* calls
 * below), and cd_host_end() sends it. The shim does, for the node named id:
 *   CD_HOST_MOUNT    node.$h = new (window.<name>)(node, value)   name is a dotted path
 *                    walked from window ("Chart", "L.Map"); a missing constructor throws.
 *   CD_HOST_CALL     node.$h.<name>(...value)                      value is an ARRAY of the
 *                    arguments (possibly empty); the return value is dropped.
 *   CD_HOST_SET      node.$h.<path> = value                        name is a dotted path inside
 *                    the instance ("data.datasets.0.data"); numeric parts index arrays.
 *   CD_HOST_PROP     node[name] = value                            a DOM property set (value,
 *                    checked, selected, disabled, scrollTop, a web component's property).
 *                    The C sink (SSR) writes value/checked/selected/disabled as the matching
 *                    attribute (or textarea text, or the selected option) so the no-JS page
 *                    shows the state; every other PROP writes nothing there.
 *   CD_HOST_UNMOUNT  node.$h.<name>() (e.g. "destroy"), then node.$h is deleted. Unmount a
 *                    widget before cd_remove of its node or an ancestor: the shim keeps no
 *                    list of instances, so a removed node's widget is otherwise leaked to it.
 * The value of an UNMOUNT is ignored but must be present (cd_v_null()).
 * A widget library must be defined before the wasm starts: put its <script src> before the
 * shim (ssr_page.h's scripts slot), so window.<name> exists when the first MOUNT runs. On a
 * server-rendered page the host ops of the first render (cd_main) on the server's nodes are
 * deferred to the end of that render (resuming, below), and the render itself runs only when
 * at the first interaction (the module is already fetched by then, after the load event); by
 * then the library, loaded before the shim, is defined. So until that interaction the widget
 * does not exist (the server writes its node empty, and only PROP leaves a trace in the HTML): a
 * page that wants it at once calls cdom.boot() right after cdom.resume (ssr_page's eager), and
 * the wasm loads and the first render runs at once.
 *
 * The op is one import, host(id, kind, name, v, nwords): v is the address in linear memory
 * of the value's nwords u32 words. The value ("cdv") is the one fixed format the shim
 * decodes, a tree of u32 words. Each value is a tag word, then its payload:
 *   0 NULL   1 FALSE   2 TRUE                    no payload
 *   3 U32    v                                    one word
 *   4 I32    v                                    one word, two's complement
 *   5 F64    lo hi                                the IEEE 754 bits, low word first
 *   6 STR    s                                    an interned string id (already sent)
 *   7 F64S   n, then 2n words                     n doubles as above: a Float64Array in JS
 *   8 ARRAY  n, then n values                     a JS array
 *   9 OBJECT n, then n pairs (key, value)         key: one word, a string id; a plain object
 * Strings decode as JS strings, numbers as numbers, TRUE/FALSE as booleans, NULL as null.
 * Containers nest at most CD_V_DEPTH deep and a whole op holds at most 32768 words (id,
 * kind and name included); a value over either limit, or one that is not exactly one
 * complete tree when cd_host_end runs, traps. The value is built aside; cd_host_end sends
 * every string it references in one batch, then makes the call. */
enum { CD_HOST_MOUNT, CD_HOST_CALL, CD_HOST_SET, CD_HOST_PROP, CD_HOST_UNMOUNT };
enum { CD_V_NULL, CD_V_FALSE, CD_V_TRUE, CD_V_U32, CD_V_I32, CD_V_F64, CD_V_STR, CD_V_F64S, CD_V_ARRAY,
       CD_V_OBJECT };
#define CD_V_DEPTH 64
void cd_host(cd_id id, uint32_t kind, cd_str name); /* begins the op; one value follows */
void cd_v_null(void);
void cd_v_bool(int b);
void cd_v_u32(uint32_t v);
void cd_v_i32(int32_t v);
void cd_v_f64(double v);
void cd_v_str(cd_str s);
void cd_v_f64s(const double *p, uint32_t n);
void cd_v_arr(uint32_t n);  /* n values follow */
void cd_v_obj(uint32_t n);  /* n pairs follow: cd_v_key, then a value */
void cd_v_key(cd_str k);
void cd_host_end(void);

/* state: the app's model as a fixed binary blob, written by the server build and
 * read by the browser build of the same C, so a server-rendered page resumes (below).
 * Layouts hold only fixed-width little-endian integers and byte arrays (no
 * pointers, no size_t). JS never interprets them. The weak defaults mean "no
 * state": save writes 0 bytes, load accepts only an empty blob. */
uint32_t cd_state_save(uint8_t *dst, uint32_t cap); /* bytes written, or needed if > cap */
int cd_state_load(const uint8_t *src, uint32_t len); /* 0 on success */

/* Resuming a server-rendered page. The server runs the same C natively into the C sink
 * (cdom_html.h), writes the HTML, the state blob as base64 in a non-script <script> element and
 * cdom.resume(url, mountId, stateId, types, names) with the event types its render listened to.
 * The page then runs no app code: the shim puts one listener per type on the mount, and after
 * the load event it fetches, compiles and instantiates the wasm and runs none of it (prefetch).
 * The first event (queued, with any that follow, until the module is up; one before the load
 * event starts the fetch itself) calls cd_start_state, which loads the blob and runs cd_main as
 * a virtual render: every op
 * that would build or write a node the server already wrote calls nothing (templates, built off
 * the document, are real), and each id the render names keeps its path, the child indices from
 * the mount. An app writes nothing special for this; what it must not do in cd_main is read an
 * input's value (cd_input_value traps there), and host calls on server nodes run right after
 * cd_main. The first op that takes an id resolves its node by that path in one call, and an op
 * that changes a node's children (an insert or append under it, a move, remove, clear, detach)
 * first resolves every unresolved id below its parent in one call, since the paths hold only
 * against the untouched DOM. A page that is never clicked never runs the module.
 * Counting children for a path, the shim reads two comments the sink writes: <!--e--> is an
 * empty text node (HTML cannot carry one; it becomes one when first reached) and counts as one
 * child, <!----> (between two texts, so the parser keeps them apart) does not count.
 * Events cross in one form, resumed or not: walking up from the target the shim writes, per level,
 * the node's index among its siblings (counted as above) and its element index, as two u32 at
 * cd_layout()[4], up to the nearest node with a JS id that takes the event, or up to the mount
 * while listening ids are unresolved (the u32 at cd_layout()[3] counts them). It posts that node
 * as the record's id (-1 for none), path = how many levels below it the target is, and depth =
 * the levels written. The core finds the deepest unresolved listener on the path, resolves it
 * when it is nearer the target, and calls cd_on_event with the record below: an app only ever
 * sees that one. */

/* Dev builds: -DCDOM_DEV=1, never a release. Code that cdtc generates then checks at run time what
 * the compiler cannot see (a state field written without its setter, a list changed without a mark,
 * a component's handler writing state it does not own) and reports each through cd_dev_warn, which
 * the shim prints with console.warn and the C sink (cdom_html.h) to stderr. With 0 (the default)
 * neither the function nor its import exists: a release module imports nothing for it. */
#ifndef CDOM_DEV
#define CDOM_DEV 0
#endif
#if CDOM_DEV
void cd_dev_warn(const char *msg); /* msg: NUL-terminated UTF-8 */
#endif

/* keyed list */
typedef struct { uint32_t key; cd_id node; uint32_t data; } cd_item;
/* owns_parent: the list is the parent's only content, so a full clear is one op
 * and a large first fill builds off-document. */
typedef struct { cd_item *items; uint32_t n, cap; cd_id parent; int owns_parent; } cd_list;
typedef struct {
  void *ctx;
  /* emit the subtree for new item i (cd_at is already set); return its root id */
  cd_id (*create)(void *ctx, uint32_t i, uint32_t *data);
  /* item i survived: emit only what changed */
  void (*patch)(void *ctx, uint32_t i, cd_item *it);
  /* item is gone (its root id is already freed): free the sub ids it named */
  void (*destroy)(void *ctx, cd_item *it);
} cd_list_fns;
void cd_list_update(cd_list *L, const uint32_t *keys, uint32_t n, const cd_list_fns *f);

#endif
