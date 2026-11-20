#!/usr/bin/env python3
"""Writes tests/probe-js1.html: what a JavaScript 1.0 engine (Netscape 2, IE 3) has, one test per <script>.

Each test is its own block, so a syntax error (the block never runs: no "start" mark) or a run-time error (a "start"
mark and no "ok") kills only that block.
A block reports through document.write of a 1x1 image, /mark/<tag>/p-<n>-<name>-<escaped value>, while the page loads;
n counts, so the order survives.  The tag is the page's query string (probe-js1.html?ns2).
tools/probe_marks.py reads a serve.py log and prints the table.
Run: python3 tools/gen_probe_js1.py > tests/probe-js1.html
     python3 tools/gen_probe_js1.py part 1 30 > tests/probe-js1-part1.html   (30 tests from the 31st: for IE 4, which stopped
     on the whole page after 40 blocks)
     python3 tools/gen_probe_js1.py speed speedLoop > tests/probe-speed-loop.html   (one speed test alone, one page each: the
     marks of a page reach the server only when its scripts end in Netscape 2, so the wall time of a page, from its GET
     to its marks, is the measure, and the in-engine ms is Date's, which Netscape 2 gets wrong)
     python3 tools/gen_probe_js1.py b > tests/probe-js1b.html   (the second probe: the questions netscape.exe raised, see
     docs/descent.md "The second probe"; the recursion depth grid comes last, so an engine that stops on a stack overflow
     has reported everything else first)
     python3 tools/gen_probe_js1.py nested > tests/probe-nested.html   (a function inside a function, alone: the alert's text)
     python3 tools/gen_probe_js1.py events > tests/probe-events.html   (which event handlers fire: driven by hand)
Written for JS 1.0: no typeof, no try, no literals, no function expressions in the helper block."""

import sys

HEAD = """<HTML>
<HEAD>
<TITLE>cdom probe-js1</TITLE>
<SCRIPT LANGUAGE="JavaScript">
var n = 0;
var TAG = "" + location.search;
TAG = TAG.substring(1, TAG.length);
if (TAG == "") { TAG = "x"; }
function mark(name, v) {
  n = n + 1;
  document.write('<IMG SRC="/mark/' + TAG + '/p-' + n + '-' + name + '-' + escape("" + v) + '" WIDTH=1 HEIGHT=1>');
}
var late_n = 0;
function late(name, v) {
  late_n = late_n + 1;
  var u = '/mark/' + TAG + '/p-L' + late_n + '-' + name + '-' + escape("" + v);
  if (window.Image) {
    var im = new Image();
    im.src = u;
  } else {
    document.write('<IMG SRC="' + u + '" WIDTH=1 HEIGHT=1>');
  }
}
</SCRIPT>
</HEAD>
<BODY>
<H3>cdom probe-js1</H3>
<P>Reports to /mark/ as it loads.
<FORM NAME="f0"><INPUT TYPE=text NAME="t0" VALUE="v0"></FORM>
"""

