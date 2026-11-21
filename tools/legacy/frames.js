/* cdom's frames tier (docs/frames-design.md): the shim for browsers with script and no DOM to build in, Netscape 2 to 4
 * and IE 3 and 4. cdom's node tree is kept here, in the top window, made by the same ops shim/cdom.js takes, under the
 * same import names; once per outermost call into C it is written whole, as HTML, into the one frame of the frameset
 * the boot made, whose handlers call back into cdomF here.
 * JavaScript 1.1, as Netscape 3 has it (tools/legacy/js11.mjs refuses the rest): every function declared at the top
 * level, no literal but strings and numbers, no try, throw, switch, regex, ===, in or delete, and no method of 1.2 and
 * later but charCodeAt and fromCharCode behind a check. All of it is global, so every name starts cf_, and the
 * markup's handlers reach the few that are cdomF's. tools/legacy/build.mjs shim-frames writes it as cdom.frames.js. */

/* The event types, as indexes into shim/cdom.js's EVENTS (cdom.h's enum). */
var cf_CLICK = 0;
var cf_INPUT = 2;
var cf_CHANGE = 3;
var cf_KEYDOWN = 4;
var cf_KEYUP = 5;
var cf_FOCUSIN = 6;
var cf_FOCUSOUT = 7;
var cf_OVER = 23;
var cf_OUT = 24;
var cf_FOCUS = 28;
var cf_BLUR = 29;
var cf_DETACHED = 4294967295; /* "no insertion parent" for at() */
var cf_UNITS = new Array("", "px", "%", "deg", "em", "rem", "vw", "vh", "ms", "s", "rad");
/* What the frame shows, and is: one frameset of one frame, written into the page while it parses. A FRAMESET after a
 * BODY tag is ignored (HTML5's frameset-ok, and Netscape's rule), so a page that opens its BODY with a tag gets it
 * over the whole page once that has loaded (cf_poll), by open, write and close. No src: the frame's first document is
 * the empty one the browser makes, which the shim writes over. */
var cf_FRAMESET = "<frameset rows=\"100%\" border=\"0\" frameborder=\"0\" framespacing=\"0\"><frame name=\"cdomView\" marginwidth=\"8\" marginheight=\"8\"></frameset>";
/* How long a change, a focus or a blur waits to be drawn (ms): leaving a field for a button fires all three between
 * the press and the release, and a rewrite there would replace the button under the pointer, and lose the click, so
 * they are drawn once, after the click. */
var cf_LATER = 250;
/* A put-off draw also waits while a mouse button is down in the frame (cf_onDown, where the browser has
 * document.onmousedown): a press held past cf_LATER would see its button replaced and its click lost. At most this many
 * waits of cf_LATER, for a release the frame never sees (outside the window). */
var cf_PRESSCAP = 20;

/* The handlers' entry points and what a page or a test reads. */
var cdomF = new Object();
cdomF.errors = new Array();
cdomF.draws = 0;

/* C's memory: the app's words (memory.words of the .ie9.js build), as tools/legacy/memory-words.js reads them. */
var cf_raw = null;   /* the module's exports */
var cf_mem = null;   /* its memory */
var cf_W = null;     /* memory.words */
function cf_load8(a) {
  return cf_W[a >> 2] >>> ((a & 3) << 3) & 255;
}
function cf_store8(a, v) {
  var i = a >> 2, s = (a & 3) << 3;
  cf_W[i] = cf_W[i] & ~(255 << s) | (v & 255) << s;
}
function cf_load32(i) {
  return cf_W[i] >>> 0;
}
function cf_store32(i, v) {
  cf_W[i] = v | 0;
}

/* Characters. String.fromCharCode and charCodeAt are JavaScript 1.2: without them a byte's character comes from
 * unescape, and a character above Latin-1 is shown as "?" (Netscape 2 and 3 are Latin-1 throughout). */
var cf_hasFrom = 0;
var cf_hasAt = 0;
if (String.fromCharCode) cf_hasFrom = 1;
if ("a".charCodeAt) cf_hasAt = 1;
var cf_HEX = "0123456789ABCDEF";
var cf_PRINT = " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~";
var cf_T = new Array(256);
var cf_k;
for (cf_k = 0; cf_k < 256; cf_k++) {
  if (cf_hasFrom) cf_T[cf_k] = String.fromCharCode(cf_k);
  else cf_T[cf_k] = unescape("%" + cf_HEX.charAt(cf_k >> 4) + cf_HEX.charAt(cf_k & 15));
}
function cf_chr(u) {
  if (u < 256) return cf_T[u];
  if (!cf_hasFrom) return "?";
  if (u < 65536) return String.fromCharCode(u);
  u -= 65536;
  return String.fromCharCode(55296 + (u >> 10)) + String.fromCharCode(56320 + (u & 1023));
}
/* The UTF-16 unit at i of s. */
function cf_code(s, i) {
  if (cf_hasAt) return s.charCodeAt(i);
  var c = s.charAt(i), e = escape(c);
  if (e.length == 1) return cf_PRINT.indexOf(c) + 32;
  if (e.length == 3) return parseInt(e.substring(1, 3), 16);
  return 63;
}
/* The n bytes of UTF-8 at p as a string. */
function cf_utf8(p, n) {
  var out = "", end = p + n, c, u;
  while (p < end) {
    c = cf_load8(p);
    p++;
    if (c < 128) {
      out += cf_T[c];
      continue;
    }
    if (c >= 240) {
      u = (c & 7) << 18 | (cf_load8(p) & 63) << 12 | (cf_load8(p + 1) & 63) << 6 | cf_load8(p + 2) & 63;
      p += 3;
    } else if (c >= 224) {
      u = (c & 15) << 12 | (cf_load8(p) & 63) << 6 | cf_load8(p + 1) & 63;
      p += 2;
    } else {
      u = (c & 31) << 6 | cf_load8(p) & 63;
      p++;
    }
    out += cf_chr(u);
  }
  return out;
}
/* As many whole characters of text as fit in cap bytes at p, as UTF-8 (encodeInto's rule): the bytes written in
 * cf_wrote, and what the whole text takes in cf_whole. A lone surrogate is U+FFFD, as TextEncoder makes it. */
var cf_wrote = 0;
var cf_whole = 0;
function cf_encode(text, p, cap) {
  var i, n = text.length, u, d, k, cut = 0;
  cf_wrote = 0;
  cf_whole = 0;
  for (i = 0; i < n; i++) {
    u = cf_code(text, i);
    if (u >= 55296 && u < 57344) {
      d = -1;
      if (u < 56320 && i + 1 < n) d = cf_code(text, i + 1);
      if (d >= 56320 && d < 57344) {
        u = 65536 + ((u - 55296) << 10) + (d - 56320);
        i++;
      } else u = 65533;
    }
    k = 4;
    if (u < 128) k = 1;
    else if (u < 2048) k = 2;
    else if (u < 65536) k = 3;
    cf_whole += k;
    if (cut || cf_wrote + k > cap) {
      cut = 1;
      continue;
    }
    if (k == 1) cf_store8(p + cf_wrote, u);
    else if (k == 2) {
      cf_store8(p + cf_wrote, 192 | u >> 6);
      cf_store8(p + cf_wrote + 1, 128 | u & 63);
    } else if (k == 3) {
      cf_store8(p + cf_wrote, 224 | u >> 12);
      cf_store8(p + cf_wrote + 1, 128 | u >> 6 & 63);
      cf_store8(p + cf_wrote + 2, 128 | u & 63);
    } else {
      cf_store8(p + cf_wrote, 240 | u >> 18);
      cf_store8(p + cf_wrote + 1, 128 | u >> 12 & 63);
      cf_store8(p + cf_wrote + 2, 128 | u >> 6 & 63);
      cf_store8(p + cf_wrote + 3, 128 | u & 63);
    }
    cf_wrote += k;
  }
}

/* Strings, as shim/cdom.js's str: a literal's slot in the page's table, a pointer into read-only data read once and
 * kept, or a runtime string's record in C's tmp block. ahead is a no-op here: every string is read from memory. */
