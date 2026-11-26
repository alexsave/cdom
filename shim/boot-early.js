/* cdom boot, the early script. The build writes it into every page, in a <script> of its own before the head's end
 * (tools/legacy/build.mjs page), so it runs before any content: a FRAMESET written by script is ignored by
 * Netscape 4 once the page has any (a DIV is enough), and a document replaced by open, write and close loses every
 * global there (docs/frames-design.md, section 3, and what the real browsers did: section 15).
 * It is JavaScript 1.0 (tools/legacy/js11.mjs, js10), since Netscape 2 parses it. A browser with getElementById and
 * createElement does nothing here. One without them is the frames tier, and the page becomes a frameset of two frames:
 * cdomView, which the app's shim writes the tree into, and cdomApp, 1 pixel high, which keeps the state: its document is
 * the page's app document (the page's name with .app.html for .html, which the build writes in place of the name below,
 * and the page's query), every script it needs inline, since Netscape 2 has no SCRIPT SRC (section 16); nothing
 * replaces it. The frames' names are the shim's (cdomView, cdomApp), and the view's handlers call parent.cdomApp. */
if (!(document.getElementById && document.createElement)) {
    document.write("<frameset rows=\"*,1\" border=\"0\" frameborder=\"0\" framespacing=\"0\"><frame name=\"cdomView\" src=\"about:blank\" marginwidth=\"8\" marginheight=\"8\"><frame name=\"cdomApp\" src=\"" + "/*CDOM_APP_DOC*/" + location.search + "\" scrolling=\"no\" noresize></frameset>");
}
