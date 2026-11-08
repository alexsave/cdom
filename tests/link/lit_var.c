/* CD_LIT given a variable: must not compile. */
#include "../../src/cdom.h"
cd_str f(const char *name) { return CD_LIT(name); }
