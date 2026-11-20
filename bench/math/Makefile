# The math bench (docs/math-design.md): each arm's wasm module, its native twin, and the tools that run them.
#   make            build/<arm>.wasm for every arm (musl msun core ours js1 jsn) and build/native/bench (the native
#                   driver: every arm's native twin and glibc linked in)
#   make accuracy   build/native/accuracy, the accuracy sweep (every native arm against MPFR, which it needs)
#   make sizes      build/size/<arm>-{empty,sin,all}.wasm, each through wasm-opt -O3 (sizes.sh measures them)
# Each arm's wasm module is built as a cdom app's code is: cdom.mk's WASM_CC, CDOM_CFLAGS, CDOM_LDFLAGS and CDOM_OPT
# (-Oz unless set), with compat/ first on the include path (its math.h, fenv.h and the rest stand in for the libc
# the vendored sources expect; cdom's src/libc/ has no math.h). Every arm adds -ffp-contract=off (no fused
# multiply-add the source did not write: wasm has none, and the native twin must compute the same) and
# -fno-math-errno (wasm's default; Linux's clang defaults to errno, which would keep sqrt a call natively).
# The native twin of an arm is the same sources and flags for the host: -nostdinc with compat/ and clang's own
# headers, __FP_FAST_FMA undefined (so musl takes its wasm paths on an arm64 Mac too), and every global symbol of
# the arm renamed to <arm>_<name> (build/native/<arm>.rename.h, from llvm-nm over the arm's wasm objects), so one
# program links every arm. The functions that are one wasm instruction keep their names (RENAME_KEEP): they are
# exact, the compiler inlines them, and it would not if they were renamed.
CDOM := $(abspath ../..)
include $(CDOM)/cdom.mk

OUT := build
ARMS := musl msun core ours
NATIVE_CC ?= $(WASM_CC)
LLVM_NM ?= $(shell $(WASM_CC) -print-prog-name=llvm-nm)
WASM_OPT ?= wasm-opt
RESDIR := $(shell $(NATIVE_CC) -print-resource-dir)

BM_FLAGS := -ffp-contract=off -fno-math-errno
WASM_FLAGS := $(CDOM_OPT) -isystem compat $(CDOM_CFLAGS) $(BM_FLAGS)
WASM_LINK := $(CDOM_OPT) $(CDOM_CFLAGS) $(CDOM_LDFLAGS)
# NATIVE_OPT: the native twins' optimization level, CDOM_OPT unless set (make NATIVE_OPT=-O2 builds them in build/native-O2/).
NATIVE_OPT ?= $(CDOM_OPT)
NATIVE_DIR := $(OUT)/native$(if $(filter-out $(CDOM_OPT),$(NATIVE_OPT)),$(NATIVE_OPT))
NATIVE_FLAGS := $(NATIVE_OPT) -std=c23 -nostdinc -isystem compat -isystem $(RESDIR)/include $(BM_FLAGS) -U__FP_FAST_FMA -U__FP_FAST_FMAF
HDRS := fns.h gen.h $(wildcard compat/*.h compat/*/*.h compat/*/*/*.h)

