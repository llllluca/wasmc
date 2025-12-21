#include <stdio.h>
#include <assert.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#define MAX_LINEAR_MEMORY_SIZE ((uint64_t)4 * 1024 * 1024 * 1024)

#define AOT_MAGIC_NUMBER 0x746f6100
#define AOT_CURRENT_VERSION 5

/* legal value for init_expr_type */
#define INIT_EXPR_NONE 0x00
#define INIT_EXPR_TYPE_I32_CONST 0x41
#define INIT_EXPR_TYPE_I64_CONST 0x42
#define INIT_EXPR_TYPE_F32_CONST 0x43
#define INIT_EXPR_TYPE_F64_CONST 0x44
#define INIT_EXPR_TYPE_V128_CONST 0xFD
#define INIT_EXPR_TYPE_GET_GLOBAL 0x23
#define INIT_EXPR_TYPE_I32_ADD 0x6A
#define INIT_EXPR_TYPE_I32_SUB 0x6B
#define INIT_EXPR_TYPE_I32_MUL 0x6C
#define INIT_EXPR_TYPE_I64_ADD 0x7C
#define INIT_EXPR_TYPE_I64_SUB 0x7D
#define INIT_EXPR_TYPE_I64_MUL 0x7E
#define INIT_EXPR_TYPE_REFNULL_CONST 0xD0
#define INIT_EXPR_TYPE_FUNCREF_CONST 0xD2
#define INIT_EXPR_TYPE_STRUCT_NEW 0xD3
#define INIT_EXPR_TYPE_STRUCT_NEW_DEFAULT 0xD4
#define INIT_EXPR_TYPE_ARRAY_NEW 0xD5
#define INIT_EXPR_TYPE_ARRAY_NEW_DEFAULT 0xD6
#define INIT_EXPR_TYPE_ARRAY_NEW_FIXED 0xD7
#define INIT_EXPR_TYPE_I31_NEW 0xD8
#define INIT_EXPR_TYPE_ANY_CONVERT_EXTERN 0xD9
#define INIT_EXPR_TYPE_EXTERN_CONVERT_ANY 0xDA

/* Legal values for bin_type */
#define BIN_TYPE_ELF32L 0 /* 32-bit little endian */
#define BIN_TYPE_ELF32B 1 /* 32-bit big endian */
#define BIN_TYPE_ELF64L 2 /* 64-bit little endian */
#define BIN_TYPE_ELF64B 3 /* 64-bit big endian */
#define BIN_TYPE_COFF32 4 /* 32-bit little endian */
#define BIN_TYPE_COFF64 6 /* 64-bit little endian */

/* Legal values for e_type (object file type). */
#define E_TYPE_NONE 0 /* No file type */
#define E_TYPE_REL 1  /* Relocatable file */
#define E_TYPE_EXEC 2 /* Executable file */
#define E_TYPE_DYN 3  /* Shared object file */
#define E_TYPE_XIP 4  /* eXecute In Place file */

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

typedef uint8_t boolean;
#define TRUE  1
#define FALSE 0

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

typedef struct AOTSection {
    struct AOTSection *next;
    /* section type */
    AOTSectionType type;
    /* section body, not include type and size */
    uint8_t *body;
    /* section body size */
    uint32_t body_size;
} AOTSection;

/* Target info, read from ELF header of object file */
typedef struct AOTTargetInfo {
    /* Binary type, elf32l/elf32b/elf64l/elf64b */
    uint16_t bin_type;
    /* ABI type */
    uint16_t abi_type;
    /* Object file type */
    uint16_t e_type;
    /* Architecture */
    uint16_t e_machine;
    /* Object file version */
    uint32_t e_version;
    /* Processor-specific flags */
    uint32_t e_flags;
    /* Specify wasm features supported */
    uint64_t feature_flags;
    /* Reserved */
    uint64_t reserved;
    /* Arch name */
    char arch[16];
} AOTTargetInfo;

typedef struct WASMMemory {
    uint32_t flags;
    uint32_t num_bytes_per_page;
    uint32_t init_page_count;
    uint32_t max_page_count;
} WASMMemory;

/* Function type */
typedef struct WASMFuncType {
    uint16_t param_count;
    uint16_t result_count;
    uint8_t types[1];
} WASMFuncType;

typedef struct AOTObjectDataSection {
    char *name;
    uint8_t *data;
    uint32_t size;
} AOTObjectDataSection;

