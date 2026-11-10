/* litgen: bare string literals in cd_str argument positions become constant string ids.
 *
 *   litgen -o OUTDIR [-h header]... source.c...
 *
 * 1. Every header and source is scanned for function prototypes/definitions at file scope; a
 *    parameter whose declaration is exactly `cd_str name` (no pointer, no function pointer)
 *    marks that argument position. So the list of calls is derived from cdom.h and the app's
 *    own headers (amgi's el(), el_text(), icon()...), never written by hand.
 * 2. At every call of such a function, an argument that is nothing but string literals
 *    ("a" "b", comments between allowed) is replaced by `((cd_str)N)`. Literals are decoded
 *    (escapes, concatenation) and de-duplicated by their bytes.
 * 3. OUTDIR/<base>.c is each rewritten source, starting with #line 1 "<original path>", with
 *    every newline kept so line numbers match. OUTDIR/litgen_table.c holds the strings and
 *    cd_lits_init(), which cdom's cd_start calls before cd_main: it interns them in order, so
 *    on a fresh table string N is id N (checked; a mismatch traps).
 * Anything else (a literal in a non-cd_str position, a call hidden behind a macro, L"wide")
 * is left alone, and then fails to compile as an int-conversion error, never silently. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

enum { T_ID, T_STR, T_PUNCT, T_OTHER };
typedef struct { int kind, start, end; } tok; /* byte span in the file; comments and spaces are not tokens */
typedef struct { char *name; unsigned mask; } sig;
typedef struct { char *b; int len; } lit;

static sig *sigs; static int nsig;
static lit *lits; static int nlit;

static char *slurp(const char *path, int *len) {
  FILE *f = fopen(path, "rb");
  if (!f) { perror(path); exit(1); }
  fseek(f, 0, SEEK_END); *len = (int)ftell(f); fseek(f, 0, SEEK_SET);
  char *b = malloc(*len + 1);
  if (fread(b, 1, *len, f) != (size_t)*len) { perror(path); exit(1); }
  b[*len] = 0; fclose(f);
  return b;
}

/* Tokens, enough for this: identifiers, string literals (with an optional u8 prefix; L/u/U
 * prefixes are T_OTHER so they are never rewritten), punctuation; comments skipped; a
 * preprocessor line that is #include is skipped whole (its "file" is not a literal). */
static tok *lex(const char *s, int n, int *ntok) {
  tok *t = malloc(sizeof(tok) * (n + 1)); int k = 0, i = 0, bol = 1;
  while (i < n) {
    char c = s[i];
    if (c == '\n') { bol = 1; i++; continue; }
    if (isspace((unsigned char)c)) { i++; continue; }
    if (c == '/' && s[i + 1] == '/') { while (i < n && s[i] != '\n') i++; continue; }
    if (c == '/' && s[i + 1] == '*') { i += 2; while (i < n && !(s[i] == '*' && s[i + 1] == '/')) i++; i += 2; continue; }
    if (bol && c == '#') {
      int j = i + 1; while (s[j] == ' ' || s[j] == '\t') j++;
      if (!strncmp(s + j, "include", 7) || !strncmp(s + j, "import", 6)) { while (i < n && s[i] != '\n') i++; continue; }
    }
    bol = 0;
    int st = i;
    if (isalpha((unsigned char)c) || c == '_') {
      while (i < n && (isalnum((unsigned char)s[i]) || s[i] == '_')) i++;
      if (i < n && (s[i] == '"' || s[i] == '\'')) { /* an encoding prefix */
        int u8 = i - st == 2 && s[st] == 'u' && s[st + 1] == '8';
        char q = s[i++];
        while (i < n && s[i] != q) i += s[i] == '\\' ? 2 : 1;
        i++;
        t[k++] = (tok){u8 && q == '"' ? T_STR : T_OTHER, st, i};
        continue;
      }
      t[k++] = (tok){T_ID, st, i}; continue;
    }
    if (c == '"' || c == '\'') {
      i++; while (i < n && s[i] != c) i += s[i] == '\\' ? 2 : 1;
      i++; t[k++] = (tok){c == '"' ? T_STR : T_OTHER, st, i}; continue;
    }
    if (isdigit((unsigned char)c)) { while (i < n && (isalnum((unsigned char)s[i]) || s[i] == '.' || s[i] == '_')) i++; t[k++] = (tok){T_OTHER, st, i}; continue; }
    i++; t[k++] = (tok){T_PUNCT, st, i};
  }
  *ntok = k; return t;
}

static int is(const char *s, tok t, const char *w) { int l = t.end - t.start; return (int)strlen(w) == l && !memcmp(s + t.start, w, l); }
static int punct(const char *s, tok t, char c) { return t.kind == T_PUNCT && s[t.start] == c; }