MUSL_FLAGS := -isystem compat/musl -include compat/musl/features.h -Ithird_party/musl/src/internal -w
musl_SRC := $(wildcard third_party/musl/src/math/*.c)
musl_FLAGS := $(MUSL_FLAGS)
msun_SRC := $(wildcard third_party/msun/src/*.c)
msun_FLAGS := -isystem compat/msun -include compat/msun/msun.h -w
# CORE-MATH names its functions cr_sin and so on; the bench calls sin. It calls fma (musl's fma.c and fmaf.c: wasm
# has no fused multiply-add) and scalbn (musl's), and, in wasm, three 128-bit integer helpers (compat/int128.c).
CR_FNS := sin cos tan asin acos atan atan2 exp exp2 expm1 log log2 log10 log1p pow sinh cosh tanh cbrt hypot
core_SRC := $(wildcard third_party/core-math/src/binary*/*/*.c) $(addprefix third_party/musl/src/math/,fma.c fmaf.c scalbn.c)
core_FLAGS := $(MUSL_FLAGS) $(foreach f,$(CR_FNS),-Dcr_$(f)=$(f) -Dcr_$(f)f=$(f)f)
core_WASM_SRC := compat/int128.c
core_BENCH := -DBM_LACK_fmod
ours_SRC := $(wildcard ours/*.c)
ours_FLAGS := -Wall -Wextra -Werror -Iours
RENAME_KEEP := sqrt sqrtf fabs fabsf floor floorf ceil ceilf trunc truncf rint rintf nearbyint copysign copysignf fmax fmin fmaxf fminf round roundf

all: $(addprefix $(OUT)/,$(addsuffix .wasm,$(ARMS) js1 jsn)) $(NATIVE_DIR)/bench
accuracy: $(NATIVE_DIR)/accuracy

# $(call arm,name): the rules of one arm.
define arm
$(1)_WOBJ := $$(patsubst %.c,$(OUT)/wasm/$(1)/%.o,$$($(1)_SRC) $$($(1)_WASM_SRC) compat/compat.c)
$(1)_NOBJ := $$(patsubst %.c,$(NATIVE_DIR)/$(1)/%.o,$$($(1)_SRC) compat/compat.c)
$(OUT)/wasm/$(1)/%.o: %.c $(HDRS)
	@mkdir -p $$(@D)
	$(WASM_CC) $(WASM_FLAGS) $$($(1)_FLAGS) -c -o $$@ $$<
$(OUT)/wasm/$(1)/bench.o $(OUT)/wasm/$(1)/special.o: $(OUT)/wasm/$(1)/%.o: %.c $(HDRS)
	@mkdir -p $$(@D)
	$(WASM_CC) $(WASM_FLAGS) -Wall -Wextra -Werror $$($(1)_BENCH) -DSP_ARM_$(1) -c -o $$@ $$<
$(OUT)/$(1).wasm: $$($(1)_WOBJ) $(OUT)/wasm/$(1)/bench.o $(OUT)/wasm/$(1)/special.o
	$(WASM_CC) $(WASM_LINK) -o $$@ $$^
# The native names: every global the arm's objects define, but the instructions, as <arm>_<name>.
$(NATIVE_DIR)/$(1).rename.h: $$(filter-out %/int128.o,$$($(1)_WOBJ))
	@mkdir -p $$(@D)
	$(LLVM_NM) -g --defined-only $$^ | awk 'NF == 3 { print $$$$3 }' | sort -u | grep -vxF $(foreach k,$(RENAME_KEEP),-e $(k)) \
	  | awk '{ print "#define " $$$$1 " $(1)_" $$$$1 }' > $$@.tmp && mv $$@.tmp $$@
$(NATIVE_DIR)/$(1)/%.o: %.c $(HDRS) $(NATIVE_DIR)/$(1).rename.h
	@mkdir -p $$(@D)
	$(NATIVE_CC) $(NATIVE_FLAGS) $$($(1)_FLAGS) -include $(NATIVE_DIR)/$(1).rename.h -c -o $$@ $$<
$(NATIVE_DIR)/$(1)/bench.o $(NATIVE_DIR)/$(1)/special.o: $(NATIVE_DIR)/$(1)/%.o: %.c $(HDRS)
	@mkdir -p $$(@D)
	$(NATIVE_CC) $(NATIVE_FLAGS) -Wall -Wextra -Werror $$($(1)_BENCH) -DBM_ARM=$(1) -DSP_ARM_$(1) -c -o $$@ $$<
endef
$(foreach a,$(ARMS),$(eval $(call arm,$(a))))

# The browser's arms: bench.c alone, every function an import (BM_JS=1 one call per value, 2 one call per pass).
JS_OBJ := $(foreach a,js1 jsn,$(OUT)/wasm/$(a)/bench.o $(OUT)/wasm/$(a)/special.o)
$(JS_OBJ): $(HDRS) bench.c special.c
	@mkdir -p $(@D)
	$(WASM_CC) $(WASM_FLAGS) -Wall -Wextra -Werror -DBM_JS=$(if $(findstring /js1/,$@),1,2) -DSP_ARM_js -c -o $@ $(notdir $(basename $@)).c
$(OUT)/js1.wasm $(OUT)/jsn.wasm: $(OUT)/%.wasm: $(OUT)/wasm/%/bench.o $(OUT)/wasm/%/special.o
	$(WASM_CC) $(WASM_LINK) -o $@ $^

# glibc's libm, natively only: bench.c's calls go to libm.
$(NATIVE_DIR)/glibc/bench.o $(NATIVE_DIR)/glibc/special.o: $(NATIVE_DIR)/glibc/%.o: %.c $(HDRS)
	@mkdir -p $(@D)
	$(NATIVE_CC) $(NATIVE_FLAGS) -Wall -Wextra -Werror -DBM_ARM=glibc -DBM_HOST_LIBM -DSP_ARM_glibc -c -o $@ $<

NATIVE_ARM_OBJ = $(foreach a,$(ARMS),$($(a)_NOBJ) $(NATIVE_DIR)/$(a)/bench.o $(NATIVE_DIR)/$(a)/special.o) \
  $(NATIVE_DIR)/glibc/bench.o $(NATIVE_DIR)/glibc/special.o
# The native driver and the accuracy sweep use the host's own headers and libc (and MPFR: brew install mpfr, or
# libmpfr-dev). NATIVE_LIBS is for a Mac's Homebrew paths.
# msun's k_exp.c holds __ldexp_cexp (for cexp, which no arm uses) beside the __ldexp_exp cosh and sinh need, and it
# calls sincos: glibc has one, macOS's libm does not, so on the Mac the native links drop unused code (as wasm-ld does).
comma := ,
NATIVE_LINK := $(if $(filter Darwin,$(shell uname -s)),-Wl$(comma)-dead_strip)
MPFR_FLAGS ?= $(if $(wildcard /opt/homebrew/include/mpfr.h),-I/opt/homebrew/include -L/opt/homebrew/lib)
$(NATIVE_DIR)/bench: native.c arms.h fns.h gen.h $(NATIVE_ARM_OBJ)
	$(NATIVE_CC) -O2 -std=c2x -Wall -Wextra -Werror $(NATIVE_LINK) -o $@ native.c $(NATIVE_ARM_OBJ) -lm
$(NATIVE_DIR)/accuracy: accuracy.c arms.h fns.h gen.h $(NATIVE_ARM_OBJ)
	$(NATIVE_CC) -O2 -std=c2x -Wall -Wextra -Werror $(MPFR_FLAGS) $(NATIVE_LINK) -o $@ accuracy.c $(NATIVE_ARM_OBJ) -lmpfr -lgmp -lm
$(NATIVE_ARM_OBJ): | $(foreach a,$(ARMS),$(NATIVE_DIR)/$(a).rename.h)

# The sizes: what one function (sin), each function alone (f-<name>) and every function add to an empty module,
# after wasm-opt -O3.
SIZE_FNS := sin cos tan asin acos atan atan2 exp exp2 expm1 log log2 log10 log1p pow sinh cosh tanh cbrt hypot fmod
SIZE_KINDS := empty sin all $(addprefix f-,$(SIZE_FNS))
size_flags = $(if $(filter f-%,$(1)),-DSIZE_ONE -DSIZE_NAME=$(patsubst f-%,%,$(1)) -DSIZE_ARITY=$(if $(filter atan2 pow hypot fmod,$(patsubst f-%,%,$(1))),2,1),-DSIZE_$(1))
define size
$(OUT)/size/$(1)-%.wasm: size.c $(HDRS) $$(filter-out %/compat.o,$$($(1)_WOBJ)) $(OUT)/wasm/$(1)/compat/compat.o
	@mkdir -p $$(@D)
	$(WASM_CC) $(WASM_LINK) -isystem compat $(BM_FLAGS) -Wall -Wextra -Werror $$($(1)_BENCH) $$(call size_flags,$$*) -o $$@.linked size.c $$(filter %.o,$$^)
	$(WASM_OPT) -O3 $$@.linked -o $$@ && rm -f $$@.linked
endef
$(foreach a,$(ARMS),$(eval $(call size,$(a))))
$(OUT)/size/js1-%.wasm: size.c $(HDRS)
	@mkdir -p $(@D)
	$(WASM_CC) $(WASM_LINK) -isystem compat $(BM_FLAGS) -Wall -Wextra -Werror -DBM_JS=1 -DSIZE_$* -o $@.linked size.c
	$(WASM_OPT) -O3 $@.linked -o $@ && rm -f $@.linked
sizes: $(filter-out $(OUT)/size/core-f-fmod.wasm,$(foreach a,$(ARMS),$(foreach k,$(SIZE_KINDS),$(OUT)/size/$(a)-$(k).wasm))) $(foreach k,empty sin all,$(OUT)/size/js1-$(k).wasm)

print-cc:
	@echo $(WASM_CC)
clean:
	rm -rf $(OUT)
.PHONY: all accuracy sizes print-cc clean
.SECONDARY:
