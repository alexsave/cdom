/* The old DOM for the fourth tier (IE 6, 7 and 8: docs/ie6-design.md). tools/legacy/build.mjs rewrites each DOM call
 * the shim makes that these browsers lack into a call of the function of the same job here (OLD_DOM in build.mjs is
 * the one table of them), and refuses cdom.legacy-ie6.js if one is left in the shim. Each uses the W3C call where the
 * object has it and the old one otherwise, so the tier also runs on an ES3 browser with the W3C DOM (Firefox 1.0, which
 * has its listeners but no pointer events, wheel event, Element Traversal or getBoundingClientRect).
 * ES3, as JScript 5.6 reads it: no reserved word as a property name, no trailing comma, no indexing into a string. */
var cdomOld = (function (w, doc) {
  var IE_BUTTON = { 1: 0, 4: 1, 2: 2 };  /* IE's button bits to the W3C button number */
  /* The type IE listens for: pointer events are mouse events, and the focus events that do not bubble are the
   * focusin and focusout that do (IE made them). DOMContentLoaded is the window's load, which comes later but
   * always comes. */
  var IE_TYPE = { pointerdown: "mousedown", pointerup: "mouseup", pointermove: "mousemove", pointerover: "mouseover",
    pointerout: "mouseout", wheel: "mousewheel", focus: "focusin", blur: "focusout", DOMContentLoaded: "load" };
  var POINTER = { pointerdown: 1, pointerup: 1, pointermove: 1, pointerover: 1, pointerout: 1 };
  /* Types IE does not bubble from a field to the mount; the mount hears them from each field, hooked when it gets
   * focus (focusin bubbles). IE has no input event: a field's value changing is its propertychange. */
  var FIELD = { change: 1, submit: 1, input: 1 };
  var bound = [];   /* [object, type, f, the handler given in its place, where, the type listened for] for off() */
  var BIT = { 0: 1, 1: 4, 2: 2 };  /* the W3C button number to its bit in buttons */
  var held = 0;                    /* the buttons held, where the browser's mouse events do not say (no buttons) */
  var tracking = false;

  function now() {
    return w.performance && w.performance.now ? w.performance.now() : new Date().getTime();
  }

  /* IE's event (the argument, or window.event) as the W3C event the shim reads: what post() and the drop read. */
  function shaped(e, node, type) {
    var b = e.button | 0, target = e.srcElement || node;
    var o = {
      type: type, target: target, currentTarget: node, srcEvent: e,
      clientX: e.clientX, clientY: e.clientY, keyCode: e.keyCode,
      shiftKey: !!e.shiftKey, ctrlKey: !!e.ctrlKey, altKey: !!e.altKey, metaKey: false,
      button: 0, buttons: 0, pointerId: 0, pointerType: "", isPrimary: false,
      deltaX: 0, deltaY: 0, deltaMode: 0, timeStamp: now(),
      preventDefault: function () { e.returnValue = false; },
      stopPropagation: function () { e.cancelBubble = true; }
    };
    if (POINTER[type] === 1 || type === "click" || type === "mousedown" || type === "mouseup") {
      o.button = IE_BUTTON[b] === undefined ? 0 : IE_BUTTON[b];
    }
    if (POINTER[type] === 1) {
      o.pointerId = 1;
      o.pointerType = "mouse";
      o.isPrimary = true;
      /* IE's button is the bitmask of buttons held, as W3C's buttons is; after a release none is. */
      o.buttons = type === "pointerup" ? 0 : b;
    }
    if (type === "wheel") o.deltaY = -(e.wheelDelta | 0) * 5 / 6;  /* a notch: 120 in IE, 100 pixels in the W3C event */
    return o;
  }

  /* A W3C mouse event as the pointer or wheel event the shim reads, on a browser that has neither (Firefox 1.0).
   * held is kept by the document's own listeners (track), which hear a press or a release before the mount does. */
  function track() {
    if (tracking) return;
    tracking = true;
    doc.addEventListener("mousedown", function (e) { held |= BIT[e.button] || 0; }, true);
    doc.addEventListener("mouseup", function (e) { held &= ~(BIT[e.button] || 0); }, true);
  }
  function w3cShaped(e, type) {
    return {
      type: type, target: e.target, currentTarget: e.currentTarget, srcEvent: e,
      clientX: e.clientX, clientY: e.clientY, keyCode: e.keyCode,
      shiftKey: !!e.shiftKey, ctrlKey: !!e.ctrlKey, altKey: !!e.altKey, metaKey: !!e.metaKey,
      button: e.button | 0, buttons: typeof e.buttons === "number" ? e.buttons : held,
      pointerId: type === "wheel" ? 0 : 1, pointerType: type === "wheel" ? "" : "mouse", isPrimary: type !== "wheel",
      /* a notch: wheelDelta 120 or a DOMMouseScroll detail of 3, and 100 pixels in the W3C event */
      deltaX: 0, deltaY: e.wheelDelta ? -e.wheelDelta * 5 / 6 : (e.detail | 0) * 100 / 3, deltaMode: 0, timeStamp: now(),
      preventDefault: function () { e.preventDefault(); },
      stopPropagation: function () { e.stopPropagation(); }
    };
  }
  /* The type a W3C browser without pointer or wheel events listens for in their place, or null when it has them. */
  function w3cType(type) {
    if (POINTER[type] === 1) return w.PointerEvent ? null : IE_TYPE[type];
    if (type !== "wheel") return null;
    var d = doc.createElement("div");
    return "onwheel" in d ? null : "onmousewheel" in d ? "mousewheel" : "DOMMouseScroll";
  }

  /* The field hook: f hears type from every field inside node as it would from a bubble. */
  function hookFields(node, type, f) {
    var hooks = node.$cdFields;
    if (!hooks) {
      hooks = node.$cdFields = {};
      node.attachEvent("onfocusin", function () {
        var t = w.event.srcElement, k;
        for (k in hooks) {
          if (hooks.hasOwnProperty(k)) hookField(t, k, hooks[k]);
        }
      });
    }
    hooks[type] = f;
  }
  function hookField(t, type, f) {
    var tag = t && t.tagName, field = tag === "INPUT" || tag === "TEXTAREA" || tag === "SELECT", form;
    if (type === "submit") {
      form = field ? t.form : null;
      if (!form || form.$cdSubmit) return;
      form.$cdSubmit = 1;
      form.attachEvent("onsubmit", function () { f(shaped(w.event, form, type)); });
      return;
    }
    if (!field || t["$cd" + type]) return;
    t["$cd" + type] = 1;
    if (type === "change") {
      t.attachEvent("onchange", function () { f(shaped(w.event, t, type)); });
    } else {
      /* propertychange fires for every property and for a value C sets too: only the user's typing is input. */
      t.attachEvent("onpropertychange", function () {
        var e = w.event;
        if (e.propertyName === "value" && doc.activeElement === t) f(shaped(e, t, type));
      });
    }
  }

  /* x.addEventListener(type, f, options). */
  function on(x, type, f, options) {
    if (x.addEventListener) {
      var capture = typeof options === "object" && options !== null ? !!options.capture : !!options;
      var as = w3cType(type);
      if (!as) {
        x.addEventListener(type, f, capture);
        return;
      }
      if (POINTER[type] === 1) track();
      var h = function (e) { f(w3cShaped(e, type)); };
      bound.push([x, type, f, h, x, as, capture]);
      x.addEventListener(as, h, capture);
      return;
    }
    if (!x.attachEvent) return;
    if (FIELD[type] === 1 && x.nodeType === 1) {
      hookFields(x, type, f);
      return;
    }
    var name = IE_TYPE[type] || type, target = type === "DOMContentLoaded" ? w : x;
    var g = function (e) { f(shaped(e || w.event, x, type)); };
    bound.push([x, type, f, g, target, name]);
    target.attachEvent("on" + name, g);
  }
  /* x.removeEventListener(type, f). */
  function off(x, type, f) {
    for (var i = 0; i < bound.length; i++) {
      var r = bound[i];
      if (r[0] === x && r[1] === type && r[2] === f) {
        if (r[4].removeEventListener) r[4].removeEventListener(r[5], r[3], r[6]);
        else r[4].detachEvent("on" + r[5], r[3]);
        bound.splice(i, 1);
        return;
      }
    }
    if (x.removeEventListener) x.removeEventListener(type, f, false);
  }
  /* A type the old DOM does not bubble (scroll, load, error inside the mount), heard on node itself and handed to f,
   * the mount's listener for it, as if it had come up to the mount; once per node and type. Nothing to do where the
   * capture phase exists. */
  function reach(node, type, f) {
    if (node.addEventListener || !node.attachEvent || node.nodeType !== 1 || node["$cdReach" + type]) return;
    node["$cdReach" + type] = 1;
    node.attachEvent("on" + type, function () { f(shaped(w.event, node, type)); });
  }

  /* "background-color" as the style object's backgroundColor. A loop, not replace with a function, which JScript 5.0
   * (IE 5.0) does not take. */
  function camel(name) {
    var out = "", i, c, up = false;
    for (i = 0; i < name.length; i++) {
      c = name.charAt(i);
      if (c === "-") up = true;
      else {
        out += up ? c.toUpperCase() : c;
        up = false;
      }
    }
    return out;
  }
  /* style.setProperty(name, value). IE 6 throws on a value it cannot parse, which other browsers ignore. */
  function setProperty(style, name, value) {
    if (style.setProperty) {
      style.setProperty(name, value);
      return;
    }
    if (name.charAt(0) === "-" && name.charAt(1) === "-") return;  /* a custom property: none in IE 6 to 8 */
    var key = name === "float" ? "styleFloat" : camel(name);
    try {
      style[key] = value;
    } catch (e) {}
    if (name === "opacity") {
      try {
        style.filter = value === "" ? "" : "alpha(opacity=" + Math.round(parseFloat(value) * 100) + ")";
      } catch (e) {}
    }
  }
  function getPropertyValue(style, name) {
    if (style.getPropertyValue) return style.getPropertyValue(name);
    if (name.charAt(0) === "-" && name.charAt(1) === "-") return "";
    var v = style[name === "float" ? "styleFloat" : camel(name)];
    return v == null ? "" : v;
  }

  /* IE 6 and 7's setAttribute sets the property of the same name, so class, for, style and the camel-case ones
   * do nothing; IE 8 fixed it. Tested once. */
  var PROPERTY = { "class": "className", "for": "htmlFor", colspan: "colSpan", rowspan: "rowSpan", tabindex: "tabIndex",
    readonly: "readOnly", maxlength: "maxLength", cellpadding: "cellPadding", cellspacing: "cellSpacing",
    frameborder: "frameBorder", usemap: "useMap", accesskey: "accessKey", contenteditable: "contentEditable" };
  var attrsAreProperties = (function () {
    var d = doc.createElement("div");
    d.setAttribute("class", "x");
    return d.className !== "x";
  })();
  function setAttribute(el, name, value) {
    if (!attrsAreProperties) {
      el.setAttribute(name, value);
      return;
    }
    var n = name.toLowerCase();
    if (n === "style") {
      el.style.cssText = value;
      return;
    }
    var p = PROPERTY[n] || n;
    /* A boolean property (checked, disabled, selected, multiple): the attribute present is true. */
    if (typeof el[p] === "boolean") el[p] = true;
    else el.setAttribute(p, value);
  }

  /* x.textContent = value. */
  function setText(x, value) {
    if ("textContent" in x) {
      x.textContent = value;
      return;
    }
    if (x.nodeType !== 1) {
      x.nodeValue = value;
      return;
    }
    while (x.firstChild) x.removeChild(x.firstChild);
    if (value !== "") x.appendChild(doc.createTextNode(value));
  }
  function remove(x) {
    if (x.parentNode) x.parentNode.removeChild(x);
  }
  function prevElement(x) {
    if ("previousElementSibling" in x) return x.previousElementSibling;
    var s = x.previousSibling;
    while (s && s.nodeType !== 1) s = s.previousSibling;
    return s;
  }
  /* No SVG in IE 6 to 8: a plain element of the tag, which draws nothing (docs/ie6-design.md, section 6). */
  function createElementNS(d, ns, tag) {
    return d.createElementNS ? d.createElementNS(ns, tag) : d.createElement(tag);
  }
  /* Every element at (x, y), topmost first: elementFromPoint's, then its ancestors. */
  function elementsFromPoint(d, x, y) {
    if (d.elementsFromPoint) return d.elementsFromPoint(x, y);
    var out = [], el = d.elementFromPoint ? d.elementFromPoint(x, y) : null;
    for (; el && el.nodeType === 1; el = el.parentNode) out.push(el);
    return out;
  }
  /* getBoundingClientRect with width and height, which IE 6 to 8 leave out, and without the 2 pixels of the root's
   * border that IE 6 and 7 add to each edge (documentElement.clientLeft and clientTop). */
  function rect(x) {
    var r = x.getBoundingClientRect(), root = doc.documentElement;
    if (r.width !== undefined) return r;
    var dx = root.clientLeft || 0, dy = root.clientTop || 0;
    return { left: r.left - dx, top: r.top - dy, right: r.right - dx, bottom: r.bottom - dy,
      width: r.right - r.left, height: r.bottom - r.top };
  }
  function viewportWidth() {
    return typeof w.innerWidth === "number" ? w.innerWidth : doc.documentElement.clientWidth || doc.body.clientWidth;
  }
  function viewportHeight() {
    return typeof w.innerHeight === "number" ? w.innerHeight : doc.documentElement.clientHeight || doc.body.clientHeight;
  }
  function pixelRatio() {
    if (typeof w.devicePixelRatio === "number") return w.devicePixelRatio;
    var s = w.screen;
    return s && s.deviceXDPI && s.logicalXDPI ? s.deviceXDPI / s.logicalXDPI : 1;
  }

  return { on: on, off: off, reach: reach, setProperty: setProperty, getPropertyValue: getPropertyValue,
    setAttribute: setAttribute, setText: setText, remove: remove, prevElement: prevElement,
    createElementNS: createElementNS, elementsFromPoint: elementsFromPoint, rect: rect,
    viewportWidth: viewportWidth, viewportHeight: viewportHeight, pixelRatio: pixelRatio };
})(window, document);