typedef struct AOTInitData {
    /* number of imported memory in the WASM module */
    uint32_t import_memory_count;
    /* number of memory in the WASM module */
    uint32_t memory_count;
    WASMMemory *memories;
    uint32_t mem_init_data_count;

    uint32_t import_table_count;
    uint32_t table_count;
    uint32_t table_init_data_count;

    uint32_t type_count;
    WASMFuncType **types;

    uint32_t import_global_count;

    uint32_t global_count;

    uint32_t import_func_count;

    uint32_t func_count;
    uint32_t start_func_index;
    uint32_t aux_data_end_global_index;
    uint64_t aux_data_end;
    uint32_t aux_heap_base_global_index;
    uint64_t aux_heap_base;
    uint32_t aux_stack_top_global_index;
    uint64_t aux_stack_bottom;
    uint32_t aux_stack_size;

    uint32_t data_section_count;
    AOTObjectDataSection  *data_sections;

} AOTInitData;

typedef struct AOTText {
    /* ".literal" section in object file */
    uint32_t literal_size;
    uint8_t *literal;
    /* ".text" section in object file */
    uint32_t code_size;
    uint8_t *code;
} AOTText;

typedef struct WASMExport {
    char *name;
    uint8_t kind;
    uint32_t index;
} WASMExport;

/* Relocation info */
typedef struct AOTRelocation {
    uint32_t offset;
    uint32_t addend;
    uint32_t type;
    char *symbol_name;
    /* index in the symbol offset field */
    uint32_t symbol_index;
} AOTRelocation;

/* Relocation Group */
typedef struct AOTRelocationGroup {
    char *section_name;
    /* index in the symbol offset field */
    uint32_t name_index;
    uint32_t relocation_count;
    AOTRelocation *relocations;
} AOTRelocationGroup;

static AOTTargetInfo target_info;
static AOTInitData data;
static AOTText text;


const char *AOTSectionType_to_string(AOTSectionType sec_type) {
    assert((0 <= sec_type && sec_type <= 6) || sec_type == 100);
    static const char *aot_sec_type[] = {
        [AOT_SECTION_TYPE_TARGET_INFO] = "TARGET_INFO",
        [AOT_SECTION_TYPE_INIT_DATA]   = "INIT_DATA",
        [AOT_SECTION_TYPE_TEXT]        = "TEXT",
        [AOT_SECTION_TYPE_FUNCTION]    = "FUNCTION",
        [AOT_SECTION_TYPE_EXPORT]      = "EXPORT",
        [AOT_SECTION_TYPE_RELOCATION]  = "RELOCATION",
        [AOT_SECTION_TYPE_SIGNATURE]   = "SIGNATURE",
        [AOT_SECTION_TYPE_CUSTOM]      = "CUSTOM",
    };

    return aot_sec_type[sec_type];
}

#define read_byte_array(p, p_end, addr, len) \
    do { \
        assert((unsigned long)(p_end - p) >= len); \
        memcpy(addr, p, len); \
        p += len; \
    } while (0)

static inline uint64_t GET_U64_FROM_ADDR(uint32_t *addr) {
    union {
        uint64_t val;
        uint32_t parts[2];
    } u;
    u.parts[0] = addr[0];
    u.parts[1] = addr[1];
    return u.val;
}

#define TEMPLATE_READ(p, p_end, res, type) \
    do { \
        if (sizeof(type) != sizeof(uint64_t)) \
            p = align_ptr(p, sizeof(type)); \
        else  \
            /* align 4 bytes if type is uint64 */ \
            p = align_ptr(p, sizeof(uint32_t)); \
        if (sizeof(type) != sizeof(uint64_t)) \
            *(res) = *(type *)(p); \
        else \
            *(res) = GET_U64_FROM_ADDR((uint32_t *)p); \
        p += sizeof(type); \
    } while (0)



#define read_uint64(p, p_end, out_ptr) TEMPLATE_READ(p, p_end, out_ptr, uint64_t)
#define read_uint32(p, p_end, out_ptr) TEMPLATE_READ(p, p_end, out_ptr, uint32_t)
#define read_uint16(p, p_end, out_ptr) TEMPLATE_READ(p, p_end, out_ptr, uint16_t)
#define read_uint8(p, p_end, out_ptr)  TEMPLATE_READ(p, p_end, out_ptr, uint8_t)

static uint8_t *align_ptr(const uint8_t *p, uint32_t b) {
    uintptr_t v = (uintptr_t)p;
    uintptr_t m = b - 1;
    return (uint8_t *)((v + m) & ~m);
}

void *xmalloc(size_t size) {
    void *p;
    if ((p = malloc(size)) == NULL) {
        fprintf(stderr, "Error: cannot malloc %ld bytes\n", size);
        exit(EXIT_FAILURE);
    }
    return p;
}

void *xcalloc(size_t nmemb, size_t size) {
    void *p;
    if ((p = calloc(nmemb, size)) == NULL) {
        fprintf(stderr, "Error: cannot calloc %ld member of size %ld bytes each\n",
                nmemb, size);
        exit(EXIT_FAILURE);
    }
    return p;
}

