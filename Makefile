CC        := gcc
# make iwasm and make test: a riscv32 Linux GCC for the soft-float ABI (-mabi=ilp32), the
# one of wasmc's code and of WAMR's RISCV32_ILP32 target; rv32-cc-check
# rejects another (an ilp32d toolchain, the usual default, would not do).
# Set it on the command line, in the environment or in config.mk.
RV32_CC   ?= riscv32-unknown-linux-gnu-gcc
BUILD_DIR := build

# Local settings, not in the repository, e.g. RV32_CC := /path/to/gcc: after
# the defaults above, which they override, before what derives from them.
-include config.mk

WARNINGS := \
	-Wall \
	-Wextra \
	-Wshadow \
	-Wconversion \
	-Wcast-qual \
	-Wstrict-prototypes \
	-Wmissing-prototypes \
	-Wformat=2 \
	-Wnull-dereference \
	-Wduplicated-cond \
	-Wlogical-op \
	-Wdouble-promotion \
	-Wundef \
	-Wwrite-strings \
	-Wredundant-decls \
	-Wmissing-declarations \
	-Wnested-externs \
	-Wold-style-definition

CFLAGS  := -std=gnu17 $(WARNINGS) -g -O1 -MMD -MP
# host builds only: --dump-ir and --stats print the IR and the per-function
# sizes (the RV32 build leaves them out, with their stdio)
HOST_FLAGS := -DIR_ENABLE_DUMP

SRC_DIR   := src
INC_DIR   := include
RV32_BUILD_DIR := $(BUILD_DIR)/rv32
WAMR_DIR := wasm-micro-runtime
# WAMR's AOT runtime for run_tests, built with RV32_CC like the rest
RV32_WAMR_BUILD_DIR := $(RV32_BUILD_DIR)/wamr
# the RV32_CC the riscv32 builds were made with, rewritten when it changes
RV32_CC_STAMP := $(RV32_BUILD_DIR)/rv32_cc
# WAMR's iwasm for riscv32 Linux, to run AOT files under qemu-riscv32
IWASM_BUILD_DIR := $(BUILD_DIR)/iwasm-rv32
TEST_BUILD_DIR := $(BUILD_DIR)/tests
ASAN_BUILD_DIR := $(BUILD_DIR)/asan
ASAN_FLAGS := -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
COV_BUILD_DIR := $(BUILD_DIR)/cov
COV_FLAGS := --coverage -O0
ANALYZE_BUILD_DIR := $(BUILD_DIR)/analyze
ANALYZE_FLAGS := -fanalyzer -Werror

