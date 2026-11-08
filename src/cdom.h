/* cdom: C owns the tree, state and diffing; a fixed JS shim makes one DOM call per
 * operation (each its own wasm import) and feeds events back. */
/* The program's libc: a libc belongs to the final program, never to a library, and in a cdom app the
 * program is the app's wasm build, so cdom provides what it needs: the heap (malloc, calloc, realloc and
 * free, weak, in cdom.c; below), and memcpy, memmove, memset, memcmp and strlen (src/mini.c, link it;
 * declared by src/libc/string.h, so build every file with -isystem cdom/src/libc). No other function or
 * header of libc is offered. */
/* The I/O contract. External adapters (a library's browser layer, built against cdom from outside it)
 * depend on these, so they are stable: cd_http_cb, cd_ws_open_cb, cd_ws_send, cd_ws_close and
 * cd_ws_buffered (cdom_net.h), cd_timer_cb and cd_timer_stop (below), cd_store_get_cb, cd_store_put_cb and
 * cd_store_del_cb (cdom_store.h), cd_epoch_ms and cd_now, and the program's libc (src/libc/string.h,
 * src/mini.c). Their signatures, their callbacks' shapes (cd_http_fn, cd_ws_fn, cd_timer_fn, cd_store_fn,
 * cd_store_done_fn), their status and event codes (CD_HTTP_*, CD_WS_*, a store answer's -1 and ok) and the
 * token range from CD_TOKEN_CDOM (0x80000000) up change only additively: a breaking change takes a new
 * name and a deprecation of the old one, never an edit in place. cdom depends on no adapter; the
 * dependency runs one way. tests/link/contract.c fails to compile if any of these declarations changes. */
#ifndef CDOM_H
#define CDOM_H
#include <stdint.h>

/* Content strings (text, class values) cross once and are cached in JS by id
 * when 1; when 0 every use re-sends the bytes. Tag and attribute names are
 * always cached: they are the enum. */
#ifndef CDOM_INTERN_TEXT
#define CDOM_INTERN_TEXT 1
#endif

typedef uint32_t cd_id;  /* node id; 0 is the mount element */
typedef uint32_t cd_str; /* interned string id */

#define CD_MOUNT 0u
/* Fixed ids for host objects, usable wherever an id is accepted (cd_sel, cd_listen, cd_set, cd_call_args,
 * cd_get_*). They are low so the shim's id-indexed tables stay dense; cd_id_new never returns an id below
 * CD_FIRST_ID, and freeing or removing one does nothing. */
#define CD_WINDOW 1u
#define CD_DOCUMENT 2u
#define CD_BODY 3u     /* document.body when the app started */
#define CD_EVENT 4u    /* the DOM event being delivered; valid inside cd_on_event or a cd_on handler only */
#define CD_RESULT 5u   /* what the last cd_call_args returned (undefined if it threw) */
#define CD_FIRST_ID 6u
#define CD_DETACHED 0xffffffffu

/* Event types. The numbers are stable: new ones are only appended, and there are at most 64.
 * Listened for on a node (cd_listen), a type is delivered from one listener on the mount to the nearest
 * listening ancestor-or-self of the target: in the bubble phase for the types that bubble, in the capture
 * phase for the ones that do not (CD_EV_FOCUS to CD_EV_PAUSE, and a <dialog>'s CD_EV_CANCEL and CD_EV_CLOSE),
 * so a node hears its own load or scroll, and a dialog its own closing.
 * Listened for on CD_WINDOW, CD_DOCUMENT or CD_BODY, it is delivered by a listener on that object, with
 * that id in the record: CD_EV_RESIZE on the window, CD_EV_VISIBILITY and CD_EV_FULLSCREENCHANGE on the
 * document, keys and the moves and release of a drag on the document, focus and blur on the window.
 * The mouse and touch types are for a drag as the browser has always delivered it: the press on the
 * node, the moves and the release on the document while it lasts. */
