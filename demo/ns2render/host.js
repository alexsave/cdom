// The browser host of Netscape 2's self-painting page (docs/descent.md, "Netscape 2 paints itself"), JavaScript 1.0: it
// runs in the frameset's cdomApp frame beside the module (render.c through tools/js1c), asks it for a few rows at a time
// from timers, turns the ops it answers into IMG markup (rsHtml, the same bytes as render.c's rd_html) and writes them into
// the cdomView frame.  The links in the picture call rsK (a click) and rsO (the pointer over one, which also gives the
// status text).  Netscape 2: no function expressions, no fromCharCode, a function is false in a condition, a click's own
// handler must not rewrite the view (it is left deaf), so every draw starts from a timer.
var rsX = null, rsM = null, rsHex = null, rsBase = "", rsPage = 0, rsState = 1, rsBusy = 0, rsWant = -1;
// rsBatch rows a timer; rsMode 0 writes each batch as it is made, 1 makes them all and then writes them in one go.
var rsBatch = 8, rsMode = 0, rsDone = null, rsNB = 0;
// times of the last draw, in the engine's milliseconds: start, compute (in the module), markup (strings), write
var rsT0 = 0, rsTc = 0, rsTm = 0, rsTw = 0, rsTe = 0, rsN = 0;
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

// Starts a draw of page in state (1 normal, 2 the pill hovered); the rows come from rsStep's timers.
function rsDraw(page, state) {
  if (rsBusy) { rsWant = page * 4 + state; return; }
  rsBusy = 1;
  rsPage = page;
  rsState = state;
  rsT0 = rsNow();
  rsTc = 0; rsTm = 0; rsTw = 0; rsN = 0; rsNB = 0;
  rsX.rs_begin(page, state);
  rsDone = new Object();
  if (rsMode == 0) { rsView.open(); rsView.write(rsHead()); }
  setTimeout("rsStep()", 0);
}

function rsStep() {
  var t = rsNow(), more = rsX.rs_rows(rsBatch), n = rsX.rs_nout(), s, u;
  u = rsNow();
  rsTc = rsTc + u - t;
  s = rsHtml(rsX.rs_out() >> 2, n);
  rsN = rsN + s.length;
  t = rsNow();
  rsTm = rsTm + t - u;
  if (rsMode == 0) rsView.write(s);
  else { rsDone[rsNB] = s; rsNB = rsNB + 1; }
  rsTw = rsTw + rsNow() - t;
  if (more) { setTimeout("rsStep()", 0); return; }
  rsEnd();
}

function rsEnd() {
  var t = rsNow(), k;
  if (rsMode != 0) {
    rsView.open();
    rsView.write(rsHead());
    for (k = 0; k < rsNB; k = k + 1) rsView.write(rsDone[k]);
  }
  rsTe = rsNow();
  // the last image names the times, so the server's log has them: the page's draw ends when it is requested
  rsView.write("<IMG SRC=\"end.gif?p" + rsPage + "s" + rsState + "c" + rsTc + "m" + rsTm + "w" + (rsTw + rsTe - t) + "t" + (rsTe - rsT0) + "b" + rsN + "\" WIDTH=1 HEIGHT=1></BODY></HTML>");
  rsView.close();
  rsDone = null;
  rsBusy = 0;
  if (rsWant >= 0) { k = rsWant; rsWant = -1; if (k != rsPage * 4 + rsState) rsDraw((k - (k % 4)) / 4, k % 4); }
}

// A click on link n: the page it leads to, drawn from a timer.
function rsK(n) {
  setTimeout("rsDraw(" + rsG[n] + ",1)", 50);
}

// The pointer over link n: the pill's hover state drawn (or put back when the pointer reaches another link); its status.
function rsO(n) {
  var want = rsH[n] == 1 ? 2 : 1;
  if (want != rsState && rsPage == (n - (n % 16)) / 16) setTimeout("rsDraw(" + rsPage + "," + want + ")", 50);
  return rsT[n];
}

function rsStart() {
  var s = "" + location.href, i;
  for (i = s.length - 1; i > 0; i = i - 1) if (s.charAt(i) == "/") break;
  rsBase = s.substring(0, i + 1);
  rsTables();
  rsX = cdomFactory(new Object());
  rsM = rsX.memory.words;
  if (rsView == null) rsView = parent.cdomView.document;
  rsDraw(0, 1);
}
