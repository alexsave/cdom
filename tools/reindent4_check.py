#!/usr/bin/env python3
# reindent4_check.py - the proof that tools/reindent4.py changed nothing but leading whitespace.
#
#   python3 -I tools/reindent4_check.py [BASE]      BASE defaults to origin/main
#
# For every in-scope file whose bytes differ from BASE (the committed state before the reindent):
#   every line: the same line count, and the line with its leading spaces and tabs stripped is byte-identical;
#   no leading tab is left in the new file;
#   *.py: ast.dump of the old and the new source is identical (docstrings and every string included);
#   *.c, *.h: each changed .c file, and each .c file that includes a changed header, is compiled in the old and the new
#             tree with cdom.mk's CDOM_CFLAGS (relative paths, so both trees compile the same flags) and CDOM_OPT, without -g,
#             and the two object files must be byte-identical;
#   *.js, *.mjs: node --check on the new file, and the acorn token stream (label and value of every token) of the old
#             and the new file must be identical;
#   *.sh: bash -n gives the same result on the old and the new file.
# The alignment proof: every line the converter classes as a continuation (inside a bracket or comment, after a backslash,
#   or a statement continued outside brackets) is measured from its reference line (the line that opened the bracket, the
#   line before the backslash, or the statement's anchor); that column offset must be the same in the old and the new file.
#   --samples-out PATH writes every statement and backslash continuation as an old/new pair for reading.
# Prints one line per check failure and a summary; exits 1 on any failure.

import ast
import os
from collections import Counter
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import reindent4 as R  # noqa: E402


def sh(args, cwd, check=True):
    return subprocess.run(args, cwd=cwd, capture_output=True, check=check)


def base_bytes(root, base, path):
    """The file's bytes at base, or None when base does not have it (a file this branch added)."""
    r = sh(['git', 'cat-file', '-e', '%s:%s' % (base, path)], root, check=False)
    if r.returncode != 0:
        return None
    return sh(['git', 'show', '%s:%s' % (base, path)], root).stdout


def strip_lead(line):
    return line.lstrip(' \t')


def line_check(path, old, new):
    errs = []
    ol, nl = old.split('\n'), new.split('\n')
    if len(ol) != len(nl):
        errs.append('%s: line count %d -> %d' % (path, len(ol), len(nl)))
        return errs
    for i, (a, b) in enumerate(zip(ol, nl), 1):
        if strip_lead(a) != strip_lead(b):
            errs.append('%s:%d: text changed beyond leading whitespace' % (path, i))
            break
        if '\t' in b[:len(b) - len(b.lstrip(' \t'))]:
            errs.append('%s:%d: leading tab left' % (path, i))
            break
    return errs


def align_check(path, old, new, errs, counts, samples):
    ol, nl = old.split('\n'), new.split('\n')
    recs = R.classify(ol, R.lang_for(path))
    for i, r in enumerate(recs):
        if r.kind != 'code' or r.cls not in ('frame', 'bs', 'stmt'):
            continue
        counts['continuations_' + r.cls] += 1
        counts['continuations'] += 1
        j = r.ref
        d_old = R.indent_of(ol[i][:len(ol[i]) - len(ol[i].lstrip(' \t'))]) - R.indent_of(ol[j][:len(ol[j]) - len(ol[j].lstrip(' \t'))])
        d_new = R.indent_of(nl[i][:len(nl[i]) - len(nl[i].lstrip(' \t'))]) - R.indent_of(nl[j][:len(nl[j]) - len(nl[j].lstrip(' \t'))])
        if d_old != d_new:
            counts['alignment_mismatches'] += 1
            errs.append('%s:%d: continuation offset from its reference line %d changed %d -> %d' % (path, i + 1, j + 1, d_old, d_new))
        elif r.cls != 'frame':
            samples.append((path, i, j, ol, nl, r.cls))


def make_vars(tree):
    out = {}
    for var in ('CDOM_CFLAGS', 'CDOM_OPT', 'CDOM_IMAGE_OWN_CFLAGS', 'CDOM_IMAGE_VENDOR_CFLAGS'):
        r = subprocess.run(['make', '-s', '--no-print-directory', '-f', 'cdom.mk', '-f', '-', 'cdom-print-var',
                            'CDOM=.'], cwd=tree, input='cdom-print-var:\n\t@echo $(%s)\n' % var,
                           capture_output=True, text=True, check=True)
        out[var] = r.stdout.split()
    return out