enum { CD_EV_CLICK, CD_EV_DBLCLICK, CD_EV_INPUT, CD_EV_CHANGE, CD_EV_KEYDOWN, CD_EV_KEYUP,
       CD_EV_FOCUSIN, CD_EV_FOCUSOUT, CD_EV_SUBMIT, CD_EV_POINTERDOWN, CD_EV_POINTERUP,
       CD_EV_POINTERMOVE, CD_EV_RESIZE, CD_EV_VISIBILITY, CD_EV_MOUSEDOWN, CD_EV_MOUSEMOVE,
       CD_EV_MOUSEUP, CD_EV_TOUCHSTART, CD_EV_TOUCHMOVE, CD_EV_TOUCHEND, CD_EV_TOUCHCANCEL,
       CD_EV_CONTEXTMENU, CD_EV_WHEEL, CD_EV_POINTEROVER, CD_EV_POINTEROUT, CD_EV_POINTERCANCEL,
       CD_EV_DRAGOVER, CD_EV_DROP,
       CD_EV_FOCUS, CD_EV_BLUR, CD_EV_SCROLL, CD_EV_LOAD, CD_EV_ERROR, CD_EV_LOADEDMETADATA,
       CD_EV_TIMEUPDATE, CD_EV_ENDED, CD_EV_PLAY, CD_EV_PAUSE,
       CD_EV_FULLSCREENCHANGE, CD_EV_CANCEL, CD_EV_CLOSE, CD_EV_TYPES };

/* The event record's mods bits. CD_MOD_EDITING: the event's target is an input, a textarea, a
 * select or contenteditable (a key typed into a field, which an app's shortcuts leave alone). */
enum { CD_MOD_SHIFT = 1, CD_MOD_CTRL = 2, CD_MOD_ALT = 4, CD_MOD_META = 8, CD_MOD_EDITING = 16 };

/* ---- memory ----
 * cd_alloc and friends go through malloc, realloc and free. cdom defines those (and calloc)
 * as weak symbols over its own size-class heap from __heap_base, so an app with no allocator
 * gets cdom's, and the app's malloc is the same heap. An app that brings an allocator defines
 * all four, strong, and cdom then allocates from it: one heap either way, never two
 * allocators that both start at __heap_base. cdom's free and realloc trap on a pointer their
 * heap did not hand out, so an app that replaces malloc alone fails loudly, not silently. */
void *cd_alloc(uint32_t n);
void *cd_realloc(void *p, uint32_t n);
void cd_free(void *p);

/* ---- strings ---- */
cd_str cd_intern(const char *s, uint32_t len);
cd_str cd_internz(const char *s);
const char *cd_bytes(cd_str s, uint32_t *len);
/* A one-off string: its bytes cross with the next op and are never interned or cached, so a
 * value that changes (a position, a count in a label) does not grow the string table. Use it
 * in the very next op that takes a string; using it after another op has crossed traps. */
cd_str cd_tmp(const char *s, uint32_t len);
cd_str cd_tmpz(const char *s);
/* A string literal interned once per call site: the hash lookup is paid the first time only.
 * ONLY A LITERAL: the cache is per call site, so a variable would answer its first value
 * forever. `"" lit` makes anything but a string literal a compile error. The check is in a
 * helper, not inline: inlined at every site it cost an app 5.5 KB. */
cd_str cd_lit_(cd_str *slot, const char *lit, uint32_t len);
#define CD_LIT(lit) ({ static cd_str cd_lit_slot_; cd_lit_(&cd_lit_slot_, "" lit, sizeof(lit) - 1); })
/* The string table's size: C's pool and the shim's array hold the same strings. */
uint32_t cd_str_count(void);
uint32_t cd_str_bytes(void);

/* Numbers as JS and CSS write them, with no printf. cd_fmt writes v / 10^decimals (decimals <= 9)
 * as JS's String(v / 10 ** decimals) does (trailing zeros dropped, no "-0"), then the unit, and
 * returns the bytes written (at most 24). cd_fmt_len rounds a double to 4 decimals first. */
enum { CD_UNIT_NONE, CD_UNIT_PX, CD_UNIT_PCT, CD_UNIT_DEG, CD_UNIT_EM, CD_UNIT_REM, CD_UNIT_VW,
       CD_UNIT_VH, CD_UNIT_MS, CD_UNIT_S, CD_UNIT_RAD, CD_UNITS };
uint32_t cd_fmt(char *dst, int32_t v, uint32_t decimals, uint32_t unit);
uint32_t cd_fmt_len(char *dst, double v, uint32_t unit);