# (name, top-level definitions, expression).  The block is: mark start, mark the expression, mark ok.
TESTS = [
    ("ua", "", "navigator.userAgent"),
    ("appVersion", "", "navigator.appVersion"),
    ("appName", "", "navigator.appName"),
    ("onerror_pre", "", "window.onerror"),
    # arrays and types
    ("newArray3", "var a = new Array(3);", "a.length"),
    ("newArrayCall", "", "new Array(3).length"),
    ("typeof", "", "typeof 1"),
    ("typeofUndef", "", "typeof zzUndefined"),
    ("void", "", "void 0"),
    ("protoAssign", "function A(){}; A.prototype.x = 1; var a = new A();", "a.x"),
    ("objLit", "var o = {a:1};", "o.a"),
    ("arrLit", "var a = [1,2];", "a.length"),
    ("fnExpr", "", "(function(){return 1})()"),
    ("newFunction", "var f = new Function('a', 'return a+1');", "f(1)"),
    ("newFunctionCall", "", "new Function('a', 'return a+1')(1)"),
    ("arrJoin", "var a = new Array(3); a[0]=1; a[1]=2; a[2]=3;", "a.join('-')"),
    ("arrGrow", "var a = new Array(3); a[5] = 1;", "a.length"),
    ("arrSort", "var a = new Array(3); a[0]=3; a[1]=1; a[2]=2; a.sort();", "a.join('')"),
    ("arrReverse", "var a = new Array(3); a[0]=3; a[1]=1; a[2]=2; a.reverse();", "a.join('')"),
    ("arrPush", "var a = new Array(1); a.push(5);", "a.length"),
    ("arrPop", "var a = new Array(2); a[1] = 7;", "a.pop()"),
    ("arrSlice", "var a = new Array(3); a[0]=1; a[1]=2; a[2]=3;", "a.slice(1).length"),
    ("arrSplice", "var a = new Array(3); a[0]=1; a[1]=2; a[2]=3; a.splice(1,1);", "a.length"),
    ("arrConcat", "var a = new Array(1); var b = new Array(2);", "a.concat(b).length"),
    ("arrNoLen", "var o = new Object(); o.length = 0;", "o.length"),
    ("ctorProp", "function A(){}; var a = new A();", "a.constructor == A"),
    ("instanceof", "function A(){}; var a = new A();", "a instanceof A"),
    ("dateGetTime", "var d = new Date();", "d.getTime() > 0"),
    ("dateGetTimeCall", "", "new Date().getTime() > 0"),
    ("isNaN", "", "isNaN(NaN)"),
    ("NaNGlobal", "", "NaN"),
    ("InfinityGlobal", "", "Infinity"),
    ("undefinedGlobal", "", "undefined"),
    ("stringProto", "String.prototype.foo = 1;", "'a'.foo"),
    ("objectProto", "Object.prototype.bar = 2; var o = new Object();", "o.bar"),
    # strings
    ("strLength", "", "'abc'.length"),
    ("charCodeAt", "", "'A'.charCodeAt(0)"),
    ("fromCharCode", "", "String.fromCharCode(65,66)"),
    ("split", "", "'a,b,c'.split(',').length"),
    ("charAt", "", "'abc'.charAt(1)"),
    ("indexOf", "", "'abcb'.indexOf('b')"),
    ("indexOfFrom", "", "'abcb'.indexOf('b', 2)"),
    ("lastIndexOf", "", "'abcb'.lastIndexOf('b')"),
    ("substring", "", "'abcdef'.substring(1,3)"),
    ("substr", "", "'abcdef'.substr(1,3)"),
    ("slice", "", "'abcdef'.slice(1,3)"),
    ("toUpperCase", "", "'abc'.toUpperCase()"),
    ("replace", "", "'aXb'.replace('X','-')"),
    ("strCompare", "", "'a' < 'b'"),
    ("strIndex", "", "'ab'[1]"),
    ("escape", "", "escape('a b\\xe9')"),
    ("unescape", "", "unescape('%41%20')"),
    ("encodeURIComponent", "", "encodeURIComponent('a b')"),
    ("strConcatNum", "", "'a' + 1 + 2"),
    ("toStringNum", "", "(255).toString(16)"),
    ("toStringBin", "", "(5).toString(2)"),
    # numbers
    ("bitAnd", "", "6 & 3"),
    ("bitOr", "", "6 | 3"),
    ("bitXor", "", "6 ^ 3"),
    ("bitNot", "", "~5"),
    ("shl", "", "1 << 4"),
    ("shr", "", "-16 >> 2"),
    ("ushr", "", "-16 >>> 28"),
    ("ushrNeg1", "", "(-1>>>0)"),
    ("shl31", "", "(1<<31)"),
    ("add2p31", "", "0x7fffffff+1"),
    ("big53", "", "9007199254740993"),
    ("bigMul", "", "65535*65535*65535"),
    ("floor", "", "Math.floor(-1.5)"),
    ("parseInt16", "", "parseInt('ff', 16)"),
    ("parseIntHexLit", "", "parseInt('0x1f')"),
    ("parseFloat", "", "parseFloat('1.5e2x')"),
    ("mod", "", "-7 % 3"),
    ("div", "", "1/3"),
    ("hexLit", "", "0xff"),
    ("mathAbs", "", "Math.abs(-2)"),
    ("mathSqrt", "", "Math.sqrt(16)"),
    ("mathPow", "", "Math.pow(2,10)"),
    ("mathRound", "", "Math.round(2.5)"),
    ("mathMax", "", "Math.max(1,3,2)"),
    # objects
    ("numKeys", "var o = new Object(); o[1] = 'a'; o[2] = 'b';", "o[1] + o[2]"),
    ("numKeyAsStr", "var o = new Object(); o[1] = 'a';", "o['1']"),
    ("forIn", "var o = new Object(); o.a = 1; o.b = 2; var s = ''; var k; for (k in o) { s = s + k; }", "s"),
    ("forInVar", "var o = new Object(); o.a = 1; function fi(){ var s = ''; for (var k in o) { s = s + k; } return s; }", "fi()"),
    ("argsLength", "function g(){ return arguments.length; }", "g(1,2,3)"),
    ("argsIndex", "function g(){ return arguments[1]; }", "g(1,2,3)"),
    ("fnArguments", "function g(){ return g.arguments.length; }", "g(1,2,3)"),
    ("hasArrayObject", "", "window.Array"),
    ("hasDateObject", "", "window.Date"),
    ("recurse300", "function r(n){ if (n == 0) { return 0; } return 1 + r(n-1); }", "r(300)"),
    ("recurse500", "function r(n){ if (n == 0) { return 0; } return 1 + r(n-1); }", "r(500)"),
    ("callee", "function g(){ return arguments.callee == g; }", "g()"),
    ("fnLength", "function g(a,b){ return 1; }", "g.length"),
    ("recurse100", "function r(n){ if (n == 0) { return 0; } return 1 + r(n-1); }", "r(100)"),
    ("recurse1000", "function r(n){ if (n == 0) { return 0; } return 1 + r(n-1); }", "r(1000)"),
    ("recurse5000", "function r(n){ if (n == 0) { return 0; } return 1 + r(n-1); }", "r(5000)"),
    ("closureVar", "function mk(){ var c = 5; function g(){ return c; } return g; } var h = mk();", "h()"),
    ("thisInMethod", "var o = new Object(); function m(){ return this.v; } o.v = 4; o.m = m;", "o.m()"),
    ("newCtor", "function P(x){ this.x = x; } var p = new P(3);", "p.x"),
    ("newCtorCall", "function P(x){ this.x = x; }", "new P(3).x"),
    ("hasOwnProperty", "var o = new Object(); o.a = 1;", "o.hasOwnProperty('a')"),
    ("toStringObj", "var o = new Object();", "o.toString()"),
    # statements
    ("switch", "function s(x){ switch (x) { case 1: return 'one'; case 2: return 'two'; default: return 'other'; } }", "s(2)"),
    ("switchStr", "function s(x){ switch (x) { case 'a': return 'A'; default: return 'other'; } }", "s('a')"),
    ("doWhile", "function d(){ var i = 0; do { i++; } while (i < 3); return i; }", "d()"),
    ("labeled", "function l(){ var i = 0; a: for (;;) { i++; if (i > 3) { break a; } } return i; }", "l()"),
    ("regexLit", "", "/a+/.test('caat')"),
    ("regexExec", "", "/(a+)/.exec('caat')[1]"),
    ("regexNew", "", "new RegExp('a+').test('caat')"),
    ("strictEq", "", "1 === 1"),
    ("inOp", "var oo = new Object(); oo.a = 1;", "'a' in oo"),
    ("tryCatch", "function tc(){ try { return 1; } catch (e) { return 2; } }", "tc()"),
    ("tryFinally", "function tf(){ try { return 1; } finally { } }", "tf()"),
    ("throw", "function th(){ throw 1; }", "1"),
    ("delete", "function d(){ var o = new Object(); o.a = 1; delete o.a; return '' + o.a; }", "d()"),
    ("ternary", "", "true ? 'y' : 'n'"),
    ("comma", "", "(1,2)"),
    ("withStmt", "var o = new Object(); o.a = 5; function w(){ with (o) { return a; } }", "w()"),
    ("whileBreakContinue", "function w(){ var i = 0; var s = 0; while (i < 10) { i++; if (i == 2) { continue; } if (i == 5) { break; } s = s + i; } return s; }", "w()"),
    ("nestedFunc", "function outer(){ function inner(){ return 3; } return inner(); }", "outer()"),
    ("fnAssignedVar", "var f = function(){ return 7; };", "f()"),
    ("evalBasic", "", "eval('1+2')"),
    ("evalFn", "", "eval('function(){}')"),
    # timers
    ("setTimeoutStr", "setTimeout(\"late('tstr','fired')\", 200);", "'set'"),
    ("setTimeoutFn", "function lf(){ late('tfn','fired'); } setTimeout(lf, 200);", "'set'"),
    ("setInterval", "var ic = 0; function iv(){ ic++; if (ic == 3) { late('interval','3'); clearInterval(ih); } } var ih = setInterval(iv, 50);", "'set'"),
    # browser objects
    ("documentImages", "", "document.images.length"),
    ("documentImagesType", "", "document.images"),
    ("newImage", "var im = new Image(); im.src = '/mark/' + TAG + '/p-img-newImage-loaded';", "im.src"),
    ("formsLength", "", "document.forms.length"),
    ("formRead", "", "document.forms[0].elements[0].value"),
    ("formWrite", "document.forms[0].elements[0].value = 'v1';", "document.forms[0].elements[0].value"),
    ("formByName", "", "document.f0.t0.value"),
    ("locationHref", "", "location.href"),
    ("documentLastModified", "", "document.lastModified"),
    ("windowName", "", "window.name"),
    ("historyLength", "", "history.length"),
    ("javaEnabled", "", "navigator.javaEnabled()"),
    ("pluginsLen", "", "navigator.plugins.length"),
    ("documentGetElementById", "", "document.getElementById"),
    ("documentAll", "", "document.all"),
    ("documentLayers", "", "document.layers"),
    ("documentCookie", "", "document.cookie"),
    ("screenWidth", "", "screen.width"),
    ("innerWidth", "", "window.innerWidth"),
    ("documentBgColor", "", "document.bgColor"),
]
# The second probe (tests/probe-js1b.html): what reading netscape.exe left open.  Double quotes throughout (the first
# probe's string tests used single quotes).
DEPTHS = [50, 100, 120, 140, 160, 180, 200, 225, 250, 275, 300, 400, 512, 600]
RFUNCS = [
    ("r0", "function r0(n){ if (n == 0) { return 0; } return r0(n-1); }", "r0(%d) == 0"),
    ("r1", "function r1(n){ if (n == 0) { return 0; } return 1 + r1(n-1); }", "r1(%d)"),
    ("r3", "function r3(n,a,b,c){ if (n == 0) { return 0; } return r3(n-1,a,b,c); }", "r3(%d,1,2,3) == 0"),
]
SPLIT = 'var s = "a,b,c"; var p = s.split(",");'
HELLO = 'var s = "hello";'
TESTS_B = [
    ("ua", "", "navigator.userAgent"),
    # split on a variable, and its parts one by one (the array may lack a length even if split works)
    ("splitVarLen", SPLIT, "p.length"),
    ("splitVar0", SPLIT, "p[0]"),
    ("splitVar1", SPLIT, "p[1]"),
    ("splitVar2", SPLIT, "p[2]"),
    ("splitVarStr", SPLIT, '"" + p'),
    ("splitLitVar0", 'var p = "a,b,c".split(",");', "p[0]"),
    ("splitLitLen", "", '"a,b,c".split(",").length'),
    # methods on a string literal at all
    ("litLength", "", '"abc".length'),
    ("litCharAt", "", '"abc".charAt(1)'),
    ("litIndexOf", "", '"abc".indexOf("c")'),
    # new Array
    ("newArray3Len", "var a = new Array(3);", "a.length"),
    ("newArray3El0", "var a = new Array(3);", "a[0]"),
    ("newArray3Grow", "var a = new Array(3); a[5] = 1;", "a.length"),
    ("newArray3Str", "var a = new Array(3);", '"" + a'),
    ("newArrayEmptyLen", "var a = new Array();", "a.length"),
    ("newArrayEmptyGrow", "var a = new Array(); a[0] = 1; a[1] = 2;", "a.length"),
    ("newArrayList1", "var a = new Array(1,2,3);", "a[1]"),
    ("newArrayListLen", "var a = new Array(1,2,3);", "a.length"),
    ("newArrayListStr", "var a = new Array(1,2,3);", '"" + a'),
    # chop, match, substr on a variable
    ("chop", HELLO, "s.chop()"),
    ("chopAfter", HELLO + " var c = s.chop();", "s"),
    ("match", HELLO, 's.match("ell")'),
    ("matchMiss", HELLO, 's.match("zz")'),
    ("substrVar", HELLO, "s.substr(1,2)"),
    # an assignment written as a condition: assigned (x becomes y) or compared (x stays)?
    ("ifAssign05", 'var x = 0; var y = 5; var r = "none"; if (x = y) { r = "assigned-true"; } else { r = "compared-false"; }', 'r + " x=" + x'),
    ("ifAssign00", 'var x = 0; var y = 0; var r = "none"; if (x = y) { r = "compared-true"; } else { r = "assigned-false"; }', 'r + " x=" + x'),
    ("whileAssign03", "var x = 0; var y = 3; var c = 0; while (x = y) { c = c + 1; if (c > 5) { break; } }", '"c=" + c + " x=" + x'),
    ("whileAssign00", "var x = 0; var y = 0; var c = 0; while (x = y) { c = c + 1; if (c > 5) { break; } }", '"c=" + c + " x=" + x'),
] + [("depth%s_%d" % (f[1:], d), defs, expr % d) for d in DEPTHS for (f, defs, expr) in RFUNCS]

