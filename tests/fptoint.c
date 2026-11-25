/* Float to integer conversions, run by tools/fptoint.mjs on every tier. clang emits the non-trapping
 * i32.trunc_sat_f64_s and its kin for these casts (nontrapping-fptoint is on in its default wasm32 features), which
 * wasm2js's i64 lowering mishandled: the legacy build (tools/legacy/build.mjs) must take them without a flag of the
 * app's. A 64-bit result goes out as two words, as wasm2js's exports cannot return an i64 to a page with no BigInt. */
#include "../src/cdom.h"
#define EXPORT(n) __attribute__((export_name(n)))

void cd_main(void) { }
EXPORT("u64_lo") uint32_t u64_lo(double v) { return (uint32_t)(uint64_t)v; }
EXPORT("u64_hi") uint32_t u64_hi(double v) { return (uint32_t)((uint64_t)v >> 32); }
EXPORT("i64_lo") uint32_t i64_lo(double v) { return (uint32_t)(int64_t)v; }
EXPORT("i64_hi") uint32_t i64_hi(double v) { return (uint32_t)((uint64_t)(int64_t)v >> 32); }
EXPORT("u32") uint32_t u32(double v) { return (uint32_t)v; }
EXPORT("i32") int32_t i32(double v) { return (int32_t)v; }
/* The conversion that stopped a real app's build: a fixed-point round of a double. */
EXPORT("scaled_lo") uint32_t scaled_lo(double v, uint32_t scale) {
    uint64_t q = (uint64_t)(v * (double)scale + 0.5);
    return (uint32_t)q;
}
EXPORT("scaled_hi") uint32_t scaled_hi(double v, uint32_t scale) {
    uint64_t q = (uint64_t)(v * (double)scale + 0.5);
    return (uint32_t)(q >> 32);
}
/* The same from a float, which wasm2js lowers on its own path. */
EXPORT("f32_u64_lo") uint32_t f32_u64_lo(float v) { return (uint32_t)(uint64_t)v; }
EXPORT("f32_i32") int32_t f32_i32(float v) { return (int32_t)v; }