var cf_lits = null;
var cf_litBase = 0;
var cf_litEnd = 0;
var cf_ro = new Object();
var cf_tmpSerial0 = 0;
var cf_tmpCount = 0;
var cf_tmpRecords = 0;
var cf_recWords = 0;
var cf_recP = 0;
var cf_recLen = 0;
function cf_str(s) {
  if (s >= cf_litBase && s < cf_litEnd) return cf_lits[(s - cf_litBase) >> 2];
  var v, e, k, at;
  if (s > 0) {
    v = cf_ro["r" + s];
    if (typeof v == "string") return v;
    e = s;
    while (cf_load8(e)) e++;
    v = cf_utf8(s, e - s);
    cf_ro["r" + s] = v;
    return v;
  }
  k = ((s & 2147483647) - cf_load32(cf_tmpSerial0)) & 2147483647;
  if (k >= cf_load32(cf_tmpCount)) {
    cf_fail("cdom: a cd_tmp string used after the call it was made for");
    return "";
  }
  at = (cf_tmpRecords >> 2) + k * cf_recWords;
  return cf_utf8(cf_load32(at + cf_recP), cf_load32(at + cf_recLen));
}
/* The outermost call into C has returned: its runtime strings are dead. */
function cf_callEnded() {
  if (!cf_started) return;
  cf_store32(cf_tmpSerial0, cf_load32(cf_tmpSerial0) + cf_load32(cf_tmpCount));
  cf_store32(cf_tmpCount, 0);
}

/* cd_layout's block, read as shim/cdom.js's readLayout reads it. */
var cf_at = 0;
var cf_block = 0;
var cf_record = 0;
var cf_evType, cf_evId, cf_evX, cf_evY, cf_evButton, cf_evKey, cf_evMods, cf_evPath, cf_evDepth, cf_evCh, cf_evMs;
var cf_evPointer, cf_evButtons, cf_evDx, cf_evDy, cf_vpW, cf_vpH, cf_vpHidden, cf_vpDpr;
var cf_valWords, cf_valTag, cf_valNum, cf_valU;
function cf_next() {
  cf_at++;
  return cf_load32(cf_at - 1);
}
function cf_word() {
  return cf_next() >> 2;
}
function cf_tmpField() {
  return (cf_block + cf_next()) >> 2;
}
function cf_field() {
  return (cf_record + cf_next()) >> 2;
}
function cf_readLayout(at) {
  cf_at = at;
  var count = cf_next(), end = cf_at + count;
  cf_block = cf_next();
  cf_tmpSerial0 = cf_tmpField();
  cf_tmpCount = cf_tmpField();
  cf_tmpRecords = cf_block + cf_next();
  cf_recWords = cf_word();
  cf_recP = cf_word();
  cf_recLen = cf_word();
  cf_record = cf_next();
  cf_evType = cf_field();
  cf_evId = cf_field();
  cf_evX = cf_field();
  cf_evY = cf_field();
  cf_evButton = cf_field();
  cf_evKey = cf_field();
  cf_evMods = cf_field();
  cf_evPath = cf_field();
  cf_evDepth = cf_field();
  cf_evCh = cf_field();
  cf_evMs = cf_field();
  cf_evPointer = cf_field();
  cf_evButtons = cf_field();
  cf_evDx = cf_field();
  cf_evDy = cf_field();
  cf_vpW = cf_word();
  cf_vpH = cf_word();
  cf_vpHidden = cf_word();
  cf_vpDpr = cf_word();
  cf_valWords = cf_word();
  cf_valTag = cf_word();
  cf_valNum = cf_word();
  cf_valU = cf_word();
  return cf_at == end;
}

/* The node tree. y is 1 for an element (t its tag) and 3 for a text (x its text); p, f, l, n and v are the parent,
 * the first and last children and the next and previous siblings, so a node goes in and out with no splice. a and s
 * are attributes and styles as name, value pairs. i is the id C listens under (shim/cdom.js's $i); fv, fc and fs a
 * field's live value, checked and selectedIndex (null and -1 until known), and seen the value C last heard of;
 * ss and se its selection. h and j are its handle and field index in the draw numbered hd and jd. z marks a node. */
function cf_Node(type, tag, text) {
  this.z = 1;
  this.y = type;
  this.t = tag;
  this.x = text;
  this.p = null;
  this.f = null;
  this.l = null;
  this.n = null;
  this.v = null;
  this.a = null;
  this.s = null;
  this.i = -1;
  this.fv = null;
  this.fc = -1;
  this.fs = -1;
  this.seen = null;
  this.ss = -1;
  this.se = -1;
  this.h = -1;
  this.hd = -1;
  this.j = -1;
  this.jd = -1;
  this.dp = null;
  this.ds = null;
}
function cf_unlink(x) {
  var p = x.p;
  if (!p) return;
  if (x.v) x.v.n = x.n;
  else p.f = x.n;
  if (x.n) x.n.v = x.v;
  else p.l = x.v;
  x.p = null;
  x.n = null;
  x.v = null;
}
/* insertBefore: x into parent before next (null: at the end). As the DOM's: x before itself stays where it is, and a
 * next that is not a child of parent is an error (the DOM throws, shim/cdom.js's trap; here it is noted, and nothing
 * moves), so no node ever links to itself or to another parent's children. */
function cf_place(parent, x, next) {
  if (next == x) return;
  if (next && next.p != parent) {
    cf_fail("cdom: insert before a node that is not a child of the parent");
    return;
  }
  cf_unlink(x);
  x.p = parent;
  if (next) {
    x.n = next;
    x.v = next.v;
    if (next.v) next.v.n = x;
    else parent.f = x;
    next.v = x;
  } else {
    x.v = parent.l;
    if (parent.l) parent.l.n = x;
    else parent.f = x;
    parent.l = x;
  }
}
/* A pair list's value for name, or null. */
function cf_pair(list, name) {
  var k;
  if (!list) return null;
  for (k = 0; k < list.length; k += 2) if (list[k] == name) return list[k + 1];
  return null;
}
/* The list with name set to value (null: taken out). */
function cf_setPair(list, name, value) {
  var k, out;
  if (!list) list = new Array();
  for (k = 0; k < list.length; k += 2) if (list[k] == name) {
    if (value != null) {
      list[k + 1] = value;
      return list;
    }
    out = new Array();
    for (k = 0; k < list.length; k += 2) if (list[k] != name) {
      out[out.length] = list[k];
      out[out.length] = list[k + 1];
    }
    return out;
  }
  if (value != null) {
    list[list.length] = name;
    list[list.length] = value;
  }
  return list;
}
function cf_copy(list) {
  var out, k;
  if (!list) return null;
  out = new Array();
  for (k = 0; k < list.length; k++) out[k] = list[k];
  return out;
}
function cf_deep(x) {
  var c = new cf_Node(x.y, x.t, x.x), ch;
  c.a = cf_copy(x.a);
  c.s = cf_copy(x.s);
  for (ch = x.f; ch; ch = ch.n) cf_place(c, cf_deep(ch), null);
  return c;
}
/* The text under x, all of it. */
function cf_textOf(x) {
  var out = "", ch;
  if (x.y == 3) return x.x;
  for (ch = x.f; ch; ch = ch.n) out += cf_textOf(ch);
  return out;
}

/* The ops, as shim/cdom.js's of the same names (and the same comments there), on the node tree. */
var cf_nodes = new Array();     /* node by id */
var cf_mask = new Array();      /* listened types per id: two words, a bit per type */
var cf_templates = new Array();
var cf_tplSlots = new Array();
var cf_slots = new Array();
var cf_stack = new Array();
var cf_depth = 0;
var cf_last = null;
var cf_lastEl = null;
var cf_lastText = null;
var cf_root = null;
var cf_base = cf_DETACHED;
var cf_before = 0;
var cf_mount = new cf_Node(1, "body", null);
var cf_doc = new Object();
var cf_dirty = 0;
function cf_placeNew(x) {
  cf_dirty = 1;
  if (cf_depth) cf_place(cf_stack[cf_depth - 1], x, null);
  else if (cf_base != cf_DETACHED) {
    if (cf_before) cf_place(cf_nodes[cf_base], x, cf_nodes[cf_before]);
    else cf_place(cf_nodes[cf_base], x, null);
  }
}
function cf_op_ahead(p, len) {
}
function cf_op_at(parent, beforeId) {
  cf_base = parent >>> 0;
  cf_before = beforeId;
}
function cf_push(el) {
  if (cf_depth) cf_place(cf_stack[cf_depth - 1], el, null);
  cf_stack[cf_depth] = el;
  cf_depth++;
  cf_last = el;
  cf_lastEl = el;
  cf_dirty = 1;
}
/* A tag as the serializer compares and writes it: lowercase (cf_SKIP, cf_DIV and cf_VOID, and the tags it writes by
 * itself, are), and letters and digits only, since it is written into the markup as it is: any other is noted and
 * drawn as a SPAN. */
