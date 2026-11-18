/* A node id is not a string: must not compile. */
#include "../../src/cdom.h"
void f(cd_id id) { cd_text(id); }
