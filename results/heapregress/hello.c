#include "cdom.h"
/* heap-regress: the hello world of docs/file-read-design.md, one div of text. */
void cd_main(void) { cd_open("div"); cd_text("hello, world"); cd_close(); }