var cf_ALNUM = "abcdefghijklmnopqrstuvwxyz0123456789";
function cf_tag(name) {
  var t = name.toLowerCase(), k;
  for (k = 0; k < t.length; k++) if (cf_ALNUM.indexOf(t.charAt(k)) < 0) break;
  if (t.length && k == t.length) return t;
  cf_fail("cdom: the tag " + name + " is not letters and digits: drawn as a span");
  return "span";
}
function cf_op_open(tag) {
  cf_push(new cf_Node(1, cf_tag(cf_str(tag)), null));
}
function cf_op_close() {
  cf_depth--;
  var el = cf_stack[cf_depth];
  if (!cf_depth) {
    cf_root = el;
    cf_placeNew(el);
  }
}
function cf_op_text(id) {
  cf_last = new cf_Node(3, null, cf_str(id));
  cf_lastText = cf_last;
  cf_placeNew(cf_last);
}
function cf_op_text_num(number) {
  cf_last = new cf_Node(3, null, "" + (number >>> 0));
  cf_lastText = cf_last;
  cf_placeNew(cf_last);
}
/* An attribute; style is read into the style pairs, which section 8 writes as markup. */
function cf_setAttr(x, name, value) {
  var parts, k, c;
  cf_dirty = 1;
  if (name != "style") {
    x.a = cf_setPair(x.a, name, value);
    return;
  }
  x.s = null;
  parts = value.split(";");
  for (k = 0; k < parts.length; k++) {
    c = parts[k].indexOf(":");
    if (c > 0) x.s = cf_setPair(x.s, cf_trim(parts[k].substring(0, c)).toLowerCase(), cf_trim(parts[k].substring(c + 1)));
  }
}
function cf_trim(s) {
  var a = 0, b = s.length;
  while (a < b && s.charAt(a) == " ") a++;
  while (b > a && s.charAt(b - 1) == " ") b--;
  return s.substring(a, b);
}
function cf_op_attr(name, value) {
  cf_setAttr(cf_last, cf_str(name), cf_str(value));
}
function cf_op_class(value) {
  cf_last.a = cf_setPair(cf_last.a, "class", cf_str(value));
}
function cf_op_name_el(id) {
  cf_nodes[id] = cf_lastEl;
}
function cf_op_name_text(id) {
  cf_nodes[id] = cf_lastText;
}
function cf_op_name_open(id) {
  cf_nodes[id] = cf_stack[cf_depth - 1];
}
/* A host object (the window, the document, the body) has no events here. */
function cf_op_listen(id, type) {
  if (id && id < 4) {
    cf_lacks("events on the window, the document and the body");
    return;
  }
  cf_nodes[id] = cf_last;
  cf_last.i = id;
  cf_mask[id * 2 + (type >> 5)] = cf_mask[id * 2 + (type >> 5)] | 1 << (type & 31);
  cf_dirty = 1;
}
function cf_op_unlisten(id, type) {
  if (id >= 4) cf_mask[id * 2 + (type >> 5)] = cf_mask[id * 2 + (type >> 5)] & ~(1 << (type & 31));
  cf_dirty = 1;
}
function cf_op_value(id) {
  cf_last.x = cf_str(id);
  cf_dirty = 1;
}
function cf_op_value_num(number) {
  cf_last.x = "" + (number >>> 0);
  cf_dirty = 1;
}
function cf_op_tpl_save(templateId) {
  cf_templates[templateId] = cf_root;
  cf_tplSlots[templateId] = cf_slots;
  cf_slots = new Array();
  cf_unlink(cf_root);
}
function cf_op_clone(templateId) {
  cf_push(cf_deep(cf_templates[templateId]));
}
function cf_op_slot(index) {
  cf_slots[index] = cf_last;
}
/* A template's slot is in the template itself, so a clone made after it has its text. */
function cf_op_tpl_text(templateId, index, id) {
  cf_tplSlots[templateId][index].x = cf_str(id);
}
function cf_op_tpl_text_num(templateId, index, number) {
  cf_tplSlots[templateId][index].x = "" + (number >>> 0);
}
/* count steps of 2 bits (1 firstChild, 2 nextSibling, 3 parentNode) from a named node, or from the last when -1. */
function cf_op_walk(from, steps, count) {
  var x = cf_last, step;
  if (from != -1) x = cf_nodes[from];
  for (; count; count--) {
    step = steps & 3;
    if (step == 1) x = x.f;
    else if (step == 2) x = x.n;
    else x = x.p;
    steps = steps >>> 2;
  }
  cf_last = x;
}
function cf_op_walk_value(from, steps, count, id) {
  cf_op_walk(from, steps, count);
  cf_op_value(id);
}
function cf_op_walk_value_num(from, steps, count, number) {
  cf_op_walk(from, steps, count);
  cf_op_value_num(number);
}
/* A text node's value: an element's id (the trap shim/cdom.js throws for) is reported here, since nothing can be
 * thrown in JavaScript 1.1, and the call goes on. */
function cf_textNode(id, call) {
  var x = cf_nodes[id];
  if (x && x.y == 3) return x;
  cf_fail("cdom: " + call + "(" + id + ", ...): not a text node");
  return null;
}
function cf_op_set_text(id, textId) {
  var x = cf_textNode(id, "cd_set_text_node");
  if (x) x.x = cf_str(textId);
  cf_dirty = 1;
}
function cf_op_set_text_num(id, number) {
  var x = cf_textNode(id, "cd_set_text_node_num");
  if (x) x.x = "" + (number >>> 0);
  cf_dirty = 1;
}
function cf_op_set_attr(id, name, value) {
  cf_setAttr(cf_nodes[id], cf_str(name), cf_str(value));
}
function cf_op_set_class(id, value) {
  var x = cf_nodes[id];
  x.a = cf_setPair(x.a, "class", cf_str(value));
  cf_dirty = 1;
}
function cf_op_style(id, property, value) {
  var x = cf_nodes[id];
  x.s = cf_setPair(x.s, cf_str(property), cf_str(value));
  cf_dirty = 1;
}
function cf_op_style_num(id, property, number, unit) {
  var x = cf_nodes[id];
  x.s = cf_setPair(x.s, cf_str(property), number + cf_UNITS[unit]);
  cf_dirty = 1;
}
function cf_forget(id) {
  var x = cf_nodes[id];
  if (x && x.i == id) x.i = -1;
  cf_nodes[id] = null;
  cf_mask[id * 2] = 0;
  cf_mask[id * 2 + 1] = 0;
}
function cf_op_remove(id) {
  cf_unlink(cf_nodes[id]);
  cf_forget(id);
  cf_dirty = 1;
}
function cf_op_free(id) {
  cf_forget(id);
}
function cf_op_move(id, beforeId) {
  var x = cf_nodes[id];
  if (!x.p) return;
  if (beforeId) cf_place(x.p, x, cf_nodes[beforeId]);
  else cf_place(x.p, x, null);
  cf_dirty = 1;
}
function cf_op_clear(id) {
  var x = cf_nodes[id];
  while (x.f) cf_unlink(x.f);
  cf_dirty = 1;
}
function cf_op_detach(id) {
  var x = cf_nodes[id];
  x.dp = x.p;
  x.ds = x.n;
  cf_unlink(x);
  cf_dirty = 1;
}
function cf_op_attach(id) {
  var x = cf_nodes[id];
  if (x.dp) cf_place(x.dp, x, x.ds);
  x.dp = null;
  x.ds = null;
  cf_dirty = 1;
}

