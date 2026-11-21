#!/usr/bin/env python3
"""Writes the probe pages of docs/descent.md "Netscape 2's limits for inline code" into tests/probe-ns2-*.html.
Run: python3 tools/gen_probe_ns2_limits.py        (writes every page; needs demo/uttt/out built for probe-ns2-big.html)
Pages (JavaScript 1.0 only, one test per <script> block so a block that fails kills only itself; flags are set at the end
of a block and reported by 1x1 images written by the last block, /mark/<query>/p-<n>-<name>-<value>, which the server's log holds):
  probe-ns2-stmt.html   blocks of 2K .. 128K of statements `var a123=123;`, ascending
  probe-ns2-stmt<K>.html, probe-ns2-str<K>.html  the same with one block of K KB per page (the all-in-one pages are slow)
  probe-ns2-str.html    blocks of one string literal of 1K .. 128K, ascending; the length the block saw is reported
  probe-ns2-many<N>.html  N blocks of SIZE bytes of statements (a counter in every block says how many ran)
  probe-ns2-fns.html    a function and globals defined in one block, used in the next
  probe-ns2-frames.html the frames tier's two frames (shim/boot-early.js's shape), calls and rewrites; driven by a click
  probe-ns2-big.html    the demo/uttt build's code, inline, about 150 KB: its load time is the gap between the page's GET and its mark
"""
import os, sys

here = os.path.dirname(os.path.abspath(__file__))
tests = os.path.join(here, "..", "tests")

HEAD = """<HTML>
<HEAD>
<TITLE>cdom probe-ns2 %(name)s</TITLE>
<SCRIPT LANGUAGE="JavaScript">
var n = 0;
var TAG = "" + location.search;
TAG = TAG.substring(1, TAG.length);
if (TAG == "") { TAG = "x"; }
function mark(name, v) {
  n = n + 1;
  document.write('<IMG SRC="/mark/' + TAG + '/p-' + n + '-' + name + '-' + escape("" + v) + '" WIDTH=1 HEIGHT=1>');
}
function nowms() {
  var d = new Date();
  return d.getTime();
}
var T0 = nowms();
%(init)s
</SCRIPT>
</HEAD>
<BODY>
<P>cdom probe-ns2 %(name)s
"""


def block(body):
    return "<SCRIPT LANGUAGE=\"JavaScript\">\n" + body + "\n</SCRIPT>\n"


def stmts(size, tag):
    """Statements `var a<tag>_<i>=<i>;` up to size bytes, one per line."""
    out, total, i = [], 0, 0
    while total < size:
        s = "var a%s_%d=%d;\n" % (tag, i, i)
        out.append(s)
        total += len(s)
        i += 1
    return "".join(out)


def page(name, init, blocks, tail):
    return HEAD % {"name": name, "init": init} + "".join(blocks) + block(tail) + "</BODY>\n</HTML>\n"


def stmt(sizes=(2, 4, 8, 16, 32, 64, 128), name="stmt"):
    init = "".join("var ok%d = 0;\n" % k for k in sizes)
    blocks = [block(stmts(k * 1024, "s%d" % k) + "ok%d = 1;" % k) for k in sizes]
    tail = "".join('mark("ok%d", ok%d);\n' % (k, k) for k in sizes) + 'mark("ms", nowms() - T0);\n'
    return page(name, init, blocks, tail)


def string(sizes=(1, 2, 4, 8, 16, 32, 64, 128), name="str"):
    init = "".join("var ok%d = 0;\nvar len%d = 0;\n" % (k, k) for k in sizes)
    blocks = []
    for k in sizes:
        lit = ("0123456789abcdef" * (k * 64))[: k * 1024]
        blocks.append(block('var s%d = "%s";\nlen%d = s%d.length;\nok%d = 1;' % (k, lit, k, k, k)))
    tail = "".join('mark("ok%d", ok%d);\nmark("len%d", len%d);\n' % (k, k, k, k) for k in sizes)
    tail += 'mark("ms", nowms() - T0);\n'
    return page(name, init, blocks, tail)


def many(count, size):
    init = "var cnt = 0;\nvar last = 0;\n"
    blocks = [block(stmts(size, "m%d" % k) + "cnt = cnt + 1;\nlast = %d;" % k) for k in range(1, count + 1)]
    tail = 'mark("cnt", cnt);\nmark("last", last);\nmark("ms", nowms() - T0);\n'
    return page("many%d" % count, init, blocks, tail)


def fns():
    init = "var cross = 0;\nvar g1 = 0;\nvar seen = 0;\nvar calls = 0;\n"
    b1 = block("function twice(x) { calls = calls + 1; return x * 2; }\ng1 = 5;\nvar defined_in_1 = 11;")
    b2 = block("seen = g1;\ncross = twice(21);\nvar via = defined_in_1;\ng1 = g1 + 1;")
    b3 = block("function again() { return twice(g1); }\nvar chain = again();")
    tail = 'mark("seenGlobal", seen);\nmark("crossCall", cross);\nmark("varFromBlock1", via);\nmark("chain", chain);\nmark("calls", calls);\n'
    return page("fns", init, [b1, b2, b3], tail)


