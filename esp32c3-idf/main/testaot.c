// gcc -o testaot testaot.c
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <assert.h>
#include <string.h>

typedef int err_t;
#define OK    0
#define FAIL -1

#define VALUE_TYPE_I32 0x7F
#define VALUE_TYPE_I64 0X7E
#define VALUE_TYPE_F32 0x7D
#define VALUE_TYPE_F64 0x7C

#define AOT_MAGIC_NUMBER 0x746f6100
#define AOT_CURRENT_VERSION 5

typedef enum AOTSectionType {
    /* These six sections are mandatory and must appear in the following order,
     * between each mandatory section there can be zero one or more custom sections. */
    AOT_SECTION_TYPE_TARGET_INFO = 0,
    AOT_SECTION_TYPE_INIT_DATA = 1,
    AOT_SECTION_TYPE_TEXT = 2,
    AOT_SECTION_TYPE_FUNCTION = 3,
    AOT_SECTION_TYPE_EXPORT = 4,
    AOT_SECTION_TYPE_RELOCATION = 5,
    /*_
     * Note: We haven't had anything to use AOT_SECTION_TYPE_SIGNATURE.
     * It's just reserved for possible module signing features.
     */
    AOT_SECTION_TYPE_SIGNATURE = 6,
    AOT_SECTION_TYPE_CUSTOM = 100,
} AOTSectionType;

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
#define E_MACHINE_386 3             /* Intel 80386 */
#define E_MACHINE_MIPS 8            /* MIPS R3000 big-endian */
#define E_MACHINE_MIPS_RS3_LE 10    /* MIPS R3000 little-endian */
#define E_MACHINE_ARM 40            /* ARM/Thumb */
#define E_MACHINE_AARCH64 183       /* AArch64 */
#define E_MACHINE_ARC 45            /* Argonaut RISC Core */
#define E_MACHINE_IA_64 50          /* Intel Merced */
#define E_MACHINE_MIPS_X 51         /* Stanford MIPS-X */
#define E_MACHINE_X86_64 62         /* AMD x86-64 architecture */
#define E_MACHINE_ARC_COMPACT 93    /* ARC International ARCompact */
#define E_MACHINE_ARC_COMPACT2 195  /* Synopsys ARCompact V2 */
#define E_MACHINE_XTENSA 94         /* Tensilica Xtensa Architecture */
#define E_MACHINE_RISCV 243         /* RISC-V 32/64 */
#define E_MACHINE_WIN_I386 0x14c    /* Windows i386 architecture */
#define E_MACHINE_WIN_X86_64 0x8664 /* Windows x86-64 architecture */

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
     * big-endian.
     */
    uint16_t bin_type;

    /* Not used by WAMR, can be any value */
    uint16_t abi_type;

    /* Object file type. This field has the same meaning as the e_type field in the
     * ELF header */
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
     * Legal value for arch are:
     * - "x86_64"
     * - "i386"
     * - "arm64"
     * - "aarch64"
     * - "mips"
     * - "xtensa"
     * - "riscv32"
     * - "riscv64"
     * - "arc"
     */
    char arch[16];
} AOTTargetInfo;

typedef struct WASMMemory {
    uint32_t flags;
    uint32_t num_bytes_per_page;
    uint32_t init_page_count;
    uint32_t max_page_count;
} WASMMemory;

typedef struct WASMFuncType {
    uint16_t param_count;
    uint16_t result_count;
    uint8_t types[0];
} WASMFuncType;

#define ALIGN_PTR(p, align) ((void *)((((uintptr_t)p) + (align-1)) & ~(align-1)))

#define TEMPLATE_WRITE(val, p, p_end, size, align) \
    do { \
        p = ALIGN_PTR(p, align); \
        if ((p_end - p) < size) { \
            goto ERROR; \
        } \
        *((uint32_t *)p) = val; \
        p += size; \
    } while (0)

#define WRITE_UINT64(val, p, p_end) \
    TEMPLATE_WRITE(val, p, p_end, sizeof(uint64_t), sizeof(uint32_t))

