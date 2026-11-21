# Runs the modules on the PC, with two engines (see run.c). Needs what the
# Makefile at the root of the repository needs for `make test`, and clang
# with the wasm32 target.
ROOT     := ../../..
WAMR     := $(ROOT)/wasm-micro-runtime
BUILD    := build
# the root's RV32_CC (see its Makefile), with its config.mk
RV32_CC  ?= riscv32-unknown-linux-gnu-gcc
-include $(ROOT)/config.mk
CFLAGS   := -std=gnu17 -Wall -Wextra -g -O1
INCLUDES := -I$(ROOT)/include -I$(WAMR)/core/iwasm/include
INTERP_LIB := $(BUILD)/wamr-interp/libiwasm.a

all: $(BUILD)/ed.wasm $(BUILD)/run_aot $(BUILD)/run_interp

$(BUILD)/ed.wasm: ../wasm/ed.c ../wasm/port.c ../wasm/port.h ../wasm/build.sh
	../wasm/build.sh

# wasmc + WAMR AOT, riscv32, run with qemu-riscv32
$(BUILD)/run_aot: run.c
	$(MAKE) -C $(ROOT) build/rv32/libwasmc.a build/rv32/wamr/libiwasm.a
	$(RV32_CC) $(CFLAGS) -DRUN_AOT -static $(INCLUDES) $< \
		$(ROOT)/build/rv32/libwasmc.a $(ROOT)/build/rv32/wamr/libiwasm.a \
		-lm -lpthread -o $@

# WAMR interpreter, on the PC
$(BUILD)/run_interp: run.c $(INTERP_LIB)
	$(CC) $(CFLAGS) $(INCLUDES) $< $(INTERP_LIB) -lm -lpthread -o $@

$(INTERP_LIB):
	cmake -S $(WAMR) -B $(BUILD)/wamr-interp \
		-DWAMR_BUILD_INTERP=1 -DWAMR_BUILD_AOT=0 -DWAMR_BUILD_JIT=0 \
		-DWAMR_BUILD_FAST_INTERP=0 -DWAMR_BUILD_SIMD=0 \
		-DWAMR_BUILD_LIBC_WASI=0 -DWAMR_BUILD_LIBC_BUILTIN=0
	cmake --build $(BUILD)/wamr-interp -j8

AOT    := qemu-riscv32 $(abspath $(BUILD)/run_aot)
INTERP := $(abspath $(BUILD)/run_interp)

test: all
	./test.sh "$(INTERP)" "$(abspath $(BUILD)/ed.wasm)"
	./test.sh "$(AOT)" "$(abspath $(BUILD)/ed.wasm)"

.PHONY: all test