AOTSection *parse_sections(uint8_t *buf_start, uint8_t *buf_end) {
    uint8_t *p = buf_start;
    AOTSection *section_list = NULL;
    AOTSection *section_list_tail = NULL;
    while (p < buf_end) {
        uint32_t section_type;
        read_uint32(p, buf_end, &section_type);

        uint32_t section_size;
        read_uint32(p, buf_end, &section_size);

        AOTSection *section = xmalloc(sizeof(AOTSection));
        section->next = NULL;
        section->type = section_type;
        section->body = p;
        section->body_size = section_size;
        if (section_list_tail == NULL) {
            section_list = section;
        } else {
            section_list_tail->next = section;
        }
        section_list_tail = section;
        p = section->body + section->body_size;
    }
    return section_list;
}

boolean check_sections_order(AOTSection *section_list) {
    AOTSection *s = section_list;
    if (s == NULL || s->type != AOT_SECTION_TYPE_TARGET_INFO) {
        return FALSE;
    }
    uint32_t last_mandatory_sec_type = AOT_SECTION_TYPE_TARGET_INFO;
    for (s = s->next; s != NULL; s = s->next) {
        if (s->type == AOT_SECTION_TYPE_SIGNATURE) return FALSE;
        if (s->type == AOT_SECTION_TYPE_CUSTOM) continue;
        if (s->type != last_mandatory_sec_type + 1) return FALSE;
        last_mandatory_sec_type = s->type;
    }
    return last_mandatory_sec_type == AOT_SECTION_TYPE_RELOCATION;
}

void print_target_info_section(AOTSection *s) {
    printf("section type: %s\n", AOTSectionType_to_string(s->type));
    printf("section length: %"PRIu32"\n", s->body_size);
    uint8_t *p = s->body;
    uint8_t *p_end = s->body + s->body_size;

    read_uint16(p, p_end, &target_info.bin_type);
    read_uint16(p, p_end, &target_info.abi_type);
    read_uint16(p, p_end, &target_info.e_type);
    read_uint16(p, p_end, &target_info.e_machine);
    read_uint32(p, p_end, &target_info.e_version);
    read_uint32(p, p_end, &target_info.e_flags);
    read_uint64(p, p_end, &target_info.feature_flags);
    read_uint64(p, p_end, &target_info.reserved);
    read_byte_array(p, p_end, target_info.arch, sizeof(target_info.arch));

    if (p != p_end) {
        fprintf(stderr, "Error: Invalid section size\n");
    }

    printf("bin type: %"PRIu16"\n", target_info.bin_type);
    printf("abi type: %"PRIu16"\n", target_info.abi_type);
    printf("object file type: %"PRIu16"\n", target_info.e_type);
    printf("machine arch: %"PRIu16"\n", target_info.e_machine);
    printf("object file version: %"PRIu16"\n", target_info.e_version);
    printf("processor-specific flag: %"PRIu16"\n", target_info.e_flags);
    printf("wasm features flag: %"PRIu64"\n", target_info.feature_flags);
    printf("reserved: %"PRIu64"\n", target_info.reserved);
    if (target_info.arch[sizeof(target_info.arch) - 1] != '\0') {
        fprintf(stderr, "Error: invalid arch name\n");
    } else {
        printf("arch name: %s\n", target_info.arch);
    }

    /* Check target endian type */
    boolean is_target_little_endian = target_info.bin_type & 1 ? FALSE : TRUE;
    printf("target endian type: %s endian\n", is_target_little_endian ? "little" : "big");

    /* Check target bit width */
    boolean is_target_64_bit = target_info.bin_type & 2 ? TRUE : FALSE;
    printf("target bit width: %s-bit\n",is_target_64_bit ? "64" : "32");

    /* Check target elf file type */
    if (target_info.e_type != E_TYPE_REL && target_info.e_type != E_TYPE_XIP) {
        fprintf(stderr, "Error: invalid object file type, "
            "expected relocatable or XIP file type but got others\n");
    }

    /* Check machine info */
    char *machine_type = NULL;
    switch (target_info.e_machine) {
        case E_MACHINE_X86_64:
        case E_MACHINE_WIN_X86_64:
            machine_type = "x86_64";
            break;
        case E_MACHINE_386:
        case E_MACHINE_WIN_I386:
            machine_type = "i386";
            break;
        case E_MACHINE_ARM:
            machine_type = "arm64";
            break;
        case E_MACHINE_AARCH64:
            machine_type = "aarch64";
            break;
        case E_MACHINE_MIPS:
            machine_type = "mips";
            break;
        case E_MACHINE_XTENSA:
            machine_type = "xtensa";
            break;
        case E_MACHINE_RISCV:
            machine_type = "riscv";
            break;
        case E_MACHINE_ARC_COMPACT:
        case E_MACHINE_ARC_COMPACT2:
            machine_type = "arc";
            break;
        default:
            fprintf(stderr, "Error: unknown machine type %d\n", target_info.e_machine);
    }
    if (machine_type != NULL) {
        if (strncmp(target_info.arch, machine_type, strlen(machine_type))) {
            fprintf(stderr, 
                "Error: machine type (%s) isn't consistent with target type (%s)\n",
                machine_type, target_info.arch);
        }
    }

    if (target_info.e_version != E_VERSION_CURRENT) {
        fprintf(stderr, "Error: invalid elf file version\n");
    }
}