/* ---- node ids ---- */
cd_id cd_id_new(void);
void cd_id_free(cd_id id);           /* JS drops its reference; no DOM change. Freeing a free id traps;
                                      * freeing a reserved id (below CD_FIRST_ID) does nothing. */
void cd_tag_set(cd_id id, uint32_t tag); /* app data per node, for events */
uint32_t cd_tag(cd_id id);
uint32_t cd_ids_live(void);          /* ids handed out and not freed (the reserved ids not counted) */
uint32_t cd_ids_high(void);          /* the highest id ever handed out: the size of the shim's node table */

/* ---- building: nodes go to (parent, before) when their root closes ---- */
void cd_at(cd_id parent, cd_id before); /* before 0 = append; parent CD_DETACHED = keep off-DOM */
/* An <svg>, and everything opened inside it, is made in the SVG namespace (C keeps the depth). */
void cd_open(cd_str tag);
void cd_close(void);
void cd_text(cd_str s);
void cd_text_num(uint32_t n);
void cd_attr(cd_str name, cd_str value);
void cd_class(cd_str value);
/* cd_name names the LAST NODE MADE: after cd_open that is the element, after cd_text the text
 * node, but after a cd_close it is the deepest node built inside, not the element closed.
 * cd_name_open names the innermost open element, whatever has been built inside it so far. */
void cd_name(cd_id id);
void cd_name_open(cd_id id);
/* Open an element and name it with a new id, in one call: cd_open(tag), cd_id_new(), cd_name_open(id).
 * The id names the element whatever is built inside it before its cd_close. It is an id like any other:
 * recorded by a region that is building, and freed with cd_id_free or cd_remove. */
cd_id cd_open_id(cd_str tag);
/* Name the last node and deliver ev for it. On CD_WINDOW, CD_DOCUMENT or CD_BODY it listens on that
 * object instead (once however often it is asked) and does not touch the last node; such a listener is not
 * passive, so cd_prevent_default works on a touchmove (it stops the page scrolling). */
void cd_listen(cd_id id, uint32_t ev);
/* Stop delivering ev: for a host object its listener goes (a drag that ended); for a node, the node no
 * longer hears it. */
void cd_unlisten(cd_id id, uint32_t ev);
void cd_value(cd_str s);             /* last.nodeValue */
void cd_value_num(uint32_t n);
void cd_tpl_save(uint32_t t);        /* the last closed root becomes template t */
void cd_clone(uint32_t t);           /* open a deep clone of template t (a child of the open element, if any) */
/* Template slots: while a template is being built, cd_slot(k) marks the last node (a text node)
 * as slot k (k < CD_SLOTS). cd_tpl_text sets that text in template t itself, so the following
 * cd_clone(t) copies it in and the clone needs no walk. A slot keeps its last value: set every
 * slot before each clone. t < CD_TPLS. */
#define CD_TPLS 64
#define CD_SLOTS 16
void cd_slot(uint32_t k);
void cd_tpl_text(uint32_t t, uint32_t k, cd_str s);
void cd_tpl_text_num(uint32_t t, uint32_t k, uint32_t n);
void cd_down(void);                  /* last = last.firstChild */
void cd_next(void);                  /* last = last.nextSibling */
void cd_up(void);                    /* last = last.parentNode */
void cd_sel(cd_id id);               /* last = the node named id; then walk with down/next/up */

/* ---- updating ---- */
void cd_set_text(cd_id id, cd_str s); /* id is a text node */
void cd_set_text_num(cd_id id, uint32_t n);
void cd_set_attr(cd_id id, cd_str name, cd_str value);
void cd_set_class(cd_id id, cd_str value);
/* One style property: element.style.setProperty(prop, value). value may be a cd_tmp; "" removes it. */
void cd_set_style(cd_id id, cd_str prop, cd_str value);
void cd_set_style_num(cd_id id, cd_str prop, double v, uint32_t unit);
/* Pixels to an image file: px is w * h * n bytes in C memory, n = 4 (RGBA) or 3 (RGB, opaque: a
 * third less room for a picture with no alpha), read during the call (reuse it after). The shim
 * encodes them as mime ("image/png", or "image/jpeg" at quality 0.92) off the
 * call; the file's bytes arrive in cd_on_encoded(token, p, len), valid until it returns. */
