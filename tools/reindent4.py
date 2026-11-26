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
# it is never changed, and neither is a blank line. Every code line gets one class, and a line that is not structural
# keeps its column offset from its reference line (new = old + (ref.new - ref.old)):
#   frame   the innermost open frame at the line's start is a "(" or "[", a block comment, an HTML tag, or (Python only)
#           a "{" with content after it: the reference is the line that opened the frame.
#   bs      the line before ends with a backslash: the reference is that line.
#   stmt    the line continues a statement outside any bracket: the reference is the anchor, the structural line that
#           began the statement. A line is a stmt continuation when it is indented deeper than its anchor and
#             C       the previous code line leaves the statement open (it does not end with ";", "{", "}", a case or
#                     label ":", or the ")" of an if/for/while/switch, or else/do), or it starts with && || ? or :
#             JS, CSS the previous code line ends with an operator (JS) or does not end with ";", "{", "}" (CSS), or
#                     a JS line starts with a binary or ternary operator or "."
#             shell   the previous code line ends with | && ||, or the line starts with | && ||
#           A line that starts with "{" or "}" is never a stmt continuation. Preprocessor lines (C) neither continue nor
#           anchor a statement, and do not change what the previous code line left open.
#   struct  anything else: new = 2 * old. A "{" block is structural even inside a call's parentheses, so a callback body
#           doubles. A comment-only line takes the class of the next code line.
# A file whose structural lines are indented in units of 2 is converted by these rules. A file whose structural lines are
# in units of 4, or have no indent, only gets its leading tabs turned into 4 spaces each. A file with an odd structural
# indent is not converted, and --verify names it. Comment lines and the lines of the other classes are not read for the unit.
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
CONTROL_WORDS = ('if', 'for', 'while', 'switch')
LABEL = re.compile(r'^(case\b[^?]*|default|[A-Za-z_]\w*)\s*:\s*(//.*|/\*.*)?$')
JS_START_OP = re.compile(r'^(\.(?![.0-9])|\?\.|&&|\|\||\?\?|\?|:|[-+*%|^&](?![-+=*|&])\s|==|!=|<=|>=|<\s|>\s|instanceof\b)')
C_START_OP = re.compile(r'^(&&|\|\||\?|:(?!:))')
SH_START_OP = re.compile(r'^(\||&&)')


class Frame:
    __slots__ = ('kind', 'name', 'line', 'cont', 'ctl')

    def __init__(self, kind, name=''):
        self.kind = kind
        self.name = name
        self.line = -1        # index of the line that opened it
        self.cont = kind in ('(', '[', 'c', 'hc', 'tag')   # a line inside it keeps its offset from the opener
        self.ctl = False      # a "(" that follows if, for, while or switch


class Rec:
    """One line. cls: blank, literal, frame, bs, stmt or struct; ref: the line a non-structural line is measured from."""
    __slots__ = ('kind', 'old', 'tab', 'lineno', 'cls', 'ref', 'frame_ref', 'bs', 'pp', 'comment', 'cand', 'epoch')

    def __init__(self, kind, lineno):
        self.kind = kind
        self.old = 0
        self.tab = False
        self.lineno = lineno
        self.cls = kind
        self.ref = None
        self.frame_ref = None
        self.bs = False
        self.pp = False
        self.comment = False
        self.cand = False
        self.epoch = 0


