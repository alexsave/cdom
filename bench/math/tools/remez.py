#!/usr/bin/env python3
"""The coefficients of our own functions (bench/math/ours/, docs/math-design.md "Our own functions"): each
polynomial is sollya's fpminimax (a minimax polynomial whose coefficients are doubles, from the Remez exchange),
fitted over the interval our range reduction leaves. Writes bench/math/ours/coef.h with each fit's error.
Needs sollya (apt install sollya, brew install sollya). A one-off tool: nothing runs it in a build, and its output
is committed. Usage: python3 -I bench/math/tools/remez.py"""
import math, pathlib, subprocess, sys, tempfile

here = pathlib.Path(__file__).resolve().parent
out = here.parent / 'ours' / 'coef.h'

def odd(a, b): return list(range(a, b + 1, 2))

# name: (what it approximates, the function of x, the free monomials (each a double), the interval, the fixed
# part). fpminimax minimizes the relative error of fixed part + free part against the function; the fixed part
# (x for sin, 1 - x^2/2 for cos, and so on) is what the C code computes exactly or in double-double itself.
FITS = [
    ('SIN', 'sin(x) = x + x^3 P(x^2)', 'sin(x)', odd(3, 13), '[2^-30; pi/4 + 2^-40]', 'x'),
    ('COS', 'cos(x) = 1 - x^2/2 + x^4 P(x^2)', 'cos(x)', list(range(4, 15, 2)), '[2^-30; pi/4 + 2^-40]', '1 - x^2/2'),
    ('TAN', 'tan(x) = x + x^3 P(x^2), |x| <= 0.6745', 'tan(x)', odd(3, 27), '[2^-30; 0.6745]', 'x'),
    ('ATAN', 'atan(x) = x + x^3 P(x^2), |x| <= 7/16', 'atan(x)', odd(3, 23), '[2^-30; 7/16]', 'x'),
    ('ASIN', 'asin(x) = x + x^3 P(x^2), |x| <= 1/2', 'asin(x)', odd(3, 31), '[2^-30; 1/2]', 'x'),
    ('EXP', 'exp(x) = 1 + x + x^2 P(x), |x| <= ln2/2', 'exp(x)', list(range(2, 12)), '[-0.3466; 0.3466]', '1 + x'),
    ('EXP2', '2^x = 1 + x P(x), |x| <= 1/2 + 2^-20 (pow takes it too)', '2^x', list(range(1, 15)), '[-0.5 - 2^-20; 0.5 + 2^-20]', '1'),
    ('EXPM1', 'expm1(x) = x + x^2/2 + x^3 P(x), |x| <= ln2/2', 'expm1(x)', list(range(3, 13)), '[-0.3466; 0.3466]', 'x + x^2/2'),
    ('LOG', 'log((1+s)/(1-s)) = 2s + s^3 P(s^2), |s| <= 3 - 2 sqrt2', '2*atanh(x)', odd(3, 15), '[2^-30; 0.1716]', '2*x'),
    ('CBRT', 'cbrt\'s first step: x^(-1/3) near 1, over the ratios its first guess leaves', 'x^(-1/3)', list(range(0, 5)), '[0.86; 1.16]', '0'),
    ('POWLOG', 'pow\'s log: log((1+s)/(1-s)) = 2s + 2s^3/3 + s^5 P(s^2)', '2*atanh(x)', odd(5, 25), '[2^-30; 0.1716]', '2*x + 2/3*x^3'),
]