/* Values (cd_val), properties and calls: a field's live state on a node of the tree; the window as it is. */
var cf_vv = null;   /* the value cf_valueAt read */
var cf_vn = 0;      /* and the index of the entry after it */
function cf_valueAt(ptr, index) {
  var at = (ptr >> 2) + index * cf_valWords, tag = cf_load32(at + cf_valTag), u = cf_load32(at + cf_valU), o, pair, next, key;
  cf_vn = index + 1;
  cf_vv = null;
  if (tag == 0) cf_vv = cf_mem.load_f64((at + cf_valNum) >> 1);
  else if (tag == 1) cf_vv = cf_str(u | 0);
  else if (tag == 2) cf_vv = u == 1;
  else if (tag == 3) cf_vv = cf_nodes[u];
  else if (tag == 5) {
    o = new Object();
    next = index + 1;
    for (pair = 0; pair < u; pair++) {
      cf_valueAt(ptr, next);
      key = cf_vv;
      cf_valueAt(ptr, cf_vn);
      o[key] = cf_vv;
      next = cf_vn;
    }
    cf_vv = o;
    cf_vn = next;
  }
}
/* A text field's value: what the user or C last put there, else its value attribute. */
function cf_fieldValue(x) {
  var v;
  if (x.fv != null) return x.fv;
  if (x.t == "textarea") return cf_textOf(x);
  v = cf_pair(x.a, "value");
  if (v == null) return "";
  return v;
}
/* A SELECT's value: its chosen OPTION's value attribute, or its text. */
function cf_selectValue(x) {
  var k = 0, o, chosen = cf_chosen(x), v;
  for (o = x.f; o; o = o.n) if (o.y == 1) {
    if (k == chosen) {
      v = cf_pair(o.a, "value");
      if (v == null) v = cf_textOf(o);
      return v;
    }
    k++;
  }
  return "";
}
function cf_chosen(x) {
  var k = 0, o;
  if (x.fs >= 0) return x.fs;
  for (o = x.f; o; o = o.n) if (o.y == 1) {
    if (cf_pair(o.a, "selected") != null) return k;
    k++;
  }
  return 0;
}
function cf_checked(x) {
  if (x.fc >= 0) return x.fc == 1;
  return cf_pair(x.a, "checked") != null;
}
function cf_prop(x, key) {
  if (key == "value") {
    if (x.t == "select") return cf_selectValue(x);
    return cf_fieldValue(x);
  }
  if (key == "checked") return cf_checked(x);
  if (key == "selectedIndex") return cf_chosen(x);
  if (key == "nodeValue" || key == "data") return x.x;
  if (key == "textContent" || key == "innerText") return cf_textOf(x);
  if (key == "tagName" || key == "nodeName") {
    if (x.y == 3) return "#text";
    return x.t.toUpperCase();
  }
  if (key == "className") return cf_pair(x.a, "class");
  if (key == "selectionStart") return x.ss;
  if (key == "selectionEnd") return x.se;
  if (key == "parentNode") return x.p;
  if (key == "firstChild") return x.f;
  if (key == "lastChild") return x.l;
  if (key == "nextSibling") return x.n;
  if (key == "previousSibling") return x.v;
  return cf_pair(x.a, key);
}
function cf_setProp(x, key, v) {
  cf_dirty = 1;
  if (key == "value") {
    if (x.t == "textarea" || x.t == "input") {
      x.fv = "" + v;
      x.seen = x.fv;
    }
    return;
  }
  if (key == "checked") {
    x.fc = 0;
    if (v) x.fc = 1;
    return;
  }
  if (key == "selectedIndex") {
    x.fs = v;
    return;
  }
  if (key == "nodeValue" || key == "data") {
    x.x = "" + v;
    return;
  }
  if (key == "textContent" || key == "innerText") {
    while (x.f) cf_unlink(x.f);
    cf_place(x, new cf_Node(3, null, "" + v), null);
    return;
  }
  if (key == "className") key = "class";
  if (v == false || v == null) x.a = cf_setPair(x.a, key, null);
  else if (v == true) x.a = cf_setPair(x.a, key, "");
  else x.a = cf_setPair(x.a, key, "" + v);
}
/* What keys lead to from o, the first count of them. */
function cf_walkPath(o, keys, count) {
  var k;
  for (k = 0; k < count && o != null; k++) {
    if (o.z == 1) o = cf_prop(o, keys[k]);
    else o = o[keys[k]];
  }
  return o;
}
function cf_op_set(id, path, valuePtr) {
  var keys = cf_str(path).split("."), o, key = keys[keys.length - 1];
  cf_valueAt(valuePtr, 0);
  o = cf_walkPath(cf_nodes[id], keys, keys.length - 1);
  if (o == null) cf_fail("cdom: cd_set(" + id + ", " + cf_str(path) + "): nothing there");
  else if (o.z == 1) cf_setProp(o, key, cf_vv);
  else o[key] = cf_vv;
}
/* Kept for cd_get_id: what the last call returned (CD_RESULT). */
var cf_result = null;
var cf_focus = null;     /* the field that has focus, as the frame last said */
var cf_wantFocus = 0;    /* C asked for a focus or a blur, which the next draw (or the end of the call) gives */
function cf_op_call(id, method, count, argsPtr) {
  var args = new Array(), k, next = 0, keys = cf_str(method).split("."), o, m = keys[keys.length - 1];
  for (k = 0; k < count; k++) {
    cf_valueAt(argsPtr, next);
    args[k] = cf_vv;
    next = cf_vn;
  }
  cf_result = null;
  o = cf_walkPath(cf_nodes[id], keys, keys.length - 1);
  if (o != null && o.z == 1) {
    if (m == "focus" || m == "select") {
      cf_focus = o;
      if (m == "select") {
        o.ss = 0;
        o.se = cf_fieldValue(o).length;
      }
      cf_wantFocus = 1;
      return;
    }
    if (m == "blur") {
      if (cf_focus == o) cf_focus = null;
      cf_wantFocus = 1;
      return;
    }
    cf_fail("cdom: cd_call(" + id + ", " + m + "): no such method on the frames tier");
    return;
  }
  if (o == null || !o[m]) {
    cf_fail("cdom: cd_call(" + id + ", " + m + "): no such method");
    return;
  }
  cf_result = o[m](args[0], args[1], args[2], args[3]);
  cf_nodes[5] = cf_result;
}
function cf_op_get(id, path, kind, destPtr, capacity) {
  var keys = cf_str(path).split("."), v = cf_walkPath(cf_nodes[id], keys, keys.length), s = "", k;
  if (kind == 0) {
    if (typeof v == "boolean") {
      if (v) return 1;
      return 0;
    }
    if (typeof v == "number") return v;
    return Number.NaN;
  }
  if (kind == 1 || kind == 3) {
    if (v != null) s = "" + v;
    cf_encode(s, destPtr, capacity);
    if (kind == 1) return cf_wrote;
    return cf_whole;
  }
  if (v == null) return -1;
  for (k = 0; k < cf_nodes.length; k++) if (k != 5 && cf_nodes[k] == v) return k;
  return -1;
}

/* Timers: setTimeout with a string, the only form before JavaScript 1.2. The handle C keeps is the shim's own, so
 * the string can name it. */
var cf_tok = new Array();
var cf_tid = new Array();
var cf_timers = 0;
var cf_d0 = new Date();
var cf_t0 = cf_d0.getTime();
function cf_op_timer(ms, token) {
  cf_timers++;
  cf_tok[cf_timers] = token;
  cf_tid[cf_timers] = setTimeout("cdomF.t(" + cf_timers + ")", ms);
  return cf_timers;
}
function cf_op_timer_stop(handle) {
  if (cf_tok[handle] == null) return;
  cf_tok[handle] = null;
  clearTimeout(cf_tid[handle]);
}
function cf_op_raf(token) {
  setTimeout("cdomF.r(" + token + ")", 16);
}
function cf_op_now() {
  var d = new Date();
  return d.getTime() - cf_t0;
}
function cf_op_epoch() {
  var d = new Date();
  return d.getTime();
}
function cf_op_origin(destPtr, capacity) {
  cf_encode(location.protocol + "//" + location.host, destPtr, capacity);
  return cf_wrote;
}
function cf_op_url_read(part, destPtr, capacity) {
  var s = location.pathname + location.search + location.hash;
  if (part == 0) s = location.pathname;
  else if (part == 1) s = location.search;
  else if (part == 2) s = location.hash;
  cf_encode(s, destPtr, capacity);
  return cf_wrote;
}
function cf_op_url_step(delta) {
  history.go(delta);
}
var cf_title = "";
function cf_op_title(p, n) {
  cf_title = cf_utf8(p, n);
  cf_dirty = 1;
}
/* The frame's size where the browser says it (innerWidth in Netscape 4, the body's client size in IE 4), else 0. */
function cf_op_viewport(recordPtr) {
  var w = cf_view(), r = recordPtr >> 2, width = 0, height = 0;
  if (w && w.innerWidth) {
    width = w.innerWidth;
    height = w.innerHeight;
  } else if (w && w.document.body && w.document.body.clientWidth) {
    width = w.document.body.clientWidth;
    height = w.document.body.clientHeight;
  }
  cf_store32(r + cf_vpW, width);
  cf_store32(r + cf_vpH, height);
  cf_store32(r + cf_vpHidden, 0);
  cf_store32(r + cf_vpDpr, 100);
}
/* No box and no hit test: nothing here says where a node is drawn. */
function cf_op_rect(id, outPtr) {
  return 0;
}
function cf_op_hit(x, y) {
  return 0;
}
/* What this tier has none of: each answers 0 or nothing, and is noted once in cdomF.errors (section 9). */
var cf_noted = new Object();
function cf_lacks(name) {
  if (!cf_noted[name]) {
    cf_noted[name] = 1;
    cf_fail("cdom: " + name + " is not on the frames tier");
  }
  return 0;
}
function cf_fail(message) {
  cdomF.errors[cdomF.errors.length] = message;
  window.status = message;
}

