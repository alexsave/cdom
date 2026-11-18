#pragma once
#include "cd_state.h"
#define NAME_CAP 256
extern cd_state name_state;                  /* the views that show the name watch this */
const char *name_get(uint32_t *len);
void name_set(const char *p, uint32_t len);  /* stores it, then cd_changed(&name_state) */
