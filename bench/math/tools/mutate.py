#!/usr/bin/env python3
"""Mutation check of the math bench's tests (docs/math-design.md, "Tests"): each mutant breaks one thing, the test
must fail on its own assertion (its message is matched), and nothing is ever mutated in place: each mutant is built
and tested in a private copy of bench/math (CDOM pointing back at this tree), removed afterwards. The tree as it is
must pass first. A mutant whose text is not in its file exactly once stops the run before any mutant runs.
Usage: python3 -I bench/math/tools/mutate.py [-j N] [name ...]   (WASM_CC, PATH with wasm-opt, as for make)"""
import os, pathlib, shutil, subprocess, sys, tempfile, threading, queue

here = pathlib.Path(__file__).resolve().parent.parent      # bench/math
top = here.parent.parent
TEST = ['node', 'test.mjs', '2000']

# name: (file under bench/math, text, replacement, the message the failing test must print)
MUTANTS = {
    'musl-sin-zero': ('third_party/musl/src/math/sin.c',
                      'FORCE_EVAL(ix < 0x00100000 ? x/0x1p120f : x+0x1p120f);\n\t\t\treturn x;',
                      'FORCE_EVAL(ix < 0x00100000 ? x/0x1p120f : x+0x1p120f);\n\t\t\treturn 0.0;',
                      'FAIL special musl (wasm): FAIL sin(-0.0, 0)'),
    'msun-exp-inf': ('third_party/msun/src/e_exp.c', 'else return (xsb==0)? x:0.0;', 'else return (xsb==0)? 0.0:0.0;',
                     'FAIL special msun (wasm): FAIL exp(INF, 0)'),
    'msun-atan2-pi': ('third_party/msun/src/e_atan2.c', 'case 2: return  pi+tiny;/* atan(+0,-anything) = pi */',
                      'case 2: return  -pi-tiny;/* atan(+0,-anything) = pi */', 'FAIL special msun (native): FAIL atan2(0.0, -0.0)'),
    'core-log-neg': ('third_party/core-math/src/binary64/log/log.c', '#endif\n        return 0.0 / 0.0;',
                     '#endif\n        return 0.0;', 'FAIL special core (wasm): FAIL log(-1.0, 0)'),
    'ours-pow-minus1-inf': ('ours/ours.c', '    if (ax == 1.0) return 1.0;\n    return (ax < 1.0) == (y < 0) ? INFINITY : 0.0;',
                            '    if (ax == 1.0) return 0.0;\n    return (ax < 1.0) == (y < 0) ? INFINITY : 0.0;',
                            'FAIL special ours (wasm): FAIL pow(-1.0, INF)'),
    'ours-fmod-small': ('ours/ours.c', '  if (ax < ay) return x;', '  if (ax < ay) return -x;', 'FAIL special ours (native): FAIL fmod(3.0, INF)'),
    'glue-exp2': ('glue.js', 'exp2: (x) => 2 ** x,', 'exp2: (x) => Math.exp(x),', 'FAIL special js1 (wasm): FAIL exp2('),
    'js-deviation-unlisted': ('special.c', '#define BJS(f, x, y, w) CASE(2 * bm_k_##f, x, y, w, DEV_JS,',
                              '#define BJS(f, x, y, w) CASE(2 * bm_k_##f, x, y, w, 0,', 'FAIL special js1 (wasm): FAIL pow(-1.0, INF)'),
    'deviation-no-longer': ('special.c', 'DGL(cbrt, 27.0, 0, 3.0)', 'BJS(cbrt, 27.0, 0, 3.0)',
                            'FAIL special js1 (wasm): NOW RIGHT, listed as a deviation: cbrt(27.0, 0)'),
    'glibc-deviation-unlisted': ('special.c', 'DGL(cbrt, TD, 0, 0x1p-358)', 'D(cbrt, TD, 0, 0x1p-358)',
                                 'FAIL special glibc (native): FAIL cbrt(TD, 0)'),
    'compat-fmax': ('compat/compat.c', 'W double fmax(double x, double y) { return x != x ? y : y != y ? x : x > y ? x : x < y ? y :',
                    'W double fmax(double x, double y) { return x != x ? y : y != y ? x : x > y ? x : y < x ? y :',
                    'FAIL twin core: wasm differs from native in atan2 typical, hypot typical'),
    # a native twin built with fused multiply-add contraction (x86-64 with FMA; this one is for the Linux VM)
    'native-contract': ('Makefile', '$(BM_FLAGS) -U__FP_FAST_FMA -U__FP_FAST_FMAF', '$(BM_FLAGS) -ffp-contract=fast -mfma -U__FP_FAST_FMA -U__FP_FAST_FMAF',
                        'FAIL twin musl: wasm differs from native in'),
    # a rounded constant of the wrong sign: near_pi must not let -pi pass for pi
    'msun-atan2-sign': ('third_party/msun/src/e_atan2.c', 'case 3: return -pi-tiny;/* atan(-0,-anything) =-pi */',
                        'case 3: return  pi+tiny;/* atan(-0,-anything) =-pi */', 'FAIL special msun (wasm): FAIL atan2(-0.0, -0.0)'),
}

def copy_tree(dst):
    shutil.copytree(here, dst, ignore=shutil.ignore_patterns('build', 'results', '__pycache__'))

def run(d, env):
    b = subprocess.run(['make', '-s', '-j4', f'CDOM={top}'], cwd=d, env=env, capture_output=True, text=True, timeout=1800)
    if b.returncode: return b.returncode, 'BUILD FAILED\n' + b.stdout + b.stderr
    t = subprocess.run(TEST, cwd=d, env=env, capture_output=True, text=True, timeout=1800)
    return t.returncode, t.stdout + t.stderr

def main():
    args = sys.argv[1:]
    jobs = 2
    if args[:1] == ['-j']: jobs, args = int(args[1]), args[2:]
    names = args or list(MUTANTS)
    for n in names:
        f, a, _, _ = MUTANTS[n]
        c = (here / f).read_text().count(a)
        if c != 1: sys.exit(f'mutate.py: {n}: the text occurs {c} times in {f}')
    env = dict(os.environ)
    with tempfile.TemporaryDirectory() as tmp:
        base = pathlib.Path(tmp) / 'base'
        copy_tree(base)
        rc, out = run(base, env)
        if rc: sys.exit(f'mutate.py: the tree as it is fails its tests:\n{out[-3000:]}')
        print('unmutated: pass')
        q, results = queue.Queue(), {}
        for n in names: q.put(n)
        def worker():
            while True:
                try: n = q.get_nowait()
                except queue.Empty: return
                d = pathlib.Path(tmp) / n
                copy_tree(d)
                f, a, b, msg = MUTANTS[n]
                p = d / f
                p.write_text(p.read_text().replace(a, b))
                rc, out = run(d, env)
                results[n] = (rc != 0 and msg in out, rc, out)
                shutil.rmtree(d, ignore_errors=True)
        ts = [threading.Thread(target=worker) for _ in range(jobs)]
        for t in ts: t.start()
        for t in ts: t.join()
    missed = 0
    for n in names:
        ok, rc, out = results[n]
        print(f'{"caught" if ok else "MISSED"} {n}: {MUTANTS[n][3]}')
        if not ok:
            missed += 1
            print('   ' + '\n   '.join(out.strip().splitlines()[-8:]))
    print(f'mutants: {len(names) - missed} of {len(names)} caught')
    sys.exit(1 if missed else 0)

main()