/* The math wasm2js output calls that JavaScript 1.1 has not (ES2015's). */
function cf_imul(a, b) {
  var al = a & 65535, bl = b & 65535;
  return al * bl + (((a >>> 16) * bl + al * (b >>> 16) & 65535) << 16) | 0;
}
function cf_clz32(x) {
  var n = 0;
  x = x >>> 0;
  if (x == 0) return 32;
  while (x < 2147483648) {
    x = x * 2;
    n++;
  }
  return n;
}
function cf_trunc(x) {
  if (x < 0) return Math.ceil(x);
  return Math.floor(x);
}
if (!Math.imul) Math.imul = cf_imul;
if (!Math.clz32) Math.clz32 = cf_clz32;
if (!Math.trunc) Math.trunc = cf_trunc;

/* Entries and drawing. holds counts the entries from the browser and the page under way, busy the calls into C;
 * when both are 0 again, a tree that changed is drawn (section 4). */
var cf_busy = 0;
var cf_holds = 0;
var cf_started = 0;
var cf_drawing = 0;
var cf_pending = 0;   /* a draw the frame was not there for yet */
var cf_later = 0;     /* a draw put off by cf_LATER */
var cf_over = 0;      /* the frameset was written over the page (cf_poll) */
var cf_waits = 0;
var cf_pressed = 0;   /* a mouse button is down in the frame */
var cf_pressWaits = 0;
function cf_cRun(name, a, b, c, d, e, f, g, h) {
  var r;
  cf_busy++;
  r = cf_raw[name](a, b, c, d, e, f, g, h);
  cf_busy--;
  if (cf_busy == 0) cf_callEnded();
  return r;
}
/* A call into C that never came back (a trap: JavaScript 1.1 has no finally) left the counts up. Only an entry from
 * the browser comes here, which runs with nothing on the stack, so they are put back. */
function cf_recover() {
  if (cf_busy || cf_holds) {
    cf_fail("cdom: a call into C did not return");
    cf_busy = 0;
    cf_holds = 0;
    cf_drawing = 0;
    cf_callEnded();
  }
}
function cf_hold(fromBrowser) {
  if (fromBrowser) cf_recover();
  if (cf_holds == 0 && cf_busy == 0) cf_sync();
  cf_holds++;
}
/* later: 1 for a change, a focus or a blur, whose draw waits cf_LATER for the click that may follow; 2 for a key
 * typed in a field whose caret cannot be read (cf_blind), whose draw waits for that field's change or blur. */
function cf_release(later) {
  cf_holds--;
  if (cf_holds || cf_busy) return;
  if (cf_dirty && later == 2) return;
  if (cf_dirty && later) {
    if (!cf_later) setTimeout("cdomF.p()", cf_LATER);
    cf_later = 1;
    return;
  }
  if (cf_dirty || cf_pending) cf_draw();
  else if (cf_wantFocus) cf_restore(cf_view());
}
/* An export, as the page and the app's own imports call it. */
function cf_exp(name, a, b, c, d, e, f, g, h) {
  var r;
  cf_hold(0);
  r = cf_cRun(name, a, b, c, d, e, f, g, h);
  cf_release(0);
  return r;
}
function cf_wrap(raw) {
  var out = new Object(), k;
  for (k in raw) {
    if (typeof raw[k] == "function") out[k] = new Function("a", "b", "c", "d", "e", "f", "g", "h", "return cf_exp(\"" + k + "\", a, b, c, d, e, f, g, h)");
    else out[k] = raw[k];
  }
  return out;
}
function cf_view() {
  if (window.frames.length < 1) return null;
  if (!window.frames[0].document) return null;
  return window.frames[0];
}

/* Fields: read back from the frame before each draw, in the order they were written, which is form.elements'. */
var cf_fields = new Array();
var cf_nfields = 0;
var cf_hit = new Array();
var cf_nhit = 0;
var cf_serial = 0;
var cf_sx = 0;
var cf_sy = 0;
function cf_isText(type) {
  return type != "checkbox" && type != "radio" && type != "button" && type != "submit" && type != "reset"
    && type != "file" && type != "image" && type != "select-one" && type != "select-multiple";
}
function cf_sync() {
  var w = cf_view(), form, k, el, x, t, b;
  if (!w || !cdomF.draws) return;
  form = w.document.forms[0];
  if (form) for (k = 0; k < cf_nfields; k++) {
    el = form.elements[k];
    x = cf_fields[k];
    if (!el || !x) continue;
    t = el.type;
    if (t == "checkbox" || t == "radio") {
      x.fc = 0;
      if (el.checked) x.fc = 1;
    } else if (t == "select-one" || t == "select-multiple") x.fs = el.selectedIndex;
    else if (cf_isText(t)) {
      x.fv = el.value;
      if (x == cf_focus) cf_caret(w, el, x);
    }
  }
  b = w.document.body;
  if (typeof w.pageYOffset == "number") {
    cf_sx = w.pageXOffset;
    cf_sy = w.pageYOffset;
  } else if (b && typeof b.scrollTop == "number") {
    cf_sx = b.scrollLeft;
    cf_sy = b.scrollTop;
  }
}
/* The focused field's selection, into x.ss and x.se: selectionStart where the browser has it; on IE 4, which has not,
 * from the document's selection, a TextRange: its text is the selection, and moved back to the field's start (moveStart
 * stops there) its text is everything before the selection's end. Netscape 4 has neither, and leaves them as they were. */
function cf_caret(w, el, x) {
  var r, n;
  if (typeof el.selectionStart == "number") {
    x.ss = el.selectionStart;
    x.se = el.selectionEnd;
  } else if (w.document.selection && el.createTextRange) {
    r = w.document.selection.createRange().duplicate();
    n = r.text.length;
    r.moveStart("character", -el.value.length - 1);
    x.se = r.text.length;
    x.ss = x.se - n;
  }
}
/* A browser that cannot read the caret (Netscape 4: no selectionStart, no document.selection) would put it back at the
 * field's start or end on every redraw, so a redraw while a key is typed waits for the field's change or blur. */
function cf_blind(x) {
  var w = cf_view(), form, el;
  if (!w || x.jd != cf_serial) return 0;
  form = w.document.forms[0];
  if (!form) return 0;
  el = form.elements[x.j];
  if (!el || typeof el.selectionStart == "number" || (w.document.selection && el.createTextRange)) return 0;
  return 1;
}
/* Focus, the selection and the scroll back where they were, after a draw (section 6). The focus given back is no
 * new focus to C: cf_drawing keeps the handlers quiet. */
function cf_restore(w) {
  var f = cf_focus, form, el, r;
  cf_wantFocus = 0;
  if (!w) return;
  cf_quiet();
  if (f && f.jd == cf_serial) {
    form = w.document.forms[0];
    el = null;
    if (form) el = form.elements[f.j];
    if (el && el.focus) {
      el.focus();
      /* Only a text field has a selection: a checkbox's setSelectionRange throws (cd_call(checkbox, "select")). */
      if (f.ss >= 0 && cf_isText(el.type) && el.setSelectionRange) el.setSelectionRange(f.ss, f.se);
      else if (f.ss >= 0 && cf_isText(el.type) && el.createTextRange) {
        r = el.createTextRange();
        r.collapse(true);
        r.moveEnd("character", f.se);
        r.moveStart("character", f.ss);
        r.select();
      }
    }
  }
  if (cf_sx || cf_sy) {
    if (w.scrollTo) w.scrollTo(cf_sx, cf_sy);
    else if (w.scroll) w.scroll(cf_sx, cf_sy);
  }
  cf_drawing = 0;
}
/* The handlers keep quiet while the shim draws (cf_drawing). A draw that throws (JavaScript 1.1 has no finally) would
 * leave them quiet for good, so each sets a timer that, run with nothing on the stack, finds it still up only after
 * such a throw: it is put down, and noted. */
var cf_settling = 0;
function cf_quiet() {
  cf_drawing = 1;
  if (cf_settling) return;
  cf_settling = 1;
  setTimeout("cdomF.s()", 0);
}
function cf_settle() {
  cf_settling = 0;
  if (!cf_drawing) return;
  cf_drawing = 0;
  cf_fail("cdom: a draw did not finish");
}
/* The tree, written whole into the frame: one open, write and close, replacing the frame's history entry where the
 * browser takes "replace" (section 7). */