NESTED = [("nestedFunc", "function f(){ function g(){ return 1; } return g(); }", "f()")]

EVENTS = """<HTML>
<HEAD>
<TITLE>cdom probe-events</TITLE>
<SCRIPT LANGUAGE="JavaScript">
var TAG = "" + location.search;
TAG = TAG.substring(1, TAG.length);
if (TAG == "") { TAG = "x"; }
function ev(s) {
  document.forms[0].log.value = document.forms[0].log.value + s + " ";
}
function report() {
  location.href = "/mark/" + TAG + "/p-1-events-" + escape(document.forms[0].log.value);
}
</SCRIPT>
</HEAD>
<BODY>
<H3>cdom probe-events</H3>
<FORM NAME="f0">
<INPUT TYPE=text NAME="log" SIZE=60 VALUE=""><BR>
<INPUT TYPE=button NAME="b" VALUE="press me" onClick="ev('click')" onMouseDown="ev('mousedown')" onMouseUp="ev('mouseup')"><BR>
<INPUT TYPE=text NAME="k" SIZE=20 VALUE="" onKeyDown="ev('keydown')" onKeyPress="ev('keypress')" onKeyUp="ev('keyup')" onChange="ev('change')"><BR>
<INPUT TYPE=button NAME="r" VALUE="report" onClick="report()">
</FORM>
<P><A HREF="#top" onMouseOver="ev('over'); return true" onMouseOut="ev('out')">hover link</A>
</BODY>
</HTML>
"""


