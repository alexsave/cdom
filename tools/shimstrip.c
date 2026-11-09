/* shimstrip: the minified shim cut down to the imports one wasm module uses (cdom.mk, a bundled page).
 *
 *   shimstrip module.wasm shim/cdom.min.js shim/cdom.strip > out.js
 *
 * Reads the module's import section, keeps every unit of cdom.strip (tools/striptable.mjs says what a
 * unit is) that a cdom.<name> import the module has, or the rest of the shim, names, directly or through
 * other kept units, and writes cdom.min.js without the bytes of the units nobody keeps. Fails, writing
 * nothing, when the module imports a cdom.<name> the shim has no import for (it would not link), when
 * the table is not this shim's (its byte count), or on a module or table it cannot read.
 * Plain C99, no dependencies: cdom.mk builds it with the host's cc. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *me = "shimstrip";

static void die(const char *what, const char *detail) {
  fprintf(stderr, "%s: %s%s%s\n", me, what, detail ? ": " : "", detail ? detail : "");
  exit(1);
}

static unsigned char *slurp(const char *path, long *size) {
  FILE *f = fopen(path, "rb");
  if (!f) die("cannot open", path);
  long cap = 1 << 16, n = 0;
  unsigned char *buf = malloc(cap + 1);
  size_t got;
  while (buf && (got = fread(buf + n, 1, cap - n, f)) > 0) {
    n += (long)got;
    if (n == cap) buf = realloc(buf, (cap *= 2) + 1);
  }
  if (!buf) die("out of memory", NULL);
  if (ferror(f)) die("cannot read", path);
  fclose(f);
  buf[n] = 0;
  *size = n;
  return buf;
}

/* The wasm reader: a cursor over the module's bytes. */
static const unsigned char *at, *end;

static unsigned long leb(void) {
  unsigned long value = 0;
  int shift = 0;
  for (;;) {
    if (at >= end || shift > 63) die("a malformed module", "LEB128 past the end");
    unsigned char b = *at++;
    value |= (unsigned long)(b & 0x7f) << shift;
    if (!(b & 0x80)) return value;
    shift += 7;
  }
}
static unsigned char byte(void) {
  if (at >= end) die("a malformed module", "past the end");
  return *at++;
}
static void limits(void) {
  unsigned char flags = byte();
  leb();
  if (flags & 1) leb();
}

/* The table: units, their ranges and deps. */
enum { F, V, G, I };
typedef struct {
  int kind;
  long start, end;
  int group;      /* a V's G */
  char name[64];  /* an I's import name */
  int *deps;
  int ndeps;
  int keep;
} unit;

static unit *units;
static int nunits;
static int *roots, nroots;

static int parse_deps(char **p, int **out) {
  int n = 0, cap = 8;
  int *list = malloc(cap * sizeof *list);
  for (;;) {
    while (**p == ' ') (*p)++;
    if (**p < '0' || **p > '9') break;
    long d = strtol(*p, p, 10);
    if (d < 0 || d >= nunits) die("a malformed table", "a dep out of range");
    if (n == cap) list = realloc(list, (cap *= 2) * sizeof *list);
    list[n++] = (int)d;
  }
  *out = list;
  return n;
}

static void read_table(char *text, long shim_bytes) {
  char *line = text, *next;
  long bytes;
  if (sscanf(line, "cdomstrip 1 %ld %d", &bytes, &nunits) != 2) die("a malformed table", "no cdomstrip 1 header");
  if (bytes != shim_bytes) die("the table is not this shim's", "its byte count differs; run node tools/minshim.mjs");
  units = calloc(nunits, sizeof *units);
  int u = 0;
  for (line = strchr(line, '\n'); line && *++line; line = next) {
    next = strchr(line, '\n');
    if (next) *next = 0;
    char *p = line + 2;
    if (line[0] == 'R') {
      p = line + 1;
      nroots = parse_deps(&p, &roots);
    } else {
      if (u >= nunits) die("a malformed table", "more units than its header says");
      unit *x = &units[u++];
      x->kind = line[0] == 'F' ? F : line[0] == 'V' ? V : line[0] == 'G' ? G : line[0] == 'I' ? I : -1;
      if (x->kind < 0) die("a malformed table", line);
      x->start = strtol(p, &p, 10);
      x->end = strtol(p, &p, 10);
      if (x->start < 0 || x->end < x->start || x->end > shim_bytes) die("a malformed table", "a range outside the shim");
      if (x->kind == V) x->group = (int)strtol(p, &p, 10);
      if (x->kind == I) {
        while (*p == ' ') p++;
        int n = 0;
        while (*p && *p != ' ' && n < (int)sizeof x->name - 1) x->name[n++] = *p++;
      }
      x->ndeps = parse_deps(&p, &x->deps);
    }
    if (!next) break;
  }
  if (u != nunits) die("a malformed table", "fewer units than its header says");
}