/* The token index of the bracket that closes the one at i. */
static int closer(const char *s, tok *t, int nt, int i) {
  int d = 0;
  for (; i < nt; i++) {
    if (t[i].kind != T_PUNCT) continue;
    char c = s[t[i].start];
    if (c == '(' || c == '[' || c == '{') d++;
    else if ((c == ')' || c == ']' || c == '}') && --d == 0) return i;
  }
  return -1;
}

static sig *find_sig(const char *s, tok t) {
  for (int i = 0; i < nsig; i++) if (is(s, t, sigs[i].name)) return &sigs[i];
  return 0;
}

static void add_sig(const char *s, tok name, unsigned mask) {
  if (find_sig(s, name)) { /* a prototype and its definition: they must agree */
    sig *g = find_sig(s, name);
    if (g->mask != mask) { fprintf(stderr, "litgen: %s declared with different cd_str parameters\n", g->name); exit(1); }
    return;
  }
  sigs = realloc(sigs, sizeof(sig) * (nsig + 1));
  sigs[nsig].name = strndup(s + name.start, name.end - name.start);
  sigs[nsig++].mask = mask;
}

/* File-scope `ident ( params ) ;|{` with at least one `cd_str name` parameter. */
static void scan_sigs(const char *s, tok *t, int nt) {
  int depth = 0;
  for (int i = 0; i < nt; i++) {
    if (punct(s, t[i], '{')) { depth++; continue; }
    if (punct(s, t[i], '}')) { depth--; continue; }
    if (depth || t[i].kind != T_ID || i + 1 >= nt || !punct(s, t[i + 1], '(')) continue;
    int c = closer(s, t, nt, i + 1);
    if (c < 0 || c + 1 >= nt || !(punct(s, t[c + 1], ';') || punct(s, t[c + 1], '{'))) continue;
    unsigned mask = 0; int arg = 0, a = i + 2;
    for (int j = i + 2; j <= c; j++) {
      if (j < c && !punct(s, t[j], ',')) continue;
      /* params a..j-1: exactly [const] cd_str [name] */
      int k = a; if (k < j && is(s, t[k], "const")) k++;
      if (k < j && is(s, t[k], "cd_str") && (j - k == 1 || (j - k == 2 && t[k + 1].kind == T_ID))) mask |= 1u << arg;
      arg++; a = j + 1;
    }
    if (mask) add_sig(s, t[i], mask);
    i = c;
  }
}

/* Decode one literal's bytes (without quotes, after any u8 prefix) onto out. */
static int decode(const char *p, const char *e, char *out) {
  int n = 0;
  while (p < e) {
    if (*p != '\\') { out[n++] = *p++; continue; }
    p++;
    switch (*p) {
    case 'n': out[n++] = '\n'; p++; break;
    case 't': out[n++] = '\t'; p++; break;
    case 'r': out[n++] = '\r'; p++; break;
    case 'a': out[n++] = '\a'; p++; break;
    case 'b': out[n++] = '\b'; p++; break;
    case 'f': out[n++] = '\f'; p++; break;
    case 'v': out[n++] = '\v'; p++; break;
    case 'x': { p++; unsigned v = 0; while (p < e && isxdigit((unsigned char)*p)) { v = v * 16 + (isdigit((unsigned char)*p) ? *p - '0' : (tolower(*p) - 'a' + 10)); p++; } out[n++] = (char)v; break; }
    case 'u': case 'U': fprintf(stderr, "litgen: \\u escapes not supported; write the UTF-8 bytes\n"); exit(1);
    default:
      if (*p >= '0' && *p <= '7') { unsigned v = 0; for (int d = 0; d < 3 && p < e && *p >= '0' && *p <= '7'; d++) v = v * 8 + (*p++ - '0'); out[n++] = (char)v; }
      else out[n++] = *p++; /* \\ \" \' \? */
    }
  }
  return n;
}

static int intern(char *b, int len) {
  for (int i = 0; i < nlit; i++) if (lits[i].len == len && !memcmp(lits[i].b, b, len)) { free(b); return i + 1; }
  lits = realloc(lits, sizeof(lit) * (nlit + 1));
  lits[nlit++] = (lit){b, len};
  return nlit;
}

