/* One call of each row of the classification table (docs/strings-design.md, "How a literal is told from a
 * runtime string"). run.sh checks which of them became slots (cdlits --list) and what string each one
 * shows (a Node run with stub imports). */
#include "stub.h"

#define GREETING "hi-macro"
static const char NAME[] = "const-array";
static char mut[8] = "mut";
static const char *names[] = {"name-zero", "name-one"};
static const char *param_text = "from-a-pointer";
static volatile int sel = 1;

static void label(const char *s) { cd_text(s); }

EX(k_literal) void k_literal(void) { cd_text("div"); }
EX(k_concat) void k_concat(void) { cd_text("a" "b"); }
EX(k_macro) void k_macro(void) { cd_text(GREETING); }
EX(k_stack) void k_stack(void) {
  char b[8] = "stack";
  b[0] = 'S';
  cd_text(b);
}
EX(k_pointer) void k_pointer(void) { cd_text(param_text); }
EX(k_table) void k_table(void) { cd_text(names[1]); }
EX(k_mut) void k_mut(void) { cd_text(mut); }
EX(k_mutate) void k_mutate(void) { mut[0] = 'n'; }
EX(k_const_array) void k_const_array(void) { cd_text(NAME); }
EX(k_ternary) void k_ternary(void) { cd_text(sel ? "danger" : ""); }
EX(k_ternary_s) void k_ternary_s(void) { cd_text(sel ? cd_s("danger-s") : cd_s("")); }
EX(k_helper) void k_helper(void) { label("via-helper"); }
EX(k_cdstr) void k_cdstr(void) {
  cd_str k = cd_s("kept");
  char b[8] = "tmp-str";
  cd_text(k);
  cd_text(cd_tmp(b, 3));
  cd_text(cd_tmpz(b));
  cd_attr("class", b);
}
EX(k_twice) void k_twice(void) { cd_attr("data-x", "data-x"); }