# Speed: ms from T().  A start mark, the ms, an ok mark.
TDEF = "function T(){ var d = new Date(); return d.getTime(); } "
SPEED = [
    ("speedLoop", TDEF + "function t(){ var x = 0; var i; var a = T(); for (i = 0; i < 100000; i++) { x = (x + i) & 0xffff; } return (T() - a) + 'ms x=' + x; }", "t()"),
    ("speedObjKeys", TDEF + "function t(){ var m = new Object(); var i; var a = T(); for (i = 0; i < 100000; i++) { m[i & 1023] = i; } return (T() - a) + 'ms m5=' + m[5]; }", "t()"),
    ("speedArrKeys", TDEF + "function t(){ var m = new Array(1024); var i; var a = T(); for (i = 0; i < 100000; i++) { m[i & 1023] = i; } return (T() - a) + 'ms m5=' + m[5]; }", "t()"),
    ("speedConcat", TDEF + "function t(){ var s = ''; var i; var a = T(); for (i = 0; i < 10000; i++) { s = s + 'a'; } return (T() - a) + 'ms len=' + s.length; }", "t()"),
    ("speedCharCodeAt", TDEF + "function t(){ var s = 'aaaaaaaaaa'; var i; for (i = 0; i < 10; i++) { s = s + s; } if (!s.charCodeAt) { return 'absent'; } var x = 0; var a = T(); for (i = 0; i < 10000; i++) { x = x + s.charCodeAt(i); } return (T() - a) + 'ms x=' + x + ' len=' + s.length; }", "t()"),
    ("speedConcat1000", TDEF + "function t(){ var s = ''; var i; var a = T(); for (i = 0; i < 1000; i++) { s = s + 'a'; } return (T() - a) + 'ms len=' + s.length; }", "t()"),
    ("speedConcat2000", TDEF + "function t(){ var s = ''; var i; var a = T(); for (i = 0; i < 2000; i++) { s = s + 'a'; } return (T() - a) + 'ms len=' + s.length; }", "t()"),
    ("speedCall", TDEF + "function f(x){ return x + 1; } function t(){ var x = 0; var i; var a = T(); for (i = 0; i < 100000; i++) { x = f(x); } return (T() - a) + 'ms x=' + x; }", "t()"),
]