function cf_draw() {
  var w = cf_view(), html, d;
  if (!w) {
    cf_pending = 1;
    return;
  }
  cf_pending = 0;
  cf_later = 0;
  cf_dirty = 0;
  html = cf_html();
  d = w.document;
  cf_quiet();
  d.open("text/html", "replace");
  d.write(html);
  d.close();
  d.onmousedown = cf_onDown;
  d.onmouseup = cf_onUp;
  if (d.captureEvents && w.Event) d.captureEvents(w.Event.MOUSEDOWN | w.Event.MOUSEUP);
  cf_drawing = 0;
  cdomF.draws++;
  cdomF.bytes = html.length;
  cf_restore(w);
}

/* Serializing (section 8): the tree as period markup, CSS as FONT, B, I, U, STRIKE, CENTER and a TABLE's BGCOLOR. */
var cf_out = null;
var cf_on = 0;
var cf_KEEP = " id href src alt title width height align valign border colspan rowspan cellpadding cellspacing size maxlength rows cols target ";
var cf_SKIP = " svg canvas script style template ";
/* form too: every control is in the shim's one FORM, and a FORM inside it is dropped by the parser, whose </form> then
 * closes the shim's, and the controls after it leave form.elements. */
var cf_DIV = " section article header footer nav main aside figure form ";
var cf_VOID = " br hr img wbr ";
function cf_in(list, name) {
  return list.indexOf(" " + name + " ") >= 0;
}
function cf_w(s) {
  cf_out[cf_on] = s;
  cf_on++;
}
function cf_esc(s) {
  var out = "", k, c;
  if (s.indexOf("&") < 0 && s.indexOf("<") < 0 && s.indexOf(">") < 0 && s.indexOf("\"") < 0) return s;
  for (k = 0; k < s.length; k++) {
    c = s.charAt(k);
    if (c == "&") out += "&amp;";
    else if (c == "<") out += "&lt;";
    else if (c == ">") out += "&gt;";
    else if (c == "\"") out += "&quot;";
    else out += c;
  }
  return out;
}
function cf_blank(s) {
  var k, c;
  for (k = 0; k < s.length; k++) {
    c = s.charAt(k);
    if (c != " " && c != "\n" && c != "\t" && c != "\r") return 0;
  }
  return 1;
}
/* The handle the markup names x by in this draw: its index in cf_hit. */
function cf_handle(x) {
  if (x.hd != cf_serial) {
    x.hd = cf_serial;
    x.h = cf_nhit;
    cf_hit[cf_nhit] = x;
    cf_nhit++;
  }
  return x.h;
}
/* x is form.elements[its index], since every control is written in order and counted here, and is read back by that
 * index. Each is named cN, but a radio keeps the app's name, which is what groups it with the others of that name. */
function cf_fieldOf(x) {
  var name = cf_pair(x.a, "name");
  x.j = cf_nfields;
  x.jd = cf_serial;
  cf_fields[cf_nfields] = x;
  cf_nfields++;
  if (x.t == "input" && name != null && cf_type(x) == "radio") return " name=\"" + cf_esc(name) + "\"";
  return " name=\"c" + x.j + "\"";
}
/* x's attributes that old browsers read (cf_KEEP), but skip (a link's href, written by itself). */
function cf_attrs(x, skip) {
  var out = "", k, a = x.a;
  if (!a) return out;
  for (k = 0; k < a.length; k += 2) if (a[k] != skip && cf_in(cf_KEEP, a[k])) out += " " + a[k] + "=\"" + cf_esc(a[k + 1]) + "\"";
  return out;
}
/* A CSS color FONT COLOR and BGCOLOR take: a name or #rrggbb as it is, rgb() and rgba() as #rrggbb. */
function cf_color(v) {
  var parts, k, n, out = "#";
  if (v.substring(0, 3) != "rgb") return v;
  parts = v.substring(v.indexOf("(") + 1, v.indexOf(")")).split(",");
  for (k = 0; k < 3; k++) {
    n = parseInt(parts[k], 10);
    if (!(n > 0)) n = 0;
    if (n > 255) n = 255;
    out += cf_HEX.charAt(n >> 4) + cf_HEX.charAt(n & 15);
  }
  return out;
}
/* A font-size as FONT SIZE 1 to 7. */
function cf_size(v) {
  var px = parseFloat(v);
  if (!(px > 0)) return 0;
  if (v.indexOf("pt") > 0) px = px * 4 / 3;
  else if (v.indexOf("em") > 0) px = px * 16;
  if (px <= 10) return 1;
  if (px <= 13) return 2;
  if (px <= 16) return 3;
  if (px <= 18) return 4;
  if (px <= 24) return 5;
  if (px <= 32) return 6;
  return 7;
}
function cf_bg(x) {
  var v = cf_pair(x.s, "background-color");
  if (v == null) v = cf_pair(x.s, "background");
  if (v == null || v.indexOf("url(") >= 0 || v.indexOf("gradient") >= 0 || v.indexOf(" ") >= 0) return null;
  return cf_color(v);
}
/* The markup around x's content for its CSS: cf_outer before its tag and cf_inner inside it, each closed in the
 * same order by cf_outerEnd and cf_innerEnd. */
