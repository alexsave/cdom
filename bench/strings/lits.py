#!/usr/bin/env python3
"""strings-bench prototype of tools/cdlits (docs/strings-design.md): reads the cd_lits slots of a linked
module, writes the page's table, and optionally strips cd_chars from the data section.

  lits.py in.wasm out.js [out.wasm]

out.js is one JS expression: [BASE, "unique strings joined by \\0", "slot map joined by ,"].
With out.wasm, the bytes of cd_chars are cut out of the data segments (the addresses stay reserved).
Not shipped: a measurement prototype. The real tool is C, per the design.
"""
import sys, struct


def uleb(b, i):
    r = s = 0
    while True:
        c = b[i]; i += 1; r |= (c & 127) << s; s += 7
        if c < 128:
            return r, i


def sleb(b, i):
    r = s = 0
    while True:
        c = b[i]; i += 1; r |= (c & 127) << s; s += 7
        if c < 128:
            if c & 64:
                r -= 1 << s
            return r, i


def enc(n):
    o = bytearray()
    while True:
        c = n & 127; n >>= 7
        if n:
            o.append(c | 128)
        else:
            o.append(c); return bytes(o)


def senc(n):
    o = bytearray()
    while True:
        c = n & 127; n >>= 7
        if (n == 0 and not c & 64) or (n == -1 and c & 64):
            o.append(c); return bytes(o)
        o.append(c | 128)


w = open(sys.argv[1], 'rb').read()
secs = []
i = 8
while i < len(w):
    sid = w[i]; n, j = uleb(w, i + 1); secs.append([sid, w[j:j + n]]); i = j + n


def sec(k):
    return next((b for s, b in secs if s == k), None)


# globals (count imported globals: none in a cdom module)
gl = []
b = sec(6)
n, i = uleb(b, 0)
for _ in range(n):
    i += 2  # type, mutability
    assert b[i] == 0x41, 'i32.const global'
    v, i = sleb(b, i + 1); i += 1; gl.append(v & 0xffffffff)
ex = {}
b = sec(7)
n, i = uleb(b, 0)
for _ in range(n):
    ln, i = uleb(b, i); name = b[i:i + ln].decode(); i += ln; kind = b[i]; idx, i = uleb(b, i + 1)
    if kind == 3:
        ex[name] = gl[idx]
segs = []
b = sec(11)
n, i = uleb(b, 0)
for _ in range(n):
    flag, i = uleb(b, i)
    assert flag == 0, 'active segment of memory 0'
    assert b[i] == 0x41
    off, i = sleb(b, i + 1); assert b[i] == 0x0b; i += 1
    ln, i = uleb(b, i); segs.append([off, b[i:i + ln]]); i += ln


def mem(a, n):
    out = bytearray(n)
    for off, d in segs:
        lo, hi = max(a, off), min(a + n, off + len(d))
        if lo < hi:
            out[lo - a:hi - a] = d[lo - off:hi - off]
    return bytes(out)


ls, le = ex.get('__start_cd_lits', 0), ex.get('__stop_cd_lits', 0)
cs, ce = ex.get('__start_cd_chars', 0), ex.get('__stop_cd_chars', 0)
chars = mem(cs, ce - cs)
slots = []
for a in range(ls, le, 4):
    p = struct.unpack('<I', mem(a, 4))[0]
    assert cs <= p < ce, 'slot points into cd_chars'
    z = chars.index(b'\0', p - cs)
    slots.append(chars[p - cs:z])
uniq, index, smap = [], {}, []
for v in slots:
    if v not in index:
        index[v] = len(uniq); uniq.append(v)
    smap.append(index[v])


def js(bs):
    o = []
    for ch in bs.decode('utf8'):
        c = ord(ch)
        if ch == '\\': o.append('\\\\')
        elif ch == '"': o.append('\\"')
        elif ch == '<': o.append('\\x3c')
        elif c < 32 or c == 127: o.append('\\x%02x' % c)
        elif c in (0x2028, 0x2029): o.append('\\u%04x' % c)
        else: o.append(ch)
    return ''.join(o)


text = '[%d,"%s","%s"]' % (ls, '\\x00'.join(js(u) for u in uniq), ','.join(map(str, smap)))
open(sys.argv[2], 'w', encoding='utf8').write(text)

if len(sys.argv) > 3:
    out = []
    for off, d in segs:
        end = off + len(d)
        if end <= cs or off >= ce:
            out.append([off, d]); continue
        if off < cs:
            out.append([off, d[:cs - off]])
        if end > ce:
            out.append([ce, d[ce - off:]])
    body = bytearray(enc(len(out)))
    for off, d in out:
        body += b'\0\x41' + senc(off if off < 2**31 else off - 2**32) + b'\x0b' + enc(len(d)) + d
    for s in secs:
        if s[0] == 11:
            s[1] = bytes(body)
        if s[0] == 12:
            s[1] = enc(len(out))
    o = bytearray(w[:8])
    for sid, bb in secs:
        o += bytes([sid]) + enc(len(bb)) + bb
    open(sys.argv[3], 'wb').write(o)
print('lits: %d slots, %d unique, %d bytes of cd_chars' % (len(slots), len(uniq), ce - cs), file=sys.stderr)
