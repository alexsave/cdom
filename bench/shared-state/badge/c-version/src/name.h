#pragma once
#include <stdint.h>
#define NAME_CAP 256
const char *name_get(uint32_t *len);
uint32_t name_version(void);                  /* changes on every name_set: the row's ETag */
void name_set(const char *p, uint32_t len);   /* stores it, bumps the version, and calls views_changed() */