var cf_outer = "";
var cf_outerEnd = "";
var cf_inner = "";
var cf_innerEnd = "";
function cf_styles(x) {
  var bg = cf_bg(x), v, font = "", n;
  cf_outer = "";
  cf_outerEnd = "";
  cf_inner = "";
  cf_innerEnd = "";
  if (!x.s) return;
  if (bg != null) {
    cf_outer = "<table border=\"0\" cellpadding=\"0\" cellspacing=\"0\"><tr><td bgcolor=\"" + cf_esc(bg) + "\">";
    cf_outerEnd = "</td></tr></table>";
  }
  v = cf_pair(x.s, "text-align");
  if (v == "center") {
    cf_outer += "<center>";
    cf_outerEnd = "</center>" + cf_outerEnd;
  } else if (v == "right") {
    cf_outer += "<div align=\"right\">";
    cf_outerEnd = "</div>" + cf_outerEnd;
  }
  v = cf_pair(x.s, "color");
  if (v != null) font += " color=\"" + cf_esc(cf_color(v)) + "\"";
  v = cf_pair(x.s, "font-size");
  if (v != null) {
    n = cf_size(v);
    if (n) font += " size=\"" + n + "\"";
  }
  v = cf_pair(x.s, "font-family");
  if (v != null) font += " face=\"" + cf_esc(v) + "\"";
  if (font != "") cf_wrapIn("<font" + font + ">", "</font>");
  v = cf_pair(x.s, "font-weight");
  if (v == "bold" || v == "bolder" || parseInt(v, 10) >= 600) cf_wrapIn("<b>", "</b>");
  v = cf_pair(x.s, "font-style");
  if (v == "italic" || v == "oblique") cf_wrapIn("<i>", "</i>");
  v = cf_pair(x.s, "text-decoration");
  if (v != null && v.indexOf("underline") >= 0) cf_wrapIn("<u>", "</u>");
  if (v != null && v.indexOf("line-through") >= 0) cf_wrapIn("<strike>", "</strike>");
}
function cf_wrapIn(open, close) {
  cf_inner += open;
  cf_innerEnd = close + cf_innerEnd;
}
/* The bits of the types listened for on x and its ancestors, the ones that can be heard here (all below 32). */
function cf_kids(x, inh, ctx) {
  var c;
  for (c = x.f; c; c = c.n) cf_node(c, inh, ctx);
}
/* A handler attribute: on, calling cdomF's f with the handle n and the rest. */
function cf_on_(on, f, n, rest) {
  return " " + on + "=\"parent.cdomF." + f + "(" + n + rest + ")\"";
}
/* A field's handlers: focus and blur always (the shim tracks focus), the rest as listened for. */
function cf_fieldHandlers(x, inh, text) {
  var n = cf_handle(x), s = cf_on_("onfocus", "f", n, ",1") + cf_on_("onblur", "f", n, ",0");
  if (text) {
    if (inh & (1 << cf_INPUT | 1 << cf_CHANGE)) s += cf_on_("onchange", "c", n, "");
    if (inh & (1 << cf_INPUT | 1 << cf_KEYUP)) s += cf_on_("onkeyup", "u", n, ",event");
    if (inh & 1 << cf_KEYDOWN) s += cf_on_("onkeydown", "d", n, ",event");
  } else if (inh & (1 << cf_CLICK | 1 << cf_INPUT | 1 << cf_CHANGE)) s += cf_on_("onclick", "k", n, "");
  return s;
}
function cf_type(x) {
  var t = cf_pair(x.a, "type");
  if (t == null) return "text";
  t = t.toLowerCase();
  if (t == "submit" || t == "reset") return "button";
  if (t == "password" || t == "checkbox" || t == "radio" || t == "hidden" || t == "button" || t == "file") return t;
  return "text";
}
function cf_node(x, inh, ctx) {
  var t, s, n, href, k, o;
  if (x.y == 3) {
    s = cf_esc(x.x);
    if (inh & 1 << cf_CLICK && !(ctx & 3) && x.p && x.p != cf_mount && !cf_blank(s)) {
      cf_w("<a href=\"javascript:parent.cdomF.k(" + cf_handle(x.p) + ")\">" + s + "</a>");
    } else cf_w(s);
    return;
  }
  t = x.t;
  if (cf_in(cf_SKIP, t) || cf_pair(x.s, "display") == "none") return;
  if (x.i >= 0) inh = inh | cf_mask[x.i * 2];
  cf_styles(x);
  cf_w(cf_outer);
  s = cf_outerEnd;
  if (t == "button") {
    cf_w("<input type=\"button\"" + cf_fieldOf(x) + " value=\"" + cf_esc(cf_textOf(x)) + "\"" + cf_attrs(x, "")
      + cf_fieldHandlers(x, inh, 0) + ">");
  } else if (t == "input") {
    k = cf_type(x);
    cf_w("<input type=\"" + k + "\"" + cf_fieldOf(x) + cf_attrs(x, ""));
    if (k == "checkbox" || k == "radio") {
      o = cf_pair(x.a, "value");
      if (o != null) cf_w(" value=\"" + cf_esc(o) + "\"");
      if (cf_checked(x)) cf_w(" checked");
    } else {
      o = cf_fieldValue(x);
      if (x.seen == null) x.seen = o;
      cf_w(" value=\"" + cf_esc(o) + "\"");
    }
    cf_w(cf_fieldHandlers(x, inh, cf_isText(k)) + ">");
  } else if (t == "textarea") {
    o = cf_fieldValue(x);
    if (x.seen == null) x.seen = o;
    cf_w("<textarea" + cf_fieldOf(x) + cf_attrs(x, "") + cf_fieldHandlers(x, inh, 1) + ">" + cf_esc(o) + "</textarea>");
  } else if (t == "select") {
    if (x.seen == null) x.seen = "" + cf_chosen(x);
    cf_w("<select" + cf_fieldOf(x) + cf_attrs(x, "") + cf_fieldHandlers(x, inh, 1) + ">");
    n = cf_chosen(x);
    k = 0;
    for (o = x.f; o; o = o.n) if (o.y == 1) {
      href = cf_pair(o.a, "value");
      cf_w("<option");
      if (href != null) cf_w(" value=\"" + cf_esc(href) + "\"");
      if (k == n) cf_w(" selected");
      cf_w(">" + cf_esc(cf_textOf(o)));
      k++;
    }
    cf_w("</select>");
  } else if (t == "a") {
    href = cf_pair(x.a, "href");
    n = cf_handle(x);
    if (inh & 1 << cf_CLICK) href = "javascript:parent.cdomF.k(" + n + ")";
    cf_w("<a");
    if (href != null) cf_w(" href=\"" + cf_esc(href) + "\"");
    if (inh & 1 << cf_OVER) cf_w(cf_on_("onmouseover", "o", n, ",1"));
    if (inh & 1 << cf_OUT) cf_w(cf_on_("onmouseout", "o", n, ",0"));
    cf_w(cf_attrs(x, "href") + ">" + cf_inner);
    cf_kids(x, inh, ctx | 1);
    cf_w(cf_innerEnd + "</a>");
  } else {
    if (cf_in(cf_DIV, t)) t = "div";
    cf_w("<" + t + cf_attrs(x, "") + ">");
    if (!cf_in(cf_VOID, t)) {
      cf_w(cf_inner);
      cf_kids(x, inh, ctx);
      cf_w(cf_innerEnd + "</" + t + ">");
    }
  }
  cf_w(s);
}
/* The frame's whole document. Every control is inside the one FORM, which Netscape needs to show one at all. */
function cf_html() {
  var bg = cf_bg(cf_mount);
  cf_serial++;
  cf_out = new Array();
  cf_on = 0;
  cf_hit = new Array();
  cf_nhit = 0;
  cf_fields = new Array();
  cf_nfields = 0;
  cf_w("<html><head><title>" + cf_esc(cf_title) + "</title></head><body");
  if (bg != null) cf_w(" bgcolor=\"" + cf_esc(bg) + "\"");
  cf_w("><form name=\"cdom\" onsubmit=\"return false\">");
  cf_kids(cf_mount, 0, 0);
  cf_w("</form></body></html>");
  return cf_out.join("");
}

/* The handlers (cdomF.k and the rest): each is an entry from the browser, which delivers its events to the nearest
 * listening ancestor with the path down, as shim/cdom.js's delegate does, and then draws once. A handler run by the
 * shim's own draw (a focus given back) does nothing. */
function cf_post(type, id, path, depth, key, mods) {
  cf_store32(cf_evType, type);
  cf_store32(cf_evId, id);
  cf_store32(cf_evX, 0);
  cf_store32(cf_evY, 0);
  cf_store32(cf_evButton, 0);
  cf_store32(cf_evKey, key);
  cf_store32(cf_evMods, mods);
  cf_store32(cf_evPath, path);
  cf_store32(cf_evDepth, depth);
  cf_store32(cf_evCh, 0);
  cf_store32(cf_evMs, 0);
  cf_store32(cf_evPointer, 0);
  cf_store32(cf_evButtons, 0);
  cf_store32(cf_evDx, 0);
  cf_store32(cf_evDy, 0);
  cf_cRun("cd_event");
}
function cf_fire(target, type, key) {
  var path = 0, depth = 0, x = target, s, index, mods = 0;
  if (target.t == "input" || target.t == "textarea" || target.t == "select") mods = 16;
  while (x && x != cf_mount) {
    if (x.i >= 0 && cf_mask[x.i * 2 + (type >> 5)] & 1 << (type & 31)) {
      cf_post(type, x.i, path, depth, key, mods);
      return;
    }
    index = 0;
    for (s = x.v; s; s = s.v) if (s.y == 1) index++;
    path = path << 8 | index & 255;
    depth++;
    x = x.p;
  }
}
/* An input event when the field's value is not the one C last heard of. */
function cf_input(x) {
  var now = cf_fieldValue(x);
  if (x.t == "select") now = "" + cf_chosen(x);
  if (now != x.seen) {
    x.seen = now;
    cf_fire(x, cf_INPUT, 0);
  }
}
function cf_key(e) {
  if (!e) return 0;
  if (e.keyCode) return e.keyCode;
  if (e.which) return e.which;
  return 0;
}
function cf_onClick(n) {
  var x = cf_hit[n], t;
  if (cf_drawing || !x) return;
  cf_hold(1);
  cf_fire(x, cf_CLICK, 0);
  if (x.t == "input") {
    t = cf_type(x);
    if (t == "checkbox" || t == "radio") {
      x.seen = null;
      cf_fire(x, cf_INPUT, 0);
      cf_fire(x, cf_CHANGE, 0);
    }
  }
  cf_release(0);
}
function cf_onChange(n) {
  var x = cf_hit[n];
  if (cf_drawing || !x) return;
  cf_hold(1);
  cf_input(x);
  cf_fire(x, cf_CHANGE, 0);
  cf_release(x.t != "select");
}
function cf_onKeyUp(n, e) {
  var x = cf_hit[n];
  if (cf_drawing || !x) return;
  cf_hold(1);
  cf_input(x);
  cf_fire(x, cf_KEYUP, cf_key(e));
  cf_release(2 * cf_blind(x));
}
function cf_onKeyDown(n, e) {
  var x = cf_hit[n];
  if (cf_drawing || !x) return;
  cf_hold(1);
  cf_fire(x, cf_KEYDOWN, cf_key(e));
  cf_release(2 * cf_blind(x));
}
function cf_onFocus(n, on) {
  var x = cf_hit[n];
  if (cf_drawing || !x) return;
  cf_hold(1);
  if (on) {
    cf_focus = x;
    cf_fire(x, cf_FOCUS, 0);
    cf_fire(x, cf_FOCUSIN, 0);
  } else {
    if (cf_focus == x) cf_focus = null;
    cf_fire(x, cf_BLUR, 0);
    cf_fire(x, cf_FOCUSOUT, 0);
  }
  cf_release(1);
}
function cf_onOver(n, on) {
  var x = cf_hit[n];
  if (cf_drawing || !x) return;
  cf_hold(1);
  if (on) cf_fire(x, cf_OVER, 0);
  else cf_fire(x, cf_OUT, 0);
  cf_release(0);
}
/* The frame document's mouse button, down and up. Netscape 4 captures the two at the document (captureEvents), and
 * routes each on to its target. Up: the click, if there is one, comes next and draws; a draw that waited is drawn after
 * it, with no wait of its own. */