#define WRITE_UINT32(val, p, p_end) \
    TEMPLATE_WRITE(val, p, p_end, sizeof(uint32_t), sizeof(uint32_t))

#define WRITE_UINT16(val, p, p_end) \
    TEMPLATE_WRITE(val, p, p_end, sizeof(uint16_t), sizeof(uint16_t))

#define WRITE_UINT8(val, p, p_end) \
    TEMPLATE_WRITE(val, p, p_end, sizeof(uint8_t), sizeof(uint8_t))

#define WRITE_BYTE_ARRAY(p, p_end, addr, len) \
    do { \
        if ((p_end - p) < len) { \
            goto ERROR; \
        } \
        memcpy(p, addr, len); \
        p += len; \
    } while (0)

static err_t emit_aot_target_info(uint8_t *buf, uint8_t *buf_end, uint8_t **curr) {


    static const AOTTargetInfo target_info = {
        .bin_type = BIN_TYPE_ELF32L,
        .abi_type = 0,
        .e_type = E_TYPE_REL,
        .e_machine = E_MACHINE_RISCV,
        .e_version = E_VERSION_CURRENT,
        .e_flags = 0,
        .feature_flags = 0,
        .reserved = 0,
        .arch = "riscv32",
    };

    uint8_t *p = buf;
    uint8_t *p_end = buf_end;

    WRITE_UINT32(AOT_SECTION_TYPE_TARGET_INFO, p, p_end);
    WRITE_UINT32(sizeof(struct AOTTargetInfo), p, p_end);
    WRITE_UINT16(target_info.bin_type, p, p_end);
    WRITE_UINT16(target_info.abi_type, p, p_end);
    WRITE_UINT16(target_info.e_type, p, p_end);
    WRITE_UINT16(target_info.e_machine, p, p_end);
    WRITE_UINT32(target_info.e_version, p, p_end);
    WRITE_UINT32(target_info.e_flags, p, p_end);
    WRITE_UINT64(target_info.reserved, p, p_end);
    WRITE_UINT64(target_info.feature_flags, p, p_end);
    WRITE_BYTE_ARRAY(p, p_end, target_info.arch, sizeof(target_info.arch));
    *curr = p;
    return OK;

ERROR:
    *curr = NULL;
    return FAIL;
}

