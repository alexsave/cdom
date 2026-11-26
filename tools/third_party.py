#!/usr/bin/env python3
"""The third-party code cdom vendors under third_party/, made from the upstream release archives.

  python3 -I tools/third_party.py fetch   downloads each archive (or takes it from the cache), checks its sha256,
                                          and writes third_party/<name>/ from it: the exact file list below, with
                                          the documented edits applied, and nothing else (README.md, written by
                                          hand, is kept)
  python3 -I tools/third_party.py check   makes the same files in a temporary directory and compares them with
                                          the committed tree, byte for byte; it fails on any difference, on a file
                                          missing, and on a file in the tree that this script does not make

Archives are cached in $CDOM_THIRD_PARTY_CACHE (default ~/.cache/cdom-third-party), so check runs offline once
they are there. The archives are read in memory (tarfile, zipfile); nothing of them is extracted to disk.
Not shipped: this is a maintenance tool. Each edit below is listed in that directory's README.md too."""
import hashlib, io, os, pathlib, shutil, sys, tarfile, tempfile, urllib.request, zipfile

top = pathlib.Path(__file__).resolve().parent.parent
cache = pathlib.Path(os.environ.get('CDOM_THIRD_PARTY_CACHE', pathlib.Path.home() / '.cache' / 'cdom-third-party'))

# ---- libjpeg-turbo 3.2.1: the 8-bit lossy Huffman decoder ----

LJT = 'libjpeg-turbo-3.2.1'
LJT_URL = 'https://github.com/libjpeg-turbo/libjpeg-turbo/releases/download/3.2.1/libjpeg-turbo-3.2.1.tar.gz'
LJT_SHA = 'c27ad72a68da151177929aa969ae773a2da5ac2af9ce946079314fdac8cece7f'
# Compiled: the files below that the module's build lists (cdom.mk, CDOM_IMAGE_VENDOR_SRC). Included: the rest.
# jmemnobs.c (the system memory layer) is not here: src/cdom_image_jpeg.c is cdom's own, over cd_alloc_try.
LJT_COPY = ['LICENSE.md', 'README.ijg'] + ['src/' + f for f in [
  'jcomapi.c', 'jdapimin.c', 'jdapistd.c', 'jdcoefct.c', 'jdcoefct.h', 'jdcol565.c', 'jdcolext.c', 'jdcolor.c',
  'jdct.h', 'jddctmgr.c', 'jdhuff.c', 'jdhuff.h', 'jdinput.c', 'jdmainct.c', 'jdmainct.h', 'jdmarker.c',
  'jdmaster.c', 'jdmaster.h', 'jdmerge.h', 'jdphuff.c', 'jdpostct.c', 'jdsample.c', 'jdsample.h', 'jerror.h',
  'jidctint.c', 'jidctred.c', 'jinclude.h', 'jmemmgr.c', 'jmemsys.h', 'jpeg_nbits.c', 'jpeg_nbits.h',
  'jpegapicomp.h', 'jpegint.h', 'jpeglib.h', 'jsamplecomp.h', 'jstdhuff.c', 'jutils.c',
  'wrapper/jdapistd-8.c', 'wrapper/jdcoefct-8.c', 'wrapper/jdcolor-8.c', 'wrapper/jddctmgr-8.c',
  'wrapper/jdmainct-8.c', 'wrapper/jdpostct-8.c', 'wrapper/jdsample-8.c', 'wrapper/jidctint-8.c',
  'wrapper/jidctred-8.c', 'wrapper/jutils-8.c']]
# Edit 1, src/jmorecfg.h: the features the decoder never uses here, switched off (each line must be there once),
# and a line in its copyright header that says so.
LJT_OFF = ['DCT_IFAST_SUPPORTED', 'DCT_FLOAT_SUPPORTED', 'D_LOSSLESS_SUPPORTED', 'SAVE_MARKERS_SUPPORTED',
           'UPSAMPLE_MERGING_SUPPORTED', 'QUANT_1PASS_SUPPORTED', 'QUANT_2PASS_SUPPORTED']
# Edits 2 and 3, src/jconfig.h and src/jconfigint.h: CMake's templates (jconfig.h.in, jconfigint.h.in) filled in
# as CMake would for this build: these values, and every #cmakedefine off but HAVE_BUILTIN_CTZL, and WITH_SIMD,
# which is on when the compiler is given -DCDOM_JSIMD (cdom.mk, CDOM_IMAGE_SIMD=1) and off otherwise.
LJT_VARS = {
    'JPEG_LIB_VERSION': '80', 'VERSION': '3.2.1', 'LIBJPEG_TURBO_VERSION_NUMBER': '3002001', 'BUILD': 'cdom',
    'HIDDEN': '__attribute__((visibility("hidden")))', 'INLINE': 'inline __attribute__((always_inline))',
    'THREAD_LOCAL': '', 'CMAKE_PROJECT_NAME': 'libjpeg-turbo', 'SIZE_T': '__SIZEOF_SIZE_T__',
    'SIMD_ARCHITECTURE': 'NONE',
}
LJT_ON = {'HAVE_BUILTIN_CTZL'}

def cmake_fill(text):
    out = []
    for line in text.split('\n'):
        if line.startswith('#cmakedefine '):
            parts = line.split()
            name = parts[1]
            if name == 'WITH_SIMD':   # on only when the build says so (cdom.mk, CDOM_IMAGE_SIMD=1 defines CDOM_JSIMD)
                out.append('#ifdef CDOM_JSIMD'); out.append('#define WITH_SIMD 1'); out.append('#else'); out.append('/* #undef WITH_SIMD */'); out.append('#endif')
                continue
            line = ('#define ' + ' '.join(parts[1:])) if name in LJT_ON else f'/* #undef {name} */'
        while '@' in line:
            a = line.index('@'); b = line.index('@', a + 1)
            line = line[:a] + LJT_VARS[line[a + 1:b]] + line[b + 1:]
        out.append(line)
    return '\n'.join(out)