void print_init_data_section(AOTSection *s) {
    printf("section type: %s\n", AOTSectionType_to_string(s->type));
    printf("section length: %"PRIu32"\n", s->body_size);
    uint8_t *p = s->body;
    uint8_t *p_end = s->body + s->body_size;

    //----- Memory Info -----
    read_uint32(p, p_end, &data.import_memory_count);
    read_uint32(p, p_end, &data.memory_count);

    printf("import memory count: %"PRIu32"\n", data.import_memory_count);
    printf("memory count: %"PRIu32"\n", data.memory_count);

    if (data.memory_count > 0) {
        data.memories = xcalloc(data.memory_count, sizeof(struct WASMMemory));
    }
    for (uint32_t i = 0; i < data.memory_count; i++) {
        read_uint32(p, p_end, &data.memories[i].flags);
        read_uint32(p, p_end, &data.memories[i].num_bytes_per_page);
        read_uint32(p, p_end, &data.memories[i].init_page_count);
        read_uint32(p, p_end, &data.memories[i].max_page_count);

        printf("mem[%"PRIu32"].flags %"PRIu32"\n",
               i, data.memories[i].flags);
        printf("mem[%"PRIu32"].num_bytes_per_page %"PRIu32"\n",
               i, data.memories[i].num_bytes_per_page);
        printf("mem[%"PRIu32"].init_page_count %"PRIu32"\n",
               i, data.memories[i].init_page_count);
        printf("mem[%"PRIu32"].max_page_count %"PRIu32"\n",
               i, data.memories[i].max_page_count);
    }

    read_uint32(p, p_end, &data.mem_init_data_count);
    printf("mem_init_data_count %"PRIu32"\n", data.mem_init_data_count);
    for (uint32_t i = 0; i < data.mem_init_data_count; i++) {
    /* is_passive and memory_index is only used in bulk memory mode */
        uint32_t is_passive;
        uint32_t memory_index;
        read_uint32(p, p_end, &is_passive);
        read_uint32(p, p_end, &memory_index);
        //p = align_ptr(p, 4);
        uint32_t init_expr_type;
        read_uint32(p, p_end, &init_expr_type);

        printf("mem_init_data[%"PRIu32"].is_passive: %"PRIu32"\n", i, is_passive);
        printf("mem_init_data[%"PRIu32"].memory_index: %"PRIu32"\n", i, memory_index);
        printf("mem_init_data[%"PRIu32"].init_expr_type: 0x%x\n", i, init_expr_type);

        switch (init_expr_type) {
        case INIT_EXPR_TYPE_I32_CONST: {
            uint32_t init_expr;
            read_uint32(p, p_end, &init_expr);
            printf("  mem_init_data[%"PRIu32"].init_expr: %"PRIu32"\n", i, init_expr);
        } break;
        default:
            printf("Error: unknown init expr type: %x\n", init_expr_type);
            exit(EXIT_FAILURE);
        }
        uint32_t byte_count;
        read_uint32(p, p_end, &byte_count);
        for (uint32_t i = 0; i < byte_count; i++) {
            printf("0x%x ", p[i]);
        }
        printf("\n");
        p += byte_count;

    }

    //----- Table Info -----
    read_uint32(p, p_end, &data.import_table_count);
    printf("import_table_count %"PRIu32"\n", data.import_table_count);
    if (data.import_table_count > 0) {
        printf("Error: import table count unimplemented\n");
        exit(EXIT_FAILURE);
        return;
    }

    read_uint32(p, p_end, &data.table_count);
    printf("table_count %"PRIu32"\n", data.table_count);
    for (uint32_t i = 0; i < data.table_count; i++) {
        uint8_t table_type;
        uint8_t table_flags;
        /* table_flag possible value:
         * 0: no max size and not shared
         * 1: has max size
         * 2: shared
         * 4: table64
         */
        boolean table_can_grow;
        uint8_t table_init_size;
        /* table_max_size is specified if (flags & 1), else it is 0x10000 */
        uint8_t table_max_size;

        read_uint8(p, p_end, &table_type);
        read_uint8(p, p_end, &table_flags);
        read_uint8(p, p_end, &table_can_grow);
        p += 1;
        read_uint32(p, p_end, &table_init_size);
        read_uint32(p, p_end, &table_max_size);

        printf("  table[%"PRIu32"].type: 0x%x\n", i, table_type);
        printf("  table[%"PRIu32"].flags: %"PRIu8"\n", i, table_flags);
        printf("  table[%"PRIu32"].can_grow: %"PRIu8"\n", i, table_can_grow);
        printf("  table[%"PRIu32"].init_size: %"PRIu8"\n", i, table_init_size);
        printf("  table[%"PRIu32"].max_size: %"PRIu8"\n", i, table_max_size);
    }

    read_uint32(p, p_end, &data.table_init_data_count);
    printf("table_init_data_count %"PRIu32"\n", data.table_init_data_count);
    if (data.table_init_data_count > 0) {
        printf("Error: table init data count unimplemented\n");
        exit(EXIT_FAILURE);
        return;
    }


    //----- Type Info -----
    read_uint32(p, p_end, &data.type_count);
    printf("type_count %"PRIu32"\n", data.type_count);
    if (data.type_count > 0) {
        data.types = xcalloc(data.type_count, sizeof(struct WASMFuncType *));
    }

    for (uint32_t i = 0; i < data.type_count; i++) {
        p = align_ptr(p, 4);
        uint32_t type_flag;
        read_uint16(p, p_end, &type_flag);
        uint32_t param_count;
        read_uint16(p, p_end, &param_count);
        uint32_t result_count;
        read_uint16(p, p_end, &result_count);

        size_t type_size = param_count + result_count;
        size_t size = sizeof(struct WASMFuncType) + type_size;
        data.types[i] = xmalloc(size);
        data.types[i]->param_count = param_count;
        data.types[i]->result_count = result_count;
        if (type_size > 0) {
            read_byte_array(p, p_end, data.types[i]->types, type_size);
        }

        printf("type[%"PRIu32"].param_count %"PRIu32"\n", i, param_count);
        printf("type[%"PRIu32"].result_count %"PRIu32"\n", i, result_count);
        for (uint32_t j = 0; j < type_size; j++) {
            printf("%x\n", data.types[i]->types[j]);
        }
    }

    //----- Import Global Info -----
    read_uint32(p, p_end, &data.import_global_count);
    printf("import global count %"PRIu32"\n", data.import_global_count);
    if (data.import_global_count > 0) {
        assert(0 && "import global count unimplemented");
    }

    //----- Global Info -----
    read_uint32(p, p_end, &data.global_count);
    printf("global count %"PRIu32"\n", data.global_count);
    for (uint32_t i = 0; i < data.global_count; i++) {
        uint8_t val_type;
        uint8_t is_mutable;
        uint32_t init_expr_type;
        read_uint8(p, p_end, &val_type);
        read_uint8(p, p_end, &is_mutable);
        p = align_ptr(p, 4);
        read_uint32(p, p_end, &init_expr_type);
        printf("  global[%"PRIu32"].val_type: 0x%x\n", i, val_type);
        printf("  global[%"PRIu32"].is_mutable: %"PRIu32"\n", i, is_mutable);
        printf("  global[%"PRIu32"].init_expr_type: 0x%x\n", i, init_expr_type);

        switch (init_expr_type) {
        case INIT_EXPR_TYPE_I32_CONST: {
            uint32_t init_expr;
            read_uint32(p, p_end, &init_expr);
            printf("  global[%"PRIu32"].init_expr: %"PRIu32"\n", i, init_expr);
        } break;
        default:
            printf("Error: unknown init expr type: %x\n", init_expr_type);
            exit(EXIT_FAILURE);
        }
    }

    //----- Import Func Info -----
    read_uint32(p, p_end, &data.import_func_count);
    printf("import func count %"PRIu32"\n", data.import_func_count);
    if (data.import_func_count > 0) {
        assert(0 && "import func count unimplemented");
    }

    //----- Extra Info -----
    read_uint32(p, p_end, &data.func_count);
    printf("func count %"PRIu32"\n", data.func_count);

    read_uint32(p, p_end, &data.start_func_index);

    if (data.start_func_index == (uint32_t)-1) {
        printf("start func index NONE\n");
    } else {
        printf("start func index %"PRIu32"\n", data.start_func_index);
        if (data.start_func_index >= data.import_func_count + data.func_count) {
            fprintf(stderr, "Error: invalid start function index\n");
        }
    }

    read_uint32(p, p_end, &data.aux_data_end_global_index);
    read_uint64(p, p_end, &data.aux_data_end);
    read_uint32(p, p_end, &data.aux_heap_base_global_index);
    read_uint64(p, p_end, &data.aux_heap_base);
    read_uint32(p, p_end, &data.aux_stack_top_global_index);
    read_uint64(p, p_end, &data.aux_stack_bottom);
    read_uint32(p, p_end, &data.aux_stack_size);

    if (data.aux_data_end >= MAX_LINEAR_MEMORY_SIZE) {
        fprintf(stderr, "Error: invalid range of aux_date_end\n");
    }

    if (data.aux_heap_base >= MAX_LINEAR_MEMORY_SIZE) {
        fprintf(stderr, "Error: invalid range of aux_heap_base\n");
    }
    if (data.aux_stack_bottom >= MAX_LINEAR_MEMORY_SIZE) {
        fprintf(stderr, "Error: invalid range of aux_stack_bottom");
    }
    printf("data.aux_data_end_global_index = %"PRIu32"\n", data.aux_data_end_global_index);
    printf("data.aux_data_end = %"PRIu64"\n", data.aux_data_end);
    printf("data.aux_heap_base_global_index = %"PRIu32"\n", data.aux_heap_base_global_index);
    printf("data.aux_heap_base = %"PRIu64"\n", data.aux_heap_base);
    printf("data.aux_stack_top_global_index = %"PRIu32"\n", data.aux_stack_top_global_index);
    printf("data.aux_stack_bottom = %"PRIu64"\n", data.aux_stack_bottom);
    printf("data.aux_stack_size = %"PRIu32"\n", data.aux_stack_size);


    read_uint32(p, p_end, &data.data_section_count);
    printf("data section count: %"PRIu32"\n", data.data_section_count);
    if (data.data_section_count > 0) {
        data.data_sections = xcalloc(
            data.data_section_count,
            sizeof(struct AOTObjectDataSection));
    }

    for (uint32_t i = 0; i < data.data_section_count; i++) {
        uint16_t name_len;
        read_uint16(p, p_end, &name_len);
        if (p[name_len-1] != '\0') {
            fprintf(stderr, "Error: the string is not terminated with \\0.");
            exit(EXIT_FAILURE);
        }
        data.data_sections[i].name = (char *)p;
        printf("data_sections[%"PRIu32"].name = %s\n",
               i, data.data_sections[i].name);
        p += name_len;
        read_uint32(p, p_end, &data.data_sections[i].size);
        printf("data_sections[%"PRIu32"].size = %"PRIu32"\n",
               i, data.data_sections[i].size);
        if (data.data_sections[i].size > 0) {
            data.data_sections[i].data = p;
            p += data.data_sections[i].size;
        } else {
            data.data_sections[i].data = NULL;
        }
    }

    if (p != p_end) {
        fprintf(stderr, "Error: invalid init data section size\n");
    }
}

