#ifndef AOT_H_INCLUDED
#define AOT_H_INCLUDED

#include <inttypes.h>
#include "wasm.h"
#include "libqbe.h"

typedef enum AOTErr_t {
    AOT_OK,
    AOT_ERR,
} AOTErr_t;

#define AOT_MAGIC_NUMBER 0x746f6100
#define AOT_CURRENT_VERSION 5

/* Legal values for bin_type */
#define BIN_TYPE_ELF32L 0 /* 32-bit little endian */
#define BIN_TYPE_ELF32B 1 /* 32-bit big endian */
#define BIN_TYPE_ELF64L 2 /* 64-bit little endian */
#define BIN_TYPE_ELF64B 3 /* 64-bit big endian */

/* Legal values for e_type (object file type). */
#define E_TYPE_NONE 0  /* No file type */
#define E_TYPE_REL  1  /* Relocatable file */
#define E_TYPE_EXEC 2  /* Executable file */
#define E_TYPE_DYN  3  /* Shared object file */
/* eXecute In Place file, it is not a standard ELF e_type,
 * see wasm-micro-runtime/doc/xip.md */
#define E_TYPE_XIP  4

/* Legal values for e_machine (architecture).  */
#define E_MACHINE_RISCV 243         /* RISC-V 32/64 */

/* Legal values for e_version */
#define E_VERSION_CURRENT 1 /* Current version */

/* Legal values for e_flags */
#define EF_RISCV_FLOAT_ABI_DOUBLE  0x0004

/* Wasm feature supported, legal value for feature_flags */
#define WASM_FEATURE_SIMD_128BIT (1 << 0)
#define WASM_FEATURE_BULK_MEMORY (1 << 1)
#define WASM_FEATURE_MULTI_THREAD (1 << 2)
#define WASM_FEATURE_REF_TYPES (1 << 3)
#define WASM_FEATURE_GARBAGE_COLLECTION (1 << 4)
#define WASM_FEATURE_EXCEPTION_HANDLING (1 << 5)
#define WASM_FEATURE_TINY_STACK_FRAME (1 << 6)
#define WASM_FEATURE_MULTI_MEMORY (1 << 7)
#define WASM_FEATURE_DYNAMIC_LINKING (1 << 8)
#define WASM_FEATURE_COMPONENT_MODEL (1 << 9)
#define WASM_FEATURE_RELAXED_SIMD (1 << 10)
#define WASM_FEATURE_FLEXIBLE_VECTORS (1 << 11)
/* Stack frame is created at the beginning of the function,
 * and not at the beginning of each function call */
#define WASM_FEATURE_FRAME_PER_FUNCTION (1 << 12)
#define WASM_FEATURE_FRAME_NO_FUNC_IDX (1 << 13)

/* Legal values for relocation type */
#define R_RISCV_CALL 18

typedef enum AOTSectionType {
    /* These six sections are mandatory and must appear in the following order,
     * between each mandatory section there can be zero one or more custom sections. */
    AOT_SECTION_TYPE_TARGET_INFO = 0,
    AOT_SECTION_TYPE_INIT_DATA = 1,
    AOT_SECTION_TYPE_TEXT = 2,
    AOT_SECTION_TYPE_FUNCTION = 3,
    AOT_SECTION_TYPE_EXPORT = 4,
    AOT_SECTION_TYPE_RELOCATION = 5,
    /* Note: We haven't had anything to use AOT_SECTION_TYPE_SIGNATURE.
     * It's just reserved for possible module signing features. */
    AOT_SECTION_TYPE_SIGNATURE = 6,
    AOT_SECTION_TYPE_CUSTOM = 100,
} AOTSectionType;