def compile_obj(tree, src, args, obj):
    """Compile src in tree to obj; True when cc succeeds."""
    r = subprocess.run(args + ['-c', src, '-o', obj], cwd=tree, capture_output=True)
    return r.returncode == 0, r.stderr.decode(errors='replace')


def c_check(root, old_root, changed_c, changed_h, errs, counts):
    targets = set(changed_c)
    for h in changed_h:
        base = os.path.basename(h)
        for p in R.scope(root):
            if p.endswith('.c') and re.search(r'#\s*include\s*["<][^">]*' + re.escape(base) + r'[">]',
                                              open(os.path.join(root, p), encoding='utf-8').read()):
                targets.add(p)
    if not targets:
        return
    wasm_cc = os.environ.get('WASM_CC') or ('/opt/homebrew/opt/llvm/bin/clang' if sys.platform == 'darwin' else 'clang')
    cdom = make_vars(root)
    # Two labels: cdom's own flags (the wasm32 modules, cdom.mk's CDOM_CFLAGS with its CDOM_OPT), and the host
    # cc for cdom's host tools (tools/*.c, built with HOST_CC and no wasm flags).
    base = [wasm_cc] + cdom['CDOM_CFLAGS'] + cdom['CDOM_OPT']
    labels = [('cdom.mk', base), ('host', ['cc', '-O2']),
              ('image-own', base + cdom['CDOM_IMAGE_OWN_CFLAGS'] + ['-DCDOM_IMAGE_SJLJ_JS']),
              ('image-vendor', base + cdom['CDOM_IMAGE_VENDOR_CFLAGS'] + ['-DCDOM_IMAGE_SJLJ_JS'])]
    uncompiled = []
    for p in sorted(targets):
        with tempfile.TemporaryDirectory() as d:
            done = False
            for label, args in labels:
                oa, ob = os.path.join(d, 'a.o'), os.path.join(d, 'b.o')
                ok_a, err_a = compile_obj(old_root, p, args, oa)
                ok_b, err_b = compile_obj(root, p, args, ob)
                if ok_a != ok_b:
                    errs.append('%s: compiles with %s on %s only: %s' % (p, label, 'new' if ok_b else 'old',
                                                                         (err_b if ok_b is False else err_a)[:300]))
                    done = True
                    break
                if not ok_a:
                    continue
                with open(oa, 'rb') as x, open(ob, 'rb') as y:
                    same = x.read() == y.read()
                counts['objects_compared_' + label] += 1
                if same:
                    counts['objects_identical'] += 1
                else:
                    errs.append('%s: object files differ (%s)' % (p, label))
                done = True
                break
            if not done:
                uncompiled.append(p)
    counts['c_not_compiled_before_or_after'] = len(uncompiled)
    if uncompiled:
        print('note: %d changed C files do not compile with either flag set on the old tree either '
              '(covered by the line proof only): %s' % (len(uncompiled), ' '.join(uncompiled)))


JS_TOKENS = r"""
const acorn = require(process.argv[1]);
const fs = require('fs');
const [oldF, newF, mode] = process.argv.slice(2);
function toks(src, st) {
  const out = [];
  for (const t of acorn.tokenizer(src, { ecmaVersion: 'latest', sourceType: st, allowHashBang: true, allowReturnOutsideFunction: true })) {
    out.push(t.type.label + '\u0001' + String(t.value));
  }
  return out;
}
const oldSrc = fs.readFileSync(oldF, 'utf8'), newSrc = fs.readFileSync(newF, 'utf8');
let st = mode;
let a;
try { a = toks(oldSrc, st); } catch (e) { process.stdout.write('OLD_PARSE_FAIL ' + e.message + '\n'); process.exit(0); }
const b = toks(newSrc, st);
if (a.length !== b.length) { process.stdout.write('DIFF count ' + a.length + ' ' + b.length + '\n'); process.exit(0); }
for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) { process.stdout.write('DIFF at token ' + i + '\n'); process.exit(0); }
process.stdout.write('SAME ' + a.length + '\n');
"""


