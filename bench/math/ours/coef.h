/* The coefficients of our own functions (bench/math/ours/), written by bench/math/tools/remez.py with sollya's
 * fpminimax: do not edit by hand. Each array is the polynomial's coefficients from the lowest monomial up, the
 * fixed leading ones included; "error" is the fit's relative error over its interval (sollya's dirtyinfnorm). */
#ifndef BENCH_MATH_OURS_COEF_H
#define BENCH_MATH_OURS_COEF_H
/* sin(x) = x + x^3 P(x^2): monomials 3..13 over [2^-30; pi/4 + 2^-40] (the function is odd or even, or the interval two-sided), error 2^-57.9 */
static const double SIN[6] = { -0x1.5555555555548p-3, 0x1.111111110f76p-7, -0x1.a01a019bf2b81p-13, 0x1.71de355fa53bdp-19, -0x1.ae5e5500c132bp-26, 0x1.5d8e62828d8b7p-33 };
/* cos(x) = 1 - x^2/2 + x^4 P(x^2): monomials 4..14 over [2^-30; pi/4 + 2^-40] (the function is odd or even, or the interval two-sided), error 2^-63.9 */
static const double COS[6] = { 0x1.555555555554bp-5, -0x1.6c16c16c15018p-10, 0x1.a01a019c8f945p-16, -0x1.27e4f7f1ee922p-22, 0x1.1ee9dc12c88acp-29, -0x1.8fa6a8a7d84dfp-37 };
/* tan(x) = x + x^3 P(x^2), |x| <= 0.6745: monomials 3..27 over [2^-30; 0.6745] (the function is odd or even, or the interval two-sided), error 2^-59.3 */
static const double TAN[13] = { 0x1.5555555555563p-2, 0x1.111111110fec8p-3, 0x1.ba1ba1bb2e134p-5, 0x1.664f4841ebdf4p-6, 0x1.226e3e63d8ba6p-7, 0x1.d6d234ef2247p-9, 0x1.7dbc1dde97ae5p-10, 0x1.3451a7d71a4e2p-11, 0x1.0255e0e82c2f2p-12, 0x1.48be4374ac792p-14, 0x1.2a627a805cfc9p-14, -0x1.33e23a1f4cd8bp-16, 0x1.b175eb1b55806p-16 };
/* atan(x) = x + x^3 P(x^2), |x| <= 7/16: monomials 3..23 over [2^-30; 7/16] (the function is odd or even, or the interval two-sided), error 2^-57.7 */
static const double ATAN[11] = { -0x1.555555555550bp-2, 0x1.999999998e7edp-3, -0x1.2492491ff3801p-3, 0x1.c71c6fdb34d7cp-4, -0x1.745cdba72b178p-4, 0x1.3b0f2086d051dp-4, -0x1.10d603345cdc1p-4, 0x1.ddddc26311df8p-5, -0x1.97a0d5e32e269p-5, 0x1.2b185f2460d95p-5, -0x1.0a7ffbf3c3232p-6 };
/* asin(x) = x + x^3 P(x^2), |x| <= 1/2: monomials 3..31 over [2^-30; 1/2] (the function is odd or even, or the interval two-sided), error 2^-65.5 */
static const double ASIN[15] = { 0x1.5555555555556p-3, 0x1.3333333333197p-4, 0x1.6db6db6dcea8ap-5, 0x1.f1c71c66152c5p-6, 0x1.6e8ba4abd664p-6, 0x1.1c4e97d11e013p-6, 0x1.c99fe11e0b69ep-7, 0x1.7a3750c5373f1p-7, 0x1.42c8fae08559dp-7, 0x1.fe158bf75dc2cp-8, 0x1.527b5586774abp-7, -0x1.17ff51ada2286p-8, 0x1.0538e7f209ccbp-5, -0x1.1ff372036cb3fp-5, 0x1.2c91820fc1be3p-5 };
/* exp(x) = 1 + x + x^2 P(x), |x| <= ln2/2: monomials 2..11 over [-0.3466; 0.3466] (the function is odd or even, or the interval two-sided), error 2^-57.9 */
static const double EXP[10] = { 0x1.000000000000ap-1, 0x1.55555555554fep-3, 0x1.55555555506bcp-5, 0x1.1111111126b36p-7, 0x1.6c16c18477083p-10, 0x1.a01a012fee984p-13, 0x1.a019995efb08bp-16, 0x1.71df18ef7c895p-19, 0x1.28adfccb1629ep-22, 0x1.ad92e88b53545p-26 };
/* 2^x = 1 + x P(x), |x| <= 1/2 + 2^-20 (pow takes it too): monomials 1..14 over [-0.5 - 2^-20; 0.5 + 2^-20] (the function is odd or even, or the interval two-sided), error 2^-59.5 */
static const double EXP2[14] = { 0x1.62e42fefa39efp-1, 0x1.ebfbdff82c58dp-3, 0x1.c6b08d704a1bep-5, 0x1.3b2ab6fba55b9p-7, 0x1.5d87fe787371ap-10, 0x1.430912f5a7e8fp-13, 0x1.ffcbfd86d2424p-17, 0x1.62c03218549eap-20, 0x1.b521b18812e32p-24, 0x1.e4724310070f9p-28, 0x1.f3474f4f8bc74p-32, 0x1.690bc4610f30fp-35, -0x1.5412aa72ea053p-37, -0x1.33501e37422f8p-36 };
/* expm1(x) = x + x^2/2 + x^3 P(x), |x| <= ln2/2: monomials 3..12 over [-0.3466; 0.3466] (the function is odd or even, or the interval two-sided), error 2^-61.5 */
static const double EXPM1[10] = { 0x1.5555555555559p-3, 0x1.555555555554ap-5, 0x1.111111110f656p-7, 0x1.6c16c16c1c0d2p-10, 0x1.a01a01b0024c7p-13, 0x1.a01a01872c07ep-16, 0x1.71ddf8146898dp-19, 0x1.27e521cd624fdp-22, 0x1.af5f203890d7ep-26, 0x1.1f0418f2158fcp-29 };
/* log((1+s)/(1-s)) = 2s + s^3 P(s^2), |s| <= 3 - 2 sqrt2: monomials 3..15 over [2^-30; 0.1716] (the function is odd or even, or the interval two-sided), error 2^-59.5 */
static const double LOG[7] = { 0x1.5555555555592p-1, 0x1.999999997ff2bp-2, 0x1.24924941e082ap-2, 0x1.c71c52148103dp-3, 0x1.74663d045e55p-3, 0x1.39a1e2da1a237p-3, 0x1.2f041b9804a66p-3 };
/* cbrt's first step: x^(-1/3) near 1, over the ratios its first guess leaves: monomials 0..4 over [0.86; 1.16] (the function is odd or even, or the interval two-sided), error 2^-20.7 */
static const double CBRT[5] = { 0x1.dfa20f5dbea1ep0, -0x1.df9c5ec5c06ddp0, 0x1.99ed0e1cb8ae1p0, -0x1.7c725c32cfda6p-1, 0x1.223367ce5c948p-3 };
/* pow's log: log((1+s)/(1-s)) = 2s + 2s^3/3 + s^5 P(s^2): monomials 5..25 over [2^-30; 0.1716] (the function is odd or even, or the interval two-sided), error 2^-77.4 */
static const double POWLOG[11] = { 0x1.999999999999ap-2, 0x1.2492492492324p-2, 0x1.c71c71c75f2aep-3, 0x1.745d1710a74fbp-3, 0x1.3b13cb3cf138bp-3, 0x1.1108d1c72b59ap-3, 0x1.e5581e166bb85p-4, 0x1.34d07e3205d64p-4, 0x1.86bfa766ea429p-1, -0x1.0f806248101f2p3, 0x1.8179e4f7d5706p5 };
/* The constants (sollya, 400 bits, then rounded as each says). */
#define INVPIO2 0x1.45f306dc9c883p-1
#define PIO2_1 0x1.921fb544p0
#define PIO2_2 0x1.0b4611a6p-34
#define PIO2_3 0x1.3198a2ep-69
#define PIO2_4 0x1.b839a252049c1p-104
#define PIO2_HI 0x1.921fb54442d18p0
#define PIO2_LO 0x1.1a62633145c07p-54
#define PIO4_HI 0x1.921fb54442d18p-1
#define PIO4_LO 0x1.1a62633145c07p-55
#define PI_HI 0x1.921fb54442d18p1
#define PI_LO 0x1.1a62633145c07p-53
#define LN2_HI 0x1.62e42fefa38p-1
#define LN2_LO 0x1.ef35793c7673p-45
#define LN2D_HI 0x1.62e42fefa39efp-1
#define LN2D_LO 0x1.abc9e3b39803fp-56
#define INVLN2 0x1.71547652b82fep0
#define IVLN2_HI 0x1.71547652b82fep0
#define IVLN2_LO 0x1.777d0ffda0d24p-56
#define IVLN10_HI 0x1.bcb7b1526e50ep-2
#define IVLN10_LO 0x1.95355baaafad3p-57
#define LOG10_2HI 0x1.34413509f78p-2
#define LOG10_2LO 0x1.fef311f12b358p-46
#define TWO3_HI 0x1.5555555555555p-1
#define TWO3_LO 0x1.5555555555555p-55
#define ATAN_HI0 0x1.dac670561bb4fp-2
#define ATAN_LO0 0x1.a2b7f222f65e2p-56
#define ATAN_HI1 0x1.921fb54442d18p-1
#define ATAN_LO1 0x1.1a62633145c07p-55
#define ATAN_HI2 0x1.f730bd281f69bp-1
#define ATAN_LO2 0x1.007887af0cbbdp-56
#define ATAN_HI3 0x1.921fb54442d18p0
#define ATAN_LO3 0x1.1a62633145c07p-54
#endif
