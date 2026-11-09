# The page with cdom's shim inlined (cdom.mk's cdom_page and APP_PAGE rule): every line of the page as it
# is, except the one line that holds only the marker /*CDOM_SHIM*/ (spaces around it allowed), which
# becomes the file named by -v shim=. Fails, and the caller keeps no output, when the page has no such
# line, more than one, or the marker on a line with other text (it would be lost with the line).
# Usage: awk -v shim=path/to/cdom.min.js -f tools/page.awk page.html > out.html
index($0, "/*CDOM_SHIM*/") {
  line = $0
  gsub(/^[ \t]+|[ \t\r]+$/, "", line)
  if (line != "/*CDOM_SHIM*/") {
    printf "%s:%d: /*CDOM_SHIM*/ must be alone on its line\n", FILENAME, FNR > "/dev/stderr"
    bad = 1
    exit 1
  }
  if (++marks > 1) {
    printf "%s:%d: a second /*CDOM_SHIM*/ (the page takes the shim once)\n", FILENAME, FNR > "/dev/stderr"
    bad = 1
    exit 1
  }
  while ((got = getline shimline < shim) > 0) print shimline
  if (got < 0) {
    printf "%s: cannot read the shim %s\n", FILENAME, shim > "/dev/stderr"
    bad = 1
    exit 1
  }
  close(shim)
  next
}
{ print }
END {
  if (!bad && marks != 1) {
    printf "%s: no /*CDOM_SHIM*/ line (a <script> whose line is only the marker, where the shim goes)\n", FILENAME > "/dev/stderr"
    exit 1
  }
}
