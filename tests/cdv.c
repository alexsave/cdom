/* cdv, the HOST op's value format (cdom.h): every tag goes through the C builders and comes
 * back out of the C sink's decoder (cdom_html.c) as a stable string, which must equal the
 * expected one. The same cases built for wasm are decoded by the shim in Chrome
 * (tests/host.mjs mounts a probe class that renders the decoded value the same way), so the
 * C and the JS decoders are held to one rendering. Natively it also checks that a value too
 * large for one batch, a misbuilt tree and a host op left open trap, that a host op after a
 * nearly full batch is never split by a flush, and what PROP leaves in the server's HTML.
 *   native: cdv           runs every check, "ok"/"FAIL" per line, exit 1 on a failure
 *           cdv --list    prints "<case>\t<expected>" per case for the browser check
 *   wasm:   exports ncases() and emit(i): MOUNT window.CdvProbe on a node with case i */
#include "../src/cdom.h"
#ifdef __wasm__
#define EXPORT(n) __attribute__((export_name(n)))
#else
#define EXPORT(n)
#endif

static cd_str z(const char *s) { return cd_internz(s); }
static cd_id NODE;

void cd_on_event(const cd_event_rec *e) { (void)e; }
void cd_main(void) {
  cd_at(CD_MOUNT, 0);
  cd_open(z("div"));
  NODE = cd_id_new();
  cd_name(NODE);
  cd_close();
}

static void c_null(void) { cd_v_null(); }
static void c_false(void) { cd_v_bool(0); }
static void c_true(void) { cd_v_bool(7); }
static void c_u32(void) { cd_v_u32(4294967295u); }
static void c_i32(void) { cd_v_i32(-2147483647 - 1); }
static void c_f64(void) {
  static const double V[] = {1.5, -0.25, 0.1, 1.0 / 3, -0.0, 100, 1e20, 1e21, 1e-7, 0.000001, 123456789012.0,
                             5e-324, 1.7976931348623157e308, 2.5e-10, -1e100};
  cd_v_arr(sizeof V / sizeof *V);
  for (uint32_t i = 0; i < sizeof V / sizeof *V; i++) cd_v_f64(V[i]);
}
static void c_special(void) {
  double zero = 0;
  cd_v_arr(3);
  cd_v_f64(zero / zero);
  cd_v_f64(1 / zero);
  cd_v_f64(-1 / zero);
}
static void c_str(void) { cd_v_str(z("h\xc3\xa9llo \"q\" \\ \xf0\x9f\x98\x80")); }
static void c_empty_str(void) { cd_v_str(z("")); }
static void c_f64s(void) {
  static const double V[] = {1, -2.5, 3e30, 0.1};
  cd_v_f64s(V, 4);
}
static void c_f64s_empty(void) { cd_v_f64s(0, 0); }
static void c_arr(void) {
  cd_v_arr(5);
  cd_v_u32(1);
  cd_v_arr(0);
  cd_v_obj(0);
  cd_v_arr(2); cd_v_null(); cd_v_str(z("x"));
  cd_v_i32(-3);
}
static void c_obj(void) {
  static const double D[] = {1, 2};
  cd_v_obj(4);
  cd_v_key(z("type")); cd_v_str(z("line"));
  cd_v_key(z("data"));
  cd_v_obj(2);
  cd_v_key(z("labels")); cd_v_arr(2); cd_v_u32(1); cd_v_u32(2);
  cd_v_key(z("datasets")); cd_v_arr(1); cd_v_obj(2);
  cd_v_key(z("data")); cd_v_f64s(D, 2);
  cd_v_key(z("borderColor")); cd_v_str(z("#c33"));
  cd_v_key(z("on")); cd_v_bool(1);
  cd_v_key(z("x-y")); cd_v_null();
}
static void c_deep(void) { /* CD_V_DEPTH arrays, the deepest allowed */
  for (uint32_t i = 0; i < CD_V_DEPTH; i++) cd_v_arr(1);
  cd_v_u32(9);
}
static void c_big(void) { /* 16000 doubles: 32002 words plus the op's 3, under ARGS_CAP */
  static double V[16000];
  for (uint32_t i = 0; i < 16000; i++) V[i] = i + 0.5;
  cd_v_obj(1);
  cd_v_key(z("n"));
  cd_v_f64s(V, 16000);
}

