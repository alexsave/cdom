// The browser host of Netscape 2's self-painting page (docs/descent.md, "Netscape 2 paints itself"), JavaScript 1.0: it
// runs in the frameset's cdomApp frame beside the module (render.c through tools/js1c), asks it for a few rows at a time
// from timers, turns the ops it answers into IMG markup (rsHtml, the same bytes as render.c's rd_html) and writes them into
// the cdomView frame.  The links in the picture call rsK (a click) and rsO (the pointer over one, which also gives the
// status text).  Netscape 2: no function expressions, no fromCharCode, a function is false in a condition, a click's own
// handler must not rewrite the view (it is left deaf), so every draw starts from a timer.
var rsX = null, rsM = null, rsHex = null, rsBase = "", rsPage = 0, rsState = 1, rsBusy = 0, rsWant = -1;
// rsBatch rows a timer; rsMode 1 makes every batch and then writes them all in one go, 0 writes each batch as it is made
// (Netscape 2 then asks for the batch's GIFs again each time: docs/descent.md).
var rsBatch = 8, rsMode = 1, rsJoin = 0, rsDone = null, rsNB = 0;
// times of the last draw, in the engine's milliseconds: start, compute (in the module), markup (strings), write, end,
// and the first batch written (from the start)
var rsT0 = 0, rsTc = 0, rsTm = 0, rsTw = 0, rsTe = 0, rsTf = 0, rsN = 0;
// the view: the cdomView frame's document, or a stand-in the test gives
var rsView = null;

function rsNow() {
  var d = new Date();
  return d.getTime();
}

// The tag before the width for each palette colour: <IMG SRC=xx.gif WIDTH=
function rsTables() {
  var h = "0123456789abcdef", k;
  rsHex = new Object();
  for (k = 0; k < 256; k = k + 1) rsHex[k] = "<IMG SRC=" + h.charAt(k >> 4) + h.charAt(k & 15) + ".gif WIDTH=";
}

// The markup of the ops [at, at + n) of the module's memory, rd_html's bytes.
function rsHtml(at, n) {
  var out = "", line = "", tail = ">", h = 1, i, op, v, kind, link = 0;
  for (i = 0; i < n; i = i + 1) {
    op = rsM[at + i];
    kind = (op >> 24) & 255;
    v = op & 16777215;
    if (kind == 2) line = line + rsHex[(v >> 12) & 255] + (v & 4095) + tail;
    else if (kind == 1) { h = v; tail = " HEIGHT=" + h + ">"; line = ""; }
    else if (kind == 3) {
      line = line + "<A HREF=\"javascript:parent.cdomApp.rsK(" + v + ")\" onMouseOver=\"window.status=parent.cdomApp.rsO(" + v + ");return true\">";
      tail = " HEIGHT=" + h + " BORDER=0>";
    } else if (kind == 4) { line = line + "</A>"; tail = " HEIGHT=" + h + ">"; }
    else if (kind == 5) { out = out + line + "<BR>\n"; line = ""; }
  }
  return out;
}

function rsHead() {
  return "<HTML><HEAD><BASE HREF=\"" + rsBase + "\"></HEAD><BODY BGCOLOR=\"#f6f7fb\">";
}

// The markup of the batches already made, by page and state: rsC["k" + (page * 4 + state)].s["y" + y] the markup of rows y
// to .e["y" + y].  A batch whose rows hold nothing that differs between the states (outside rs_dirty's rows) is taken from
// the normal state's.  The keys are strings: Netscape 2 reads o[n], n a number, as the object's nth property, not as the
// property named n (an object whose number keys were not written in order from 0 answers the wrong one).
var rsC = new Object(), rsKey = 0, rsNorm = null, rsD0 = 0, rsD1 = 0, rsY = 0, rsBegun = 0;

function rsCache(k) {
  var o = rsC["k" + k];
  if (o == null) { o = new Object(); o.s = new Object(); o.e = new Object(); rsC["k" + k] = o; }
  return o;
}

// Starts a draw of page in state (1 normal, 2 the pill hovered); the rows come from rsStep's timers.
function rsDraw(page, state) {
  var d;
  if (rsBusy) { rsWant = page * 4 + state; return; }
  rsBusy = 1;
  rsPage = page;
  rsState = state;
  rsT0 = rsNow();
  rsTc = 0; rsTm = 0; rsTw = 0; rsTf = -1; rsN = 0; rsNB = 0;
  rsKey = page * 4 + state;
  rsCache(rsKey);
  rsNorm = state != 1 ? rsCache(page * 4 + 1) : null;
  d = rsX.rs_dirty(page);
  rsD1 = d % 1024;
  rsD0 = (d - rsD1) / 1024;
  rsY = 0;
  rsBegun = 0;
  rsDone = new Object();
  if (rsMode == 0) { rsView.open(); rsView.write(rsHead()); }
  setTimeout("rsStep()", 0);
}

