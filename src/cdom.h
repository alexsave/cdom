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

typedef uint32_t cd_id;  /* node id; 0 is the mount element */
typedef uint32_t cd_str; /* interned string id */

#define CD_MOUNT 0u
/* Fixed ids for host objects, usable wherever an id is accepted (sel, call, set, get, listen).
 * They are low so the shim's id-indexed tables stay dense; cd_id_new never returns an id below CD_FIRST_ID. */
#define CD_WINDOW 1u
#define CD_DOCUMENT 2u
#define CD_BODY 3u     /* document.body when the app started */
#define CD_EVENT 4u    /* the DOM event being delivered; valid inside cd_on_event only */
#define CD_RESULT 5u   /* what the last cd_call_args returned (undefined if it threw) */
#define CD_FIRST_ID 6u
#define CD_DETACHED 0xffffffffu

/* Event types. The numbers are stable: new ones are only appended, and there are at most 32.
 * 0-18 bubble and are delivered from a listener on the mount to the nearest listening ancestor-or-self of
 * the target. 19-28 do not bubble; a capture-phase listener on the mount delivers them the same way.
 * Any type listened on CD_WINDOW, CD_DOCUMENT or CD_BODY is delivered by a listener on that object, with
 * that id as the record's id, in addition to the mount delivery: resize (window), fullscreenchange and
 * visibilitychange (document), document-level keydown and keyup, window focus and blur. */
enum { CD_EV_CLICK, CD_EV_DBLCLICK, CD_EV_INPUT, CD_EV_CHANGE, CD_EV_KEYDOWN, CD_EV_KEYUP,
       CD_EV_FOCUSIN, CD_EV_FOCUSOUT, CD_EV_SUBMIT, CD_EV_POINTERDOWN, CD_EV_POINTERUP,
       CD_EV_POINTERMOVE,
       CD_EV_CONTEXTMENU, CD_EV_WHEEL, CD_EV_POINTEROVER, CD_EV_POINTEROUT, CD_EV_POINTERCANCEL,
       CD_EV_DRAGOVER, CD_EV_DROP,
       CD_EV_FOCUS, CD_EV_BLUR, CD_EV_SCROLL, CD_EV_LOAD, CD_EV_ERROR, CD_EV_LOADEDMETADATA,
       CD_EV_TIMEUPDATE, CD_EV_ENDED, CD_EV_PLAY, CD_EV_PAUSE,
       CD_EV_RESIZE, CD_EV_FULLSCREENCHANGE, CD_EV_VISIBILITYCHANGE };

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
void cd_listen(cd_id id, uint32_t ev); /* name the last node and deliver ev for it; on CD_WINDOW, CD_DOCUMENT
                                       * or CD_BODY it listens on that object and does not touch the last node */
void cd_value(cd_str s);             /* last.nodeValue */
void cd_value_num(uint32_t n);
void cd_tpl_save(uint32_t t);        /* the last closed root becomes template t */
void cd_clone(uint32_t t);           /* open a deep clone of template t */
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

/* canvas and frames */
/* Copy w*h RGBA pixels (bytes R,G,B,A; a uint32 0xAABBGGRR) from linear memory into the canvas at 0,0 with
 * putImageData when the batch is applied. The pixels are read then, so keep them alive until the flush.
 * The canvas's width and height attributes are the app's to set. */
void cd_blit(cd_id canvas, const void *rgba, uint32_t w, uint32_t h);
/* 1 starts a requestAnimationFrame loop that calls cd_on_frame every frame, 0 stops it. */
void cd_frames(int on);
void cd_on_frame(double ms);         /* the app may implement this; ms is the rAF timestamp */
/* Call a no-argument method on a node, e.g. "click" or "requestFullscreen", when the batch is applied.
 * Inside an event the batch is applied before the handler returns, so the call keeps the user activation. */
void cd_call(cd_id id, cd_str method);

/* host access: properties and methods of nodes and of the reserved ids, along dotted paths.
 * A path is an interned string such as "style.width", "files.0.name" or "childNodes.2.nodeValue"; it is
 * walked from the object one property per segment, and a segment of digits indexes an array or a
 * collection the same way. A segment that starts with "--" reads or writes a custom property through
 * getPropertyValue or setProperty on the object before it, so "style.--accent" names a CSS variable. */
enum { CD_V_NUM, CD_V_STR, CD_V_BOOL, CD_V_ID, CD_V_NULL, CD_V_OBJ };
/* A tagged value. CD_V_ID is the node (or reserved object) with that id. CD_V_OBJ starts a plain object of
 * u key-value pairs, which follow it in the same array as a CD_V_STR key then a value (objects may nest). */