void cd_encode_image(const uint8_t *px, uint32_t w, uint32_t h, uint32_t n, cd_str mime, uint32_t token);
/* The same, the pixels placed at (x, y) in an image of W x H whose rest is transparent (black in a
 * JPEG): for a picture of which only a part is ever shown, but whose size the page lays out by. */
void cd_encode_image_in(const uint8_t *px, uint32_t w, uint32_t h, uint32_t n, uint32_t W, uint32_t H, uint32_t x,
                        uint32_t y, cd_str mime, uint32_t token);
void cd_on_encoded(uint32_t token, const uint8_t *p, uint32_t len); /* the app's hook (empty weak default) */
/* An image file's bytes (len of them at p, read during the call) as a CSS image: style property
 * prop of node id becomes url(blob:...) once the image has decoded, so no frame paints the
 * property before its picture (a custom property such as "--tex" works), and then
 * cd_on_image(token, 1) is called, in the same task: what goes with the picture (a colour under
 * it) is set there and paints with it. Bytes that do not decode set nothing: cd_on_image(token, 0). */
void cd_set_image_bytes(cd_id id, cd_str prop, const uint8_t *p, uint32_t len, cd_str mime, uint32_t token);
void cd_on_image(uint32_t token, uint32_t ok); /* the app's hook (empty weak default) */
void cd_remove(cd_id id);            /* detach and free the id; ignored below CD_FIRST_ID */
void cd_move(cd_id id, cd_id before);
void cd_clear(cd_id id);             /* drop all children; caller frees their ids (a cd_region does it for you) */
void cd_detach(cd_id id);            /* take a node out of the document, remembering its place */
void cd_attach(cd_id id);            /* put it back where it was */
/* Sends pending strings and any held walk. Every entry from JS (start, events, timers, frames,
 * the network) flushes on its way out, so an app calls this only from an export of its own. */
void cd_flush(void);
uint32_t cd_pending(void);           /* nonzero while strings or a walk wait to cross (for tests) */

/* ---- events: delivered to a cd_on handler, else to cd_on_event; the shim fills the record ----
 * id is the nearest listening ancestor-or-self of the target, or the host object listened on.
 * path holds the element-child indices from that node down toward the target, one byte per
 * level (byte 0 = which child of the listener), for depth levels (max 4).
 * x, y: clientX and clientY (a touch event's: its first changed touch, the finger that pressed,
 * moved or lifted). key: keyCode. ch: the key's character when event.key is one UTF-16 unit
 * ("a", "A", "3", "-", " "), else 0 ("ArrowUp", "Shift"). mods: CD_MOD_* bits. ms: when the
 * event happened (event.timeStamp, cd_now()'s clock, whole ms): a press and its release are timed
 * by their own stamps, not by when their handlers got to run. flags is cdom's (cd_prevent_default,
 * cd_stop_propagation), cleared before every event.
 * CD_EV_RESIZE and CD_EV_VISIBILITY carry the viewport: x and y its size, key 1 when the document
 * is hidden. pointer (pointerId) and buttons are filled for the pointer types (down, up, move, over,
 * out, cancel) and wheel, dx and dy (deltaX, deltaY) for wheel; other types read 0 there.
 * Fields are only ever appended. Any other event property is a path away: cd_get_str(CD_EVENT, "key"),
 * cd_get_id(CD_EVENT, "target"), "dataTransfer.files.0.name". */
typedef struct {
  int32_t type, id, x, y, button, key, mods, path, depth, ch, flags, ms;
  int32_t pointer, buttons;
  float dx, dy;
} cd_event_rec;
#define CD_PREVENT 1
#define CD_STOP 2
/* The app's hook for every event with no cd_on handler (cdom has an empty weak default, so an app that
 * uses cd_on alone need not define it). */
