/* An app file that includes the test module's private header: it must not compile, since a part's -I is
 * the part's alone. */
#include "parts_private.h"
int parts_app_sees = PARTS_PRIVATE;