typedef struct { uint32_t tag; union { double num; uint32_t u; }; } cd_val;
static inline cd_val cd_v_num(double n) { cd_val v = {CD_V_NUM, {.num = n}}; return v; }
static inline cd_val cd_v_str(cd_str s) { cd_val v = {CD_V_STR, {.u = s}}; return v; }
static inline cd_val cd_v_bool(int b) { cd_val v = {CD_V_BOOL, {.u = b != 0}}; return v; }
static inline cd_val cd_v_id(cd_id id) { cd_val v = {CD_V_ID, {.u = id}}; return v; }
static inline cd_val cd_v_null(void) { cd_val v = {CD_V_NULL, {.u = 0}}; return v; }
static inline cd_val cd_v_obj(uint32_t pairs) { cd_val v = {CD_V_OBJ, {.u = pairs}}; return v; }

/* Set the property at path to v when the batch is applied: a property, not an attribute, so it works on
 * input.value after the user typed, checked, volume, currentTime, muted, scrollTop and style.width.
 * A failing set (a missing step, a read-only property) is reported with reportError and the batch goes on. */
void cd_set(cd_id id, cd_str path, cd_val v);
static inline void cd_set_num(cd_id id, cd_str path, double n) { cd_set(id, path, cd_v_num(n)); }
static inline void cd_set_str(cd_id id, cd_str path, cd_str s) { cd_set(id, path, cd_v_str(s)); }
static inline void cd_set_bool(cd_id id, cd_str path, int b) { cd_set(id, path, cd_v_bool(b)); }
/* Call the method at path (its last segment, with the object before it as this) with the n values in a,
 * e.g. "setPointerCapture", "classList.toggle" or "history.pushState". Like cd_call it runs inside the event
 * that emitted it; a returned promise's rejection is swallowed and a thrown error is reported and skipped.
 * The result is kept as CD_RESULT, so the app can read it with cd_get_* after a flush. */
void cd_call_args(cd_id id, cd_str method, uint32_t n, const cd_val *a);

/* Reads apply the pending batch first, so they see every op emitted before them, then walk the path from id
 * synchronously. A missing or null step never throws: it reads as NaN, an empty string or CD_DETACHED. */
double cd_get_num(cd_id id, cd_str path);  /* a number, a boolean as 0 or 1; NaN for anything else */
/* The string form (String(v)) as UTF-8 into dst, at most cap bytes and never a split character; returns
 * the bytes written. An object reads as its "[object ...]" name, so a non-zero length also tests presence. */
uint32_t cd_get_str(cd_id id, cd_str path, char *dst, uint32_t cap);
cd_id cd_get_id(cd_id id, cd_str path);    /* the id of the named node the path holds, or CD_DETACHED */
/* getBoundingClientRect of the node into out as left, top, width, height; returns 0 (out untouched) if
 * id names nothing with a rect. */
int cd_get_rect(cd_id id, double out[4]);

/* events: the app implements this; the shim fills the record */
/* id is the nearest listening ancestor-or-self of the target (or the reserved id listened on). path holds
 * the element-child indices from that node down toward the target, one byte per level (byte 0 = which
 * child of the listener), for depth levels (max 4). Fields are only ever appended. pointer is pointerId,
 * buttons the pressed-buttons mask, dx and dy the wheel deltas. Any other event property is a path away:
 * cd_get_str(CD_EVENT, "key", ...), cd_get_id(CD_EVENT, "target"), "dataTransfer.files.0.name". */
typedef struct {
  int32_t type, id, x, y, button, key, mods, path, depth;
  int32_t pointer, buttons;
  float dx, dy;
  int32_t flags; /* CD_PREVENT | CD_STOP, cleared before every event; set them with the calls below */
} cd_event_rec;
#define CD_PREVENT 1
#define CD_STOP 2
void cd_on_event(const cd_event_rec *e);
/* Inside cd_on_event: call preventDefault or stopPropagation on the event once the handler returns.
 * An event dispatched while the shim is applying a batch (focus() fires focus events, click() fires click)
 * is queued and delivered in a microtask, after the batch; preventing such an event has no effect. */
void cd_prevent_default(void);
void cd_stop_propagation(void);
void cd_main(void);                  /* the app's entry, called once at start */
uint32_t cd_input_value(cd_id id, char *dst, uint32_t cap); /* cd_get_str of "value" */

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
