/* A string argument is a literal, a char pointer, a cd_tmp or a chosen cd_s, and CD_NONE is no string:
 * the control for str_id.c, str_int.c and str_cmp.c, with the same flags. */
#include "../../src/cdom.h"
void f(cd_id id, const char *name, int sel, char *buf, uint32_t n) {
  cd_text("literal");
  cd_text(name);
  cd_text(cd_tmp(buf, n));
  cd_set_class(id, sel ? cd_s("danger") : cd_s(""));
  cd_str a = cd_s("x"), b = cd_s("y");
  cd_set_attr(id, a, b);
  (void)cd_get_num(CD_RESULT, CD_NONE);
}
