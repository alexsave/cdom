#!/usr/bin/env python3
# reindent4.py - cdom's indentation rule: every file in scope indents with 4 spaces; Makefiles and *.mk keep tabs.
#
#   python3 -I tools/reindent4.py                 convert every file in scope, in place
#   python3 -I tools/reindent4.py PATH...         convert the given files only
#   python3 -I tools/reindent4.py --verify        exit 1 and list the offenders: a code line indented with a tab,
#                                                 or a file whose code lines are indented in units of 2
#   python3 -I tools/reindent4.py --stats PATH... print each file's detected unit and line counts (no writes)
#
# Scope: the files git ls-files lists with one of SCOPE_EXTS, outside third_party/, node_modules/, results/ and any
# out/ directory, and not generated (the words "generated" or "do not edit" in the first five lines, any case).
#
# A line is blank, literal or code. A literal line starts inside a multi-line string (a JS template literal, a Python
# triple-quoted string, a shell heredoc body, a C string continued with a backslash, HTML pre and textarea content):
# it is never changed, and neither is a blank line. Every other code line takes the innermost open frame at its start:
#   "(" or "[", a block comment, an HTML tag, or a backslash continuation: a continuation line keeps its offset from
#         the line that opened the frame: new = old + (opener.new - opener.old), so argument alignment and the " * "
#         columns of a comment stay where they were relative to their opener.
#   otherwise (no frame, or a "{" block, which is structural even inside a call's parentheses): new = 2 * old.
# Structural lines therefore double, and a block's lines double from their own old indent, so a callback body keeps
# its depth under the call, and a function whose signature wraps keeps its body under the signature's start line.
# A file whose code lines are in units of 2 is converted by these rules. A file whose code lines are in units of 4, or
# have no indent, only gets its leading tabs turned into 4 spaces each. A file with an odd indent is skipped.
# The unit is read from the code lines outside any "(", "[", comment, tag or backslash continuation.
#
# The checker is tools/reindent4_check.py; --verify is the test that fails on a tab or a 2-space unit in code.

import os
import re
import subprocess
import sys

SCOPE_EXTS = ('.c', '.h', '.js', '.mjs', '.ts', '.tsx', '.html', '.css', '.sh', '.py', '.swift')
EXCLUDED_DIRS = ('third_party', 'node_modules', 'out', 'results')
LANG_OF = {
    '.c': 'c', '.h': 'c', '.swift': 'c', '.js': 'js', '.mjs': 'js', '.ts': 'js', '.tsx': 'js',
    '.css': 'css', '.py': 'py', '.sh': 'sh', '.html': 'html',
}
REGEX_AFTER_WORD = {'return', 'typeof', 'case', 'do', 'else', 'in', 'of', 'new', 'delete', 'void', 'throw',
                    'instanceof', 'yield', 'await'}
REGEX_AFTER_CHAR = set('(,=:[!&|?{};+-*%<>~^')
CLOSER = {')': '(', ']': '[', '}': '{'}
CONTINUATION_KINDS = ('(', '[', 'c', 'hc', 'tag')


class Frame:
    __slots__ = ('kind', 'name', 'old', 'new')

    def __init__(self, kind, name=''):
        self.kind = kind
        self.name = name
        self.old = 0
        self.new = 0


class Rec:
    __slots__ = ('kind', 'old', 'tab', 'frame', 'in_paren', 'lineno')

    def __init__(self, kind, lineno):
        self.kind = kind
        self.old = 0
        self.tab = False
        self.frame = None
        self.in_paren = False
        self.lineno = lineno