function cf_route(e) {
  var w = cf_view();
  if (e && w && w.document.routeEvent) return w.document.routeEvent(e);
  return true;
}
function cf_onDown(e) {
  cf_pressed = 1;
  cf_pressWaits = 0;
  return cf_route(e);
}
function cf_onUp(e) {
  cf_pressed = 0;
  if (cf_later) setTimeout("cdomF.p()", 0);
  return cf_route(e);
}
function cf_onTimer(n) {
  var token = cf_tok[n];
  if (token == null) return;
  cf_tok[n] = null;
  cf_hold(1);
  cf_cRun("cd_timer", token, n);
  cf_release(0);
}
function cf_onRaf(token) {
  cf_hold(1);
  cf_cRun("cd_raf", token, cf_op_now());
  cf_release(0);
}
/* Waits for the frame, and draws what waited: a draw before the frame was there, or one put off by cf_LATER. A
 * frameset the parser ignored leaves no frame once the page has loaded (or, with no readyState, after 50 waits): the
 * frameset then goes over the page. */
var cf_startDone = 1;
function cf_poll() {
  if (!cf_startDone) {
    cf_startDone = 1;
    cf_fail("cdom: cd_start did not return (a trap in cd_main?)");
  }
  if (cf_drawing) return;
  if (!cf_view()) {
    cf_waits++;
    if (!cf_over && (document.readyState == "complete" || (!document.readyState && cf_waits > 50))) {
      cf_over = 1;
      document.open("text/html", "replace");
      document.write(cf_FRAMESET);
      document.close();
    }
    setTimeout("cdomF.p()", 20);
    return;
  }
  if (cf_later && cf_pressed && cf_pressWaits < cf_PRESSCAP) {
    cf_pressWaits++;
    setTimeout("cdomF.p()", cf_LATER);
    return;
  }
  cf_pressWaits = 0;
  cf_hold(1);
  cf_later = 0;
  cf_release(0);
}
cdomF.k = cf_onClick;
cdomF.c = cf_onChange;
cdomF.u = cf_onKeyUp;
cdomF.d = cf_onKeyDown;
cdomF.f = cf_onFocus;
cdomF.o = cf_onOver;
cdomF.t = cf_onTimer;
cdomF.r = cf_onRaf;
cdomF.p = cf_poll;
cdomF.s = cf_settle;

/* The imports, by shim/cdom.js's names; what the tier lacks answers through cf_lacks, named. */
var cf_imports = new Object();
var cf_ops = new Object();
cf_imports.cdom = cf_ops;
cf_ops.ahead = cf_op_ahead;
cf_ops.at = cf_op_at;
cf_ops.open = cf_op_open;
cf_ops.close = cf_op_close;
cf_ops.text = cf_op_text;
cf_ops.text_num = cf_op_text_num;
cf_ops.attr = cf_op_attr;
cf_ops["class"] = cf_op_class;
cf_ops.name_el = cf_op_name_el;
cf_ops.name_text = cf_op_name_text;
cf_ops.name_open = cf_op_name_open;
cf_ops.listen = cf_op_listen;
cf_ops.unlisten = cf_op_unlisten;
cf_ops.value = cf_op_value;
cf_ops.value_num = cf_op_value_num;
cf_ops.tpl_save = cf_op_tpl_save;
cf_ops.clone = cf_op_clone;
cf_ops.slot = cf_op_slot;
cf_ops.tpl_text = cf_op_tpl_text;
cf_ops.tpl_text_num = cf_op_tpl_text_num;
cf_ops.walk = cf_op_walk;
cf_ops.walk_value = cf_op_walk_value;
cf_ops.walk_value_num = cf_op_walk_value_num;
cf_ops.set_text = cf_op_set_text;
cf_ops.set_text_num = cf_op_set_text_num;
cf_ops.set_attr = cf_op_set_attr;
cf_ops.set_class = cf_op_set_class;
cf_ops.style = cf_op_style;
cf_ops.style_num = cf_op_style_num;
cf_ops.remove = cf_op_remove;
cf_ops.free = cf_op_free;
cf_ops.move = cf_op_move;
cf_ops.clear = cf_op_clear;
cf_ops.detach = cf_op_detach;
cf_ops.attach = cf_op_attach;
cf_ops.hit = cf_op_hit;
cf_ops.viewport = cf_op_viewport;
cf_ops.rect = cf_op_rect;
cf_ops.set = cf_op_set;
cf_ops["call"] = cf_op_call; /* quoted: the gate refuses .call, Function's (JavaScript 1.3) */
cf_ops.get = cf_op_get;
cf_ops.timer = cf_op_timer;
cf_ops.timer_stop = cf_op_timer_stop;
cf_ops.raf = cf_op_raf;
cf_ops.now = cf_op_now;
cf_ops.epoch = cf_op_epoch;
cf_ops.origin = cf_op_origin;
cf_ops.url_read = cf_op_url_read;
cf_ops.url_step = cf_op_url_step;
cf_ops.title = cf_op_title;
var cf_LACKS = "encode image_bytes blit ws_open ws_send ws_close ws_buffered http file_read file_open file_next file_close job_run job_stop image_call image_throw store_put store_get store_del audio_load audio_free audio_play audio_stop audio_gain audio_master audio_state random url_go language prefs clipboard_write share fonts_wait".split(" ");
for (cf_k = 0; cf_k < cf_LACKS.length; cf_k++) cf_ops[cf_LACKS[cf_k]] = new Function("return cf_lacks(\"" + cf_LACKS[cf_k] + "\")");

/* cd_seed: options.seed when the page fixes one, else 0, noted in cdomF.errors. None of these browsers has a
 * cryptographic generator, and cdom's one exception to its seed rule (the engine's generator and the clock, shim/cdom.js's seed())
 * is approved for IE 9 and 10 alone (tools/seeds.mjs holds it there), so this tier does not draw one itself: a page
 * that wants a seed per visit passes one, as a server can. */
function cf_seed(options) {
  if (options && typeof options.seed == "number") return options.seed >>> 0;
  cf_fail("cdom: no options.seed, and no cryptographic generator here: cd_seed is 0");
  return 0;
}
/* The start, which the boot's written script calls once both files have run: factory is the app's (window.cdomFactory),
 * the rest is what cdomBoot was given. The mount is the frame's body, whatever was named. */
function cdomFramesStart(factory, mount, env, ready, options) {
  var table = window.cdomLits, x;
  window.cdomFactory = null;
  if (!factory) {
    cf_fail("cdom: the app's file did not load");
    return;
  }
  if (!table) {
    cf_fail("cdom: this page has no string table (build it with cdom.mk or tools/cdpage, which write it before the shim)");
    return;
  }
  if (cf_hasFrom && table[2] != String.fromCharCode(9731)) {
    cf_fail("cdom: this page was not read as UTF-8");
    return;
  }
  if (env) cf_imports.env = env;
  else cf_imports.env = new Object();
  cf_raw = factory(cf_imports);
  cf_mem = cf_raw.memory;
  cf_W = cf_mem.words;
  if (!cf_raw.cd_lits || cf_raw.cd_lits() >>> 0 != table[1] >>> 0) {
    cf_fail("cdom: the page's string table is not this module's");
    return;
  }
  cf_lits = table[3].split(cf_T[0]);
  cf_litBase = table[0];
  cf_litEnd = 0;
  if (cf_litBase) cf_litEnd = cf_litBase + 4 * cf_lits.length;
  if (!cf_readLayout(cf_raw.cd_layout() >> 2)) {
    cf_fail("cdom: this shim and the module's cdom.c lay out C memory differently");
    return;
  }
  cf_nodes[0] = cf_mount;
  cf_nodes[1] = window;
  cf_nodes[2] = cf_doc;
  cf_nodes[3] = cf_mount;
  cf_started = 1;
  document.write(cf_FRAMESET);
  x = cf_wrap(cf_raw);
  /* Set before the start, so a trap in it (which throws out of here) still has the frame drawn, or the frameset
   * written over the page, by cf_poll, which notes the trap. */
  setTimeout("cdomF.p()", 0);
  cf_startDone = 0;
  cf_exp("cd_start", cf_seed(options));
  cf_startDone = 1;
  if (ready) ready(x);
}