FRAMES = """<HTML>
<HEAD>
<TITLE>cdom probe-ns2 frames</TITLE>
<SCRIPT LANGUAGE="JavaScript">
// The shape of shim/boot-early.js and cdomApp: this page is the frameset (early script), and again, as window cdomApp,
// the app frame, whose script rewrites cdomView with open("text/html", "replace"), write, close, and whose function f
// the view's link calls.  The app frame's marks go into the view's document (Netscape 2 has no Image object), absolute.
var Q = "" + location.search;
Q = Q.substring(1, Q.length);
var TAG = Q;
var BASE = "http://" + location.host;
var K = 0;
var HITS = 0;
var BUSY = 0;
var PAD = "";
var C5 = "";
function body5k(tag) {
  var s = "<HTML><BODY>view " + tag + "<BR>";
  s = s + '<A HREF="javascript:parent.cdomApp.f(1)">call</A> ';
  s = s + '<A HREF="javascript:parent.cdomApp.f(2)">call2</A><BR>';
  var i = 0;
  while (i < 60) {
    s = s + "<P>row " + i + ": the quick brown fox jumps over the lazy dog, <B>bold</B> and <I>italic</I> text to fill it.";
    i = i + 1;
  }
  return s;
}
function mk(name, v) {
  K = K + 1;
  return '<IMG SRC="' + BASE + '/mark/' + TAG + '/p-' + K + '-' + name + '-' + escape("" + v) + '" WIDTH=1 HEIGHT=1>';
}
function draw(tag, extra) {
  var d = parent.cdomView.document;
  d.open("text/html", "replace");
  var b = body5k(tag);
  d.write(b);
  d.write(extra);
  d.write("</BODY></HTML>");
  d.close();
  return b.length;
}
// the shape of the tier's rewrite loop: R rewrites one after another, every document carrying a mark
var R = 0;
function loop() {
  R = R + 1;
  var len = draw("loop" + R, mk("rewrite" + R, "len"));
  if (R < 12) setTimeout("loop()", 1);
}
function f(who) {
  HITS = HITS + 1;
  // a rewrite from inside the call: the document that holds the link that called is replaced
  var len = draw("hit" + HITS, mk("hit" + who, "hits" + HITS + "-len-" + body5k("x").length));
  if (who == 2) {
    R = 0;
    setTimeout("loop()", 1);
  }
}
function started() {
  var len = draw("first", mk("appStarted", "name=" + window.name + "-frames=" + parent.frames.length));
}
if (window.name != "cdomApp") {
  document.write('<FRAMESET ROWS="*,1" BORDER=0 FRAMEBORDER=0 FRAMESPACING=0><FRAME NAME="cdomView" SRC="about:blank" MARGINWIDTH=8 MARGINHEIGHT=8><FRAME NAME="cdomApp" SRC="' + location.pathname + '?' + TAG + '&cdomApp" SCROLLING=no NORESIZE></FRAMESET>');
}
</SCRIPT>
</HEAD>
<BODY onLoad="if (window.name == 'cdomApp') started()">
app frame
</BODY>
</HTML>
"""


def big():
    out = os.path.join(here, "..", "demo", "uttt", "out")
    js1 = open(os.path.join(out, "uttt.wasm.js1.js")).read()
    frames = open(os.path.join(out, "cdom.frames.js")).read()
    lines = js1.split("\n")
    blocks = [block(frames)]
    # the module's lines are whole statements: blocks of whole lines up to 30000 bytes, the module repeated to 150 KB
    total = len(frames)
    chunk, size = [], 0
    reps = 0
    done = False
    while not done:
        reps += 1
        for ln in lines:
            if total + size >= 150 * 1000:
                done = True
                break
            if size + len(ln) > 30000 and chunk:
                blocks.append(block("\n".join(chunk)))
                total += size
                chunk, size = [], 0
            chunk.append(ln)
            size += len(ln) + 1
    if chunk:
        blocks.append(block("\n".join(chunk)))
        total += size
    init = ""
    tail = 'mark("blocks", %d);\nmark("bytes", %d);\nmark("ms", nowms() - T0);\n' % (len(blocks), total)
    return page("big", init, blocks, tail)


def write(name, text):
    with open(os.path.join(tests, name), "w", newline="\n") as f:
        f.write(text)
    print(name, len(text))


if __name__ == "__main__":
    write("probe-ns2-stmt.html", stmt())
    write("probe-ns2-str.html", string())
    for k in (8, 16, 32, 64, 128, 256, 512, 1024):  # one block per page: the all-in-one pages are slow to load (docs/descent.md)
        write("probe-ns2-stmt%d.html" % k, stmt((k,), "stmt%d" % k))
        write("probe-ns2-str%d.html" % k, string((k,), "str%d" % k))
    for c in (10, 20, 40):
        write("probe-ns2-many%d.html" % c, many(c, 30000))
    write("probe-ns2-fns.html", fns())
    write("probe-ns2-frames.html", FRAMES)
    if os.path.exists(os.path.join(here, "..", "demo", "uttt", "out", "uttt.wasm.js1.js")):
        write("probe-ns2-big.html", big())