static err_t emit_aot_init_data(uint8_t *buf, uint8_t *buf_end, uint32_t func_count,
    WASMFuncType **types, uint32_t type_count, uint8_t **curr) {

    uint8_t *p = buf;
    uint8_t *p_end = buf_end;

    WRITE_UINT32(AOT_SECTION_TYPE_INIT_DATA, p, p_end);
    uint8_t *section_size = p;
    p += sizeof(uint32_t);
    uint8_t *section_start = p;

    //----- Emit Memory Info subsection -----
    const uint32_t import_memory_count = 0;
    const uint32_t memory_count = 1;
    const WASMMemory mem = {
        .flags = 0,
        .num_bytes_per_page = 65536,
        .init_page_count = 0,
        .max_page_count = 0,
    };
    const uint32_t mem_init_data_count = 0;

    WRITE_UINT32(import_memory_count, p, p_end);
    WRITE_UINT32(memory_count, p, p_end);

    WRITE_UINT32(mem.flags, p, p_end);
    WRITE_UINT32(mem.num_bytes_per_page, p, p_end);
    WRITE_UINT32(mem.init_page_count, p, p_end);
    WRITE_UINT32(mem.max_page_count, p, p_end);

    WRITE_UINT32(mem_init_data_count, p, p_end);

    //----- Emit Memory Info subsection -----
    const uint32_t import_table_count = 0;
    const uint32_t table_count = 0;
    const uint32_t table_init_data_count = 0;
    WRITE_UINT32(import_table_count, p, p_end);
    WRITE_UINT32(table_count, p, p_end);
    WRITE_UINT32(table_init_data_count, p, p_end);

    //----- Emit Type Info subsection -----
    WRITE_UINT32(type_count, p, p_end);
    for (uint32_t i = 0; i < type_count; i++) {
        p = ALIGN_PTR(p, 4);
        //type_flag field is not used
        uint16_t type_flag = 0;
        WRITE_UINT16(type_flag, p, p_end);
        WRITE_UINT16(types[i]->param_count, p, p_end);
        WRITE_UINT16(types[i]->result_count, p, p_end);

        size_t type_size = types[i]->param_count + types[i]->result_count;
        WRITE_BYTE_ARRAY(p, p_end, types[i]->types, type_size);
    }

    //----- Emit Import Global Info subsection  -----
    const uint32_t import_global_count = 0;
    WRITE_UINT32(import_global_count, p, p_end);

    //----- Emit Global Info subsection  -----
    const uint32_t global_count = 0;
    WRITE_UINT32(global_count, p, p_end);

    //----- Emit Import Func Info subsection -----
    const uint32_t import_func_count = 0;
    WRITE_UINT32(import_func_count, p, p_end);

    //----- Emit function count and start function index -----
    WRITE_UINT32(func_count, p, p_end);
    const uint32_t start_func_index = -1;
    WRITE_UINT32(start_func_index, p, p_end);

    //----- Emit aux -----
    const uint32_t aux_data_end_global_index = -1;
    const uint64_t aux_data_end = 0;
    const uint32_t aux_heap_base_global_index = -1;
    const uint64_t aux_heap_base = 0;
    const uint32_t aux_stack_top_global_index = -1;
    const uint64_t aux_stack_bottom = 0;
    const uint32_t aux_stack_size = 0;

    WRITE_UINT32(aux_data_end_global_index, p, p_end);
    WRITE_UINT64(aux_data_end, p, p_end);
    WRITE_UINT32(aux_heap_base_global_index, p, p_end);
    WRITE_UINT64(aux_heap_base, p, p_end);
    WRITE_UINT32(aux_stack_top_global_index, p, p_end);
    WRITE_UINT64(aux_stack_bottom, p, p_end);
    WRITE_UINT32(aux_stack_size, p, p_end);

    //----- Emit data sections -----
    const uint32_t data_section_count = 0;
    WRITE_UINT32(data_section_count, p, p_end);

    //Fix section size
    *section_size = p - section_start;
    *curr = p;
    return OK;

ERROR:
    *curr = NULL;
    return FAIL;
}

static err_t emit_aot_text(uint8_t *buf, uint8_t *buf_end, uint8_t **curr) {

    uint8_t *p = buf;
    uint8_t *p_end = buf_end;

    WRITE_UINT32(AOT_SECTION_TYPE_TEXT, p, p_end);
    uint8_t *section_size = p;
    p += sizeof(uint32_t);
    uint8_t *section_start = p;

    const uint32_t literal_size = 0;
    WRITE_UINT32(literal_size, p, p_end);

    const uint8_t code[] = {
        0x33, 0x85, 0xc5, 0x00,
        0x67, 0x80, 0x00, 0x00,
    };
    const uint32_t code_size = sizeof(code);
    WRITE_BYTE_ARRAY(p, p_end, code, code_size);

    //Fix section size
    *section_size = p - section_start;

    *curr = p;
    return OK;

ERROR:
    *curr = NULL;
    return FAIL;
}

static err_t emit_aot_function(uint8_t *buf, uint8_t *buf_end, uint8_t **curr) {

    uint8_t *p = buf;
    uint8_t *p_end = buf_end;

    WRITE_UINT32(AOT_SECTION_TYPE_FUNCTION, p, p_end);
    uint8_t *section_size = p;
    p += sizeof(uint32_t);
    uint8_t *section_start = p;

    uint32_t function_offset = 0;
    WRITE_UINT32(function_offset, p, p_end);

    uint32_t function_type_index = 0;
    WRITE_UINT32(function_type_index, p, p_end);
    
    /* not used */
    uint32_t max_local_cell_num = 0;
    uint32_t max_stack_cell_num = 0;
    WRITE_UINT32(max_local_cell_num, p, p_end);
    WRITE_UINT32(max_stack_cell_num, p, p_end);

    //Fix section size
    *section_size = p - section_start;

    *curr = p;
    return OK;

ERROR:
    *curr = NULL;
    return FAIL;
}

