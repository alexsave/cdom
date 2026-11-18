/* A private copy of the strings macro block of docs/strings-design.md ("The API as it appears in cdom.h"),
 * used only by tests/lits until cdom.h has it. Three imports stand in for the cdom functions that take a
 * string: the tests see which strings reach them. */
#ifndef LITS_H
#define LITS_H
#include <stdint.h>

typedef struct { uint32_t v; } cd_str;
cd_str cd_tmp(const char *s, uint32_t len);
cd_str cd_tmpz(const char *s);

#define CD_SPELL_(s) #s
#define CD_SPELL(s) CD_SPELL_(s)
#define CD_CHARP(s) _Generic((s), cd_str: (const char *)0, default: (s))
#define CD_ASSTR(s) _Generic((s), cd_str: (s), default: (cd_str){0})
#define CD_LITQ(s) (__builtin_strncmp(CD_SPELL(s), "\"", 1) == 0 && \
                    __builtin_constant_p(__builtin_strlen(CD_CHARP(s))))
#ifdef CD_LITS_CHECK /* the check build: each slot also holds the compiler's own bytes (cdlits --check) */
typedef struct { const char *spell, *real; } cd_lit_slot;
#define CD_REAL(s) , (__builtin_constant_p(__builtin_strlen(CD_CHARP(s))) ? CD_CHARP(s) : 0)
#else
typedef struct { const char *spell; } cd_lit_slot;
#define CD_REAL(s)
#endif
#define CD_LIT_(s) ({ \
  static const char cd_c_[] __attribute__((section("cd_chars"), aligned(1))) = CD_SPELL(s); \
  static const cd_lit_slot cd_l_ __attribute__((section("cd_lits"))) = { cd_c_ CD_REAL(s) }; \
  (cd_str){(uint32_t)(uintptr_t)&cd_l_}; })
#define cd_s(s) _Generic((s), cd_str: CD_ASSTR(s), \
  default: __builtin_choose_expr(CD_LITQ(s), CD_LIT_(s), cd_tmpz(CD_CHARP(s))))

#define IMPORT(n) __attribute__((import_module("env"), import_name(n)))
IMPORT("open") void cd_open(cd_str tag);
IMPORT("text") void cd_text(cd_str s);
IMPORT("attr") void cd_attr(cd_str name, cd_str value);
#define cd_open(tag) (cd_open)(cd_s(tag))
#define cd_text(s) (cd_text)(cd_s(s))
#define cd_attr(name, value) (cd_attr)(cd_s(name), cd_s(value))

#define EX(n) __attribute__((export_name(#n)))
#endif