class Scanner:
    def __init__(self, lang):
        self.file_lang = lang
        self.lang = lang          # the lexer in force: for an HTML file, 'html', or 'js'/'css' inside a script or style
        self.embed = False
        self.stack = []
        self.mode = None          # None, or a literal: ('str', q, multi), ('tpl',), ('triple', q3), ('heredoc', word, dash), ('raw', name)
        self.pending = []         # shell heredocs opened on this line, whose bodies start on the next line
        self.last = None          # JS: the last code token, for telling a regex from a division
        self.last_word = None
        self.cur_old = 0
        self.cur_new = 0

    def push(self, kind, name=''):
        f = Frame(kind, name)
        f.old = self.cur_old
        f.new = self.cur_new
        self.stack.append(f)
        return f

    def is_literal_start(self):
        return self.mode is not None or any(f.kind == 'tplx' for f in self.stack)

    def end_embed(self):
        self.stack = []
        self.mode = None
        self.lang = 'html'
        self.embed = False

    def scan(self, s):
        if self.embed:
            m = re.search(r'</(script|style)', s, re.I)
            if m:
                self._scan(s[:m.start()])
                self.end_embed()
                self._scan(s[m.start():])
                return
        self._scan(s)
        if self.pending and self.mode is None:
            word, dash = self.pending.pop(0)
            self.mode = ('heredoc', word, dash)

    def ends_in_backslash(self, s):
        return s.endswith('\\') and self.mode is None and not (self.stack and self.stack[-1].kind in ('c', 'hc'))

    def _scan(self, s):
        i, n = 0, len(s)
        while i < n:
            if self.mode is not None:
                i = self.scan_literal(s, i)
                continue
            top = self.stack[-1] if self.stack else None
            if top is not None and top.kind in ('c', 'hc'):
                end = '*/' if top.kind == 'c' else '-->'
                j = s.find(end, i)
                if j < 0:
                    return
                self.stack.pop()
                i = j + len(end)
                continue
            if top is not None and top.kind == 'tag':
                i = self.scan_tag(s, i)
                continue
            if self.lang == 'html':
                if s.startswith('<!--', i):
                    self.push('hc')
                    i += 4
                    continue
                m = re.match(r'<(/?)([A-Za-z][A-Za-z0-9-]*|!)', s[i:])
                if m:
                    name = m.group(2).lower()
                    self.push('tag', ('/' if m.group(1) else '') + name)
                    i += m.end()
                    continue
                i += 1
                continue
            ch = s[i]
            if self.lang in ('js', 'c', 'css') and s.startswith('/*', i):
                self.push('c')
                i += 2
                continue
            if self.lang in ('js', 'c') and s.startswith('//', i):
                return
            if self.lang == 'py' and ch == '#':
                return
            if self.lang == 'sh' and ch == '#' and (i == 0 or s[i - 1] in ' \t;&|('):
                return
            if ch in '"\'`' and not (ch == '`' and self.lang != 'js'):
                if self.lang == 'py' and s.startswith(ch * 3, i):
                    self.mode = ('triple', ch * 3)
                    i += 3
                    continue
                if ch == '`':
                    self.mode = ('tpl',)
                else:
                    self.mode = ('str', ch, self.lang == 'sh')
                i += 1
                self.last = 's'
                continue
            if self.lang == 'sh' and s.startswith('<<', i) and not s.startswith('<<<', i):
                m = re.match(r"<<(-?)[ \t]*(['\"]?)([A-Za-z_][A-Za-z0-9_]*)\2", s[i:])
                if m:
                    self.pending.append((m.group(3), m.group(1) == '-'))
                    i += m.end()
                    continue
            if self.lang == 'js' and ch == '/':
                if self.last is None or (self.last == 'w' and self.last_word in REGEX_AFTER_WORD) or \
                        (self.last not in (None, 'w') and self.last in REGEX_AFTER_CHAR):
                    i = self.scan_regex(s, i)
                    self.last = 's'
                    continue
            if ch in '([{':
                self.push(ch)
                self.last = ch
                i += 1
                continue
            if ch in ')]}':
                if ch == '}' and top is not None and top.kind == 'tplx':
                    self.stack.pop()
                    self.mode = ('tpl',)
                    i += 1
                    continue
                for k in range(len(self.stack) - 1, -1, -1):
                    if self.stack[k].kind == CLOSER[ch]:
                        del self.stack[k:]
                        break
                self.last = ch
                i += 1
                continue
            if ch.isalnum() or ch in '_$':
                j = i
                while j < n and (s[j].isalnum() or s[j] in '_$'):
                    j += 1
                self.last = 'w'
                self.last_word = s[i:j]
                i = j
                continue
            if ch.isspace():
                i += 1
                continue
            self.last = ch
            i += 1

    def scan_regex(self, s, i):
        j, n, in_class = i + 1, len(s), False
        while j < n:
            c = s[j]
            if c == '\\':
                j += 2
                continue
            if in_class:
                in_class = c != ']'
            elif c == '[':
                in_class = True
            elif c == '/':
                j += 1
                while j < n and s[j].isalpha():
                    j += 1
                return j
            j += 1
        return n

    def scan_literal(self, s, i):
        m = self.mode
        kind = m[0]
        if kind == 'str':
            q, multi = m[1], m[2]
            j = i
            while j < len(s):
                c = s[j]
                if c == '\\':
                    j += 2
                    continue
                if c == q:
                    self.mode = None
                    return j + 1
                j += 1
            if not (multi or s.endswith('\\')):
                self.mode = None
            return len(s)
        if kind == 'tpl':
            j = i
            while j < len(s):
                c = s[j]
                if c == '\\':
                    j += 2
                    continue
                if c == '`':
                    self.mode = None
                    return j + 1
                if s.startswith('${', j):
                    self.mode = None
                    self.push('tplx')
                    return j + 2
                j += 1
            return len(s)
        if kind == 'triple':
            j = s.find(m[1], i)
            if j < 0:
                return len(s)
            self.mode = None
            return j + 3
        if kind == 'heredoc':
            word, dash = m[1], m[2]
            if (s.lstrip('\t') if dash else s) == word:
                self.mode = None
            return len(s)
        if kind == 'raw':
            j = s.lower().find('</' + m[1], i)
            if j < 0:
                return len(s)
            self.mode = None
            return j
        raise AssertionError(kind)

    def scan_tag(self, s, i):
        top = self.stack[-1]
        j, n = i, len(s)
        while j < n:
            c = s[j]
            if c in '"\'':
                k = s.find(c, j + 1)
                if k < 0:
                    return n
                j = k + 1
                continue
            if c == '>':
                self.stack.pop()
                name = top.name
                if name in ('pre', 'textarea'):
                    self.mode = ('raw', name)
                elif name == 'script' and not self.embed:
                    self.lang, self.embed = 'js', True
                elif name == 'style' and not self.embed:
                    self.lang, self.embed = 'css', True
                return j + 1
            j += 1
        return n