class Scanner:
    def __init__(self, lang):
        self.file_lang = lang
        self.lang = lang          # the lexer in force: for an HTML file, 'html', or 'js'/'css' inside a script or style
        self.embed = False
        self.epoch = 0            # counts the starts and ends of a script or style
        self.stack = []
        self.mode = None          # None, or a literal: ('str', q, multi), ('tpl',), ('triple', q3), ('heredoc', word, dash), ('raw', name)
        self.pending = []         # shell heredocs opened on this line, whose bodies start on the next line
        self.last = None          # the last code token: a punctuation char, 'w' (word or number) or 's' (string or regex)
        self.last2 = None         # the token before it, so that && -- => can be told from & - =
        self.last_word = None
        self.endctl = False       # the last token closed an if/for/while/switch header, or is else or do
        self.ntok = 0             # tokens seen so far; a line with no new token is blank or comment-only
        self.cur_line = 0

    def tok(self, t):
        self.last2 = self.last
        self.last = t
        self.ntok += 1
        self.endctl = False

    def reset_tokens(self):
        self.last = self.last2 = self.last_word = None
        self.endctl = False

    def push(self, kind, name=''):
        f = Frame(kind, name)
        f.line = self.cur_line
        self.stack.append(f)
        return f

    def is_literal_start(self):
        return self.mode is not None or any(f.kind == 'tplx' for f in self.stack)

    def end_embed(self):
        self.stack = []
        self.mode = None
        self.lang = 'html'
        self.embed = False
        self.epoch += 1
        self.reset_tokens()

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
                    self.tok('s')
                    continue
                if ch == '`':
                    self.mode = ('tpl',)
                else:
                    self.mode = ('str', ch, self.lang == 'sh')
                i += 1
                self.tok('s')
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
                    self.tok('s')
                    continue
            if ch in '([{':
                ctl = ch == '(' and self.last == 'w' and self.last_word in CONTROL_WORDS
                f = self.push(ch)
                f.ctl = ctl
                if ch == '{' and self.lang == 'py':
                    # a Python dict or set: content after the "{" is aligned under it, a "{" last on its line is hanging
                    rest = s[i + 1:].strip()
                    f.cont = not (rest == '' or rest.startswith('#'))
                self.tok(ch)
                i += 1
                continue
            if ch in ')]}':
                if ch == '}' and top is not None and top.kind == 'tplx':
                    self.stack.pop()
                    self.mode = ('tpl',)
                    i += 1
                    continue
                ctl = False
                for k in range(len(self.stack) - 1, -1, -1):
                    if self.stack[k].kind == CLOSER[ch]:
                        ctl = self.stack[k].ctl
                        del self.stack[k:]
                        break
                self.tok(ch)
                self.endctl = ctl
                i += 1
                continue
            if ch.isalnum() or ch in '_$':
                j = i
                while j < n and (s[j].isalnum() or s[j] in '_$'):
                    j += 1
                word = s[i:j]
                self.tok('w')
                self.last_word = word
                self.endctl = word in ('else', 'do')
                i = j
                continue
            if ch.isspace():
                i += 1
                continue
            if ch == '\\' and i == n - 1:
                i += 1
                continue
            self.tok(ch)
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
                    self.epoch += 1
                    self.reset_tokens()
                elif name == 'style' and not self.embed:
                    self.lang, self.embed = 'css', True
                    self.epoch += 1
                    self.reset_tokens()
                return j + 1
            j += 1
        return n


def indent_of(ws):
    return len(ws.replace('\t', '    '))


def leaves_open(lang, sc, s):
    """After the code line s: does the statement it ends continue on the next code line?"""
    last, last2 = sc.last, sc.last2
    if last is None:
        return False
    if lang in ('c', 'css'):
        if lang == 'c' and sc.endctl:
            return False
        if last in (';', '{', '}'):
            return False
        if last == ':':
            return LABEL.match(s.strip()) is None
        return True
    if lang == 'js':
        if last in ('+', '-'):
            return last2 != last
        return last in ('*', '%', '&', '|', '^', '?', ',', '.', '=', '/')
    if lang == 'sh':
        return last == '|' or (last == '&' and last2 == '&')
    return False


def starts_continuing(lang, body):
    if body[0] in '{}':
        return False
    if lang == 'c':
        return C_START_OP.match(body) is not None
    if lang == 'js':
        return JS_START_OP.match(body) is not None
    if lang == 'sh':
        return SH_START_OP.match(body) is not None
    return False


def classify(lines, lang):
    """Return one Rec per line, each with its class and reference line (see the header)."""
    sc = Scanner(lang)
    recs = []
    prev_open = False
    prev_bs = False
    in_pp = False
    epoch = 0
    for idx, s in enumerate(lines):
        sc.cur_line = idx
        ws = s[:len(s) - len(s.lstrip(' \t'))]
        body = s[len(ws):]
        if sc.embed and re.match(r'</(script|style)', body, re.I):
            sc.end_embed()
        if sc.epoch != epoch:
            epoch = sc.epoch
            prev_open = False
        old = indent_of(ws)
        if body.strip() == '':
            rec = Rec('blank', idx + 1)
            recs.append(rec)
            sc.scan(s)
            continue
        if sc.is_literal_start():
            rec = Rec('literal', idx + 1)
            rec.old = old
            rec.tab = '\t' in ws
            recs.append(rec)
            ntok = sc.ntok
            sc.scan(s)
            if sc.ntok != ntok:
                prev_open = leaves_open(sc.lang, sc, s)
            prev_bs = False
            continue
        top = sc.stack[-1] if sc.stack else None
        rec = Rec('code', idx + 1)
        rec.old = old
        rec.tab = '\t' in ws
        rec.epoch = sc.epoch
        if top is not None and top.cont:
            rec.frame_ref = top.line
        rec.bs = prev_bs
        lex = sc.lang
        rec.pp = lex == 'c' and (in_pp or (body.startswith('#') and (top is None or not top.cont)))
        in_pp = rec.pp
        snap = (sc.last, sc.last2, sc.last_word, sc.endctl)
        ntok = sc.ntok
        sc.scan(s)
        added = sc.ntok != ntok
        if rec.pp:
            sc.last, sc.last2, sc.last_word, sc.endctl = snap
            in_pp = s.endswith('\\')
        else:
            rec.comment = not added and lex != 'html' and body.startswith(('//', '/*', '#'))
            rec.cand = not rec.comment and (prev_open or starts_continuing(lex, body))
            if added:
                prev_open = leaves_open(lex, sc, s)
        prev_bs = sc.ends_in_backslash(s)
        recs.append(rec)
    resolve(recs)
    return recs