void print_text_section(AOTSection *s) {
    printf("section type: %s\n", AOTSectionType_to_string(s->type));
    printf("section length: %"PRIu32"\n", s->body_size);
    uint8_t *p = s->body;
    uint8_t *p_end = s->body + s->body_size;

    read_uint32(p, p_end, &text.literal_size);
    if (text.literal_size > 0) {
        text.literal = p;
        p += text.literal_size;
    } else {
        text.literal = NULL;
    }
    text.code = p;
    text.code_size = p_end - text.code;
    char *tmp_file_path = "readaot-text.bin";
    FILE *tmp_file;
    if ((tmp_file = fopen(tmp_file_path, "w")) == NULL) {
        fprintf(stderr, "Error: cannot open %s\n", tmp_file_path);
        exit(EXIT_FAILURE);
    }
    if (fwrite(p, 1, text.code_size, tmp_file) != text.code_size) {
        fprintf(stderr, "Error: cannot write to %s\n", tmp_file_path);
        exit(EXIT_FAILURE);
    }
    fclose(tmp_file);
    tmp_file = NULL;

    printf("literal size: %"PRIu32"\n", text.literal_size);
    printf("code size: %"PRIu32"\n", text.code_size);


    char *output_path = "text.bin";
    FILE *f;
    if ((f = fopen(output_path, "w")) == NULL) {
        fprintf(stderr, "Error: cannot open %s\n", output_path);
        exit(EXIT_FAILURE);
    }
    fwrite(text.code, text.code_size, 1, f);
    fclose(f);
}