def ljt(arch):
    t = tarfile.open(fileobj=io.BytesIO(arch), mode='r:gz')
    get = lambda p: t.extractfile(f'{LJT}/{p}').read()
    files = {p: get(p) for p in LJT_COPY}
    cfg = get('src/jmorecfg.h').decode()
    for name in LJT_OFF:
        old = f'#define {name} '
        if cfg.count(old) != 1: sys.exit(f'third_party: jmorecfg.h holds "{old}" {cfg.count(old)} times, not once')
        cfg = cfg.replace(old, f'/* cdom: off */ /* #define {name} */ ')
    # and a notice of the change in its header, as the licence asks of a modified file (LICENSE.md, item 1.2)
    head = ' *           D. R. Commander.\n'
    if cfg.count(head) != 1: sys.exit('third_party: jmorecfg.h has no copyright header to add the notice under')
    cfg = cfg.replace(head, head + ' * cdom Modifications (2026): seven features switched off, each marked "cdom: off" (see ../README.md).\n')
    files['src/jmorecfg.h'] = cfg.encode()
    files['src/jconfig.h'] = cmake_fill(get('src/jconfig.h.in').decode()).encode()
    files['src/jconfigint.h'] = cmake_fill(get('src/jconfigint.h.in').decode()).encode()
    return files

# ---- miniz 3.1.2: the decompressor (tinfl) only ----

MZ = 'miniz-3.1.2'
MZ_URL = 'https://github.com/richgel999/miniz/releases/download/3.1.2/miniz-3.1.2.zip'
MZ_SHA = 'f0446d863f9c19926ad9483c523fdc42e42b8d4a6a431d27e09d49c79a140d9a'
# Edit 1: tinfl.c is miniz.c's inflate section and tinfl.h miniz.h's, cut whole and unchanged: from its
# "#ifndef MINIZ_NO_INFLATE_APIS" to the matching "#endif" (line numbers, then the two lines checked).
# The types and macros the cut needs from the rest of miniz are cdom's (src/cdom_image_miniz.h, which the
# build includes ahead of tinfl.c with -include).
MZ_CUT = {'tinfl.c': ('miniz.c', 2278, 3027), 'tinfl.h': ('miniz.h', 911, 1056)}
MZ_FIRST = '#ifndef MINIZ_NO_INFLATE_APIS'
MZ_LAST = '#endif /*#ifndef MINIZ_NO_INFLATE_APIS*/'

def mz(arch):
    z = zipfile.ZipFile(io.BytesIO(arch))
    files = {'LICENSE': z.read('LICENSE')}
    for out, (src, a, b) in MZ_CUT.items():
        lines = z.read(src).decode().split('\n')
        cut = lines[a - 1:b]
        if cut[0].strip() != MZ_FIRST or cut[-1].strip() != MZ_LAST:
            sys.exit(f'third_party: {src} lines {a} to {b} are not its inflate section ({cut[0]!r} ... {cut[-1]!r})')
        files[out] = ('\n'.join(cut) + '\n').encode()
    return files

PARTS = [(LJT, LJT_URL, LJT_SHA, ljt), (MZ, MZ_URL, MZ_SHA, mz)]

def archive(url, sha):
    cache.mkdir(parents=True, exist_ok=True)
    path = cache / url.rsplit('/', 1)[1]
    if not path.exists():
        data = urllib.request.urlopen(url, timeout=120).read()
        path.with_suffix('.part').write_bytes(data)
        path.with_suffix('.part').rename(path)
    data = path.read_bytes()
    got = hashlib.sha256(data).hexdigest()
    if got != sha: sys.exit(f'third_party: {path} has sha256 {got}, not {sha}')
    return data

def make(dest):
    made = {}
    for name, url, sha, fn in PARTS:
        for rel, data in fn(archive(url, sha)).items():
            made[f'{name}/{rel}'] = data
    for rel, data in made.items():
        p = dest / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(data)
    return made

def tree(root):
    return sorted(str(p.relative_to(root)) for p in root.rglob('*') if p.is_file())

def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else ''
    tp = top / 'third_party'
    if cmd == 'fetch':
        for name, *_ in PARTS:
            d = tp / name
            if d.exists():
                for f in tree(d):
                    if f != 'README.md': (d / f).unlink()
        made = make(tp)
        print(f'third_party: wrote {len(made)} files under third_party/')
    elif cmd == 'check':
        with tempfile.TemporaryDirectory() as t:
            made = make(pathlib.Path(t))
            bad = []
            for rel, data in sorted(made.items()):
                p = tp / rel
                if not p.exists(): bad.append(f'missing {rel}')
                elif p.read_bytes() != data: bad.append(f'differs {rel}')
            for name, *_ in PARTS:
                for f in tree(tp / name):
                    if f != 'README.md' and f'{name}/{f}' not in made: bad.append(f'not made by this script {name}/{f}')
                if not (tp / name / 'README.md').exists(): bad.append(f'missing {name}/README.md')
            for b in bad: print(f'FAIL third_party: {b}')
            if bad: sys.exit(1)
            print(f'ok   third_party: the {len(made)} vendored files are the upstream archives with the documented edits, and nothing else')
    else:
        sys.exit(__doc__)

main()