def resolve(recs):
    """Pass 2: give every code line its class and reference. A comment-only line takes the next code line's."""
    n = len(recs)
    nxt = [False] * (n + 1)       # nxt[i]: is the next non-comment code line at or after i a stmt candidate?
    for i in range(n - 1, -1, -1):
        r = recs[i]
        if r.kind == 'code' and not r.comment:
            nxt[i] = r.cand
        elif r.kind == 'code' or r.kind == 'blank':
            nxt[i] = nxt[i + 1]
        else:
            nxt[i] = False
    anchor = None
    epoch = 0
    for i, r in enumerate(recs):
        if r.kind != 'code':
            continue
        if r.epoch != epoch:
            epoch = r.epoch
            anchor = None
        if r.frame_ref is not None:
            r.cls, r.ref = 'frame', r.frame_ref
        elif r.bs:
            r.cls, r.ref = 'bs', i - 1
        else:
            cand = nxt[i] if r.comment else r.cand
            if cand and not r.pp and anchor is not None and r.old > recs[anchor].old:
                r.cls, r.ref = 'stmt', anchor
            else:
                r.cls = 'struct'
                if not r.comment and not r.pp:
                    anchor = i


def render(lines, recs, unit):
    """The new lines. unit 2 converts; anything else only turns leading tabs into spaces."""
    olds = [r.old for r in recs]
    news = list(olds)
    out = []
    for i, (s, r) in enumerate(zip(lines, recs)):
        if r.kind != 'code':
            out.append(s)
            continue
        if unit == 2:
            news[i] = 2 * r.old if r.cls == 'struct' else r.old + (news[r.ref] - olds[r.ref])
        ws = s[:len(s) - len(s.lstrip(' \t'))]
        if news[i] == len(ws) and '\t' not in ws:
            out.append(s)
        else:
            out.append(' ' * news[i] + s[len(ws):])
    return out


def detect(recs):
    olds = [r.old for r in recs if r.kind == 'code' and r.cls == 'struct' and not r.comment]
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
    recs = classify(lines, lang_for(path))
    unit = detect(recs)
    if unit == 'other':
        return unit, False
    new = '\n'.join(render(lines, recs, 2 if unit == 2 else 4))
    if new != text:
        with open(path, 'wb') as fh:
            fh.write(new.encode('utf-8'))
        return unit, True
    return unit, False


def verify(root, path):
    """Every in-scope file must be at 4: no tab in a code line's indent, and a structural indent in units of 4."""
    try:
        _, lines = read_lines(os.path.join(root, path))
    except (ValueError, UnicodeDecodeError) as e:
        return ['%s: cannot be read for conversion (%s)' % (path, e)]
    recs = classify(lines, lang_for(path))
    problems = []
    tabs = [r.lineno for r in recs if r.kind == 'code' and r.tab]
    for n in tabs[:3]:
        problems.append('%s:%d: indented with a tab' % (path, n))
    if len(tabs) > 3:
        problems.append('%s: %d more tab-indented lines' % (path, len(tabs) - 3))
    unit = detect(recs)
    if unit in ('other', 2):
        bad = [r for r in recs if r.kind == 'code' and r.cls == 'struct' and not r.comment and r.old % 4]
        problems.append('%s:%d: structural lines are not indented in units of 4 (indent %d%s)' % (
            path, bad[0].lineno, bad[0].old, ', %d such lines' % len(bad) if len(bad) > 1 else ''))
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
            problems += verify(root, p)
        for line in problems:
            print(line)
        print('reindent4 --verify: %d files in scope, %d problems' % (len(paths), len(problems)))
        return 1 if problems else 0
    if mode == 'stats':
        for p in paths:
            _, lines = read_lines(os.path.join(root, p))
            recs = classify(lines, lang_for(p))
            code = [r for r in recs if r.kind == 'code']
            cnt = {c: sum(1 for r in code if r.cls == c) for c in ('struct', 'frame', 'bs', 'stmt')}
            st = [r.old for r in code if r.cls == 'struct' and not r.comment]
            print('%s unit=%s struct=%d frame=%d bs=%d stmt=%d mod4=%d mod2=%d odd=%d tabs=%d' % (
                p, detect(recs), cnt['struct'], cnt['frame'], cnt['bs'], cnt['stmt'], sum(1 for o in st if o and o % 4 == 0),
                sum(1 for o in st if o % 4 == 2), sum(1 for o in st if o % 2), sum(1 for r in code if r.tab)))
        return 0
    converted, unchanged, skipped = [], [], []
    for p in paths:
        try:
            unit, changed = convert(os.path.join(root, p))
        except (ValueError, UnicodeDecodeError) as e:
            skipped.append((p, str(e)))
            continue
        if unit == 'other':
            skipped.append((p, 'odd structural indent'))
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
