/* cdom boot, the early script. The build writes it into every page, in a <script> of its own before the head's end
 * (tools/legacy/build.mjs page), so it runs before any content: a FRAMESET written by script is ignored by
 * Netscape 4 once the page has any (a DIV is enough), and a document replaced by open, write and close loses every
 * global there (docs/frames-design.md, section 3, and what the real browsers did: section 15).
 * It is JavaScript 1.0 (tools/legacy/js11.mjs, js10), since Netscape 2 parses it. A browser with getElementById and
 * createElement does nothing here. One without them is the frames tier, and the page becomes a frameset of two frames:
 * cdomView, which the app's shim writes the tree into, and cdomApp, 1 pixel high, which is this same page under another
 * URL (its query gains cdomApp: Netscape 4 drops a frame whose URL is the frameset's own) and which keeps the state:
 * its scripts run in full and nothing replaces its document. A window named cdomApp is that frame and writes no frameset.
 * The frames' names are the shim's (cdomView, cdomApp), and the view's handlers call parent.cdomApp. */
if (!(document.getElementById && document.createElement) && window.name != "cdomApp") {
  var cdomQ = location.search;
  if (cdomQ) cdomQ = cdomQ + "&";
  else cdomQ = "?";
  document.write("<frameset rows=\"*,1\" border=\"0\" frameborder=\"0\" framespacing=\"0\"><frame name=\"cdomView\" src=\"about:blank\" marginwidth=\"8\" marginheight=\"8\"><frame name=\"cdomApp\" src=\"" + location.pathname + cdomQ + "cdomApp\" scrolling=\"no\" noresize></frameset>");
}
