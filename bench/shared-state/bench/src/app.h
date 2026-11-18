/* What the shared code (main.c, ops.c) and each entry (entry_*.c, one per build: entry.c picks it by
 * ENTRY_A, ENTRY_B, ENTRY_C, ENTRY_CL, ENTRY_E or ENTRY_L) give each other. */
#pragma once
#include "cdom_ui.h"
#include "model.h"

/* ---- main.c: the DOM every entry builds, the same in all ---- */
extern cd_list list;          /* the rows: the tbody's only content (owns_parent) */
extern cd_region table_region;
extern cd_id textarea;
void table_show(void);        /* builds the table region (it calls table_built inside its build) */
/* One row, <tr class=sel_cls[sel]><td>id</td><td>price</td></tr>, built where cd_at points (a list's
 * create). with_text 0 leaves the price text empty, for an entry whose view draws it at once. */
cd_id row_build(uint32_t i, cd_id *txt, int with_text);
/* Writes rows[i]'s price into text node txt (cd_set_text_node with cd_tmp). */
void row_write_text(cd_id txt, uint32_t i);

/* ---- each entry: the setters (the model changes, then the entry tells whoever shows it) ---- */
void set_currency(uint32_t c);
void set_selected(uint32_t id);
void set_price(uint32_t i, uint32_t cents); /* rows[i]: the price, and rows[i].rev moves on */
void set_field(uint32_t k, uint32_t v);
void draft_append(char c);                   /* wraps at DRAFT_WRAP */
void set_big(uint32_t len);                  /* big[0, len) holds the new text */
void rows_changed(void);                     /* rows[], keys[] and nrows were replaced (added, removed) */
/* ---- each entry: its views, made while the shared builders build ---- */
void rows_reserve(uint32_t cap);             /* room for the per-row structs of keys 1..cap */
void table_built(void);                      /* inside the table region's build, list.parent set */
void leaf_built(uint32_t leaf, uint32_t k, cd_id txt); /* inside leaf region leaf's build, showing f[k] */
void draft_built(cd_id txt);                 /* inside the draft span's build */
void mirror_built(cd_id txt);                /* inside the mirror's build */
uint32_t row_struct_size(void);              /* sizeof the entry's per-row struct(s) */
extern const cd_list_fns row_fns;            /* the list's create, patch and destroy */