void print_function_section(AOTSection *s) {
    printf("section type: %s\n", AOTSectionType_to_string(s->type));
    printf("section length: %"PRIu32"\n", s->body_size);
    uint8_t *p = s->body;
    uint8_t *p_end = s->body + s->body_size;

    for (uint32_t i = 0; i < data.func_count; i++) {
        uint32_t text_offset;
        assert((!(target_info.bin_type & 2)) &&
               "The following code works only for 32-bit target machine");
        read_uint32(p, p_end, &text_offset);
        if (text_offset >= text.code_size) {
            fprintf(stderr, "Error: invalid function code offset\n");
            exit(EXIT_FAILURE);
        }
        printf("func[%"PRIu32"] offset: %x\n", i, text_offset);
    }

    for (uint32_t i = 0; i < data.func_count; i++) {
        uint32_t func_type_indexes;
        read_uint32(p, p_end, &func_type_indexes);
        if (func_type_indexes >= data.type_count) {
            fprintf(stderr, "Error: unknown type\n");
            exit(EXIT_FAILURE);
        }
        printf("func[%"PRIu32"] type index: %"PRIu32"\n", i, func_type_indexes);
    }

    for (uint32_t i = 0; i < data.func_count; i++) {
        // Ignore max_local_cell_num and max_stack_cell_num of each function
        uint32_t max_local_cell_num;
        uint32_t max_stack_cell_num;
        read_uint32(p, p_end, &max_local_cell_num);
        read_uint32(p, p_end, &max_stack_cell_num);
        printf("func[%"PRIu32"] max local cell num: %"PRIu32"\n", i, max_local_cell_num);
        printf("func[%"PRIu32"] max stack cell num: %"PRIu32"\n", i, max_stack_cell_num);
    }

    if (p != p_end) {
        fprintf(stderr, "Error: invalid function section size\n");
        exit(EXIT_FAILURE);
    }
}

