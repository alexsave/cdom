# The page with cdom's shim inlined, and its module named or bundled (cdom.mk's cdom_page and APP_PAGE
# rule): every line of the page as it is, except
# - the one line that holds only the marker /*CDOM_SHIM*/ (spaces around it allowed), which becomes the
#   file named by -v shim=;
# - the marker /*CDOM_WASM*/ (cdom.start's first argument), which becomes the module as a JS string:
#   with -v wasm=bundle, a data:application/wasm;base64 URL of the module whose base64 is the first line
#   of standard input; with -v wasm=url, the URL in the environment's CDOM_WASM_URL; with neither, the
#   marker is an error (there is no module to put there);
# - with -v title=1, the marker /*CDOM_TITLE*/, which becomes the environment's CDOM_TITLE, escaped for
#   HTML (the default page's <title>).
# Fails, and the caller keeps no output, when the page has no /*CDOM_SHIM*/ line, more than one, or that
# marker on a line with other text (it would be lost with the line); when /*CDOM_WASM*/ is there twice;
# and, bundling, when the page has no /*CDOM_WASM*/: a page that names its module by URL would fetch it
# besides carrying it, so it is refused, naming the URL it loads, and never rewritten.
# Usage: awk -v shim=path/to/cdom.min.js [-v wasm=bundle|url] [-v title=1] -f tools/page.awk page.html > out.html
function fail(msg) {
  printf "%s\n", msg > "/dev/stderr"
  bad = 1
  exit 1
}
function js_string(s) {
  gsub(/\\/, "\\\\", s)
  gsub(/"/, "\\\"", s)
  return "\"" s "\""
}
function html_text(s) {
  gsub(/&/, "\\&amp;", s)
  gsub(/</, "\\&lt;", s)
  gsub(/>/, "\\&gt;", s)
  return s
}
# The module, as cdom.start's first argument.
function module(   b64) {
  if (wasm == "url") return js_string(ENVIRON["CDOM_WASM_URL"])
  if (wasm != "bundle") fail(sprintf("%s:%d: /*CDOM_WASM*/, and no module to put there", FILENAME, FNR))
  if ((getline b64 < "/dev/stdin") <= 0 || b64 == "") fail(sprintf("%s: cannot read the module to bundle", FILENAME))
  return "\"data:application/wasm;base64," b64 "\""
}
index($0, "/*CDOM_SHIM*/") {
  line = $0
  gsub(/^[ \t]+|[ \t\r]+$/, "", line)
  if (line != "/*CDOM_SHIM*/") fail(sprintf("%s:%d: /*CDOM_SHIM*/ must be alone on its line", FILENAME, FNR))
  if (++marks > 1) fail(sprintf("%s:%d: a second /*CDOM_SHIM*/ (the page takes the shim once)", FILENAME, FNR))
  while ((got = getline shimline < shim) > 0) print shimline
  if (got < 0) fail(sprintf("%s: cannot read the shim %s", FILENAME, shim))
  close(shim)
  next
}
{
  line = $0
  if (title && (at = index(line, "/*CDOM_TITLE*/")))
    line = substr(line, 1, at - 1) html_text(ENVIRON["CDOM_TITLE"]) substr(line, at + 14)
  if ((at = index(line, "/*CDOM_WASM*/"))) {
    rest = substr(line, at + 13)
    if (++wasms > 1 || index(rest, "/*CDOM_WASM*/"))
      fail(sprintf("%s:%d: a second /*CDOM_WASM*/ (the page starts one module)", FILENAME, FNR))
    line = substr(line, 1, at - 1) module() rest
  } else if (loads == "" && match(line, /(cdom\.start|cdomBoot)[ \t]*\([ \t]*["'`][^"'`]*/)) {
    loads = substr(line, RSTART, RLENGTH)
    sub(/^[^(]*\([ \t]*./, "", loads)
  }
  print line
}
END {
  if (bad) exit 1
  if (marks != 1)
    fail(sprintf("%s: no /*CDOM_SHIM*/ line (a <script> whose line is only the marker, where the shim goes)", FILENAME))
  if (wasm == "bundle" && !wasms) {
    if (loads != "")
      fail(sprintf("%s: this page loads %s; set APP_WASM_SEPARATE := 1, or start with the bundled module (the /*CDOM_WASM*/ marker)", FILENAME, loads))
    fail(sprintf("%s: no /*CDOM_WASM*/ for the bundled module (cdom.start(/*CDOM_WASM*/, ...)); or set APP_WASM_SEPARATE := 1", FILENAME))
  }
}