typedef struct { const char *want; void (*build)(void); } Case;
static const Case CASES[] = {
  {"null", c_null},
  {"false", c_false},
  {"true", c_true},
  {"4294967295", c_u32},
  {"-2147483648", c_i32},
  {"[1.5,-0.25,0.1,0.3333333333333333,0,100,100000000000000000000,1e+21,1e-7,0.000001,123456789012,5e-324,"
   "1.7976931348623157e+308,2.5e-10,-1e+100]", c_f64},
  {"[NaN,Infinity,-Infinity]", c_special},
  {"\"h\xc3\xa9llo \\\"q\\\" \\\\ \xf0\x9f\x98\x80\"", c_str},
  {"\"\"", c_empty_str},
  {"f64[1,-2.5,3e+30,0.1]", c_f64s},
  {"f64[]", c_f64s_empty},
  {"[1,[],{},[null,\"x\"],-3]", c_arr},
  {"{type:\"line\",data:{labels:[1,2],datasets:[{data:f64[1,2],borderColor:\"#c33\"}]},on:true,x-y:null}", c_obj},
  {"[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[9]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]",
   c_deep},
  {0, c_big}, /* checked by its length and ends natively; the browser compares the whole rendering */
};
#define NCASES (sizeof CASES / sizeof *CASES)

EXPORT("ncases") uint32_t ncases(void) { return NCASES; }
EXPORT("emit") void emit(uint32_t i) {
  cd_host(NODE, CD_HOST_MOUNT, z("CdvProbe"));
  CASES[i].build();
  cd_host_end();
  cd_flush();
}

#ifndef __wasm__
#include "../src/cdom_html.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int fails;
static void ok(int c, const char *what) {
  printf("%s %s\n", c ? "ok  " : "FAIL", what);
  fails += !c;
}

static char *last(void) {
  uint32_t n = cd_html_host_last(0, 0);
  char *b = malloc(n + 1);
  cd_html_host_last(b, n);
  b[n] = 0;
  return b;
}

static char *html(void) {
  uint32_t n = cd_html_write(0, 0);
  char *b = malloc(n + 1);
  cd_html_write(b, n);
  b[n] = 0;
  return b;
}

/* run f in a child: did it die by a trap? */
static int traps(void (*f)(void)) {
  fflush(stdout);
  pid_t p = fork();
  if (!p) {
    signal(SIGTRAP, SIG_DFL);
    signal(SIGILL, SIG_DFL);
    f();
    cd_flush();
    _exit(0);
  }
  int st = 0;
  waitpid(p, &st, 0);
  return WIFSIGNALED(st) && (WTERMSIG(st) == SIGTRAP || WTERMSIG(st) == SIGILL);
}

static void t_no_value(void) { cd_host(NODE, CD_HOST_CALL, z("f")); cd_host_end(); }
static void t_two_values(void) { cd_host(NODE, CD_HOST_CALL, z("f")); cd_v_null(); cd_v_null(); }
static void t_short_array(void) { cd_host(NODE, CD_HOST_CALL, z("f")); cd_v_arr(2); cd_v_u32(1); cd_host_end(); }
static void t_missing_key(void) { cd_host(NODE, CD_HOST_SET, z("a")); cd_v_obj(1); cd_v_u32(1); }
static void t_key_in_array(void) { cd_host(NODE, CD_HOST_SET, z("a")); cd_v_arr(1); cd_v_key(z("k")); }
static void t_two_keys(void) { cd_host(NODE, CD_HOST_SET, z("a")); cd_v_obj(1); cd_v_key(z("k")); cd_v_key(z("j")); }
static void t_too_deep(void) {
  cd_host(NODE, CD_HOST_SET, z("a"));
  for (uint32_t i = 0; i <= CD_V_DEPTH; i++) cd_v_arr(1);
}
static void t_too_big(void) {
  static double V[16400];
  cd_host(NODE, CD_HOST_SET, z("a"));
  cd_v_f64s(V, 16400);
}
static void t_left_open(void) { cd_host(NODE, CD_HOST_SET, z("a")); cd_v_null(); cd_host(NODE, CD_HOST_SET, z("b")); }
static void t_no_host(void) { cd_v_null(); }
static void t_good(void) { cd_host(NODE, CD_HOST_SET, z("a")); cd_v_arr(1); cd_v_obj(1); cd_v_key(z("k")); cd_v_null(); cd_host_end(); }

