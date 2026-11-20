#!/usr/bin/env python3
"""The HTML 2.0 lens (docs/noscript-design.md): a page as a browser of 1993 to 1995 reads it, for tests/noscript/run.sh.

  python3 tools/html2lens.py [--script] page.html

Prints what the browser shows, one item a line: IMG <src> <width>x<height> <alt>, P (a paragraph starts), BR, and
TEXT <the text>, with runs of white space made one space. The page is read as Latin-1 (Mosaic has no UTF-8).
What it models, plainly:
- A comment is <!-- to the first --> (the minimal reading, as Lynx's default and every browser of the matrix, as far as
  known, do); any other <!...> declaration (the doctype) is dropped to its >.
- A tag is < and a letter (or /), to the first > outside quotes. The elements of HTML 2.0 (RFC 1866) are known; a tag of
  any other is dropped and its content kept as text, as Mosaic does with what it does not know. So a <script>'s or a
  <style>'s text shows, unless it is in a comment.
- TITLE's text is the window's, not the page's: not shown. So is everything in HEAD but text: that is how the
  browsers of the time drew a stray text in the head, in the page, and it is shown here too.
- --script models Netscape 2: script, and no <noscript>. SCRIPT is known and its text never shown; the one script it
  runs is the fallback's own, document.write("<!-"+"-"), as the comment it opens at the script's end.
"""
import re
import sys

HTML2 = set('a address b base blockquote body br cite code dd dir dl dt em form h1 h2 h3 h4 h5 h6 head hr html i img '
            'input isindex kbd li link listing menu meta nextid ol option p plaintext pre samp select strong textarea '
            'title tt ul var xmp'.split())
HIDER = 'document.write("<!-"+"-")'
ENT = {'amp': '&', 'lt': '<', 'gt': '>', 'quot': '"'}


def text_of(s):
    s = re.sub(r'&(#(\d+)|(\w+));?', lambda m: chr(int(m.group(2))) if m.group(2) else ENT.get(m.group(3), m.group(0)), s)
    return s


def attrs_of(tag):
    out = {}
    for m in re.finditer(r'([A-Za-z-]+)\s*(?:=\s*("([^"]*)"|\'([^\']*)\'|([^\s>]+)))?', tag):
        out[m.group(1).lower()] = text_of(m.group(3) if m.group(3) is not None else m.group(4) if m.group(4) is not None else m.group(5) or '')
    return out


def lens(page, script):
    items, text, i, n, in_title = [], [], 0, len(page), False

    def flush():
        t = ' '.join(''.join(text).split())
        if t:
            items.append(f'TEXT {t}')
        text.clear()

    while i < n:
        c = page[i]
        if page.startswith('<!--', i):
            j = page.find('-->', i + 4)
            i = n if j < 0 else j + 3
            continue
        if page.startswith('<!', i):
            j = page.find('>', i)
            i = n if j < 0 else j + 1
            continue
        m = re.compile(r'<(/?)([A-Za-z][A-Za-z0-9]*)').match(page, i)
        if not m:
            if not in_title:
                text.append(c)
            i += 1
            continue
        j, q = m.end(), None
        while j < n and (q or page[j] != '>'):
            if page[j] in '"\'':
                q = None if q == page[j] else (q or page[j])
            j += 1
        tag, end, name = page[m.end():j], m.group(1) == '/', m.group(2).lower()
        i = j + 1
        if script and name == 'script' and not end:
            k = page.lower().find('</script', i)
            k = n if k < 0 else k
            body = page[i:k]
            k = page.find('>', k)
            i = n if k < 0 else k + 1
            if HIDER in body:
                page = page[:i] + '<!--' + page[i:]
                n = len(page)
            continue
        if name not in HTML2:
            continue
        if name == 'title':
            in_title = not end
            continue
        if name == 'img' and not end:
            flush()
            a = attrs_of(tag)
            items.append(f"IMG {a.get('src', '')} {a.get('width', '?')}x{a.get('height', '?')} {a.get('alt', '')}")
        elif name == 'p' and not end:
            flush()
            items.append('P')
        elif name == 'br':
            flush()
            items.append('BR')
        elif name in ('h1', 'h2', 'h3', 'h4', 'h5', 'h6', 'pre', 'hr', 'ul', 'ol', 'dl', 'li', 'blockquote', 'address'):
            flush()
    flush()
    return items


if __name__ == '__main__':
    args = sys.argv[1:]
    script = args[:1] == ['--script']
    if script:
        args = args[1:]
    if len(args) != 1:
        sys.exit(__doc__)
    with open(args[0], 'rb') as f:
        page = f.read().decode('latin-1')
    for item in lens(page, script):
        print(item)