void print_export_section(AOTSection *s) {
    printf("section type: %s\n", AOTSectionType_to_string(s->type));
    printf("section length: %"PRIu32"\n", s->body_size);
    uint8_t *p = s->body;
    uint8_t *p_end = s->body + s->body_size;

    uint32_t export_count;
    read_uint32(p, p_end, &export_count);
    printf("export count %"PRIu32"\n", export_count);

    WASMExport export;
    for (uint32_t i = 0; i < export_count; i++) {
        read_uint32(p, p_end, &export.index);
        read_uint8(p, p_end, &export.kind);
        uint16_t name_len;
        read_uint16(p, p_end, &name_len);
        export.name = (char *)p;
        p += name_len;

        printf("export[%"PRIu32"].name_len: %"PRIi32"\n", i, name_len);
        printf("export[%"PRIu32"].name: %*s\n", i, name_len, export.name);
        printf("export[%"PRIu32"].kind: %"PRIu8"\n", i, export.kind);
        printf("export[%"PRIu32"].index: %"PRIu32"\n", i, export.index);
    }

    if (p != p_end) {
        fprintf(stderr, "Error: invalid export section size\n");
        exit(EXIT_FAILURE);
    }
}

void print_relocation_section(AOTSection *s) {
    printf("section type: %s\n", AOTSectionType_to_string(s->type));
    printf("section length: %"PRIu32"\n", s->body_size);
    uint8_t *p = s->body;
    uint8_t *p_end = s->body + s->body_size;

    uint32_t symbol_count;
    read_uint32(p, p_end, &symbol_count);
    printf("symbol count: %"PRIu32"\n", symbol_count);

    uint32_t *symbol_offsets = (uint32_t *)p;
    for (uint32_t i = 0; i < symbol_count; i++) {
        printf("symbol %"PRIu32" offset: %"PRIu32"\n", i, symbol_offsets[i]);
    }
    p += symbol_count * sizeof(uint32_t);

    uint32_t total_string_len;
    read_uint32(p, p_end, &total_string_len);
    printf("total string len: %"PRIu32"\n", total_string_len);
    uint8_t *symbol_buf = (uint8_t *)p;
    uint8_t *symbol_buf_end = symbol_buf + total_string_len;
    p += total_string_len;

    // symbol table validation
    uint8_t *psym = symbol_buf;
    uint32_t str_len_addr = 0;
    for (uint32_t i = 0; i < symbol_count; i++) {
        assert(symbol_offsets[i] == str_len_addr);
        uint16_t str_len;
        read_uint16(psym, symbol_buf_end, &str_len);
        str_len_addr += sizeof(uint16_t) + str_len;
        str_len_addr = (str_len_addr + 1) & ~1;
        psym += str_len;
        psym = align_ptr(psym, 2);
    }
    assert(psym == symbol_buf_end);
    // end symbol table validation
    
    for (uint32_t i = 0; i < symbol_count; i++) {
        uint8_t *symbol_addr = symbol_buf + symbol_offsets[i];
        uint16_t str_len;
        read_uint16(symbol_addr, symbol_buf_end, &str_len);
        printf("symbol %"PRIu32": %s\n", i, (char *)symbol_addr);
    }

    uint32_t group_count;
    read_uint32(p, p_end, &group_count);
    printf("relocation group count: %"PRIu32"\n", group_count);

    for (uint32_t i = 0; i < group_count; i++) {
        AOTRelocationGroup group;

        /* section name address is 4 bytes aligned. */
        p = align_ptr(p, sizeof(uint32_t));
        read_uint32(p, p_end, &group.name_index);
        printf("name_index = %"PRIu32"\n", group.name_index);

        if (group.name_index >= symbol_count) {
            fprintf(stderr, "Error: symbol index out of range\n");
            exit(EXIT_FAILURE);
        }
        uint8_t *name_addr = symbol_buf + symbol_offsets[group.name_index] + sizeof(uint16_t);
        group.section_name = (char *)name_addr;
        read_uint32(p, p_end, &group.relocation_count);
        printf("group[%"PRIu32"].section_name: %s\n", i, group.section_name);
        printf("group[%"PRIu32"].relocation_count: %"PRIu32"\n", i, group.relocation_count);

        for (uint32_t j = 0; j < group.relocation_count; j++) {
            assert((!(target_info.bin_type & 2)) &&
               "The following code works only for 32-bit target machine");

            AOTRelocation relocation;
            read_uint32(p, p_end, &relocation.offset);
            read_uint32(p, p_end, &relocation.addend);
            read_uint32(p, p_end, &relocation.type);
            read_uint32(p, p_end, &relocation.symbol_index);

            if (relocation.symbol_index >= symbol_count) {
                fprintf(stderr, "Error: symbol index out of range. The relocation symbol "
                    "index is %"PRIu32" but the symbol count is %"PRIu32"\n", relocation.symbol_index, symbol_count);
                exit(EXIT_FAILURE);
            }

            uint8_t * name_addr = symbol_buf + symbol_offsets[relocation.symbol_index] + sizeof(uint16_t);
            relocation.symbol_name = (char *)name_addr;

            printf("  relocation[%"PRIu32"].offset: 0x%x\n", j, relocation.offset);
            printf("  relocation[%"PRIu32"].addend: %"PRIu32"\n", j, relocation.addend);
            printf("  relocation[%"PRIu32"].type: %"PRIu32"\n", j, relocation.type);
            printf("  relocation[%"PRIu32"].symbol_index: %"PRIu32"\n", j, relocation.symbol_index);
            printf("  relocation[%"PRIu32"].symbol_name: %s\n\n", j, relocation.symbol_name);
        }
    }

    if (p != p_end) {
        fprintf(stderr, "Error: invalid relocation section size\n");
        exit(EXIT_FAILURE);
    }

}

