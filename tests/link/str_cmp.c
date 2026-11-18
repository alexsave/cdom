/* Two strings are not compared by id: must not compile. */
#include "../../src/cdom.h"
int f(cd_str a, cd_str b) { return a != b; }