# The constants: name, sollya expression (each rounded to the bits it is used at: the _1, _2, _3 parts of pi/2
# have 33 bits, so a multiple k < 2^20 of each is exact; LN2_HI and LOG10_2HI have 42, so k < 2^11 times them is).
CONSTS = [
    ('INVPIO2', 'round(2/pi, 53, RN)'), ('PIO2_1', 'round(pi/2, 33, RN)'), ('PIO2_2', 'round(pi/2 - PIO2_1, 33, RN)'),
    ('PIO2_3', 'round(pi/2 - PIO2_1 - PIO2_2, 33, RN)'), ('PIO2_4', 'round(pi/2 - PIO2_1 - PIO2_2 - PIO2_3, 53, RN)'),
    ('PIO2_HI', 'round(pi/2, 53, RN)'), ('PIO2_LO', 'round(pi/2 - PIO2_HI, 53, RN)'),
    ('PIO4_HI', 'round(pi/4, 53, RN)'), ('PIO4_LO', 'round(pi/4 - PIO4_HI, 53, RN)'),
    ('PI_HI', 'round(pi, 53, RN)'), ('PI_LO', 'round(pi - PI_HI, 53, RN)'),
    ('LN2_HI', 'round(log(2), 42, RN)'), ('LN2_LO', 'round(log(2) - LN2_HI, 53, RN)'),
    ('LN2D_HI', 'round(log(2), 53, RN)'), ('LN2D_LO', 'round(log(2) - LN2D_HI, 53, RN)'),
    ('INVLN2', 'round(1/log(2), 53, RN)'), ('IVLN2_HI', 'round(1/log(2), 53, RN)'), ('IVLN2_LO', 'round(1/log(2) - IVLN2_HI, 53, RN)'),
    ('IVLN10_HI', 'round(1/log(10), 53, RN)'), ('IVLN10_LO', 'round(1/log(10) - IVLN10_HI, 53, RN)'),
    ('LOG10_2HI', 'round(log10(2), 42, RN)'), ('LOG10_2LO', 'round(log10(2) - LOG10_2HI, 53, RN)'),
    ('TWO3_HI', 'round(2/3, 53, RN)'), ('TWO3_LO', 'round(2/3 - TWO3_HI, 53, RN)'),
    ('ATAN_HI0', 'round(atan(1/2), 53, RN)'), ('ATAN_LO0', 'round(atan(1/2) - ATAN_HI0, 53, RN)'),
    ('ATAN_HI1', 'round(atan(1), 53, RN)'), ('ATAN_LO1', 'round(atan(1) - ATAN_HI1, 53, RN)'),
    ('ATAN_HI2', 'round(atan(3/2), 53, RN)'), ('ATAN_LO2', 'round(atan(3/2) - ATAN_HI2, 53, RN)'),
    ('ATAN_HI3', 'round(pi/2, 53, RN)'), ('ATAN_LO3', 'round(pi/2 - ATAN_HI3, 53, RN)'),
]

def consts():
    script = 'prec=400!;\ndisplay=hexadecimal!;\n'
    for name, expr in CONSTS: script += f'{name} = {expr};\nprint({name});\n'
    script += 'quit;\n'
    with tempfile.NamedTemporaryFile('w', suffix='.sollya', delete=False) as t:
        t.write(script)
    r = subprocess.run(['sollya', t.name], capture_output=True, text=True, timeout=600)
    lines = [l.strip() for l in r.stdout.splitlines() if l.strip()]
    if r.returncode or len(lines) != len(CONSTS): sys.exit(f'remez.py: sollya failed on the constants:\n{r.stdout}\n{r.stderr}')
    return [f'#define {name} {v}' for (name, _), v in zip(CONSTS, lines)]

def fit(name, what, f, mons, interval, cons):
    script = f'''prec=400!;
display=hexadecimal!;
I = {interval};
f = {f};
p = fpminimax(f, [|{", ".join(map(str, mons))}|], [|D...|], I, relative, floating, {cons});
'''
    for m in mons: script += f'print(coeff(p, {m}));\n'
    script += 'print(dirtyinfnorm(p/f - 1, I));\nquit;\n'
    with tempfile.NamedTemporaryFile('w', suffix='.sollya', delete=False) as t:
        t.write(script)
    r = subprocess.run(['sollya', t.name], capture_output=True, text=True, timeout=1200)
    lines = [l.strip() for l in r.stdout.splitlines() if l.strip()]
    if r.returncode or len(lines) < len(mons) + 1:
        sys.exit(f'remez.py: sollya failed on {name}:\n{r.stdout}\n{r.stderr}')
    coefs, err = lines[:len(mons)], lines[len(mons)]
    e = abs(float.fromhex(err))
    bits = -math.log2(e) if e > 0 else float('inf')
    return coefs, bits

def main():
    parts = ['/* The coefficients of our own functions (bench/math/ours/), written by bench/math/tools/remez.py with sollya\'s',
             ' * fpminimax: do not edit by hand. Each array is the polynomial\'s coefficients from the lowest monomial up, the',
             ' * fixed leading ones included; "error" is the fit\'s relative error over its interval (sollya\'s dirtyinfnorm). */',
             '#ifndef BENCH_MATH_OURS_COEF_H', '#define BENCH_MATH_OURS_COEF_H']
    for spec in FITS:
        name, what = spec[0], spec[1]
        coefs, bits = fit(*spec)
        print(f'{name}: {len(coefs)} coefficients, error 2^-{bits:.1f}', file=sys.stderr)
        parts.append(f'/* {what}: monomials {spec[3][0]}..{spec[3][-1]} over {spec[4]} (the function is odd or even, or the interval two-sided), error 2^-{bits:.1f} */')
        parts.append(f'static const double {name}[{len(coefs)}] = {{ {", ".join(coefs)} }};')
    parts.append('/* The constants (sollya, 400 bits, then rounded as each says). */')
    parts += consts()
    parts.append('#endif')
    out.write_text('\n'.join(parts) + '\n')

main()
