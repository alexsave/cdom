/* The read-only range cd_tmpz tests is not empty with cdom's toolchain: wasm-ld defines __rodata_start and
 * __rodata_end from LLVM 22. Where it does not, the weak symbols are 0, the range is empty, and every
 * string takes the slow path with no error - so only a test sees it. */
#include "../../src/cdom.h"

extern const char __rodata_start[] __attribute__((weak)), __rodata_end[] __attribute__((weak));
static char mutable_bytes[8] = "mut";
void cd_main(void) {}
__attribute__((export_name("ro_span"))) uint32_t ro_span(void) { return (uint32_t)((uintptr_t)__rodata_end - (uintptr_t)__rodata_start); }
/* A literal the macro cannot see: cd_tmpz gets it, and it must be its own id. */
__attribute__((export_name("ro_id"))) uint32_t ro_id(uint32_t i) { return cd_tmpz(i ? "ro-yes" : "ro-no").v; }
__attribute__((export_name("ro_ptr"))) uint32_t ro_ptr(uint32_t i) { return (uint32_t)(uintptr_t)(i ? "ro-yes" : "ro-no"); }
/* Mutable bytes are not read-only: a runtime string, whose id has the top bit set. */
__attribute__((export_name("mut_id"))) uint32_t mut_id(void) { return cd_tmpz(mutable_bytes).v; }
