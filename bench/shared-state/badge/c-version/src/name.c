#include <string.h>
#include "name.h"
#include "views.h"

static char name[NAME_CAP];
static uint32_t name_len, version;

const char *name_get(uint32_t *len) { *len = name_len; return name; }
uint32_t name_version(void) { return version; }

void name_set(const char *p, uint32_t len) {
  memcpy(name, p, len);
  name_len = len;
  version++;
  views_changed();
}