void cd_on_event(const cd_event_rec *e);
/* A handler for one event type on one node or host object: fn(e, ctx) is called in place of cd_on_event
 * for the events whose record has that id and type, and cd_prevent_default and cd_stop_propagation work
 * inside it as they do in cd_on_event. Events with no handler still go to cd_on_event, unchanged.
 * id is a live node id or CD_WINDOW, CD_DOCUMENT or CD_BODY; cd_on also makes the shim deliver ev for id,
 * as cd_listen does: for a node it selects id's node first (cd_sel), so the last node is id's node after
 * the call, and it may be called any time after the node was named. Registering again for the same
 * (id, ev) replaces fn and ctx. fn 0 removes the handler and stops delivery of ev for id (cd_unlisten),
 * also when the app had listened for the pair itself, since cdom cannot tell; removing a pair with no
 * handler does nothing. A node's handlers go when its id is freed (cd_id_free, cd_remove, a region or a
 * list), so an id handed out again starts with none. Traps on ev >= CD_EV_TYPES, on CD_MOUNT, CD_EVENT,
 * CD_RESULT and a free id. Freeing an id with no handler costs one byte read. */
typedef void (*cd_event_fn)(const cd_event_rec *e, void *ctx);
void cd_on(cd_id id, uint32_t ev, cd_event_fn fn, void *ctx);
void cd_main(void);                  /* the app's entry, called once at start */
uint32_t cd_input_value(cd_id id, char *dst, uint32_t cap); /* cd_get_str of "value": an input's value now */
/* Whole-page delivery, as cd_listen on the host object that has the event: CD_EV_RESIZE on CD_WINDOW,
 * every other type on CD_DOCUMENT (keys, and the moves and release of a drag). */
void cd_listen_window(uint32_t ev);
void cd_unlisten_window(uint32_t ev);
/* During cd_on_event: the browser's default action for this event is not taken (event.preventDefault
 * once the hook returns): no native drag after a mousedown, no scroll after a touchmove, no mouse
 * events and click after a touchend, no character typed after a keydown, no menu after contextmenu.
 * cd_stop_propagation: event.stopPropagation once the hook returns, so a listener further out (on
 * CD_BODY, say) does not hear it.
 * An event the browser dispatches while C is running (focus() called through cd_call_args fires focus
 * events, click() fires a click) is queued and delivered in a microtask, once C has returned, so it never
 * lands in the middle of a handler; preventing such an event has no effect. */
void cd_prevent_default(void);
void cd_stop_propagation(void);
/* The listening node under (x, y) in the viewport: the first that listens, whatever type, among
 * every element there that takes pointer events, topmost first with each one's ancestors after it
 * (elementsFromPoint: a pointer-events: none overlay is not there, and a layer that takes pointer
 * events but listens nowhere is seen through, as a drop's target is read under a seat or a
 * flight); 0 for none. The browser lays the page out first if anything changed. */
cd_id cd_hit(int32_t x, int32_t y);
typedef struct { int32_t w, h, hidden, dpr100; } cd_viewport_rec;
/* innerWidth, innerHeight, document.hidden and devicePixelRatio (times 100, rounded), read now: one call
 * for the four an app reads together (each is also a path away: cd_get_num(CD_WINDOW, "innerWidth")). */
void cd_viewport(cd_viewport_rec *v);
/* A node's box on screen now (getBoundingClientRect: with its transforms, in CSS px from the
 * viewport's top left): out[0] left, out[1] top, out[2] width, out[3] height; returns 1, or 0 with out
 * untouched when id names nothing with a box (the window, an unbound id). The browser lays the page
 * out first if anything changed, so measure once per change, never every frame. */
int cd_rect(cd_id id, double out[4]);

/* ---- host access: properties and methods of nodes and of the reserved ids, along dotted paths ----
 * A path is an interned string such as "style.width", "files.0.name" or "childNodes.2.nodeValue"; it is
 * walked from the object one property per segment, and a segment of digits indexes an array or a
 * collection the same way. A segment that starts with "--" reads or writes a custom property through
 * getPropertyValue or setProperty on the object before it, so "style.--accent" names a CSS variable.
 * A path may be interned (its segments are split once and kept by its id) or a cd_tmp (split each time,
 * since a one-off string's id is reused by the next one); interning is the cheap choice for a path used
 * more than once. Each call is made the moment C makes it, like every other operation. */