/* Target info, read from ELF header of object file */
typedef struct AOTTargetInfo {
    /* Binary type (elf32l/elf32b/elf64l/elf64b). The only two least significant bits
     * of bin_type has a meaning, the other bit are useless.

     * target_info.bin_type & 1 identifies the architecture for this binary.(This
     * bit has the same meaning as the EI_CLASS field of e_ident in the ELF header).
     * - If target_info.bin_type & 1 == 1 then this binary is for 64-bit machine.
     * - If target_info.bin_type & 1 == 1 then this binary is for 32-bit machine.

     * target_info.bin_type & 2 specifies the data encoding of the processor-specific
     * data in the file. (This bit has the same meaning as the EI_DATA field of
     * e_ident in the ELF header).
     * - If target_info.bin_type & 2 == 0 then the data encoding is two's complement,
     * little-endian.
     * - If target_info.bin_type & 2 == 1 then the data encoding is two's complement,
     * big-endian. */
    uint16_t bin_type;
    /* Not used by WAMR, can be any value */
    uint16_t abi_type;
    /* e_type identifies the object file type. This field has the same meaning as the
     * e_type field in the ELF header */
    uint16_t e_type;
    /* e_machine specifies the architecture for which this file is intended,
     * indicating the type of machine that the executable is designed to run on */
    uint16_t e_machine;
    /* Object file version */
    uint32_t e_version;
    /* e_flags holds processor-specific flags. See chapter 8 of riscv-abi.pdf */
    uint32_t e_flags;
    /* Specify wasm features supported */
    uint64_t feature_flags;
    /* Reserved, always zero */
    uint64_t reserved;
    /* Architecture string name, must be set consistence with e_machine.
     * arch can only be "riscv32" */
    char arch[16];
} AOTTargetInfo;

/* Relocation info */
typedef struct AOTRelocation {
    struct AOTRelocation *next;
    /* offset gives the location at which to apply the relocation action.
     * The value is the byte offset from the beginning of the section to
     * the storage unit affected by the relocation */
    uint32_t offset;
    /* addend specifies a constant addend used to compute the value to be
     * stored into the relocatable field */
    uint32_t addend;
    /* relocation type */
    uint32_t type;
    struct AOTRelocation *representative;
    uint32_t symbol_index;
    char *symbol_name;
} AOTRelocation;

typedef struct AOTFuncSecEntry {
    uint32_t text_offset;
    uint32_t type_index;
} AOTFuncSecEntry;

typedef struct AOTModule {
    WASMModule *wasm_mod;
    uint8_t *buf;
    uint32_t buf_len;
    uint8_t *offset;
    uint8_t *buf_end;
    uint8_t *p_end;
    uint32_t *text_size;
    uint8_t *text_start;
    uint32_t next_func;
    AOTFuncSecEntry *funcs;
    uint32_t reloc_count;
    AOTRelocation *reloc_list;
} AOTModule;

typedef struct Target {
    char *name;
    AOTErr_t (*emit_target_info)(AOTModule *);
    AOTErr_t (*emitfn)(AOTModule *, IRFunction *, uint32_t);
} Target;

#define ALIGN_PTR(p, align) ((void *)((((uintptr_t)p) + (align-1)) & ~(align-1)))

#define TEMPLATE_WRITE(val, p, p_end, type, align) \
    do { \
        p = ALIGN_PTR(p, align); \
        if ((long unsigned int)(p_end - p) < sizeof(type)) { \
            return AOT_ERR; \
        } \
        *((type *)p) = (val); \
        p += sizeof(type); \
    } while (0)

#define WRITE_UINT64(m, val) \
    TEMPLATE_WRITE(val, (m)->offset, (m)->buf_end, uint64_t, sizeof(uint32_t))

#define WRITE_UINT32(m, val) \
    TEMPLATE_WRITE(val, (m)->offset, (m)->buf_end, uint32_t, sizeof(uint32_t))

#define WRITE_UINT16(m, val) \
    TEMPLATE_WRITE(val, (m)->offset, (m)->buf_end, uint16_t, sizeof(uint16_t))

#define WRITE_UINT8(m, val) \
    TEMPLATE_WRITE(val, (m)->offset, (m)->buf_end, uint8_t, sizeof(uint8_t))

#define WRITE_BYTE_ARRAY(m, addr, len) \
    do { \
        if ((long unsigned int)((m)->buf_end - (m)->offset) < len) { \
            return AOT_ERR; \
        } \
        memcpy((m)->offset, addr, len); \
        (m)->offset += len; \
    } while (0)


AOTErr_t aot_module_init(AOTModule *aot_mod, uint8_t *aot_buf,
                         unsigned int aot_len, WASMModule *w);
AOTErr_t emit_target_info(AOTModule *m);

//AOTErr_t aot_module_finalize(AOTModule *m, uint8_t **buf, uint32_t *len);
//void aot_module_cleanup(AOTModule *m);

#endif

