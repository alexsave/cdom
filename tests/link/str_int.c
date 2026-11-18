/* A number is not a string: must not compile. */
#include "../../src/cdom.h"
void f(void) { cd_text(5); }