enum { CD_V_NUM, CD_V_STR, CD_V_BOOL, CD_V_ID, CD_V_NULL, CD_V_OBJ };
/* A tagged value. CD_V_ID is the node (or reserved object) with that id. CD_V_OBJ starts a plain object of
 * u key-value pairs, which follow it in the same array as a CD_V_STR key then a value (objects may nest).
 * The shim reads the array in C memory during the call, so it may live on the stack. */
typedef struct { uint32_t tag; union { double num; uint32_t u; }; } cd_val;
static inline cd_val cd_v_num(double n) { cd_val v = {CD_V_NUM, {.num = n}}; return v; }
static inline cd_val cd_v_str(cd_str s) { cd_val v = {CD_V_STR, {.u = s}}; return v; }
static inline cd_val cd_v_bool(int b) { cd_val v = {CD_V_BOOL, {.u = b != 0}}; return v; }
static inline cd_val cd_v_id(cd_id id) { cd_val v = {CD_V_ID, {.u = id}}; return v; }
static inline cd_val cd_v_null(void) { cd_val v = {CD_V_NULL, {.u = 0}}; return v; }
static inline cd_val cd_v_obj(uint32_t pairs) { cd_val v = {CD_V_OBJ, {.u = pairs}}; return v; }

/* Set the property at path to v: a property, not an attribute, so it works on input.value after the user
 * typed, checked, volume, currentTime, muted, scrollTop and style.width. v is one value, not an object
 * (that traps: pass an object to a method instead). A failing set (a missing step, a read-only property)
 * is reported with reportError and C goes on. */
void cd_set(cd_id id, cd_str path, cd_val v);
static inline void cd_set_num(cd_id id, cd_str path, double n) { cd_set(id, path, cd_v_num(n)); }
static inline void cd_set_str(cd_id id, cd_str path, cd_str s) { cd_set(id, path, cd_v_str(s)); }
static inline void cd_set_bool(cd_id id, cd_str path, int b) { cd_set(id, path, cd_v_bool(b)); }
/* Call the method at path (its last segment, with the object before it as this) with the n values in a,
 * e.g. "setPointerCapture", "classList.toggle" or "history.pushState". Inside an event it runs before the
 * handler returns, so it keeps the user activation (requestFullscreen, play). A returned promise's
 * rejection is swallowed and a thrown error is reported and skipped. The result is kept as CD_RESULT, so
 * the app can read it with cd_get_*. cd_call is the same with no arguments ("click", "focus"). */
void cd_call_args(cd_id id, cd_str method, uint32_t n, const cd_val *a);
void cd_call(cd_id id, cd_str method);

/* Reads walk the path from id synchronously and see every operation made before them. A missing or null
 * step never throws: it reads as NaN, an empty string or CD_DETACHED. Path 0 reads the object itself, e.g.
 * cd_get_num(CD_RESULT, 0) for a call's result. */
double cd_get_num(cd_id id, cd_str path);  /* a number, a boolean as 0 or 1; NaN for anything else */
/* The string form (String(v)) as UTF-8 into dst, at most cap bytes and never a split character; returns
 * the bytes written. An object reads as its "[object ...]" name, so a non-zero length also tests presence. */
uint32_t cd_get_str(cd_id id, cd_str path, char *dst, uint32_t cap);
/* The id under which the node the path holds is named, or CD_DETACHED. It searches the id table, so it is
 * linear in the number of ids: fine for activeElement in a handler, not for a per-row loop. */
cd_id cd_get_id(cd_id id, cd_str path);

/* ---- canvas and frames ---- */
/* Copy w*h RGBA pixels (bytes R,G,B,A; a uint32 0xAABBGGRR) from C memory into the canvas at 0,0 with
 * putImageData, read during the call. The canvas's width and height attributes are the app's to set. */
void cd_blit(cd_id canvas, const void *rgba, uint32_t w, uint32_t h);
/* 1 starts a loop that calls cd_on_frame before every frame, 0 stops it. It is cd_raf_request asked again
 * each frame under a token of cdom's own (CD_FRAMES_TOKEN, never passed to cd_on_raf), so starting twice
 * is one loop. */
void cd_frames(int on);
void cd_on_frame(double ms);         /* the app's hook (empty weak default); ms is the frame's timestamp */
#define CD_FRAMES_TOKEN 0xffffffffu