void print_custom_section(AOTSection *s) {
    printf("section type: %s\n", AOTSectionType_to_string(s->type));
    printf("section length: %"PRIu32"\n", s->body_size);
}

void print_sections(AOTSection *section_list) {
    AOTSection *s;
    for (s = section_list; s != NULL; s = s->next) {
        switch (s->type) {
            case AOT_SECTION_TYPE_TARGET_INFO:
                print_target_info_section(s);
                break;

            case AOT_SECTION_TYPE_INIT_DATA:
                print_init_data_section(s);
                break;

            case AOT_SECTION_TYPE_TEXT:
                print_text_section(s);
                break;

            case AOT_SECTION_TYPE_FUNCTION:
                print_function_section(s);
                break;

            case AOT_SECTION_TYPE_EXPORT:
                print_export_section(s);
                break;

            case AOT_SECTION_TYPE_RELOCATION:
                print_relocation_section(s);
                break;

            case AOT_SECTION_TYPE_CUSTOM:
                print_custom_section(s);
                break;

            default:
                assert(0);
        }
        printf("\n");
    }
}


int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Error: provide input file\n");
        return EXIT_FAILURE;
    }

    char *input_path = argv[1];
    FILE *f;
    if ((f = fopen(input_path, "r")) == NULL) {
        fprintf(stderr, "Error: cannot open %s\n", input_path);
        return EXIT_FAILURE;
    }

    fseek(f, 0, SEEK_END);
    unsigned long buf_size  = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = xmalloc(buf_size);
    if (fread(buf, 1, buf_size, f) != buf_size) {
        fprintf(stderr, "Error: cannot read the entire file %s\n", input_path);
        return EXIT_FAILURE;
    }
    fclose(f);
    f = NULL;

    uint8_t *p = buf;
    uint8_t *p_end = buf + buf_size;

    uint32_t magic_number;
    read_uint32(p, p_end, &magic_number);
    if (magic_number != AOT_MAGIC_NUMBER) {
        fprintf(stderr, "Error: wrong magic header number, expected %x, got %x\n", AOT_MAGIC_NUMBER, magic_number);
        return EXIT_FAILURE;
    }

    uint32_t version;
    read_uint32(p, p_end, &version);
    if (version != AOT_CURRENT_VERSION) {
        fprintf(stderr, "Error: wrong aot file version\n");
        return EXIT_FAILURE;
    }
    printf("version: %"PRIu32"\n", version);

    AOTSection *sec_list = parse_sections(p, p_end);
    for (AOTSection *s = sec_list; s != NULL; s = s->next) {
        printf("section: %s, body_size: %"PRIu32"\n", AOTSectionType_to_string(s->type), s->body_size);
    }

    if (!check_sections_order(sec_list)) {
        fprintf(stderr, "Error: wrong section order\n");
        return EXIT_FAILURE;
    }
    print_sections(sec_list);

    return EXIT_SUCCESS;
}
