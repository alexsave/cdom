#include <string.h>
#include "name.h"

static char name[NAME_CAP];
static uint32_t name_len;
cd_state name_state;

const char *name_get(uint32_t *len) { cd_read(&name_state); *len = name_len; return name; }

void name_set(const char *p, uint32_t len) {
  memcpy(name, p, len);
  name_len = len;
  cd_changed(&name_state);
}