def js_check(root, old_path_tmp, path, errs, counts):
    r = subprocess.run(['node', '--check', os.path.join(root, path)], capture_output=True)
    if r.returncode != 0:
        errs.append('%s: node --check failed: %s' % (path, r.stderr.decode(errors='replace')[:300]))
        return
    counts['js_node_check'] += 1
    acorn = os.path.join(root, 'node_modules', 'acorn')
    if not os.path.isdir(acorn):
        errs.append('%s: no acorn in node_modules, token check not run' % path)
        return
    for st in ('module', 'script'):
        r = subprocess.run(['node', '-e', JS_TOKENS, acorn, old_path_tmp[path], os.path.join(root, path), st],
                           capture_output=True, text=True)
        out = r.stdout.strip()
        if out.startswith('OLD_PARSE_FAIL'):
            continue
        counts['js_token_streams_compared'] += 1
        if out.startswith('SAME'):
            counts['js_token_streams_identical'] += 1
            return
        errs.append('%s: token streams differ (%s)' % (path, out))
        return
    errs.append('%s: old file does not tokenize as module or script' % path)


def main(argv):
    root = sh(['git', 'rev-parse', '--show-toplevel'], os.getcwd()).stdout.decode().strip()
    samples_out = None
    if '--samples-out' in argv:
        k = argv.index('--samples-out')
        samples_out = argv[k + 1]
        del argv[k:k + 2]
    samples = []
    base = argv[0] if argv else 'origin/main'
    scope = R.scope(root)
    changed = []
    added = []
    for p in scope:
        old = base_bytes(root, base, p)
        if old is None:
            added.append(p)
        elif old != open(os.path.join(root, p), 'rb').read():
            changed.append(p)
    errs = []
    counts = Counter()
    counts['files_changed'] = len(changed)
    counts['files_added_not_checked'] = len(added)
    by_ext = {}
    tmp = tempfile.mkdtemp(prefix='reindent4-old-')
    old_paths = {}
    for p in changed:
        ext = os.path.splitext(p)[1]
        by_ext[ext] = by_ext.get(ext, 0) + 1
        old = base_bytes(root, base, p).decode('utf-8')
        new = open(os.path.join(root, p), 'rb').read().decode('utf-8')
        line_errs = line_check(p, old, new)
        errs += line_errs
        if not line_errs:
            align_check(p, old, new, errs, counts, samples)
        if ext == '.py':
            counts['py_ast_compared'] += 1
            if ast.dump(ast.parse(old)) == ast.dump(ast.parse(new)):
                counts['py_ast_identical'] += 1
            else:
                errs.append('%s: ast differs' % p)
        if ext == '.sh':
            counts['sh_checked'] += 1
            oldp = os.path.join(tmp, 'old.sh')
            open(oldp, 'w').write(old)
            a = subprocess.run(['bash', '-n', oldp], capture_output=True).returncode
            b = subprocess.run(['bash', '-n', os.path.join(root, p)], capture_output=True).returncode
            if a != b:
                errs.append('%s: bash -n differs (%d -> %d)' % (p, a, b))
        if ext in ('.js', '.mjs'):
            oldp = os.path.join(tmp, p.replace('/', '__'))
            open(oldp, 'w').write(old)
            old_paths[p] = oldp
            js_check(root, old_paths, p, errs, counts)
    c_changed = [p for p in changed if p.endswith('.c')]
    h_changed = [p for p in changed if p.endswith('.h')]
    if c_changed or h_changed:
        old_root = os.path.join(tmp, 'tree')
        os.makedirs(old_root)
        archive = subprocess.run(['git', 'archive', base], cwd=root, capture_output=True, check=True).stdout
        subprocess.run(['tar', '-x', '-C', old_root], input=archive, check=True)
        c_check(root, old_root, c_changed, h_changed, errs, counts)
    if samples_out:
        with open(samples_out, 'w') as fh:
            for path, i, j, ol, nl, cls in samples:
                fh.write('%s:%d (%s, anchor line %d)\n' % (path, i + 1, cls, j + 1))
                lo = max(j, i - 3) if i - j > 6 else j
                for k in sorted({j} | set(range(max(j, i - 3), i + 1))):
                    fh.write('  old %4d |%s\n' % (k + 1, ol[k]))
                for k in sorted({j} | set(range(max(j, i - 3), i + 1))):
                    fh.write('  new %4d |%s\n' % (k + 1, nl[k]))
    for e in errs:
        print('FAIL ' + e)
    print('reindent4_check against %s: %s' % (base, ', '.join('%s=%s' % kv for kv in sorted(counts.items()))))
    print('changed files by extension: %s' % ', '.join('%s %d' % (k, v) for k, v in sorted(by_ext.items())))
    print('%d failures' % len(errs))
    return 1 if errs else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
