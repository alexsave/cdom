#!/usr/bin/env python3
"""Writes tests/probe-js1e.html: what Netscape 3.04 refuses or gets wrong in js1c's output (docs/js1-compile-design.md,
section 12), one test a SCRIPT block as tools/gen_probe_js1.py's pages on main: a syntax error kills only its block (no
mark at all), a run-time error leaves a "start" without a value.  Marks are IMG requests to
/mark/<tag>/p-<n>-<name>-<escaped value>, the tag the page's query string; tools/probe_marks.py (on main) reads them.
  for loops: which form of "for (...; ...; i--)" Netscape 3 refuses ("missing ) after for-loop control").
  ranges: |0, >>>0, &255, <<1, >>1, >>>1 on values around 2^31, 2^32, 2^53 and their negatives, and the reductions that
  bring a value into range first, with their cost.
Run: python3 tools/gen_probe_js1e.py > tests/probe-js1e.html"""

HEAD = """<HTML>
<HEAD>
<TITLE>cdom probe-js1e</TITLE>
<SCRIPT LANGUAGE="JavaScript">
var n = 0;
var TAG = "" + location.href;
var Q = TAG.indexOf("?");
if (Q < 0) { TAG = "x"; } else { TAG = TAG.substring(Q + 1, TAG.length); }
function mark(name, v) {
  n = n + 1;
  document.write('<IMG SRC="/mark/' + TAG + '/p-' + n + '-' + name + '-' + escape("" + v) + '" WIDTH=1 HEIGHT=1>');
}
function T() { var d = new Date(); return d.getTime(); }
</SCRIPT>
</HEAD>
<BODY>
<H3>cdom probe-js1e</H3>
"""

TESTS = [
    # for loops: the update and the test, one change at a time from js1c's JMc
    ("forDecGe", "var x = 0; var i; for (i = 3; i >= 0; i--) x++;", "x"),
    ("forDecGt", "var x = 0; var i; for (i = 3; i > 0; i--) x++;", "x"),
    ("forDecBraces", "var x = 0; var i; for (i = 3; i >= 0; i--) { x++; }", "x"),
    ("forDecSpaced", "var x = 0; var i; for (i = 3; i >= 0; i -- ) { x++; }", "x"),
    ("forPreDec", "var x = 0; var i; for (i = 3; i >= 0; --i) { x++; }", "x"),
    ("forIncLt", "var x = 0; var i; for (i = 0; i < 4; i++) x++;", "x"),
    ("forMinusAssign", "var x = 0; var i; for (i = 3; i >= 0; i = i - 1) x++;", "x"),
    ("forDecNoTest", "var x = 0; var i; for (i = 3; ; i--) { x++; if (i == 0) break; }", "x"),
    ("forDecInit", "var x = 0; var i = 3; for (; i >= 0; i--) x++;", "x"),
    ("forDecCall", "var x = 0; var i; function f(a) { x = x + a; } for (i = 3; i >= 0; i--) f(1);", "x"),
    ("stmtDec", "var i = 3; i--;", "i"),
    ("exprDec", "var i = 3; var j = i--;", "j + ',' + i"),
    ("whileDec", "var x = 0; var i = 4; while (i-- > 0) x++;", "x"),
    ("parenDec", "var i = 3; var j = (i--);", "j + ',' + i"),
    ("callDec", "var x = 0; function g(a) { x = a; } var i = 3; g(i--);", "x + ',' + i"),
    ("forIncCall", "var x = 0; var i; function f(a) { x = x + a; } for (i = 0; i < 4; i++) f(1);", "x"),
]

VALUES = [("p31m1", "2147483647"), ("p31", "2147483648"), ("p32m1", "4294967295"), ("p32", "4294967296"),
          ("p32p5", "4294967301"), ("p33", "8589934597"), ("p52", "4503599627370501"), ("p53", "9007199254740995"),
          ("m31", "-2147483648"), ("m31m1", "-2147483649"), ("m32", "-4294967296"), ("m32m5", "-4294967301"),
          ("m33", "-8589934597"), ("m53", "-9007199254740995")]
OPS = [("or0", "%s | 0"), ("ushr0", "%s >>> 0"), ("and255", "%s & 255"), ("shl1", "%s << 1"), ("shr1", "%s >> 1"),
       ("ushr1", "%s >>> 1")]
for vn, v in VALUES:
    for on, o in OPS:
        TESTS.append(("%s_%s" % (on, vn), "var x = %s;" % v, o % "x"))
# Reductions into the signed or unsigned 32-bit range before the bit operation.
for vn, v in [("p32p5", "4294967301"), ("p33", "8589934597"), ("m32m5", "-4294967301"), ("p52", "4503599627370501")]:
    TESTS.append(("mod_%s" % vn, "var x = %s; var y = x %% 4294967296;" % v, "y + ',' + (y | 0)"))
    TESTS.append(("sub_%s" % vn, "var x = %s; var y = x - Math.floor(x / 4294967296) * 4294967296;" % v, "y + ',' + (y | 0)"))
# Costs: 100000 of each form, the ms (a loop alone first, to take away).
COST = "var a = 1234567; var b = 3000000001; var s = 0; var i; var t = T();"
TESTS += [
    ("costLoop", COST + " for (i = 0; i < 20000; i++) { s = a + i; }", "(T() - t) + 'ms'"),
    ("costOr", COST + " for (i = 0; i < 20000; i++) { s = (a + i) | 0; }", "(T() - t) + 'ms'"),
    ("costMod", COST + " for (i = 0; i < 20000; i++) { s = ((b + i) % 4294967296) | 0; }", "(T() - t) + 'ms'"),
    ("costIf", COST + " for (i = 0; i < 20000; i++) { s = b + i; if (s > 2147483647) s = s - 4294967296; s = s | 0; }", "(T() - t) + 'ms'"),
]


def block(name, defs, expr):
    return ('<SCRIPT LANGUAGE="JavaScript">\nmark("%s", "start");\n%s\nmark("%s", %s);\n</SCRIPT>\n' % (name, defs, name, expr))


print(HEAD + "".join(block(*t) for t in TESTS) + '<SCRIPT LANGUAGE="JavaScript">\nmark("done", "end");\n</SCRIPT>\n</BODY>\n</HTML>\n', end="")