static void rewrite(const char *path, const char *outdir, int emit) {
  int n, nt; char *s = slurp(path, &n); tok *t = lex(s, n, &nt);
  const char *base = strrchr(path, '/'); base = base ? base + 1 : path;
  char out[4096]; snprintf(out, sizeof out, "%s/%s", outdir, base);
  FILE *f = emit ? fopen(out, "w") : fopen("/dev/null", "w"); if (!f) { perror(out); exit(1); }
  fprintf(f, "#line 1 \"%s\"\n", path);
  int pos = 0, sites = 0;
  for (int i = 0; i + 1 < nt; i++) {
    if (t[i].kind != T_ID || !punct(s, t[i + 1], '(')) continue;
    if (i > 0 && (punct(s, t[i - 1], '.') || (punct(s, t[i - 1], '>') && i > 1 && punct(s, t[i - 2], '-')))) continue;
    sig *g = find_sig(s, t[i]); if (!g) continue;
    int c = closer(s, t, nt, i + 1); if (c < 0) continue;
    int arg = 0, a = i + 2, d = 0;
    for (int j = i + 2; j <= c; j++) {
      if (t[j].kind == T_PUNCT && strchr("([{", s[t[j].start])) d++;
      if (j < c && t[j].kind == T_PUNCT && strchr(")]}", s[t[j].start])) d--;
      if (j < c && (d || !punct(s, t[j], ','))) continue;
      int all = a < j;
      for (int k = a; k < j; k++) all &= t[k].kind == T_STR;
      if (all && (g->mask >> arg & 1)) {
        char *b = malloc(t[j - 1].end - t[a].start + 1); int len = 0;
        for (int k = a; k < j; k++) {
          const char *p = s + t[k].start; if (*p == 'u') p += 2;
          len += decode(p + 1, s + t[k].end - 1, b + len);
        }
        int id = intern(b, len); /* pass 1: lits is sorted and complete, so this is the sorted id */
        fwrite(s + pos, 1, t[a].start - pos, f);
        fprintf(f, "((cd_str)%d)", id);
        for (int q = t[a].start; q < t[j - 1].end; q++) if (s[q] == '\n') fputc('\n', f); /* keep the lines */
        pos = t[j - 1].end; sites++;
      }
      arg++; a = j + 1;
    }
  }
  fwrite(s + pos, 1, n - pos, f); fclose(f);
  if (emit) fprintf(stderr, "litgen: %s: %d sites\n", path, sites);
  free(t); free(s);
}

/* Ids 1..N by the strings' bytes, so one set of strings always gets the same ids. */
static int cmp_lit(const void *x, const void *y) {
  const lit *a = x, *b = y; int m = a->len < b->len ? a->len : b->len, c = memcmp(a->b, b->b, m);
  return c ? c : a->len - b->len;
}
static void sort_lits(void) { qsort(lits, nlit, sizeof(lit), cmp_lit); }

static void write_table(const char *outdir) {
  char out[4096]; snprintf(out, sizeof out, "%s/litgen_table.c", outdir);
  FILE *f = fopen(out, "w"); if (!f) { perror(out); exit(1); }
  fprintf(f, "/* generated by litgen: do not edit. String N is cd_str N. */\n#include \"cdom.h\"\n");
  fprintf(f, "static const char lit_bytes[] =\n");
  for (int i = 0; i < nlit; i++) {
    fputs("  \"", f);
    for (int k = 0; k < lits[i].len; k++) {
      unsigned char c = (unsigned char)lits[i].b[k];
      if (c == '"' || c == '\\') fprintf(f, "\\%c", c);
      else if (c < 32 || c == 127 || c == '?') fprintf(f, "\\%03o", c); /* ? : no trigraphs */
      else fputc(c, f);
    }
    fputs("\"\n", f);
  }
  fprintf(f, "  ;\nstatic const uint16_t lit_len[%d] = {", nlit ? nlit : 1);
  for (int i = 0; i < nlit; i++) fprintf(f, "%s%d", i ? "," : "", lits[i].len);
  fprintf(f, "};\nvoid cd_lits_init(void) { cd_lits_register(lit_bytes, lit_len, %d); }\n", nlit);
  fclose(f);
  fprintf(stderr, "litgen: %d strings, %d calls with cd_str parameters\n", nlit, nsig);
}

int main(int argc, char **argv) {
  const char *outdir = 0; const char *hdr[64]; int nh = 0; const char *src[256]; int ns = 0;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "-o") && i + 1 < argc) outdir = argv[++i];
    else if (!strcmp(argv[i], "-h") && i + 1 < argc) hdr[nh++] = argv[++i];
    else src[ns++] = argv[i];
  }
  if (!outdir || !ns) { fprintf(stderr, "usage: litgen -o OUTDIR [-h header]... source.c...\n"); return 2; }
  for (int pass = 0; pass < 2; pass++)
    for (int i = 0; i < (pass ? ns : nh); i++) {
      int n, nt; char *s = slurp(pass ? src[i] : hdr[i], &n); tok *t = lex(s, n, &nt);
      scan_sigs(s, t, nt); free(t); free(s);
    }
  for (int i = 0; i < ns; i++) rewrite(src[i], outdir, 0);
  sort_lits();
  for (int i = 0; i < ns; i++) rewrite(src[i], outdir, 1);
  write_table(outdir);
  return 0;
}
