/* The smallest app, built by cdom.mk's APP_WASM rule from tests/link/app.mk: it includes cdom.h by name
 * and string.h from the program's libc (CDOM_CFLAGS), and links with CDOM_SRC and the one module it names
 * in APP_MODULES (random). */
#include <string.h>
#include "cdom.h"
#include "cdom_random.h"

static char note[8];
void cd_main(void) {
  memcpy(note, "hello", 6);
  if (!cd_random_bytes(note + 6, 1)) note[0] = 'H';
  cd_at(CD_MOUNT, 0);
  cd_open(cd_internz("p"));
  cd_text(cd_intern(note, (uint32_t)strlen(note)));
  cd_name_text(cd_id_new());
  cd_close();
}