static void keep(int u) {
  if (units[u].keep) return;
  units[u].keep = 1;
  for (int i = 0; i < units[u].ndeps; i++) keep(units[u].deps[i]);
}

/* Marks the I units of every cdom import the module has. */
static void read_imports(const unsigned char *wasm, long size) {
  at = wasm;
  end = wasm + size;
  if (size < 8 || memcmp(wasm, "\0asm", 4)) die("not a wasm module", NULL);
  at += 8;
  while (at < end) {
    unsigned char id = byte();
    unsigned long length = leb();
    const unsigned char *section_end = at + length;
    if (length > (unsigned long)(end - at)) die("a malformed module", "a section past the end");
    if (id != 2) {
      at = section_end;
      continue;
    }
    unsigned long count = leb();
    for (unsigned long k = 0; k < count; k++) {
      unsigned long module_len = leb();
      const unsigned char *module = at;
      if (module_len > (unsigned long)(end - at)) die("a malformed module", "a name past the end");
      at += module_len;
      unsigned long field_len = leb();
      const unsigned char *field = at;
      if (field_len > (unsigned long)(end - at)) die("a malformed module", "a name past the end");
      at += field_len;
      switch (byte()) {
        case 0: leb(); break;              /* a function: its type */
        case 1: byte(); limits(); break;   /* a table */
        case 2: limits(); break;           /* a memory */
        case 3: byte(); byte(); break;     /* a global */
        case 4: byte(); leb(); break;      /* a tag */
        default: die("a malformed module", "an import of unknown kind");
      }
      if (module_len != 4 || memcmp(module, "cdom", 4)) continue;
      int found = 0;
      for (int u = 0; u < nunits; u++) {
        if (units[u].kind == I && strlen(units[u].name) == field_len && !memcmp(units[u].name, field, field_len)) {
          keep(u);
          found = 1;
        }
      }
      if (!found) {
        char name[128];
        snprintf(name, sizeof name, "cdom.%.*s", (int)(field_len < 100 ? field_len : 100), (const char *)field);
        die("the module imports a function this shim does not have (a cdom.c and a shim from two cdoms?)", name);
      }
    }
    return;
  }
}

int main(int argc, char **argv) {
  if (argc != 4) {
    fprintf(stderr, "usage: %s module.wasm cdom.min.js cdom.strip > out.js\n", me);
    return 2;
  }
  long wasm_size, shim_size, table_size;
  unsigned char *wasm = slurp(argv[1], &wasm_size);
  unsigned char *shim = slurp(argv[2], &shim_size);
  char *table = (char *)slurp(argv[3], &table_size);
  read_table(table, shim_size);
  for (int i = 0; i < nroots; i++) keep(roots[i]);
  read_imports(wasm, wasm_size);
  /* A G whose declarators are all cut goes too. */
  for (int u = 0; u < nunits; u++) {
    if (units[u].kind != G) continue;
    int any = 0;
    for (int v = 0; v < nunits; v++) any |= units[v].kind == V && units[v].group == u && units[v].keep;
    units[u].keep = any;
  }
  unsigned char *cut = calloc(shim_size, 1);
  for (int u = 0; u < nunits; u++) {
    if (units[u].kind == F && !units[u].keep) memset(cut + units[u].start, 1, units[u].end - units[u].start);
    if (units[u].kind == G && !units[u].keep) memset(cut + units[u].start, 1, units[u].end - units[u].start);
  }
  /* The lists: each G's V, and the I. */
  for (int g = -1; g < nunits; g++) {
    if (g >= 0 && units[g].kind != G) continue;
    int *list = malloc(nunits * sizeof *list), n = 0, last_kept = -1;
    for (int u = 0; u < nunits; u++) {
      if (g >= 0 ? units[u].kind == V && units[u].group == g : units[u].kind == I) {
        if (n && (units[u].start != units[list[n - 1]].end + 1 || shim[units[list[n - 1]].end] != ','))
          die("a malformed table", "two entries of a list not joined by one comma");
        if (units[u].keep) last_kept = n;
        list[n++] = u;
      }
    }
    for (int k = 0; k < n; k++) {
      unit *x = &units[list[k]];
      if (x->keep) continue;
      if (k < last_kept) memset(cut + x->start, 1, x->end + 1 - x->start); /* with the comma after it */
      else if (last_kept >= 0) memset(cut + x->start - 1, 1, x->end + 1 - x->start); /* with the one before */
      else memset(cut + x->start, 1, (k + 1 < n ? x->end + 1 : x->end) - x->start);
    }
    free(list);
  }
  for (long i = 0; i < shim_size; i++) {
    if (!cut[i]) putchar(shim[i]);
  }
  if (fflush(stdout) || ferror(stdout)) die("cannot write the stripped shim", NULL);
  return 0;
}
