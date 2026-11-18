#pragma once
#include <stdint.h>
#define NAME_CAP 256
extern char name[NAME_CAP];  /* plain globals: anyone may read them, and a writer calls views_changed() */
extern uint32_t name_len;