/* ---- time: answered through the app's hooks (cdom has empty weak defaults) ----
 * Tokens: an app's tokens for cd_timer_start, cd_http and the store (cdom_store.h) stay below
 * CD_TOKEN_CDOM. The tokens from CD_TOKEN_CDOM up are cdom's own: those the _cb forms hand out
 * (cd_timer_cb, cd_http_cb, cd_ws_open_cb, cd_store_get_cb, cd_store_put_cb, cd_store_del_cb), and
 * CD_FRAMES_TOKEN.
 * The hooks (cd_on_timer, cd_on_http, cd_on_ws_*, cd_on_store*, cd_on_event) are app-wide, so they are
 * the app's own: a library linked inside an app (an SDK, a client) never defines one, and uses the _cb
 * forms (and cd_on for events), whose answers reach only the callback that asked. */
#define CD_TOKEN_CDOM 0x80000000u
/* cd_on_timer(token) after ms. A token started again before it fired fires once per start. Traps on a
 * token in cdom's range (token >= CD_TOKEN_CDOM). */
void cd_timer_start(uint32_t ms, uint32_t token);
/* Stops every pending timer of token (clearTimeout): neither cd_on_timer nor a callback hears it. A token
 * that already fired, was already stopped or was never started is no-op, so a timer may be stopped
 * whether or not it fired. Takes an app token or one from cd_timer_cb. */
void cd_timer_stop(uint32_t token);
/* fn(ctx) once, after ms; returns the timer's token (in cdom's range), for cd_timer_stop. To repeat, fn
 * starts another (it may reuse the slot, under a new token). A token is not reused: it carries its slot's
 * generation, so stopping a token that fired or was stopped never stops the timer that took its slot
 * since (until the slot has been reused 32,768 times). At most 65,535 cd_timer_cb timers, cd_http_cb
 * requests, cd_ws_open_cb sockets and store _cb answers are pending at once (traps past that). */
typedef void (*cd_timer_fn)(void *ctx);
uint32_t cd_timer_cb(uint32_t ms, cd_timer_fn fn, void *ctx);
void cd_raf_request(uint32_t token);              /* cd_on_raf(token, t) before the next frame */
void cd_on_timer(uint32_t token);
void cd_on_raf(uint32_t token, double t);
double cd_now(void);                              /* performance.now(), for work cut into slices */

/* ---- regions: children that come and go ----
 *   if (cd_region_show(&r, want)) { cd_at(...); ...build...; cd_region_end(&r); }
 * cd_region_show returns 1 when the region must be built now (it is hidden and want is set);
 * with want 0 and the region shown, it takes the region away. While a region builds, every id
 * cd_id_new makes is recorded in it, and taking the region away frees them all.
 * Two kinds, by r->parent, set once before first use:
 * - r->parent != 0: the region is ALL the children of that (named, non-mount) node;
 *   taking it away is one clear.
 * - r->parent == 0: the region is one element, which the builder names cd_name_open(r->root)
 *   (cd_region_show makes r->root); taking it away is one remove.
 * Lists updated while a region builds keep their own ids (cd_list_update does not record);
 * drop a list that lived inside a region with cd_list_forget. Regions do not nest (traps). */
typedef struct cd_region { cd_id parent, root; cd_id *ids; uint32_t n, cap; int shown; } cd_region;
int cd_region_show(cd_region *r, int want);
void cd_region_end(cd_region *r);
void cd_region_hide(cd_region *r);   /* = cd_region_show(r, 0) */

/* ---- keyed list ---- */
typedef struct { uint32_t key; cd_id node; uint32_t data; } cd_item;
/* owns_parent: the list is the parent's only content, so a full clear is one op
 * and a large first fill builds off-document.
 * end: when fixed siblings follow the list in its parent, the first of them; items at the
 * list's tail go before it (0: the list runs to the parent's end). Only with owns_parent 0. */
typedef struct { cd_item *items; uint32_t n, cap; cd_id parent; int owns_parent; cd_id end; } cd_list;
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
/* The list's DOM already went (an ancestor was cleared or removed): free every item's ids,
 * emit nothing for their nodes, and leave the list empty. */
void cd_list_forget(cd_list *L, const cd_list_fns *f);

#endif
