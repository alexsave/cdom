/* What tests/lits sources include: cdom.h, whose string macros these tests exercise, and EX for exports.
 * stub.c defines the three cdom functions the tests call (cd_open, cd_text, cd_attr) as imports the Node
 * run reads, so the tests link without the rest of cdom. */
#ifndef LITS_STUB_H
#define LITS_STUB_H
#include "cdom.h"
#define EX(n) __attribute__((export_name(#n)))
#endif
