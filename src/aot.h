#ifndef AOT_H_INCLUDED
#define AOT_H_INCLUDED

#include <inttypes.h>
#include "wasm.h"
#include "listx.h"

typedef enum AOTErr_t {
    AOT_OK,
    AOT_ERR,
} AOTErr_t;

typedef enum AOTSectionType {
    AOT_SECTION_TYPE_TARGET_INFO = 0,
    AOT_SECTION_TYPE_INIT_DATA   = 1,
    AOT_SECTION_TYPE_TEXT        = 2,
    AOT_SECTION_TYPE_FUNCTION    = 3,
    AOT_SECTION_TYPE_EXPORT      = 4,
    AOT_SECTION_TYPE_RELOCATION  = 5,
    AOT_REQUIRED_SECTION_COUNT   = 6,
    AOT_SECTION_TYPE_CUSTOM      = 100,
} AOTSectionType;

/* AOT File Header */
#define AOT_MAGIC0 '\0'
#define AOT_MAGIC1  'a'
#define AOT_MAGIC2  'o'
#define AOT_MAGIC3  't'

#define AOT_CURRENT_VERSION 5

/* AOT Taget Info Section */
typedef struct AOTTargetInfo {
    uint16_t bin_type;
    uint16_t abi_type;
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_flags;
    uint64_t feature_flags;
    uint64_t reserved;
    char arch[16];
} AOTTargetInfo;

/* Legal values for bin_type */
#define AOT_BIN_TYPE_ELF32L 0 /* ELF, 32-bit, little endian */
#define AOT_BIN_TYPE_ELF32B 1 /* ELF, 32-bit, big endian */
#define AOT_BIN_TYPE_ELF64L 2 /* ELF, 64-bit, little endian */
#define AOT_BIN_TYPE_ELF64B 3 /* ELF, 64-bit, big endian */
#define AOT_BIN_TYPE_COFF32 4 /* COFF, 32-bit, little endian */
#define AOT_BIN_TYPE_COFF64 6 /* COFF, 64-bit, little endian */

#define AOT_IS_TARGET_LITTLE_ENDIAN(bin_type) (~((bin_type) & 1))
#define AOT_IS_TARGET_BIG_ENDIAN(bin_type) ((bin_type) & 1)

#define AOT_IS_TARGET_32_BIT(bin_type) (~((bin_type) & 2))
#define AOT_IS_TARGET_64_BIT(bin_type) ((bin_type) & 2)

/* Legal values for abi_type */
#define AOT_ABI_DEFAULT 0 /* abi_type is never used by WAMR */

/* Legal values for e_type */
#define AOT_E_TYPE_REL  1 /* Relocatable object file */
#define AOT_E_TYPE_XIP  4 /* Enable execution in place */

/* Legal values for e_machine */
#define AOT_E_MACHINE_X86_64       62     /* AMD x86-64 architecture */
#define AOT_E_MACHINE_WIN_X86_64   0x8664 /* Windows x86-64 architecture */
#define AOT_E_MACHINE_386          3      /* Intel 80386 */
#define AOT_E_MACHINE_WIN_I386     0x14c  /* Windows i386 architecture */
#define AOT_E_MACHINE_ARM          40     /* ARM 32-bit architecture (AARCH32) */
#define AOT_E_MACHINE_AARCH64      183    /* ARM 64-bit architecture (AARCH64) */
#define AOT_E_MACHINE_MIPS         8      /* MIPS I Architecture */
#define AOT_E_MACHINE_XTENSA       94     /* Tensilica Xtensa Architecture */
#define AOT_E_MACHINE_RISCV        243    /* RISC-V 32/64 bit */
#define AOT_E_MACHINE_ARC_COMPACT  93     /* ARC International ARCompact */
#define AOT_E_MACHINE_ARC_COMPACT2 195    /* Synopsys ARCompact V2 */

/* Legal values for e_version */
#define AOT_E_VERSION_CURRENT 1 /* Current version */

/* Legal values for e_flags */
#define AOT_FLAGS_DEFAULT 0 /* e_flags is never used by WAMR */

/* Legal values for feature_flags */
#define AOT_FEATURE_FLAGS_DEFAULT 0

/* Legal values for arch */
#define AOT_ARCH_X86_64       "x86_64"
#define AOT_ARCH_386          "i386"
#define AOT_ARCH_MIPS         "mips"
#define AOT_ARCH_XTENSA       "xtensa"
#define AOT_ARCH_RISCV        "riscv"
#define AOT_ARCH_ARC_COMPACT  "arc"
#define AOT_ARCH_ARC_COMPACT2 "arc"

typedef struct AOTModule {
    WASMModule *wasm_mod;
    uint8_t *buf;
    uint32_t buf_len;
    uint8_t *offset;
    uint8_t *buf_end;
    uint8_t *p_end;
    uint32_t *text_size;
    uint8_t *text_start;
    struct list_head patch_list;
    uint8_t **function_text_start;
} AOTModule;

AOTErr_t aot_module_init(AOTModule *aot_mod, uint8_t *aot_buf,
                         unsigned int aot_len, WASMModule *w);
AOTErr_t emit_target_info(AOTModule *m);
AOTErr_t emit_init_data(AOTModule *m);
AOTErr_t emit_function(AOTModule *m);
AOTErr_t emit_export(AOTModule *m);
AOTErr_t emit_relocation(AOTModule *m);
void aot_module_cleanup(AOTModule *m);

#endif

