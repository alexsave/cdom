/* The benchmark's model, plain C and the same in every entry: the rows, the shared currency and
 * selection, 100 scalar fields, a draft URL and a big text. Nothing here knows who shows what: that is
 * each entry's (entry_*.c), through the setters app.h declares. */
#pragma once
#include <stdint.h>
#include "cdom.h"

#define N_MAX 100000
#define NF 100
#define LEAVES 1000
#define DRAFT_CAP 256
#define DRAFT_WRAP 200
#define BIG_CAP (2u << 20)

typedef struct { uint32_t id, price_cents, rev; } row_t;
extern row_t rows[N_MAX];     /* rows[i] is the list's item i: keys[i] == rows[i].id */
extern uint32_t keys[N_MAX];
extern uint32_t nrows;
extern uint32_t currency;     /* index into the prefixes */
extern uint32_t selected;     /* a row id, 0 none */
extern uint32_t f[NF];
extern char draft[DRAFT_CAP];
extern uint32_t draft_len;
extern char big[BIG_CAP];
extern uint32_t big_len;

/* The text each view shows, formatted into dst (cd_buf); each returns the bytes. */
uint32_t price_text(char *dst, uint32_t cap, const row_t *r); /* prefix + units + "." + two digits */
uint32_t leaf_text(char *dst, uint32_t cap, uint32_t k);      /* "f" + k + "=" + f[k] */
uint32_t draft_text(char *dst, uint32_t cap);                 /* the draft's length */
/* A row's class: a pointer into read-only data, so one address per value (cdom.h, strings). */
extern const char *const sel_cls[2];
