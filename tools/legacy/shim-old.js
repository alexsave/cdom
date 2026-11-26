// The shim's functions for the fourth tier (IE 6, 7 and 8: docs/ie6-design.md): tools/legacy/build.mjs puts each in
// place of shim/cdom.js's function of the same name when it builds cdom.legacy-ie6.js, as it puts memory-words.js's,
// and fails if one has no such function there.

// The types that do not bubble and have no bubbling twin in IE (focus and blur do: focusin and focusout, which
// dom-old.js listens for in their place): scroll to pause, and a dialog's cancel and close. The node C listens on
// hears them itself and hands them to the mount's listener (tools/legacy/dom-old.js, reach).
function reach(node, type) {
    if ((type > FOCUS + 1 && type <= PAUSE) || type === CANCEL || type === CLOSE) {
        cdomOld.reach(node, EVENTS[type], delegatedListener[type]);
    }
}

// IE 6 to 8's DOM methods are host objects with no apply or call (el.focus.apply is undefined), so a method is called
// directly, by the count of its arguments; past eight, through apply, which a host method then lacks.
function invoke(object, key, args) {
    const a = args;
    switch (a.length) {
        case 0: return object[key]();
        case 1: return object[key](a[0]);
        case 2: return object[key](a[0], a[1]);
        case 3: return object[key](a[0], a[1], a[2]);
        case 4: return object[key](a[0], a[1], a[2], a[3]);
        case 5: return object[key](a[0], a[1], a[2], a[3], a[4]);
        case 6: return object[key](a[0], a[1], a[2], a[3], a[4], a[5]);
        case 7: return object[key](a[0], a[1], a[2], a[3], a[4], a[5], a[6]);
        case 8: return object[key](a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
        default: return object[key].apply(object, a);
    }
}

// A node moved or put back inside an svg (cd_move, cd_attach) may now inherit another transform or paint: on this tier
// the svg is VML, which tools/legacy/vml.js draws again from its new parents (docs/vml-design.md).
function place(parent, node, next) {
    parent.insertBefore(node, next);
    if (node.$vml) {
        cdomVml.placed(node);
    }
}

// The VML shapes whose attributes changed during the call into C that just ended, each drawn once now
// (tools/legacy/vml.js, docs/vml-design.md, section 10).
function drawHeld() {
    cdomVml.drawHeld();
}