def block(name, defs, expr):
    """Start first, then the definitions, so a run-time error in them still shows as start without ok."""
    return ('<SCRIPT LANGUAGE="JavaScript">\nmark("%s", "start");\n%s\nmark("%s", %s);\nmark("%s", "ok");\n</SCRIPT>\n'
            % (name, defs, name, expr, name))


def main():
    out = [HEAD]
    if sys.argv[1:2] == ["part"]:
        # part <k> <size>: tests k*size .. (k+1)*size, for an engine that hangs on one block of the whole page
        k, size = int(sys.argv[2]), int(sys.argv[3])
        out.append("".join(block(*t) for t in TESTS[k * size:(k + 1) * size] if not t[0].startswith("recurse")))
        out.append('<SCRIPT LANGUAGE="JavaScript">\nmark("done", "end");\n</SCRIPT>\n</BODY>\n</HTML>\n')
        print("".join(out), end="")
        return
    if sys.argv[1:2] == ["events"]:
        print(EVENTS, end="")
        return
    if sys.argv[1:2] in (["b"], ["nested"]):
        out[0] = out[0].replace("probe-js1</", "probe-js1%s</" % ("b" if sys.argv[1] == "b" else "-nested"))
        out.append("".join(block(*t) for t in (TESTS_B if sys.argv[1] == "b" else NESTED)))
        out.append('<SCRIPT LANGUAGE="JavaScript">\nmark("done", "end");\n</SCRIPT>\n</BODY>\n</HTML>\n')
        print("".join(out), end="")
        return
    which = [a for a in sys.argv[1:] if a != "speed"]
    tests = [t for t in SPEED if t[0] == which[0]] if which else (SPEED if "speed" in sys.argv[1:] else TESTS)
    for i, (name, defs, expr) in enumerate(tests):
        out.append(block(name, defs, expr))
    out.append('<SCRIPT LANGUAGE="JavaScript">\nmark("done", "end");\n</SCRIPT>\n</BODY>\n</HTML>\n')
    print("".join(out), end="")


main()