SRCS := $(wildcard $(SRC_DIR)/*.c)
# real programs (the ESP32-C3 benchmark suites), compiled by test-asan and
# coverage next to the spec-test modules
BENCH_MODULES := $(sort $(wildcard bench/esp32c3-idf/suites/modules/*/*.wasm))
INCS := $(wildcard $(SRC_DIR)/*.h) $(wildcard $(INC_DIR)/wasmc/*.h)

# The public headers are in $(INC_DIR)/wasmc/, the private ones in $(SRC_DIR)/:
# the library sees both, the programs that use it only the public ones (a
# private header included by one of them does not compile).
LIB_INCLUDES := -I$(INC_DIR) -I$(SRC_DIR)
APP_INCLUDES := -I$(INC_DIR)

RV32_OBJS := $(patsubst $(SRC_DIR)/%.c, $(RV32_BUILD_DIR)/%.o, $(SRCS))

FORMAT_SRCS := $(SRCS) $(INCS) main.c readaot.c run_tests.c
TIDY_SRCS   := $(SRCS) main.c readaot.c

all: $(BUILD_DIR)/wasmc $(BUILD_DIR)/readaot

help:
	@echo "Targets:"
	@echo "  all (default)  build the host tools: $(BUILD_DIR)/wasmc and $(BUILD_DIR)/readaot"
	@echo "  test           run the spec tests (tests/*.wast) on riscv32: wast2json,"
	@echo "                 then $(RV32_BUILD_DIR)/run_tests under qemu-riscv32;"
	@echo "                 builds WAMR with RV32_CC in $(RV32_WAMR_BUILD_DIR) first;"
	@echo "                 the riscv32 builds are redone when RV32_CC changes"
	@echo "  iwasm          build WAMR's iwasm for riscv32 Linux with RV32_CC, to run"
	@echo "                 AOT files: qemu-riscv32 $(IWASM_BUILD_DIR)/iwasm file.aot"
	@echo "  rv32-cc-check  check that RV32_CC exists and targets the soft-float"
	@echo "                 ABI (-mabi=ilp32); run before every riscv32 build"
	@echo "  test-asan      compile all the spec-test and benchmark modules with an"
	@echo "                 ASan + UBSan host wasmc ($(ASAN_BUILD_DIR)); fails on a"
	@echo "                 sanitizer report, a crash or a benchmark that does not compile"
	@echo "  coverage       line/function coverage of $(SRC_DIR)/ over all the spec-test"
	@echo "                 and benchmark modules (gcov + lcov); HTML in"
	@echo "                 $(COV_BUILD_DIR)/html/index.html"
	@echo "  format         format the C sources in place with clang-format"
	@echo "  format-check   check the formatting without changing files"
	@echo "  infer          static analysis with Infer: clean, then capture a full"
	@echo "                 build ($(MAKE) all); report in infer-out/report.txt"
	@echo "  tidy           clang-tidy on the host sources (checks in .clang-tidy)"
	@echo "  cppcheck       cppcheck (all checks) on the host sources; fails on a finding"
	@echo "  analyze        GCC -fanalyzer on the host sources; fails on a finding"
	@echo "  clean          remove $(BUILD_DIR)/, WAMR's build included"
	@echo "  help           show this help"
	@echo ""
	@echo "Variables (on the command line, e.g. make test RV32_CC=..., or in a"
	@echo "config.mk next to the Makefile, which is not in the repository):"
	@echo "  RV32_CC        riscv32 Linux GCC for the soft-float ABI, for make iwasm and test"
	@echo "                 (now $(RV32_CC))"
	@echo "  CC             host C compiler (now $(CC))"
	@echo "  BUILD_DIR      where the builds go (now $(BUILD_DIR))"

# Host wasmc: src/*.c + main.c into $(1)/wasmc, with the extra flags $(2).
# Used for the normal, the ASan + UBSan and the gcov build.
define host_wasmc
$(1)/wasmc: $(patsubst $(SRC_DIR)/%.c, $(1)/%.o, $(SRCS)) $(1)/main.o
	$$(CC) $$(CFLAGS) $$(HOST_FLAGS) $(2) $$^ -o $$@

$(1)/main.o: main.c | $(1)
	$$(CC) $$(CFLAGS) $$(HOST_FLAGS) $(2) $$(APP_INCLUDES) -c $$< -o $$@

$(1)/%.o: $$(SRC_DIR)/%.c | $(1)
	$$(CC) $$(CFLAGS) $$(HOST_FLAGS) $(2) $$(LIB_INCLUDES) -c $$< -o $$@
endef

$(eval $(call host_wasmc,$(BUILD_DIR),))
$(eval $(call host_wasmc,$(ASAN_BUILD_DIR),$(ASAN_FLAGS)))
$(eval $(call host_wasmc,$(COV_BUILD_DIR),$(COV_FLAGS)))

$(BUILD_DIR)/readaot.o: readaot.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/readaot: $(BUILD_DIR)/readaot.o | $(BUILD_DIR)
	$(CC) $(CFLAGS) $< -o $@

$(RV32_BUILD_DIR)/run_tests: $(RV32_BUILD_DIR)/run_tests.o $(RV32_BUILD_DIR)/libwasmc.a $(RV32_WAMR_BUILD_DIR)/libiwasm.a
	$(RV32_CC) $(CFLAGS) -static $^ -o $@ -lm

$(RV32_BUILD_DIR)/run_tests.o: run_tests.c $(RV32_CC_STAMP)
	$(RV32_CC) $(CFLAGS) $(APP_INCLUDES) -I$(WAMR_DIR)/core/iwasm/include -c $< -o $@

$(RV32_BUILD_DIR)/libwasmc.a: $(RV32_OBJS) | $(RV32_BUILD_DIR)
	$(AR) rcs $@ $^

$(RV32_BUILD_DIR)/%.o: $(SRC_DIR)/%.c $(RV32_CC_STAMP)
	$(RV32_CC) $(CFLAGS) $(LIB_INCLUDES) -c $< -o $@

# RV32_CC must exist and target the soft-float ABI (see RV32_CC above)
rv32-cc-check:
	@command -v $(RV32_CC) > /dev/null || { \
		echo "error: RV32_CC=$(RV32_CC) not found; set RV32_CC to a" \
		     "riscv32 Linux GCC for -mabi=ilp32 (see the Makefile)"; \
		exit 1; }
	@$(RV32_CC) -dM -E - < /dev/null | grep -q __riscv_float_abi_soft || { \
		echo "error: RV32_CC=$(RV32_CC) does not target the soft-float" \
		     "ABI (-mabi=ilp32); set RV32_CC to one that does"; \
		exit 1; }

# Every riscv32 object depends on this stamp, which holds RV32_CC: it is
# rewritten, and they are rebuilt, only when RV32_CC changes (a recipe that
# leaves the file as it was does not make its dependents out of date).
$(RV32_CC_STAMP): FORCE | $(RV32_BUILD_DIR) rv32-cc-check
	@echo '$(RV32_CC)' | cmp -s - $@ || echo '$(RV32_CC)' > $@

# A fresh CMake directory each time: CMake keeps the compiler of its first
# configuration.
$(RV32_WAMR_BUILD_DIR)/libiwasm.a: $(RV32_CC_STAMP)
	rm -rf $(RV32_WAMR_BUILD_DIR)
	cmake -S $(WAMR_DIR) -B $(RV32_WAMR_BUILD_DIR) \
		-DCMAKE_C_COMPILER=$(RV32_CC) \
		-DWAMR_BUILD_TARGET=RISCV32_ILP32 \
		-DWAMR_BUILD_INTERP=0 \
		-DWAMR_BUILD_SIMD=0 \
		-DWAMR_BUILD_LIBC_WASI=0 \
		-DWAMR_BUILD_LIBC_BUILTIN=0
	cmake --build $(RV32_WAMR_BUILD_DIR) -j$(shell nproc)

# iwasm, static so that qemu-riscv32 needs no riscv32 sysroot. Static
# linking also explains the other flags: -DBH_HAS_DLFCN=0 drops the
# --native-lib option, which needs dlopen, and WASI is left out because its
# sockets use getaddrinfo; both warn in a static binary.
# -Wno-discarded-qualifiers silences harmless warnings in WAMR's
# libc-builtin wrappers. Fresh CMake directory as above.
iwasm: $(IWASM_BUILD_DIR)/iwasm

$(IWASM_BUILD_DIR)/iwasm: $(RV32_CC_STAMP)
	rm -rf $(IWASM_BUILD_DIR)
	cmake -S $(WAMR_DIR)/product-mini/platforms/linux -B $(IWASM_BUILD_DIR) \
		-DCMAKE_C_COMPILER=$(RV32_CC) \
		-DWAMR_BUILD_TARGET=RISCV32_ILP32 \
		-DWAMR_BUILD_LIBC_WASI=0 \
		-DWAMR_BUILD_SIMD=0 \
		-DCMAKE_EXE_LINKER_FLAGS=-static \
		-DCMAKE_C_FLAGS="-DBH_HAS_DLFCN=0 -Wno-discarded-qualifiers"
	cmake --build $(IWASM_BUILD_DIR) -j$(shell nproc)

FORCE:

$(BUILD_DIR) $(RV32_BUILD_DIR) $(ASAN_BUILD_DIR) $(COV_BUILD_DIR) $(ANALYZE_BUILD_DIR):
	mkdir -p $@

# Recipe: convert tests/*.wast with wast2json into the directory $(1).
define wast2json_all
@mkdir -p $(1)
@for f in tests/*.wast; do \
	json_file="$(1)/$$(basename "$$f" .wast).json"; \
	wast2json "$$f" -o "$$json_file" || exit 1; \
done
endef

# Recipe: compile every .wasm in $(2)/tests with $(2)/wasmc, printing
# "$(1): ..." progress per .wast file. A rejected module (exit 1) is fine,
# make test checks that; a sanitizer report (exit 99) or a crash fails,
# and its stderr is printed. The tests dir is made fresh, so no stale
# .wasm of deleted tests is compiled. Then the $(BENCH_MODULES), which
# must all compile: any non-zero exit fails.
define compile_modules
@rm -rf $(2)/tests
@echo "$(1): wast2json tests/*.wast -> $(2)/tests"
$(call wast2json_all,$(2)/tests)
@echo "$(1): compiling the modules with $(2)/wasmc"
@export ASAN_OPTIONS=exitcode=99 UBSAN_OPTIONS=exitcode=99:print_stacktrace=1; \
fail=0; i=0; total=0; n_wast=$$(ls tests/*.wast | wc -l); \
for w in tests/*.wast; do \
	i=$$((i + 1)); n=0; \
	name=$$(basename "$$w" .wast); \
	for f in $(2)/tests/$$name.*.wasm; do \
		[ -e "$$f" ] || continue; \
		n=$$((n + 1)); \
		$(2)/wasmc "$$f" -o /dev/null 2> $(2)/err.txt; \
		if [ $$? -gt 1 ]; then cat $(2)/err.txt; fail=1; fi; \
	done; \
	total=$$((total + n)); \
	printf "$(1): [%2d/%d] %-28s %4d modules\n" $$i $$n_wast "$$name.wast" $$n; \
done; \
n=0; \
for f in $(BENCH_MODULES); do \
	n=$$((n + 1)); \
	$(2)/wasmc "$$f" -o /dev/null 2> $(2)/err.txt || \
		{ echo "$(1): $$f failed"; cat $(2)/err.txt; fail=1; }; \
done; \
total=$$((total + n)); \
printf "$(1): %-36s %4d modules\n" "bench modules" $$n; \
rm -f $(2)/err.txt; \
if [ $$fail -eq 0 ]; then \
	echo "$(1): $$total modules compiled, no crash or sanitizer report"; \
else \
	echo "$(1): $$total modules compiled, failures above"; \
fi; \
exit $$fail
endef

test: $(RV32_BUILD_DIR)/run_tests
	$(call wast2json_all,$(TEST_BUILD_DIR))
	@qemu-riscv32 $(RV32_BUILD_DIR)/run_tests $(TEST_BUILD_DIR)/*.json

test-asan: $(ASAN_BUILD_DIR)/wasmc
	$(call compile_modules,test-asan,$(ASAN_BUILD_DIR))

coverage: $(COV_BUILD_DIR)/wasmc
	@rm -rf $(COV_BUILD_DIR)/html
	@rm -f $(COV_BUILD_DIR)/*.gcda $(COV_BUILD_DIR)/coverage.info
	$(call compile_modules,coverage,$(COV_BUILD_DIR))
	@echo "coverage: lcov -> $(COV_BUILD_DIR)/coverage.info"
	@lcov --quiet --capture --directory $(COV_BUILD_DIR) \
		--rc geninfo_unexecuted_blocks=1 \
		--include '$(CURDIR)/$(SRC_DIR)/*' \
		--output-file $(COV_BUILD_DIR)/coverage.info > /dev/null
	@genhtml --quiet $(COV_BUILD_DIR)/coverage.info \
		--output-directory $(COV_BUILD_DIR)/html > /dev/null
	@lcov --list $(COV_BUILD_DIR)/coverage.info 2> /dev/null | \
		grep -v -e '^\[' -e '^Message summary' -e 'no messages were reported'
	@echo "coverage: report in $(COV_BUILD_DIR)/html/index.html"

format:
	clang-format -i $(FORMAT_SRCS)

format-check:
	clang-format --dry-run --Werror $(FORMAT_SRCS)

infer:
	$(MAKE) clean
	infer run --progress-bar-style plain -- $(MAKE) all

tidy:
	clang-tidy $(TIDY_SRCS) -- $(CFLAGS) $(HOST_FLAGS) $(LIB_INCLUDES)

cppcheck:
	@mkdir -p $(BUILD_DIR)/cppcheck
	cppcheck --quiet -j$(shell nproc) --std=c17 --check-level=exhaustive --inline-suppr \
		--enable=all --suppress=missingIncludeSystem --suppress=checkersReport \
		--suppress=unusedFunction --error-exitcode=1 \
		--cppcheck-build-dir=$(BUILD_DIR)/cppcheck \
		$(LIB_INCLUDES) $(TIDY_SRCS)

# GCC's static analyzer runs while compiling, so analyze compiles each
# host source into $(ANALYZE_BUILD_DIR); only changed files are reanalyzed.
analyze: $(patsubst %.c, $(ANALYZE_BUILD_DIR)/%.o, $(notdir $(TIDY_SRCS)))

$(ANALYZE_BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(ANALYZE_BUILD_DIR)
	$(CC) $(CFLAGS) $(HOST_FLAGS) $(ANALYZE_FLAGS) $(LIB_INCLUDES) -c $< -o $@

$(ANALYZE_BUILD_DIR)/%.o: %.c | $(ANALYZE_BUILD_DIR)
	$(CC) $(CFLAGS) $(HOST_FLAGS) $(ANALYZE_FLAGS) $(APP_INCLUDES) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR)

-include $(wildcard $(BUILD_DIR)/*.d $(BUILD_DIR)/*/*.d)

.PHONY: all help clean test iwasm rv32-cc-check FORCE test-asan coverage format format-check infer tidy cppcheck analyze

