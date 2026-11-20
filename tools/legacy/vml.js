/* SVG drawn through VML on the fourth tier (IE 5 to 8: docs/vml-design.md). dom-old.js's createElementNS hands an SVG
 * tag here where the document has no createElementNS, and its setAttribute hands here every attribute set on a node
 * made here, so C's SVG becomes VML as the shim builds it, and later changes reach the VML node.
 * Every shape is a v:shape in a 1 px box with coordsize K by K, its path's points baked through the CTM (the svg's
 * viewBox, each g's and its own translate and scale) into pixels times K.
 * ES3 as JScript 5.0 reads it: no binary in, no number as an object key, no replace with a function, no toFixed. */
var cdomVml = (function (w, doc) {
  var K = 100;                       /* VML path units per pixel: VML takes whole numbers only */
  var URN = "urn:schemas-microsoft-com:vml";
  var SHAPE = { path: 1, rect: 1, circle: 1, ellipse: 1, line: 1, polyline: 1, polygon: 1 };
  /* Inherited from the nearest svg or g that sets them, as SVG's presentation attributes are. */
  var PAINT = { fill: 1, stroke: 1, "stroke-width": 1, "fill-opacity": 1, "stroke-opacity": 1 };
  /* Attributes that hide their node while set: what VML cannot draw. */
  var HIDES = { "clip-path": 1, mask: 1, filter: 1, "marker-start": 1, "marker-mid": 1, "marker-end": 1 };
  var NAMED = /^[a-z]+$/;
  var ready = false;
  var lost = [];                     /* the kinds of SVG met that are not drawn, each once */
  var seen = {};

  /* --- the path data, as a pure function of the string ---------------------------------------------------------- */

  /* VML's whole number for x pixels. */
  function q(x) {
    return String(Math.round(x * K));
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
    var out = [], n = d.length, i = 0, cmd = "", x = 0, y = 0, x0 = 0, y0 = 0;
    var cx = 0, cy = 0, prev = "", closed = false, c, v, args, need, k, rel, up;
    var NEED = { M: 2, L: 2, H: 1, V: 1, C: 6, S: 4, Q: 4, T: 2, A: 7, Z: 0 };
    function pt(px, py) {
      return q(sx * px + tx) + "," + q(sy * py + ty);
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
    function skip() {
      for (; i < n; i++) {
        c = d.charAt(i);
        if (c !== " " && c !== "," && c !== "\t" && c !== "\n" && c !== "\r" && c !== "\f") break;
      }
    }
    /* One number at i, or null. "1.5.5" is two numbers, "1-2" too. */
    function number() {
      skip();
      var s = i, digits = 0;
      c = d.charAt(i);
      if (c === "+" || c === "-") i++;
      while (i < n && d.charAt(i) >= "0" && d.charAt(i) <= "9") { i++; digits++; }
      if (d.charAt(i) === ".") {
        i++;
        while (i < n && d.charAt(i) >= "0" && d.charAt(i) <= "9") { i++; digits++; }
      }
      if (!digits) {
        i = s;
        return null;
      }
      c = d.charAt(i);
      if (c === "e" || c === "E") {
        var e = i;
        i++;
        c = d.charAt(i);
        if (c === "+" || c === "-") i++;
        var ed = 0;
        while (i < n && d.charAt(i) >= "0" && d.charAt(i) <= "9") { i++; ed++; }
        if (!ed) i = e;
      }
      return parseFloat(d.substring(s, i));
    }
    /* An arc's flag: one character, 0 or 1. */
    function flag() {
      skip();
      c = d.charAt(i);
      if (c !== "0" && c !== "1") return null;
      i++;
      return c === "1" ? 1 : 0;
    }
    for (;;) {
      skip();
      if (i >= n) break;
      c = d.charAt(i);
      up = c.toUpperCase();
      if (NEED[up] !== void 0) {
        cmd = c;
        i++;
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
      var ox = rel ? x : 0, oy = rel ? y : 0;
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
    if (!out.length) return "m0,0 e";
    out[out.length] = "e";
    return out.join(" ");
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
  function setup() {
    ready = true;
    var ns = doc.namespaces;
    if (ns && ns.add) {
      try {
        ns.add("v", URN, "#default#VML");
      } catch (caughtVml) {
        try { ns.add("v", URN); } catch (caughtVml2) {}
      }
    }
    try {
      if (doc.createStyleSheet) doc.createStyleSheet().addRule("v\\:*", "behavior:url(#default#VML);display:inline-block");
    } catch (caughtVml3) {}
  }
  function make(tag) {
    if (!ready) setup();
    return doc.createElement("v:" + tag);
  }

  /* --- the nodes ---------------------------------------------------------------------------------------------- */

  /* The VML node for the SVG element tag. Its state is node.$vml: kind (0 the svg, 1 a g, 2 a shape, 3 not drawn), the
   * SVG attributes set on it (a), what hides it (hide), and for a shape its v:fill and v:stroke and the last path key. */
  function create(tag) {
    var el, st;
    if (tag === "svg" || tag === "g") {
      el = make("group");
      st = { kind: tag === "svg" ? 0 : 1, tag: tag, a: {}, hide: {} };
      el.$vml = st;
      if (tag === "g") {
        el.style.cssText = "position:absolute;left:0;top:0;width:1px;height:1px";
        el.coordsize = "1,1";
        el.coordorigin = "0,0";
      } else {
        frame(el);
      }
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
      el.appendChild(st.vf);
      el.appendChild(st.vs);
      return el;
    }
    report("<" + tag + ">");
    el = doc.createElement("span");
    el.style.display = "none";
    el.$vml = { kind: 3, tag: tag, a: {}, hide: {} };
    return el;
  }
  /* The svg's box: its size in pixels (300 by 150 when it says none, as a browser's), an identity frame. */
  function frame(el) {
    var a = el.$vml.a, W = Math.round(num(a.width, 300)), H = Math.round(num(a.height, 150)), s = el.style;
    s.position = "relative";
    s.width = W + "px";
    s.height = H + "px";
    el.coordsize = W + "," + H;
    el.coordorigin = "0,0";
    hidden(el);
  }
  /* The svg's CTM: its viewBox into its box, preserveAspectRatio's default (xMidYMid meet). */
  function rootMatrix(a) {
    var W = num(a.width, 300), H = num(a.height, 150), vb = a.viewBox ? numbers(a.viewBox) : [];
    if (vb.length !== 4 || vb[2] <= 0 || vb[3] <= 0) return [1, 1, 0, 0];
    var s = Math.min(W / vb[2], H / vb[3]);
    return [s, s, (W - vb[2] * s) / 2 - vb[0] * s, (H - vb[3] * s) / 2 - vb[1] * s];
  }
  /* What a node's children see: the CTM, the inherited paint, and the opacity so far. */
  function context(el) {
    var chain = [], p = el, ctx, i, st;
    while (p && p.$vml && p.$vml.kind !== 2) {
      chain[chain.length] = p;
      if (p.$vml.kind === 0) break;
      p = p.parentNode;
    }
    ctx = { m: [1, 1, 0, 0], paint: {}, opacity: 1 };
    for (i = chain.length - 1; i >= 0; i--) ctx = within(chain[i], ctx);
    return ctx;
  }
  /* ctx as it is inside node el. */
  function within(el, ctx) {
    var st = el.$vml, a = st.a, m = ctx.m, own, k, paint = {};
    for (k in ctx.paint) {
      if (ctx.paint.hasOwnProperty(k)) paint[k] = ctx.paint[k];
    }
    for (k in PAINT) {
      if (PAINT.hasOwnProperty(k) && a.hasOwnProperty(k)) paint[k] = a[k];
    }
    own = st.kind === 0 ? rootMatrix(a) : a.transform ? transform(a.transform) : null;
    if (own && typeof own !== "string") m = [m[0] * own[0], m[1] * own[1], m[0] * own[2] + m[2], m[1] * own[3] + m[3]];
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
  /* A paint's VML colour, or "" for none (reported when it is one VML cannot draw). */
  function paintOf(v) {
    var c = colour(v);
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
    var fill = st.tag === "line" ? "" : paintOf(paint.hasOwnProperty("fill") ? paint["fill"] : "black");
    var stroke = paintOf(paint.hasOwnProperty("stroke") ? paint.stroke : "none");
    el.filled = fill !== "";
    if (fill) el.fillcolor = fill;
    el.stroked = stroke !== "";
    if (stroke) el.strokecolor = stroke;
    var weight = num(paint["stroke-width"], 1) * Math.sqrt(m[0] * m[1]);
    el.strokeweight = Math.round(weight * 100) / 100 + "px";
    st.vf.opacity = String(Math.round(own.opacity * num(paint["fill-opacity"], 1) * 1000) / 1000);
    st.vs.opacity = String(Math.round(own.opacity * num(paint["stroke-opacity"], 1) * 1000) / 1000);
  }
  /* Draws el and everything in it again, given what its parents give it. */
  function redraw(el, ctx) {
    var st = el.$vml, c, inner;
    if (st.kind === 2) {
      draw(el, ctx);
      return;
    }
    if (st.kind === 3) return;
    if (st.kind === 0) frame(el);
    inner = within(el, st.kind === 0 ? { m: [1, 1, 0, 0], paint: {}, opacity: 1 } : ctx);
    for (c = el.firstChild; c; c = c.nextSibling) {
      if (c.$vml) redraw(c, inner);
    }
  }
  /* el's state changed, or it moved: it and what it holds are drawn again from its parents' state. */
  function refresh(el) {
    var st = el.$vml;
    redraw(el, st.kind === 0 ? null : context(el.parentNode));
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
      t = transform(value);
      if (typeof t === "string") report(t);
      st.hide.transform = typeof t === "string";
      hidden(el);
    }
    if (st.kind === 2 || name === "transform" || name === "opacity" || PAINT[name] === 1 ||
        (st.kind === 0 && (name === "width" || name === "height" || name === "viewBox"))) refresh(el);
  }
  /* el was put somewhere else (the shim's place: cd_move, cd_attach): what it inherits may have changed. */
  function placed(el) {
    if (el.$vml.kind !== 3) refresh(el);
  }

  return { K: K, path: path, create: create, attr: attr, placed: placed, lost: lost };
})(window, document);