static void prop_page(void) {
  cd_at(CD_DETACHED, 0); /* cd_at skips a repeat, and the sink forgot the last one at reset */
  cd_at(CD_MOUNT, 0);
  const char *tags[] = {"input", "input", "textarea", "select", "button"};
  static cd_id ids[5];
  for (int i = 0; i < 5; i++) {
    cd_open(z(tags[i]));
    ids[i] = cd_id_new();
    cd_name(ids[i]);
    if (i == 1) cd_attr(z("checked"), z(""));
    if (i == 2) cd_text(z("old"));
    if (i == 3) {
      const char *o[] = {"a", "b", "c"};
      for (int k = 0; k < 3; k++) {
        cd_open(z("option"));
        if (k != 1) cd_attr(z("value"), z(o[k]));
        if (k == 0) cd_attr(z("selected"), z(""));
        cd_text(z(o[k]));
        cd_close();
      }
    }
    cd_close();
  }
  cd_host(ids[0], CD_HOST_PROP, z("value")); cd_v_str(z("a\"b")); cd_host_end();
  cd_host(ids[0], CD_HOST_PROP, z("checked")); cd_v_u32(1); cd_host_end();
  cd_host(ids[1], CD_HOST_PROP, z("checked")); cd_v_bool(0); cd_host_end();
  cd_host(ids[2], CD_HOST_PROP, z("value")); cd_v_f64(2.5); cd_host_end();
  cd_host(ids[3], CD_HOST_PROP, z("value")); cd_v_str(z("b")); cd_host_end();
  cd_host(ids[4], CD_HOST_PROP, z("disabled")); cd_v_str(z("x")); cd_host_end();
  cd_host(ids[4], CD_HOST_PROP, z("scrollTop")); cd_v_u32(5); cd_host_end();
  cd_flush();
}

int main(int argc, char **argv) {
  if (argc > 1 && !strcmp(argv[1], "--list")) {
    for (uint32_t i = 0; i < NCASES; i++)
      if (CASES[i].want) printf("%u\t%s\n", i, CASES[i].want);
    return 0;
  }
  cd_html_reset();
  cd_main();
  cd_flush();
  char want[4096];
  for (uint32_t i = 0; i < NCASES; i++) {
    emit(i);
    char *got = last();
    if (CASES[i].want) {
      snprintf(want, sizeof want, "0 %u CdvProbe %s", NODE, CASES[i].want);
      int same = !strcmp(got, want);
      snprintf(want, sizeof want, "case %u decodes to %.60s%s", i, CASES[i].want, strlen(CASES[i].want) > 60 ? "..." : "");
      ok(same, want);
      if (!same) printf("     got %s\n", got);
    } else { /* the big one: {n:f64[0.5,1.5,...,15999.5]} */
      ok(!strncmp(got, "0 1 CdvProbe {n:f64[0.5,1.5,2.5,", 32) && strstr(got, ",15998.5,15999.5]}") &&
           got[strlen(got) - 1] == '}',
         "case 14: 16000 doubles round-trip through one op");
    }
    free(got);
  }
  ok(cd_html_host_ops(CD_HOST_MOUNT) == NCASES, "the sink counted every MOUNT");

  /* a host op after a nearly full argument stream: reserve flushes first, never mid-value */
  for (uint32_t i = 0; i < 32700; i++) cd_sel(NODE);
  emit(NCASES - 1);
  char *got = last();
  ok(strstr(got, ",15999.5]}") != 0, "a host op after 32700 queued words is not split by a flush");
  free(got);
  for (uint32_t i = 0; i < 32765; i++) cd_sel(NODE);
  emit(1);
  got = last();
  ok(!strcmp(got, "0 1 CdvProbe false"), "a small host op at the end of a batch arrives whole");
  free(got);

  ok(traps(t_no_value), "cd_host_end with no value traps");
  ok(traps(t_two_values), "a second top-level value traps");
  ok(traps(t_short_array), "an array short of its items traps at cd_host_end");
  ok(traps(t_missing_key), "an object member without its key traps");
  ok(traps(t_key_in_array), "a key inside an array traps");
  ok(traps(t_two_keys), "two keys in a row trap");
  ok(traps(t_too_deep), "nesting past CD_V_DEPTH traps");
  ok(traps(t_too_big), "a value larger than the argument stream traps");
  ok(traps(t_left_open), "a host op begun while another is open traps");
  ok(traps(t_no_host), "a value outside a host op traps");
  ok(!traps(t_good), "a well-formed value does not trap");

  cd_html_reset();
  prop_page();
  char *h = html();
  const char *want_html = "<input value=\"a&quot;b\" checked=\"\"><input><textarea>2.5</textarea>"
                          "<select><option value=\"a\">a</option><option selected=\"\">b</option><option value=\"c\">c</option>"
                          "</select><button disabled=\"\"></button>";
  ok(!strcmp(h, want_html), "PROP writes value, checked, selected, disabled and textarea text into the HTML");
  if (strcmp(h, want_html)) printf("     got  %s\n     want %s\n", h, want_html);
  free(h);
  ok(cd_html_host_ops(CD_HOST_PROP) == 7 && cd_html_host_ops(CD_HOST_MOUNT) == 0, "the counters restart at reset");
  printf(fails ? "cdv: %d FAILED\n" : "cdv: all ok\n", fails);
  return fails != 0;
}
#endif