static err_t emit_aot_export(uint8_t *buf, uint8_t *buf_end, uint8_t **curr) {

    uint8_t *p = buf;
    uint8_t *p_end = buf_end;

    WRITE_UINT32(AOT_SECTION_TYPE_EXPORT, p, p_end);
    uint8_t *section_size = p;
    p += sizeof(uint32_t);
    uint8_t *section_start = p;

    uint32_t export_count = 1;
    WRITE_UINT32(export_count, p, p_end);

    uint32_t export_func_index = 0;
    #define EXPORT_FUNC_KIND 0x00
    WRITE_UINT32(export_func_index, p, p_end);
    WRITE_UINT8(EXPORT_FUNC_KIND, p, p_end);
    char *name = "add";
    uint32_t name_len = strlen(name)+1;
    WRITE_UINT16(name_len, p, p_end);
    WRITE_BYTE_ARRAY(p, p_end, "add", name_len);

    //Fix section size
    *section_size = p - section_start;

    *curr = p;
    return OK;

ERROR:
    *curr = NULL;
    return FAIL;
}

static err_t emit_aot_relocation(uint8_t *buf, uint8_t *buf_end, uint8_t **curr) {

    uint8_t *p = buf;
    uint8_t *p_end = buf_end;

    WRITE_UINT32(AOT_SECTION_TYPE_RELOCATION, p, p_end);
    uint8_t *section_size = p;
    p += sizeof(uint32_t);
    uint8_t *section_start = p;

    uint32_t symbol_count = 0;
    WRITE_UINT32(symbol_count, p, p_end);

    uint32_t total_string_len = 0;
    WRITE_UINT32(total_string_len, p, p_end);

    uint32_t group_count = 0;
    WRITE_UINT32(group_count, p, p_end);

    //Fix section size
    *section_size = p - section_start;

    *curr = p;
    return OK;

ERROR:
    *curr = NULL;
    return FAIL;
}


static err_t emit(uint8_t *buf, size_t len, uint8_t **curr) {
    uint8_t *p = buf;
    uint8_t *p_end = buf + len;

    WRITE_UINT32(AOT_MAGIC_NUMBER, p, p_end);
    WRITE_UINT32(AOT_CURRENT_VERSION, p, p_end);
    if (emit_aot_target_info(p, p_end, &p)) {
        goto ERROR;
    }
    uint32_t func_count = 1;
    uint32_t types_count = 1;
    WASMFuncType **types = calloc(types_count, sizeof(WASMFuncType *));
    types[0] = malloc(sizeof(WASMFuncType));
    types[0]->param_count = 2;
    types[0]->result_count = 1;
    types[0]->types[0] = VALUE_TYPE_I32;
    types[0]->types[1] = VALUE_TYPE_I32;
    types[0]->types[2] = VALUE_TYPE_I32;

    if (emit_aot_init_data(p, p_end, func_count, types, types_count, &p)) {
        goto ERROR;
    }

    if (emit_aot_text(p, p_end, &p)) {
        goto ERROR;
    }

    if (emit_aot_function(p, p_end, &p)) {
        goto ERROR;
    }

    if (emit_aot_export(p, p_end, &p)) {
        goto ERROR;
    }

    if (emit_aot_relocation(p, p_end, &p)) {
        goto ERROR;
    }

    *curr = p;
    return OK;

ERROR:
    *curr = NULL;
    return FAIL;
}

#define LEN 1024
static uint8_t buf[LEN];

void test_aot(uint8_t **buf_out, unsigned int *len_out) {

    uint8_t *curr;

    if (emit(buf, LEN, &curr)) {
        fprintf(stderr, "Error: emit failed\n");
        exit(EXIT_FAILURE);
    }

    *buf_out = buf;
    *len_out = curr - buf;
}