// One piece of the view: written now (mode 0) or kept for rsEnd (mode 1).
function rsPut(s) {
  var t = rsNow();
  rsN = rsN + s.length;
  if (rsMode == 0) rsView.write(s);
  else { rsDone[rsNB] = s; rsNB = rsNB + 1; }
  if (rsTf < 0) rsTf = rsNow() - rsT0;
  rsTw = rsTw + rsNow() - t;
}

// The batches from rsY on: every one already made is taken, and at most one is made, then the next timer goes on.
function rsStep() {
  var o = rsC["k" + rsKey], t, u, s, y, at;
  while (rsY < 320) {
    at = "y" + rsY;
    if (o.s[at] != null) { rsPut(o.s[at]); rsY = o.e[at]; continue; }
    if (rsNorm != null && rsNorm.s[at] != null && (rsNorm.e[at] <= rsD0 || rsY >= rsD1)) { rsPut(rsNorm.s[at]); rsY = rsNorm.e[at]; continue; }
    t = rsNow();
    if (rsBegun == 0) { rsX.rs_begin(rsPage, rsState); rsBegun = 1; }
    rsX.rs_at(rsY);
    rsX.rs_rows(rsBatch);
    y = rsX.rs_y();
    u = rsNow();
    rsTc = rsTc + u - t;
    s = rsHtml(rsX.rs_out() >> 2, rsX.rs_nout());
    rsTm = rsTm + rsNow() - u;
    o.s[at] = s;
    o.e[at] = y;
    rsPut(s);
    rsY = y;
    if (rsY < 320) { setTimeout("rsStep()", 0); return; }
  }
  rsEnd();
}

function rsEnd() {
  var t = rsNow(), k, n, step;
  if (rsMode != 0) {
    // the pieces joined in pairs (each character copied about log2 n times, not n times) into one write per rsJoin of them,
    // or one write of all (rsJoin 0): Netscape 2 asks for a write's GIFs again on each write() call
    rsDone[0] = rsHead() + rsDone[0];
    n = rsNB;
    step = 1;
    while (step < n && (rsJoin == 0 || step < rsJoin)) {
      for (k = 0; k + step < n; k = k + 2 * step) if (rsJoin == 0 || (k % rsJoin) + step < rsJoin) { rsDone[k] = rsDone[k] + rsDone[k + step]; rsDone[k + step] = null; }
      step = step * 2;
    }
    rsView.open();
    for (k = 0; k < n; k = k + 1) if (rsDone[k] != null) rsView.write(rsDone[k]);
  }
  rsTe = rsNow();
  // the last image names the times, so the server's log has them: the page's draw ends when it is requested
  rsView.write("<IMG SRC=\"end.gif?p" + rsPage + "s" + rsState + "c" + rsTc + "m" + rsTm + "w" + (rsTw + rsTe - t) + "t" + (rsTe - rsT0) + "f" + rsTf + "b" + rsN + "\" WIDTH=1 HEIGHT=1></BODY></HTML>");
  rsView.close();
  rsDone = null;
  rsBusy = 0;
  if (rsWant >= 0) { k = rsWant; rsWant = -1; if (k != rsPage * 4 + rsState) rsDraw((k - (k % 4)) / 4, k % 4); }
}

// A click on link n: the page it leads to, drawn from a timer.
function rsK(n) {
  setTimeout("rsDraw(" + rsG["k" + n] + ",1)", 50);
}

// The pointer over link n: the pill's hover state drawn (or put back when the pointer reaches another link); its status.
function rsO(n) {
  var want = rsH["k" + n] == 1 ? 2 : 1;
  if (want != rsState && rsPage == (n - (n % 16)) / 16) setTimeout("rsDraw(" + rsPage + "," + want + ")", 50);
  return rsT["k" + n];
}

// Options from the frameset's query, for the measurements: m<mode>, b<rows a batch> and j<batches a write, 0 all>, as
// index.html?m1b32j4.
function rsOptions() {
  var q = "x" + parent.location.search, i, c, k, v;
  for (i = 1; i < q.length; i = i + 1) {
    c = q.charAt(i);
    if (c == "m" || c == "b" || c == "j") {
      v = 0;
      for (k = i + 1; k < q.length && "0123456789".indexOf(q.charAt(k)) >= 0; k = k + 1) v = v * 10 + "0123456789".indexOf(q.charAt(k));
      if (c == "m") rsMode = v;
      else if (c == "j") rsJoin = v;
      else if (v > 0) rsBatch = v;
    }
  }
}

function rsStart() {
  var s = "" + location.href, i;
  rsOptions();
  for (i = s.length - 1; i > 0; i = i - 1) if (s.charAt(i) == "/") break;
  rsBase = s.substring(0, i + 1);
  rsTables();
  rsX = cdomFactory(new Object());
  rsM = rsX.memory.words;
  if (rsView == null) rsView = parent.cdomView.document;
  rsDraw(0, 1);
}