def indent_of(ws):
    return len(ws.replace('\t', '    '))


def run(lines, lang, unit):
    """Return (new lines, recs). unit 2 converts; anything else only turns leading tabs into spaces."""
    sc = Scanner(lang)
    out, recs = [], []
    prev_shift = None
    for lineno, s in enumerate(lines, 1):
        ws = s[:len(s) - len(s.lstrip(' \t'))]
        body = s[len(ws):]
        if sc.embed and re.match(r'</(script|style)', body, re.I):
            sc.end_embed()
        if body.strip() == '':
            rec = Rec('blank', lineno)
            out.append(s)
            recs.append(rec)
            sc.scan(s)
            continue
        old = indent_of(ws)
        if sc.is_literal_start():
            rec = Rec('literal', lineno)
            rec.old = old
            rec.tab = '\t' in ws
            sc.cur_old = sc.cur_new = old
            out.append(s)
            recs.append(rec)
            sc.scan(s)
            prev_shift = None
            continue
        top = sc.stack[-1] if sc.stack else None
        rec = Rec('code', lineno)
        rec.old = old
        rec.tab = '\t' in ws
        rec.frame = top
        rec.in_paren = prev_shift is not None or any(f.kind in CONTINUATION_KINDS for f in sc.stack)
        if unit == 2:
            if top is not None and top.kind in CONTINUATION_KINDS:
                new = old + (top.new - top.old)
            elif prev_shift is not None:
                new = old + prev_shift
            else:
                new = 2 * old
        else:
            new = old
        sc.cur_old, sc.cur_new = old, new
        if new == len(ws) and '\t' not in ws:
            out.append(s)
        else:
            out.append(' ' * new + body)
        recs.append(rec)
        sc.scan(s)
        prev_shift = (new - old) if sc.ends_in_backslash(s) else None
    return out, recs


