/* The table's text: a literal used at three call sites, and the characters that must not break a script
 * or the separator. run.sh runs the table line in Node and compares each string with the one written here. */
#include "stub.h"

EX(k_table) void k_table(void) {
  cd_text("1first-digit");
  cd_text("same");
  cd_text("same");
  cd_text("same");
  cd_text("</script><!-- ok");
  cd_text("quote \" back \\ end");
  cd_text("line\nbreak and \u2028 sep \u2029 ");
  cd_text("");
  cd_text("0 digit after the separator");
  cd_text("tab\t\x03 \x7f del");
  cd_text("\xe2\x98\x83 snowman, \U0001F600 and h\xc3\xa9");
  cd_text("tail\\");
}
