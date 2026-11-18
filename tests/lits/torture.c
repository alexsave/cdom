/* Every way a string literal can be spelled that the lexer must read as clang does. Built with
 * -DCD_LITS_CHECK, each slot also holds the compiler's own bytes and `cdlits --check` compares them. */
#include "stub.h"

#define GREETING "hi " "there"
#define PASS(x) x

EX(k_torture) void k_torture(void) {
  cd_text("");
  cd_text("plain");
  cd_text("a" "b");
  cd_text("a"   "b"  "c");
  cd_text(GREETING);
  cd_text(PASS("through a macro"));
  cd_text("\a\b\f\n\r\t\v\\\'\"\?");        /* every simple escape */
  cd_text("\t");                              /* a tab is 9 */
  cd_text("\e");                              /* clang's extension, 27 */
  cd_text("\?");
  cd_text("\x41");                            /* hex */
  cd_text("\x41g");                           /* g is not a hex digit */
  cd_text("\x041");                           /* more than two digits are one escape */
  cd_text("\x0041");
  cd_text("\x4a" "b");                        /* a literal break ends the escape */
  cd_text("\x1" "2");
  cd_text("\102");                            /* octal */
  cd_text("\1\12\123");
  cd_text("\1012");                           /* three octal digits, then a 2 */
  cd_text("\0127");
  cd_text("\u00e9");                          /* universal character names, as UTF-8 */
  cd_text("\u2603");
  cd_text("\U0001F600");
  cd_text("\u2028\u2029");
  cd_text("\xe2\x98\x83");                    /* UTF-8 spelled as bytes */
  cd_text("h\xc3\xa9llo");
  cd_text("caf\u00e9 \U0001F600 end");
  cd_text("\u2212 minus");                    /* an escaped minus sign, and the raw one: */
  cd_text("\xe2\x88\x92");
  cd_text("− minus, héllo, 日本語, 😀");     /* raw UTF-8 in the source */
  cd_text(u8"u8 prefix");
  cd_text("a" u8"b");
  cd_text("/* not a comment */ // nor this");
  cd_text("a\\");
  cd_text("\\n is not a newline");
  cd_text("</script><!--");
  cd_text("tab\there \"quoted\" back\\slash");
  cd_text("long long long long long long long long long long long long long long long long long long long long "
          "long long long long long long long long long long long long long long long long long long long long "
          "long long long long long long long long long long long long long long long long long long long long");
  cd_text("\x7f\x01\x1f");
  cd_text("%d %s");
  cd_text("0123456789");
}