def detect(recs):
    olds = [r.old for r in recs if r.kind == 'code' and not r.in_paren]
    if not olds or all(o == 0 for o in olds):
        return None
    if any(o % 2 for o in olds):
        return 'other'
    if all(o % 4 == 0 for o in olds):
        return 4
    return 2


def lang_for(path):
    return LANG_OF[os.path.splitext(path)[1]]


def scope(root):
    raw = subprocess.run(['git', '-C', root, 'ls-files', '-z'], capture_output=True, check=True).stdout
    out = []
    for p in raw.decode('utf-8').split('\0'):
        if not p or not p.endswith(SCOPE_EXTS):
            continue
        if any(part in EXCLUDED_DIRS for part in p.split('/')[:-1]):
            continue
        with open(os.path.join(root, p), 'rb') as fh:
            head = fh.read().decode('utf-8', 'replace').split('\n')[:5]
        low = '\n'.join(head).lower()
        if 'generated' in low or 'do not edit' in low:
            continue
        out.append(p)
    return out


def read_lines(path):
    with open(path, 'rb') as fh:
        text = fh.read().decode('utf-8')
    if '\r' in text:
        raise ValueError('CRLF line endings')
    return text, text.split('\n')


def convert(path):
    text, lines = read_lines(path)
    _, recs = run(lines, lang_for(path), None)
    unit = detect(recs)
    if unit == 'other':
        return unit, False
    out, _ = run(lines, lang_for(path), 2 if unit == 2 else 4)
    new = '\n'.join(out)
    if new != text:
        with open(path, 'wb') as fh:
            fh.write(new.encode('utf-8'))
        return unit, True
    return unit, False


def verify(path):
    _, lines = read_lines(path)
    _, recs = run(lines, lang_for(path), None)
    problems = []
    tabs = [r.lineno for r in recs if r.kind == 'code' and r.tab]
    for n in tabs[:3]:
        problems.append('%s:%d: indented with a tab' % (path, n))
    if len(tabs) > 3:
        problems.append('%s: %d more tab-indented lines' % (path, len(tabs) - 3))
    if detect(recs) == 2:
        problems.append('%s: code lines are indented in units of 2' % path)
    return problems


def main(argv):
    root = subprocess.run(['git', 'rev-parse', '--show-toplevel'], capture_output=True, check=True, text=True).stdout.strip()
    mode = 'convert'
    args = []
    for a in argv:
        if a in ('--verify', '--stats'):
            mode = a[2:]
        else:
            args.append(a)
    paths = args or scope(root)
    if mode == 'verify':
        problems = []
        for p in paths:
            problems += verify(os.path.join(root, p) if not os.path.isabs(p) else p)
        for line in problems:
            print(line)
        print('reindent4 --verify: %d files in scope, %d problems' % (len(paths), len(problems)))
        return 1 if problems else 0
    if mode == 'stats':
        for p in paths:
            _, lines = read_lines(os.path.join(root, p))
            _, recs = run(lines, lang_for(p), None)
            code = [r.old for r in recs if r.kind == 'code' and not r.in_paren]
            print('%s unit=%s structural=%d mod4=%d mod2=%d odd=%d tabs=%d' % (
                p, detect(recs), len(code), sum(1 for o in code if o % 4 == 0 and o),
                sum(1 for o in code if o % 4 == 2), sum(1 for o in code if o % 2), sum(1 for r in recs if r.kind == 'code' and r.tab)))
        return 0
    converted, unchanged, skipped = [], [], []
    for p in paths:
        try:
            unit, changed = convert(os.path.join(root, p))
        except (ValueError, UnicodeDecodeError) as e:
            skipped.append((p, str(e)))
            continue
        if unit == 'other':
            skipped.append((p, 'odd indent unit'))
        elif changed:
            converted.append((p, unit))
        else:
            unchanged.append(p)
    for p, why in skipped:
        print('skipped %s: %s' % (p, why))
    print('converted %d, unchanged %d, skipped %d' % (len(converted), len(unchanged), len(skipped)))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
