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
