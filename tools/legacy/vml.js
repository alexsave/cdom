/* SVG drawn through VML on the fourth tier (IE 5 to 8: docs/vml-design.md). dom-old.js's createElementNS hands an SVG
 * tag here where the document has no createElementNS, and its setAttribute hands here every attribute set on a node
 * made here, so C's SVG becomes VML as the shim builds it, and later changes reach the VML node.
 * Every shape is a v:shape in a 1 px box with coordsize K by K, its path's points baked through the CTM (the svg's
 * viewBox, each g's and its own translate and scale) into pixels times K.
 * ES3 as JScript 5.0 reads it: no binary in, no number as an object key, no replace with a function, no toFixed. */
var cdomVml = (function (w, doc) {
  var K = 100;                       /* VML path units per pixel: VML takes whole numbers only */
  var URN = "urn:schemas-microsoft-com:vml";
  var TAGS = ["shape", "fill", "stroke"];  /* every v: tag made here (make), each with its behavior rule */
  var SHAPE = { path: 1, rect: 1, circle: 1, ellipse: 1, line: 1, polyline: 1, polygon: 1 };
  /* Inherited from the nearest svg or g that sets them, as SVG's presentation attributes are. */
  var PAINT = { fill: 1, stroke: 1, "stroke-width": 1, "fill-opacity": 1, "stroke-opacity": 1, "font-family": 1,
    "font-size": 1, "font-style": 1, "font-weight": 1, "text-anchor": 1 };
  var PAINTS = ["fill", "stroke", "stroke-width", "fill-opacity", "stroke-opacity", "font-family", "font-size",
    "font-style", "font-weight", "text-anchor"];
  var ASCENTS = {};                  /* ascent(): a font's baseline below the top of its line at 100 px, by font */
  var COLOURS = {};                  /* colour()'s answers, each worked out once ("k" and the value: no prototype names) */
  /* Attributes that hide their node while set: what VML cannot draw. */
  var HIDES = { "clip-path": 1, mask: 1, filter: 1, "marker-start": 1, "marker-mid": 1, "marker-end": 1 };
  var NAMED = /^[a-z]+$/;
  var ready = false;
  var held = [];                     /* shapes changed during this call into C, drawn once when it ends (drawHeld) */
  var loose = [];                    /* svgs sized in a unit while out of the page, measured again when the call ends */
  var lost = [];                     /* the kinds of SVG met that are not drawn, each once */
  var seen = {};

  /* --- the path data, as a pure function of the string ---------------------------------------------------------- */

  /* The path's tokens: a command letter, a number ("1.5.5" is two, "1-2" too), or any other character, which ends the
   * path where it stands, as a malformed path is drawn up to its error. One match, not a character at a time: in IE 6
   * that loop was 17 ms a path (docs/vml-design.md, section 10). Capturing groups only: JScript 5.0 refuses (?:. */
  var TOKEN = /[A-Za-z]|[+\-]?(\d+\.?\d*|\.\d+)([eE][+\-]?\d+)?|[^\s,]/g;
  var NEED = { M: 2, L: 2, H: 1, V: 1, C: 6, S: 4, Q: 4, T: 2, A: 7, Z: 0 };
  /* The converter's state, kept here rather than in closures made each call: the output, the map (sx, sy, tx, ty),
   * the current point, the subpath's start, the last control point, whether the subpath is closed, and the tokens. */
  var out, msx, msy, mtx, mty, x, y, x0, y0, cx, cy, closed, toks, ti, tn;

  /* The point (px, py), mapped, as VML's two whole numbers: Math.round(v K) each, as floor(v K + 0.5) by truncation,
   * since in IE 6 a call costs as much as several operations (section 10); exact while it fits 32 bits (21 million
   * pixels), Math.floor past that. */
  function pt(px, py) {
    var a = (msx * px + mtx) * K + 0.5, b = (msy * py + mty) * K + 0.5, i, j;
    if (a < 2147483647 && a > -2147483647 && b < 2147483647 && b > -2147483647) {
      i = a | 0;
      j = b | 0;
      if (i > a) i--;
      if (j > b) j--;
      return i + "," + j;
    }
    return Math.floor(a) + "," + Math.floor(b);
  }
  function moveTo(px, py) {
    out[out.length] = "m" + pt(px, py);
    x = x0 = px;
    y = y0 = py;
    closed = false;
  }
  function lineTo(px, py) {
    if (closed) moveTo(x0, y0);
    out[out.length] = "l" + pt(px, py);
    x = px;
    y = py;
  }
  function curveTo(ax, ay, bx, by, px, py) {
    if (closed) moveTo(x0, y0);
    out[out.length] = "c" + pt(ax, ay) + "," + pt(bx, by) + "," + pt(px, py);
    x = px;
    y = py;
  }
  /* The number at the next token, or null. Letters and other characters are one-character tokens; so a token longer than
   * one character, or a digit, is a number. */
  function number() {
    if (ti >= tn) return null;
    var t = toks[ti], c = t.charAt(0);
    if (t.length < 2 && (c < "0" || c > "9")) return null;
    ti++;
    return parseFloat(t);
  }
  /* An arc's flag: one character, 0 or 1, which may run into what follows ("0110" is flags 0 and 1, then 10). */
  function flag() {
    if (ti >= tn) return null;
    var t = toks[ti], c = t.charAt(0);
    if (c !== "0" && c !== "1") return null;
    if (t.length < 2) ti++;
    else toks[ti] = t.substring(1);
    return c === "1" ? 1 : 0;
  }
  /* The path data d in VML's path language, each point (x, y) mapped to (sx x + tx, sy y + ty) pixels first.
   * A malformed path is drawn up to its error, as SVG does. */
  function path(d, sx, sy, tx, ty) {
    if (sx === void 0) {
      sx = 1;
      sy = 1;
      tx = 0;
      ty = 0;
    }
    var cmd = "", prev = "", t, v, args, need, k, rel, up, ox, oy;
    out = [];
    msx = sx;
    msy = sy;
    mtx = tx;
    mty = ty;
    x = y = x0 = y0 = cx = cy = 0;
    closed = false;
    toks = d.match(TOKEN) || [];
    ti = 0;
    tn = toks.length;
    while (ti < tn) {
      t = toks[ti];
      up = t.toUpperCase();
      if (t.length === 1 && NEED[up] !== void 0) {
        cmd = t;
        ti++;
      } else if (!cmd || cmd === "z" || cmd === "Z") {
        break;                                 /* a number with no command to repeat */
      }
      up = cmd.toUpperCase();
      rel = cmd !== up;
      if (!out.length && up !== "M") break;    /* a path starts with a move */
      need = NEED[up];
      args = [];
      for (k = 0; k < need; k++) {
        v = up === "A" && (k === 3 || k === 4) ? flag() : number();
        if (v === null) break;
        args[k] = v;
      }
      if (k < need) break;
      if (up === "Z") {
        if (!closed) out[out.length] = "x";
        closed = true;
        x = x0;
        y = y0;
        prev = "Z";
        continue;
      }
      ox = rel ? x : 0;
      oy = rel ? y : 0;
      if (up === "M") {
        moveTo(args[0] + ox, args[1] + oy);
        cmd = rel ? "l" : "L";                 /* pairs after a move are lines */
      } else if (up === "L") {
        lineTo(args[0] + ox, args[1] + oy);
      } else if (up === "H") {
        lineTo(args[0] + ox, y);
      } else if (up === "V") {
        lineTo(x, args[0] + oy);
      } else if (up === "C") {
        cx = args[2] + ox;
        cy = args[3] + oy;
        curveTo(args[0] + ox, args[1] + oy, cx, cy, args[4] + ox, args[5] + oy);
      } else if (up === "S") {
        var ax = prev === "C" || prev === "S" ? 2 * x - cx : x, ay = prev === "C" || prev === "S" ? 2 * y - cy : y;
        cx = args[0] + ox;
        cy = args[1] + oy;
        curveTo(ax, ay, cx, cy, args[2] + ox, args[3] + oy);
      } else if (up === "Q" || up === "T") {
        var qx, qy, px, py;
        if (up === "Q") {
          qx = args[0] + ox;
          qy = args[1] + oy;
          px = args[2] + ox;
          py = args[3] + oy;
        } else {
          qx = prev === "Q" || prev === "T" ? 2 * x - cx : x;
          qy = prev === "Q" || prev === "T" ? 2 * y - cy : y;
          px = args[0] + ox;
          py = args[1] + oy;
        }
        var sx0 = x, sy0 = y;
        cx = qx;
        cy = qy;
        curveTo(sx0 + 2 / 3 * (qx - sx0), sy0 + 2 / 3 * (qy - sy0), px + 2 / 3 * (qx - px), py + 2 / 3 * (qy - py), px, py);
      } else {
        arc(args[0], args[1], args[2], args[3], args[4], args[5] + ox, args[6] + oy);
      }
      prev = up;
    }
    if (!out.length) return "m0,0 e";
    out[out.length] = "e";
    return out.join(" ");
  }
  /* An elliptical arc from the current point to (x2, y2), as cubics of a quarter turn or less (the SVG
   * implementation notes: endpoint to centre, radii scaled up when too small). */
  function arc(rx, ry, deg, large, sweep, x2, y2) {
    var x1 = x, y1 = y;
    if (x1 === x2 && y1 === y2) return;
    rx = Math.abs(rx);
    ry = Math.abs(ry);
    if (!rx || !ry) {
      lineTo(x2, y2);
      return;
    }
    var phi = deg % 360 * Math.PI / 180, cos = Math.cos(phi), sin = Math.sin(phi);
    var dx = (x1 - x2) / 2, dy = (y1 - y2) / 2;
    var x1p = cos * dx + sin * dy, y1p = -sin * dx + cos * dy;
    var lam = x1p * x1p / (rx * rx) + y1p * y1p / (ry * ry);
    if (lam > 1) {
      lam = Math.sqrt(lam);
      rx *= lam;
      ry *= lam;
    }
    var num = rx * rx * ry * ry - rx * rx * y1p * y1p - ry * ry * x1p * x1p;
    var den = rx * rx * y1p * y1p + ry * ry * x1p * x1p;
    var co = Math.sqrt(Math.max(0, num / den)) * (large === sweep ? -1 : 1);
    var cxp = co * rx * y1p / ry, cyp = -co * ry * x1p / rx;
    var ecx = cos * cxp - sin * cyp + (x1 + x2) / 2, ecy = sin * cxp + cos * cyp + (y1 + y2) / 2;
    var t1 = Math.atan2((y1p - cyp) / ry, (x1p - cxp) / rx);
    var dt = Math.atan2((-y1p - cyp) / ry, (-x1p - cxp) / rx) - t1;
    if (sweep && dt < 0) dt += 2 * Math.PI;
    else if (!sweep && dt > 0) dt -= 2 * Math.PI;
    var segs = Math.max(1, Math.ceil(Math.abs(dt) / (Math.PI / 2) - 1e-9)), step = dt / segs;
    var h = 4 / 3 * Math.tan(step / 4), a = t1, s;
    function onX(u, v) { return ecx + rx * cos * u - ry * sin * v; }
    function onY(u, v) { return ecy + rx * sin * u + ry * cos * v; }
    for (s = 0; s < segs; s++) {
      var b = a + step, ca = Math.cos(a), sa = Math.sin(a), cb = Math.cos(b), sb = Math.sin(b);
      var u1 = ca - h * sa, v1 = sa + h * ca, u2 = cb + h * sb, v2 = sb - h * cb;
      var last = s === segs - 1;
      curveTo(onX(u1, v1), onY(u1, v1), onX(u2, v2), onY(u2, v2), last ? x2 : onX(cb, sb), last ? y2 : onY(cb, sb));
      a = b;
    }
  }

  /* --- the attributes ----------------------------------------------------------------------------------------- */

  /* The numbers in a list ("1,2 3-4"), for points and viewBox: match, not an exec loop, since JScript 5.0's regular
   * expressions have no lastIndex. */
  function numbers(s) {
    var m = s.match(/[+\-]?(\d+\.?\d*|\.\d+)([eE][+\-]?\d+)?/g), out = [], j;
    for (j = 0; m && j < m.length; j++) out[j] = parseFloat(m[j]);
    return out;
  }
  function num(s, dflt) {
    var v = parseFloat(s);
    return isNaN(v) ? dflt : v;
  }
  /* The translate and scale of a transform list as [sx, sy, tx, ty], or the name of what VML cannot do. */
  function transform(s) {
    var m = [1, 1, 0, 0], parts = s.split(")"), j, f, name, a, sx, sy;
    for (j = 0; j < parts.length; j++) {
      f = parts[j].split("(");
      name = f[0].replace(/^[\s,]+|\s+$/g, "");
      if (f.length !== 2) {
        if (name) return "transform " + name;
        continue;
      }
      a = numbers(f[1]);
      if (name === "translate" && a.length) {
        m[2] += m[0] * a[0];
        m[3] += m[1] * (a.length > 1 ? a[1] : 0);
      } else if (name === "scale" && a.length) {
        sx = a[0];
        sy = a.length > 1 ? a[1] : sx;
        if (sx <= 0 || sy <= 0) return "transform scale (zero or negative)";
        m[0] *= sx;
        m[1] *= sy;
      } else {
        return "transform " + name;
      }
    }
    return m;
  }
  /* A colour VML takes, "" for none, or null for a paint it cannot draw. */
  function colour(s) {
    var c = COLOURS["k" + s];
    if (c === void 0) c = COLOURS["k" + s] = colourOf(s);
    return c;
  }
  function colourOf(s) {
    var v = s.toLowerCase(), hex = "0123456789abcdef", rgb, j, out;
    if (v === "none" || v === "transparent") return "";
    if (/^#[0-9a-f]{3}$/.test(v)) return "#" + v.charAt(1) + v.charAt(1) + v.charAt(2) + v.charAt(2) + v.charAt(3) + v.charAt(3);
    if (/^#[0-9a-f]{6}$/.test(v)) return v;
    if (/^rgb\(/.test(v)) {
      rgb = numbers(v);
      if (rgb.length !== 3) return null;
      out = "#";
      for (j = 0; j < 3; j++) {
        var b = Math.max(0, Math.min(255, Math.round(/%/.test(v) ? rgb[j] * 2.55 : rgb[j])));
        out += hex.charAt(b >> 4) + hex.charAt(b & 15);
      }
      return out;
    }
    if (NAMED.test(v) && v !== "currentcolor" && v !== "inherit") return v;
    return null;
  }

  /* --- reporting and setup ------------------------------------------------------------------------------------ */

  /* What is not drawn, reported once per kind through the shim's report path (window.reportError, which the polyfill
   * makes reach window.onerror), never thrown. */
  function report(kind) {
    if (seen.hasOwnProperty(kind)) return;
    seen[kind] = 1;
    lost[lost.length] = kind;
    var m = "cdom: SVG " + kind + " is not drawn in VML (IE 5 to 8: docs/vml-design.md)";
    var e = typeof w.cdomError === "function" ? w.cdomError(m) : new Error();
    e.message = m;
    if (typeof w.reportError === "function") w.reportError(e);
  }
  /* Measured in IE 6 (docs/vml-design.md, section 9): the namespace with two arguments, before any v: node is made,
   * and the behavior from a stylesheet rule. The three-argument add (an implementation, "#default#VML") binds the
   * behavior as the node is made, and then every property set by script is lost: the fill, the path, and coordsize,
   * which stays beside the default 1000,1000. One rule a tag, since IE 8's standards mode ignores v\:*. */
  function setup() {
    ready = true;
    var ns = doc.namespaces, ss, j;
    if (ns && ns.add) {
      try { ns.add("v", URN); } catch (caughtVml) {}
    }
    try {
      if (doc.createStyleSheet) {
        ss = doc.createStyleSheet();
        for (j = 0; j < TAGS.length; j++) ss.addRule("v\\:" + TAGS[j], "behavior:url(#default#VML)");
      }
    } catch (caughtVml2) {}
  }
  function make(tag) {
    if (!ready) setup();
    return doc.createElement("v:" + tag);
  }

  /* --- the nodes ---------------------------------------------------------------------------------------------- */

  /* The node for the SVG element tag. Its state is node.$vml: kind (0 an svg, 1 a g, 2 a shape, 3 not drawn, 4 a text), the
   * SVG attributes set on it (a), what hides it (hide), and for a shape its v:fill and v:stroke and the last path key.
   * The svg and a g are spans, not v:group: IE 6 measures a v:group by what it holds, not by its width and height, and
   * does not clip to it, where a span with overflow hidden is the svg's box and clips as an svg does (docs/vml-design.md,
   * section 9). Every point is baked through the CTM, so a g needs no frame of its own, only a place to hold its shapes
   * at the svg's top left. */
  function create(tag) {
    var el, st;
    if (tag === "svg" || tag === "g") {
      el = doc.createElement("span");
      st = { kind: tag === "svg" ? 0 : 1, tag: tag, a: {}, hide: {} };
      el.$vml = st;
      if (tag === "g") el.style.cssText = "position:absolute;left:0;top:0";
      else frame(el);
      return el;
    }
    if (SHAPE[tag] === 1) {
      el = make("shape");
      st = { kind: 2, tag: tag, a: {}, hide: {}, key: "" };
      el.$vml = st;
      el.style.cssText = "position:absolute;left:0;top:0;width:1px;height:1px";
      el.coordsize = K + "," + K;
      el.coordorigin = "0,0";
      el.path = "m0,0 e";
      st.vf = make("fill");
      st.vs = make("stroke");
      st.vs.joinstyle = "miter";     /* SVG's defaults; VML's are a round join and a round cap */
      st.vs.miterlimit = "4";
      st.vs.endcap = "flat";
      el.appendChild(st.vf);
      el.appendChild(st.vs);
      return el;
    }
    if (tag === "text") {
      /* An HTML span at the text's baseline, in its font scaled by the CTM: VML's own text (v:textpath) stretches the
       * text along a path, and a span draws it as the browser draws text. */
      el = doc.createElement("span");
      el.$vml = { kind: 4, tag: tag, a: {}, hide: {}, key: "" };
      el.style.cssText = "position:absolute;left:0;top:0;white-space:nowrap;line-height:normal";
      return el;
    }
    report("<" + tag + ">");
    el = doc.createElement("span");
    el.style.display = "none";
    el.$vml = { kind: 3, tag: tag, a: {}, hide: {} };
    return el;
  }
  /* Whether svg el is the outermost: one in another svg (or in a g) is a viewport inside it, placed by its x and y. */
  function outermost(el) {
    var p = el.parentNode;
    return !(p && p.$vml);
  }
  /* A length of the outermost svg as CSS: a plain number is pixels, and a unit (em, %, ...) is left to CSS. */
  function cssLength(v, dflt) {
    if (v === void 0 || /^\s*[+\-]?(\d+\.?\d*|\.\d+)(px)?\s*$/.test(v)) return Math.round(num(v, dflt)) + "px";
    return v;
  }
  /* The svg's box, clipping what it holds. The outermost: its size (300 by 150 when it says none, as a browser's), in
   * the page's flow. One inside: at its x and y and of its width and height, in the units of what holds it (ctx). */
  function frame(el, ctx) {
    var a = el.$vml.a, s = el.style, m;
    s.overflow = "hidden";
    if (!ctx) {
      s.position = "relative";
      s.width = cssLength(a.width, 300);
      s.height = cssLength(a.height, 150);
    } else {
      m = ctx.m;
      s.position = "absolute";
      s.left = Math.round(m[0] * num(a.x, 0) + m[2]) + "px";
      s.top = Math.round(m[1] * num(a.y, 0) + m[3]) + "px";
      s.width = Math.round(m[0] * num(a.width, 0)) + "px";
      s.height = Math.round(m[1] * num(a.height, 0)) + "px";
    }
    hidden(el);
  }
  /* An outermost svg's size in pixels: as it says, or, given in a unit, as CSS laid it out (em: the formulas of a
   * LaTeX page), or 16 px an em while it is out of the page. */
  function size(el, v, dflt, px) {
    if (v === void 0 || !/[a-df-z%]\s*$/i.test(v) || /px\s*$/.test(v)) return num(v, dflt);
    if (px > 0) return px;
    if (!el.$vml.loose) {
      el.$vml.loose = true;
      loose[loose.length] = el;
    }
    return num(v, dflt) * (/em\s*$/.test(v) ? 16 : 1);
  }
  /* The map of an svg's viewBox into its box of W by H: [sx, sy, tx, ty], by preserveAspectRatio (xMidYMid meet when it
   * says none). */
  function viewMap(a, W, H) {
    var vb = a.viewBox ? numbers(a.viewBox) : [], par = a.preserveAspectRatio || "", sx, sy, s, ax, ay;
    if (vb.length !== 4 || vb[2] <= 0 || vb[3] <= 0) return [1, 1, 0, 0];
    sx = W / vb[2];
    sy = H / vb[3];
    if (/^\s*none/.test(par)) return [sx, sy, -vb[0] * sx, -vb[1] * sy];
    s = /slice/.test(par) ? Math.max(sx, sy) : Math.min(sx, sy);
    ax = /xMin/.test(par) ? 0 : /xMax/.test(par) ? 1 : 0.5;
    ay = /YMin/.test(par) ? 0 : /YMax/.test(par) ? 1 : 0.5;
    return [s, s, (W - vb[2] * s) * ax - vb[0] * s, (H - vb[3] * s) * ay - vb[1] * s];
  }
  /* What a node's children see: the CTM, the inherited paint, and the opacity so far. An svg or g keeps it (inner),
   * set whenever it is drawn again (redraw), which every change to it or to what it is in, and every move of it, does:
   * so a shape's attribute does not walk up to the svg again (docs/vml-design.md, section 10). */
  function context(el) {
    var chain = [], p = el, ctx, i;
    if (el && el.$vml && el.$vml.inner) return el.$vml.inner;
    while (p && p.$vml && p.$vml.kind !== 2) {
      chain[chain.length] = p;
      if (p.$vml.kind === 0 && outermost(p)) break;
      p = p.parentNode;
    }
    ctx = { m: [1, 1, 0, 0], paint: {}, opacity: 1 };
    for (i = chain.length - 1; i >= 0; i--) ctx = chain[i].$vml.inner = within(chain[i], ctx);
    return ctx;
  }
  /* ctx as it is inside node el. */
  function within(el, ctx) {
    var st = el.$vml, a = st.a, m = ctx.m, own, k, j, paint = ctx.paint, copied = false;
    for (j = 0; j < PAINTS.length; j++) {
      if (a[PAINTS[j]] === void 0) continue;
      if (!copied) {
        paint = {};
        for (k in ctx.paint) {
          if (ctx.paint.hasOwnProperty(k)) paint[k] = ctx.paint[k];
        }
        copied = true;                          /* a paint dictionary is never changed once made: shared until then */
      }
      paint[PAINTS[j]] = a[PAINTS[j]];
    }
    if (st.kind === 0 && outermost(el)) {
      m = viewMap(a, size(el, a.width, 300, el.offsetWidth), size(el, a.height, 150, el.offsetHeight));
    } else if (st.kind === 0) {
      /* inside another svg: the box is a span at its x and y (frame), so what it holds is drawn from that corner */
      own = viewMap(a, num(a.width, 0), num(a.height, 0));
      m = [m[0] * own[0], m[1] * own[1], m[0] * own[2], m[1] * own[3]];
    } else if (a.transform && typeof st.tm !== "string") {
      own = st.tm;
      m = [m[0] * own[0], m[1] * own[1], m[0] * own[2] + m[2], m[1] * own[3] + m[3]];
    }
    return { m: m, paint: paint, opacity: ctx.opacity * num(a.opacity, 1) };
  }
  /* The SVG path of a shape's geometry. */
  function geometry(st) {
    var a = st.a, t = st.tag, x, y, wd, ht, rx, ry, p, j, s;
    if (t === "path") return a.d || "";
    if (t === "rect") {
      x = num(a.x, 0);
      y = num(a.y, 0);
      wd = num(a.width, 0);
      ht = num(a.height, 0);
      if (wd <= 0 || ht <= 0) return "";
      rx = num(a.rx, -1);
      ry = num(a.ry, -1);
      if (rx < 0) rx = ry < 0 ? 0 : ry;
      if (ry < 0) ry = rx;
      rx = Math.min(rx, wd / 2);
      ry = Math.min(ry, ht / 2);
      if (!rx || !ry) return "M" + x + " " + y + "H" + (x + wd) + "V" + (y + ht) + "H" + x + "Z";
      return "M" + (x + rx) + " " + y + "H" + (x + wd - rx) + "A" + rx + " " + ry + " 0 0 1 " + (x + wd) + " " + (y + ry) +
        "V" + (y + ht - ry) + "A" + rx + " " + ry + " 0 0 1 " + (x + wd - rx) + " " + (y + ht) + "H" + (x + rx) +
        "A" + rx + " " + ry + " 0 0 1 " + x + " " + (y + ht - ry) + "V" + (y + ry) + "A" + rx + " " + ry + " 0 0 1 " + (x + rx) + " " + y + "Z";
    }
    if (t === "circle" || t === "ellipse") {
      x = num(a.cx, 0);
      y = num(a.cy, 0);
      rx = t === "circle" ? num(a.r, 0) : num(a.rx, 0);
      ry = t === "circle" ? rx : num(a.ry, 0);
      if (rx <= 0 || ry <= 0) return "";
      return "M" + (x - rx) + " " + y + "A" + rx + " " + ry + " 0 1 0 " + (x + rx) + " " + y +
        "A" + rx + " " + ry + " 0 1 0 " + (x - rx) + " " + y + "Z";
    }
    if (t === "line") return "M" + num(a.x1, 0) + " " + num(a.y1, 0) + "L" + num(a.x2, 0) + " " + num(a.y2, 0);
    p = numbers(a.points || "");
    if (p.length < 2) return "";
    s = "M" + p[0] + " " + p[1];
    for (j = 2; j + 1 < p.length; j += 2) s += "L" + p[j] + " " + p[j + 1];
    return t === "polygon" ? s + "Z" : s;
  }
  /* A paint's VML colour, or "" for none (reported when it is one VML cannot draw). currentColor is the CSS color of el,
   * read when it is drawn (IE's currentStyle; getComputedStyle elsewhere): VML has no keyword for it. */
  function paintOf(v, el) {
    var c, cs;
    if (v.toLowerCase() === "currentcolor") {
      cs = el.currentStyle || (w.getComputedStyle ? w.getComputedStyle(el, null) : null);
      v = cs && cs.color ? cs.color : v;
    }
    c = colour(v);
    if (c === null) {
      report("paint " + v.split("(")[0]);
      return "";
    }
    return c;
  }
  /* Draws shape el from its attributes and what its parents give it. */
  function draw(el, ctx) {
    var st = el.$vml, own = within(el, ctx), m = own.m, paint = own.paint, g = geometry(st);
    var key = g + "|" + m.join(",");
    if (key !== st.key) {
      st.key = key;
      el.path = g ? path(g, m[0], m[1], m[2], m[3]) : "m0,0 e";
    }
    var fill = st.tag === "line" ? "" : paintOf(paint["fill"] !== void 0 ? paint["fill"] : "black", el);
    var stroke = paintOf(paint.stroke !== void 0 ? paint.stroke : "none", el);
    var weight = num(paint["stroke-width"], 1) * Math.sqrt(m[0] * m[1]);
    var w = Math.round(weight * 100) / 100 + "px";
    var fo = String(Math.round(own.opacity * num(paint["fill-opacity"], 1) * 1000) / 1000);
    var so = String(Math.round(own.opacity * num(paint["stroke-opacity"], 1) * 1000) / 1000);
    /* Each VML property set only when it changes: a property set on a VML node is a call into IE (section 10). */
    if (fill !== st.f) {
      st.f = fill;
      el.filled = fill !== "";
      if (fill) el.fillcolor = fill;
    }
    if (stroke !== st.s) {
      st.s = stroke;
      el.stroked = stroke !== "";
      if (stroke) el.strokecolor = stroke;
    }
    if (w !== st.w) el.strokeweight = st.w = w;
    if (fo !== st.fo) st.vf.opacity = st.fo = fo;
    if (so !== st.so) st.vs.opacity = st.so = so;
  }
  /* The distance from the top of a line of text to its baseline, in pixels at a font size of 100 px, in the font that
   * style, weight and family name: an image of height 1 on the baseline, in a span of that font out of view, whose
   * top is the line's. Measured once a font, while the page has a body; 80 until then. */
  function ascent(style, weight, family) {
    var key = "k" + style + "|" + weight + "|" + family, c = ASCENTS[key], box, img, body = doc.body;
    if (c !== void 0) return c;
    if (!body) return 80;
    box = doc.createElement("span");
    box.style.cssText = "position:absolute;left:-10000px;top:0;visibility:hidden;white-space:nowrap;line-height:normal;font-size:100px";
    box.style.fontStyle = style;
    box.style.fontWeight = weight;
    box.style.fontFamily = family;
    box.appendChild(doc.createTextNode("x"));
    img = doc.createElement("img");
    img.width = 1;
    img.height = 1;
    img.style.verticalAlign = "baseline";
    box.appendChild(img);
    body.appendChild(box);
    c = img.offsetTop + img.offsetHeight;
    body.removeChild(box);
    if (c > 0) ASCENTS[key] = c;
    return c > 0 ? c : 80;
  }
  /* Draws text el: its x and y through the CTM, its font size by the CTM's vertical scale, its fill as the colour. */
  function drawText(el, ctx) {
    var st = el.$vml, own = within(el, ctx), m = own.m, paint = own.paint, a = st.a, s = el.style;
    var px = num(paint["font-size"], 16) * m[1], style = paint["font-style"] || "normal";
    var weight = paint["font-weight"] || "normal", family = paint["font-family"] || "serif";
    /* A family the browser lacks falls back to its default, serif in an SVG, not IE's default for HTML (sans here) */
    if (!/(^|,)\s*(serif|sans-serif|monospace|cursive|fantasy)\s*$/.test(family)) family += ", serif";
    var fill = paint["fill"] !== void 0 ? paint["fill"] : "black", anchor = paint["text-anchor"];
    var left = m[0] * num(a.x, 0) + m[2], top = m[1] * num(a.y, 0) + m[3] - ascent(style, weight, family) * px / 100;
    s.fontSize = Math.round(px * 100) / 100 + "px";
    s.fontStyle = style;
    s.fontWeight = weight;
    s.fontFamily = family;
    /* currentColor is the CSS color the span inherits */
    s.color = fill.toLowerCase() === "currentcolor" ? "" : paintOf(fill, el);
    s.visibility = fill === "none" ? "hidden" : "";
    if (anchor === "middle" || anchor === "end") left -= el.offsetWidth * (anchor === "end" ? 1 : 0.5);
    s.left = Math.round(left) + "px";
    s.top = Math.round(top) + "px";
  }
  /* Draws el and everything in it again, given what its parents give it. */
  function redraw(el, ctx) {
    var st = el.$vml, c, inner;
    if (st.kind === 2) {
      draw(el, ctx);
      return;
    }
    if (st.kind === 4) {
      drawText(el, ctx);
      return;
    }
    if (st.kind === 3) return;
    if (st.kind === 0 && outermost(el)) ctx = { m: [1, 1, 0, 0], paint: {}, opacity: 1 };
    if (st.kind === 0) frame(el, outermost(el) ? null : ctx);
    inner = st.inner = within(el, ctx);
    for (c = el.firstChild; c; c = c.nextSibling) {
      if (c.$vml) redraw(c, inner);
    }
  }
  /* el's state changed, or it moved: it and what it holds are drawn again from its parents' state. */
  function refresh(el) {
    var st = el.$vml;
    redraw(el, st.kind === 0 && outermost(el) ? null : context(el.parentNode));
  }
  /* Hides el while it has an attribute or transform VML cannot draw. */
  function hidden(el) {
    var st = el.$vml, k;
    for (k in st.hide) {
      if (st.hide.hasOwnProperty(k) && st.hide[k]) {
        el.style.display = "none";
        return;
      }
    }
    el.style.display = st.kind === 0 ? "inline-block" : "";
  }

  /* An SVG attribute set on a node made here (cd_attr while it is built, cd_set_attr after). */
  function attr(el, name, value) {
    var st = el.$vml, t;
    if (name === "id") {
      el.id = value;
      return;
    }
    if (st.kind === 3) return;
    st.a[name] = value;
    if (HIDES[name] === 1) {
      report("attribute " + name);
      st.hide[name] = value !== "" && value !== "none";
      hidden(el);
      return;
    }
    if (name === "transform") {
      t = st.tm = transform(value);
      if (typeof t === "string") report(t);
      st.hide.transform = typeof t === "string";
      hidden(el);
    }
    if (st.kind === 2 || st.kind === 4) hold(el);
    else if (name === "transform" || name === "opacity" || PAINT[name] === 1 || (st.kind === 0 && (name === "width" ||
        name === "height" || name === "viewBox" || name === "x" || name === "y" || name === "preserveAspectRatio"))) refresh(el);
  }
  /* el was put somewhere else (the shim's place: cd_move, cd_attach): what it inherits may have changed. */
  function placed(el) {
    var k = el.$vml.kind;
    if (k === 2 || k === 4) hold(el);
    else if (k !== 3) refresh(el);
  }
  /* A shape is drawn once when the call into C ends, not once for each of its attributes: its d, fill and stroke were
   * three draws, and in IE 6 a draw is some twenty calls (docs/vml-design.md, section 10). An svg's and a g's own
   * changes are drawn at once (the svg's size is its span's), and draw the shapes they hold then too. */
  function hold(el) {
    var st = el.$vml;
    if (st.held) return;
    st.held = true;
    held[held.length] = el;
  }
  /* The shim's drawHeld, at the end of the outermost call into C (tools/legacy/shim-old.js). */
  function drawHeld() {
    var list = loose, j;
    /* An svg sized in em is built out of the page (the shim inserts a subtree when its root closes): by now it is in,
     * so it is measured and drawn again, and with it what it holds. Once: one still out is measured when it is put in. */
    if (list.length) {
      loose = [];
      for (j = 0; j < list.length; j++) {
        list[j].$vml.loose = false;
        if (list[j].offsetWidth > 0) refresh(list[j]);
      }
    }
    list = held;
    if (!list.length) return;
    held = [];
    for (j = 0; j < list.length; j++) {
      list[j].$vml.held = false;
      refresh(list[j]);
    }
  }

  return { K: K, path: path, create: create, attr: attr, placed: placed, drawHeld: drawHeld, lost: lost };
})(window, document);
