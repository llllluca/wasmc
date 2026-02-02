#include <stdio.h>
#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include "listx.h"
#include "aot.h"

typedef struct AOTSection {
    struct list_head link;
    AOTSectionType type;
    uint8_t *start;
    uint8_t *end;
} AOTSection;

char *AOTSectionType_to_string(AOTSectionType sec_type) {
    switch (sec_type) {
        case AOT_SECTION_TYPE_TARGET_INFO: return "Target Info";
        case AOT_SECTION_TYPE_INIT_DATA:   return "Init Data";
        case AOT_SECTION_TYPE_TEXT:        return "Text";
        case AOT_SECTION_TYPE_FUNCTION:    return "Function";
        case AOT_SECTION_TYPE_EXPORT:      return "Export";
        case AOT_SECTION_TYPE_RELOCATION:  return "Relocation";
        case AOT_SECTION_TYPE_CUSTOM:      return "Custom";
        default:
            fprintf(stderr, "Error: %d is an invalid section type\n", sec_type);
            exit(EXIT_FAILURE);
    }
}

#define READ_BYTE_ARRAY(offset, end, addr, len) \
    do { \
        if((unsigned long)(end - offset) < len) { \
            fprintf(stderr, "Error: out of bound read in the AOT file\n"); \
            exit(EXIT_FAILURE); \
        } \
        memcpy(addr, offset, len); \
        offset += len; \
    } while (0)

#define ALIGN_PTR(p, align) ((void *)((((uintptr_t)p) + (align-1)) & ~(align-1)))

#define AOT_TEMPLATE_READ(offset, end, result, type) \
    do { \
        if ((long unsigned int)(end - offset) < sizeof(type)) { \
            fprintf(stderr, "Error: out of bound read in the AOT file\n"); \
            exit(EXIT_FAILURE); \
        } \
        *(result) = *(type *)(offset); \
        offset += sizeof(type); \
    } while (0)

#define READ_UINT64(offset, end, result) \
    AOT_TEMPLATE_READ(offset, end, result, uint64_t)

#define READ_UINT32(offset, end, result) \
    AOT_TEMPLATE_READ(offset, end, result, uint32_t)

#define READ_UINT16(offset, end, result) \
    AOT_TEMPLATE_READ(offset, end, result, uint16_t)

#define READ_UINT8(offset, end, result)  \
    AOT_TEMPLATE_READ(offset, end, result, uint8_t)

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

char *bin_type_to_string(uint16_t bin_type) {
    switch (bin_type) {
        case AOT_BIN_TYPE_ELF32L: return "BIN_TYPE_ELF32L";
        case AOT_BIN_TYPE_ELF32B: return "BIN_TYPE_ELF32B";
        case AOT_BIN_TYPE_ELF64L: return "BIN_TYPE_ELF64L";
        case AOT_BIN_TYPE_ELF64B: return "BIN_TYPE_ELF64B";
        case AOT_BIN_TYPE_COFF32: return "BIN_TYPE_COFF32";
        case AOT_BIN_TYPE_COFF64: return "BIN_TYPE_COFF64";
        default:
            fprintf(stderr, "Error: %"PRIu16" is an invalid bin_type\n", bin_type);
            exit(EXIT_FAILURE);
    }
}

char *e_type_to_string(uint16_t e_type) {
    switch (e_type) {
        case AOT_E_TYPE_REL: return "E_TYPE_REL";
        case AOT_E_TYPE_XIP: return "E_TYPE_XIP";
        default:
            fprintf(stderr, "Error: %"PRIu16" is an invalid e_type\n", e_type);
            exit(EXIT_FAILURE);

    }
}

char *e_machine_to_string(uint16_t e_machine) {
    switch (e_machine) {
        case AOT_E_MACHINE_X86_64:       return "E_MACHINE_X86_64";
        case AOT_E_MACHINE_WIN_X86_64:   return "E_MACHINE_WIN_X86_64";
        case AOT_E_MACHINE_386:          return "E_MACHINE_386";
        case AOT_E_MACHINE_WIN_I386:     return "E_MACHINE_WIN_I386";
        case AOT_E_MACHINE_ARM:          return "E_MACHINE_ARM";
        case AOT_E_MACHINE_AARCH64:      return "E_MACHINE_AARCH64";
        case AOT_E_MACHINE_MIPS:         return "E_MACHINE_MIPS";
        case AOT_E_MACHINE_XTENSA:       return "E_MACHINE_XTENSA";
        case AOT_E_MACHINE_RISCV:        return "E_MACHINE_RISCV";
        case AOT_E_MACHINE_ARC_COMPACT:  return "E_MACHINE_ARC_COMPACT";
        case AOT_E_MACHINE_ARC_COMPACT2: return "E_MACHINE_ARC_COMPACT2";
        default:
            fprintf(stderr, "Error: %"PRIu16" is an invalid e_machine\n", e_machine);
            exit(EXIT_FAILURE);
    }
}

char *e_version_to_string(uint16_t e_version) {

    switch (e_version) {
        case AOT_E_VERSION_CURRENT: return "E_VERSION_CURRENT";
        default:
            fprintf(stderr, "Error: %"PRIu16" is an invalid e_version\n", e_version);
            exit(EXIT_FAILURE);
    }
}

int is_e_machine_matching_arch(uint16_t e_machine, char *arch) {

    char *name;
    switch (e_machine) {
        case AOT_E_MACHINE_X86_64:
        case AOT_E_MACHINE_WIN_X86_64:
            name = "x86_64";
            break;
        case AOT_E_MACHINE_386:
        case AOT_E_MACHINE_WIN_I386:
            name = "i386";
            break;
        case AOT_E_MACHINE_ARM:
        case AOT_E_MACHINE_AARCH64:
            return 0;
        case AOT_E_MACHINE_MIPS:
            name = "mips";
            break;
        case AOT_E_MACHINE_XTENSA:
            name = "xtensa";
            break;
        case AOT_E_MACHINE_RISCV:
            name = "riscv";
            break;
        case AOT_E_MACHINE_ARC_COMPACT:
        case AOT_E_MACHINE_ARC_COMPACT2:
            name = "arc";
            break;
        default:
            return 0;
    }
    return strncmp(name, arch, 16);
}

static void print_target_info(AOTSection *s) {

    char *section_name = AOTSectionType_to_string(s->type);
    printf("\n*=== %s ===*\n", section_name);
    printf("Length: %ld\n", s->end - s->start);

    AOTTargetInfo t;
    uint8_t *offset = s->start;

    READ_UINT16(offset, s->end, &t.bin_type);
    READ_UINT16(offset, s->end, &t.abi_type);
    READ_UINT16(offset, s->end, &t.e_type);
    READ_UINT16(offset, s->end, &t.e_machine);
    READ_UINT32(offset, s->end, &t.e_version);
    READ_UINT32(offset, s->end, &t.e_flags);
    READ_UINT64(offset, s->end, &t.feature_flags);
    READ_UINT64(offset, s->end, &t.reserved);
    READ_BYTE_ARRAY(offset, s->end, t.arch, sizeof(t.arch));

    if (offset != s->end) {
        fprintf(stderr, "Error: section %s has an invalid section size\n", section_name);
        exit(EXIT_FAILURE);
    }

    printf("bin_type: %"PRIu16" (%s)\n", t.bin_type, bin_type_to_string(t.bin_type));
    printf("abi_type: %"PRIu16"\n", t.abi_type);
    printf("e_type: %"PRIu16" (%s)\n", t.e_type, e_type_to_string(t.e_type));
    printf("e_machine: %"PRIu16" (%s)\n", t.e_machine, e_machine_to_string(t.e_machine));
    printf("e_version: %"PRIu16" (%s)\n", t.e_version, e_version_to_string(t.e_version));
    printf("e_flags: %"PRIu16"\n", t.e_flags);
    printf("features_flag: %"PRIu64"\n", t.feature_flags);
    printf("reserved: %"PRIu64"\n", t.reserved);
    printf("arch: %s\n", t.arch);

    if (!is_e_machine_matching_arch(t.e_machine, t.arch)) {
        fprintf(stderr, "Error: e_machine (%"PRIu16") isn't consistent with arch (%s)\n",
                t.e_machine, t.arch);
        exit(EXIT_FAILURE);
    }
}

static void print_memory(AOTSection *s, uint8_t **offset) {

    uint8_t *p = *offset;

    printf("\n*--- Memory subsection ---*\n");

    p = ALIGN_PTR(p, 4);
    uint32_t import_memory_count, memory_count;
    READ_UINT32(p, s->end, &import_memory_count);
    READ_UINT32(p, s->end, &memory_count);

    printf("Import memory count: %"PRIu32"\n", import_memory_count);
    if (import_memory_count != 0) {
        fprintf(stderr, "Error: import memory is not implemented\n");
        exit(EXIT_FAILURE);
    }
    printf("\nMemory count: %"PRIu32"\n", memory_count);

    uint32_t flags, num_bytes_per_page, init_page_count, max_page_count;
    for (uint32_t i = 0; i < memory_count; i++) {
        READ_UINT32(p, s->end, &flags);
        READ_UINT32(p, s->end, &num_bytes_per_page);
        READ_UINT32(p, s->end, &init_page_count);
        READ_UINT32(p, s->end, &max_page_count);

        printf("Memory[%"PRIu32"]\n", i);
        printf("  Flags: %"PRIu32"\n", flags);
        printf("  Bytes per page: %"PRIu32"\n", num_bytes_per_page);
        printf("  Init page count: %"PRIu32"\n", init_page_count);
        printf("  Max page count: %"PRIu32"\n", max_page_count);
    }

    uint32_t data_segment_count, is_passive, memory_index,
             i32const, linear_memory_index, byte_count;
    READ_UINT32(p, s->end, &data_segment_count);
    printf("\nData segment count: %"PRIu32"\n", data_segment_count);
    for (uint32_t i = 0; i < data_segment_count; i++) {

        p = ALIGN_PTR(p, 4);
        READ_UINT32(p, s->end, &is_passive);
        READ_UINT32(p, s->end, &memory_index);
        READ_UINT32(p, s->end, &i32const);
        READ_UINT32(p, s->end, &linear_memory_index);
        READ_UINT32(p, s->end, &byte_count);

        printf("Data segment[%"PRIu32"]\n", i);
        printf("  Is passive: %"PRIu32"\n", is_passive);
        printf("  Memory index: %"PRIu32"\n", memory_index);
        if (memory_index >= memory_count) {
            fprintf(stderr, "Error: memory index must be less than memory count\n");
            exit(EXIT_FAILURE);
        }
        if (i32const != 0x41) {
            char *b = (char *)&i32const;
            fprintf(stderr, "Error: extected 0x41 0x00 0x00 0x00 before "
                    "linear memory index but got 0x%2x 0x%2x 0x%2x 0x%2x\n",
                    b[0], b[1], b[2], b[3]);
            exit(EXIT_FAILURE);
        }
        printf("  Linear memory index: %"PRIu32"\n", linear_memory_index);
        printf("  Buffer byte count: %"PRIu32"\n", byte_count);
        p += byte_count;
    }
    *offset = p;
}

static void print_table(AOTSection *s, uint8_t **offset) {

    uint8_t *p = *offset;

    printf("\n*--- Table subsection ---*\n");
    printf("TODO: print_table() is not implemented!\n");

    p = ALIGN_PTR(p, 4);
    uint32_t import_table_count;
    READ_UINT32(p, s->end, &import_table_count);
    if (import_table_count != 0) {
        printf("Error: import table is not implemented\n");
        exit(EXIT_FAILURE);
    }

    uint32_t table_count;
    READ_UINT32(p, s->end, &table_count);

    uint8_t table_type;
    uint8_t table_flags;
    uint8_t table_can_grow;
    uint8_t table_init_size;
    uint8_t table_max_size;

    for (uint32_t i = 0; i < table_count; i++) {
        READ_UINT8(p, s->end, &table_type);
        READ_UINT8(p, s->end, &table_flags);
        READ_UINT8(p, s->end, &table_can_grow);
        p += 1;
        READ_UINT32(p, s->end, &table_init_size);
        READ_UINT32(p, s->end, &table_max_size);
    }

    uint32_t table_init_data_count;
    READ_UINT32(p, s->end, &table_init_data_count);
    if (table_init_data_count > 0) {
        printf("Error: table init data is not implemented\n");
        exit(EXIT_FAILURE);
        return;
    }
    *offset = p;
}

static void print_type(AOTSection *s, uint8_t **offset) {

    uint8_t *p = *offset;

    printf("\n*--- Type subsection ---*\n");

    p = ALIGN_PTR(p, 4);
    uint32_t type_count;
    READ_UINT32(p, s->end, &type_count);
    printf("Type count: %"PRIu32"\n", type_count);

    uint16_t type_flag, param_count, result_count;
    for (uint32_t i = 0; i < type_count; i++) {
        p = ALIGN_PTR(p, 4);
        READ_UINT16(p, s->end, &type_flag);
        READ_UINT16(p, s->end, &param_count);
        READ_UINT16(p, s->end, &result_count);

        printf("Type[%"PRIu32"]\n", i);
        printf("  Type flag: %"PRIu16"\n", type_flag);
        printf("  Param count: %"PRIu16"\n", param_count);
        printf("  Result count: %"PRIu16"\n", result_count);

        printf("  Param:");
        for (uint32_t j = 0; j < param_count; j++) {
            printf(" %x", p[j]);
        }
        printf("\n");
        p += param_count;

        printf("  Result:");
        for (uint32_t j = 0; j < result_count; j++) {
            printf(" %x", p[j]);
        }
        printf("\n");
        p += result_count;
    }
    *offset = p;
}


static void print_import_global(AOTSection *s, uint8_t **offset) {

    uint8_t *p = *offset;

    printf("\n*--- Import Global subsection ---*\n");
    printf("TODO: print_import_global() is not implemented!\n");

    p = ALIGN_PTR(p, 4);
    uint32_t import_global_count;
    READ_UINT32(p, s->end, &import_global_count);
    if (import_global_count != 0) {
        printf("Error: import global is not implemented\n");
        exit(EXIT_FAILURE);
    }
    *offset = p;
}

static void print_global(AOTSection *s, uint8_t **offset) {

    uint8_t *p = *offset;

    printf("\n*--- Global subsection ---*\n");

    p = ALIGN_PTR(p, 4);
    uint32_t global_count;
    READ_UINT32(p, s->end, &global_count);
    printf("global count %"PRIu32"\n", global_count);
    uint8_t val_type;
    uint8_t is_mutable;
    uint32_t init_expr_type;

    for (uint32_t i = 0; i < global_count; i++) {
        READ_UINT8(p, s->end, &val_type);
        READ_UINT8(p, s->end, &is_mutable);
        p = ALIGN_PTR(p, 4);
        READ_UINT32(p, s->end, &init_expr_type);

        printf("Global[%"PRIu32"]\n", i);
        printf("  WASM value type: 0x%x\n", val_type);
        printf("  Is mutable: %"PRIu32"\n", is_mutable);
        printf("  Init expr type: 0x%x\n", init_expr_type);

        switch (init_expr_type) {
        case 0x41: {
            uint32_t init_expr;
            READ_UINT32(p, s->end, &init_expr);
            printf("  Init expr: %"PRIu32"\n", init_expr);
        } break;
        default:
            printf("Error: unknown init expr type: %x\n", init_expr_type);
            exit(EXIT_FAILURE);
        }
    }
    *offset = p;
}

static void print_import_function(AOTSection *s, uint8_t **offset) {

    uint8_t *p = *offset;

    printf("\n*--- Import Function subsection ---*\n");
    printf("TODO: print_import_function() is not implemented!\n");

    p = ALIGN_PTR(p, 4);
    uint32_t import_function_count;
    READ_UINT32(p, s->end, &import_function_count);
    if (import_function_count != 0) {
        printf("Error: import function is not implemented\n");
        exit(EXIT_FAILURE);
    }
    *offset = p;
}

static void print_auxiliary(AOTSection *s, uint8_t **offset) {

    uint8_t *p = *offset;

    printf("\n*--- Auxiliary subsection ---*\n");
    p = ALIGN_PTR(p, 4);

    uint32_t func_count, start_func_index;
    READ_UINT32(p, s->end, &func_count);
    READ_UINT32(p, s->end, &start_func_index);

    printf("Function count: %"PRIu32"\n", func_count);
    printf("start function index: ");
    if (start_func_index == (uint32_t)-1) {
        printf("NONE\n");
    } else {
        printf("%"PRIu32"\n", start_func_index);
    }

    uint32_t data_end_global_index, heap_base_global_index,
             stack_ptr_global_index, stack_size;
    uint64_t data_end, heap_base, stack_ptr;
    READ_UINT32(p, s->end, &data_end_global_index);
    READ_UINT64(p, s->end, &data_end);
    READ_UINT32(p, s->end, &heap_base_global_index);
    READ_UINT64(p, s->end, &heap_base);
    READ_UINT32(p, s->end, &stack_ptr_global_index);
    READ_UINT64(p, s->end, &stack_ptr);
    READ_UINT32(p, s->end, &stack_size);

    printf("\"__data_end\" global variable index: %"PRIu32"\n", data_end_global_index);
    printf("Data end linear memory index: %"PRIu64"\n", data_end);
    printf("\"__heap_base\" global variable index: %"PRIu32"\n", heap_base_global_index);
    printf("heap base linear memory index: %"PRIu64"\n", heap_base);
    printf("stack pointer global variable index: %"PRIu32"\n", stack_ptr_global_index);
    printf("stack pointer linear memory index: %"PRIu64"\n", stack_ptr);
    printf("stack size: %"PRIu32"\n", stack_size);

    *offset = p;
}

static void print_object_data(AOTSection *s, uint8_t **offset) {

    uint8_t *p = *offset;

    printf("\n*--- Object Data subsection ---*\n");
    printf("TODO: print_object_data() is not implemented!\n");

    p = ALIGN_PTR(p, 4);
    uint32_t object_data_count;
    READ_UINT32(p, s->end, &object_data_count);

    uint16_t name_len;
    uint32_t size;
    for (uint32_t i = 0; i < object_data_count; i++) {
        p = ALIGN_PTR(p, 2);
        READ_UINT16(p, s->end, &name_len);
        p += name_len;
        p = ALIGN_PTR(p, 4);
        READ_UINT32(p, s->end, &size);
        p += size;
    }
    *offset = p;
}

static void print_init_data(AOTSection *s) {
    char *section_name = AOTSectionType_to_string(s->type);
    printf("\n*=== %s ===*\n", section_name);

    if (s->start == s->end) return;
    uint8_t *offset = s->start;
    print_memory(s, &offset);
    print_table(s, &offset);
    print_type(s, &offset);
    print_import_global(s, &offset);
    print_global(s, &offset);
    print_import_function(s, &offset);
    print_auxiliary(s, &offset);
    print_object_data(s, &offset);

    if (offset != s->end) {
        fprintf(stderr, "Error: invalid init data section size\n");
        exit(EXIT_FAILURE);
    }
}

void print_text(AOTSection *s) {
    char *section_name = AOTSectionType_to_string(s->type);
    printf("\n*=== %s ===*\n", section_name);

    uint8_t *offset = s->start;

    uint32_t literal_size;
    READ_UINT32(offset, s->end, &literal_size);
    if (literal_size > 0) {
        offset += literal_size;
    }

    uint8_t *code = offset;
    uint32_t code_size = s->end - code;
    #define OUTPUT_PATH "/tmp/text.bin"

    FILE *f;
    if ((f = fopen(OUTPUT_PATH, "w")) == NULL) {
        fprintf(stderr, "Error: cannot open "OUTPUT_PATH"\n");
        exit(EXIT_FAILURE);
    }

    fwrite(code, code_size, 1, f);
    fclose(f);

    char *cmd = "riscv32-unknown-linux-gnu-objdump"
        " -b binary -m riscv:rv32 -D "OUTPUT_PATH;
    system(cmd);

    if (remove(OUTPUT_PATH) != 0) {
        fprintf(stderr, "Error: unable to remove "OUTPUT_PATH"\n");
    }
}

void print_function(AOTSection *s) {

    char *section_name = AOTSectionType_to_string(s->type);
    printf("\n*=== %s ===*\n", section_name);

    uint8_t *offset = s->start;

    uint32_t func_count = (s->end - s->start) / (4 * sizeof(uint32_t));

    uint32_t text_offset;
    for (uint32_t i = 0; i < func_count; i++) {
        READ_UINT32(offset, s->end, &text_offset);
        printf("Func[%"PRIu32"] offset: %x\n", i, text_offset);
    }

    uint32_t type_index;
    for (uint32_t i = 0; i < func_count; i++) {
        READ_UINT32(offset, s->end, &type_index);
        printf("Func[%"PRIu32"] type index: %"PRIu32"\n", i, type_index);
    }

    for (uint32_t i = 0; i < func_count; i++) {
        // Ignore max_local_cell_num and max_stack_cell_num of each function
        uint32_t max_local_cell_num;
        uint32_t max_stack_cell_num;
        READ_UINT32(offset, s->end, &max_local_cell_num);
        READ_UINT32(offset, s->end, &max_stack_cell_num);
    }

    if (offset != s->end) {
        fprintf(stderr, "Error: invalid function section size\n");
        exit(EXIT_FAILURE);
    }
}

void print_export(AOTSection *s) {
    char *section_name = AOTSectionType_to_string(s->type);
    printf("\n*=== %s ===*\n", section_name);
    //printf("Length: %ld\n", s->end - s->start);
    uint8_t *offset = s->start;

    uint32_t export_count;
    READ_UINT32(offset, s->end, &export_count);
    printf("Export count: %"PRIu32"\n", export_count);

    uint32_t index;
    uint8_t kind;
    uint16_t name_len;
    char *name;
    for (uint32_t i = 0; i < export_count; i++) {
        printf("Export[%"PRIu32"]\n", i);
        offset = ALIGN_PTR(offset, 4);
        READ_UINT32(offset, s->end, &index);
        READ_UINT8(offset, s->end, &kind);
        offset = ALIGN_PTR(offset, 2);
        READ_UINT16(offset, s->end, &name_len);
        /* name is '\0' terminated and name_len count also the final '\0' */
        name = (char *)offset;
        offset += name_len;

        printf("  Index: %"PRIu32"\n", index);
        printf("  Kind: %"PRIu8"\n", kind);
        printf("  Name length: %"PRIi32"\n", name_len);
        if (name[name_len-1] != '\0') {
            fprintf(stderr, "Error: exported name is not '\\0' terminated\n");
            exit(EXIT_FAILURE);
        }
        printf("  Name: %s\n", name);
    }

    if (offset != s->end) {
        fprintf(stderr, "Error: invalid export section size\n");
        exit(EXIT_FAILURE);
    }
}

#if 0
void print_relocation(AOTSection *s) {
    char *section_name = AOTSectionType_to_string(s->type);
    printf("\n*=== %s ===*\n", section_name);

    uint8_t *offset = s->start;

    uint32_t symbol_count;
    READ_UINT32(offset, s->end, &symbol_count);
    printf("Symbol count: %"PRIu32"\n", symbol_count);

    uint32_t *symbol_offset = (uint32_t *)offset;
    for (uint32_t i = 0; i < symbol_count; i++) {
        printf("Symbol[%"PRIu32"] offset: %"PRIu32"\n", i, symbol_offset[i]);
    }
    offset += symbol_count * sizeof(uint32_t);

    uint32_t total_string_len;
    READ_UINT32(offset, s->end, &total_string_len);
    printf("Total string len: %"PRIu32"\n", total_string_len);
    uint8_t *symbol_buf = (uint8_t *)offset;
    uint8_t *symbol_buf_end = symbol_buf + total_string_len;
    offset += total_string_len;

    for (uint32_t i = 0; i < symbol_count; i++) {
        uint8_t *symbol = symbol_buf + symbol_offset[i];
        uint16_t len = *(uint16_t *)symbol;
        char *name = (char *)symbol + sizeof(uint16_t);
        printf("symbol[%"PRIu32"]: %.*s\n", i, len, name);
    }

    offset = ALIGN_PTR(offset, 4);
    uint32_t group_count;
    READ_UINT32(offset, s->end, &group_count);
    printf("Relocation group count: %"PRIu32"\n", group_count);

    AOTRelocationGroup group;
    for (uint32_t i = 0; i < group_count; i++) {

        offset = ALIGN_PTR(offset, 4);
        READ_UINT32(p, p_end, &group.name_index);
        printf("name_index = %"PRIu32"\n", group.name_index);

        if (group.name_index >= symbol_count) {
            fprintf(stderr, "Error: symbol index out of range\n");
            exit(EXIT_FAILURE);
        }
        uint8_t *name_addr = symbol_buf + symbol_offsets[group.name_index] + sizeof(uint16_t);
        group.section_name = (char *)name_addr;
        READ_UINT32(p, p_end, &group.relocation_count);
        printf("group[%"PRIu32"].section_name: %s\n", i, group.section_name);
        printf("group[%"PRIu32"].relocation_count: %"PRIu32"\n", i, group.relocation_count);

        for (uint32_t j = 0; j < group.relocation_count; j++) {
            assert((!(target_info.bin_type & 2)) &&
               "The following code works only for 32-bit target machine");

            AOTRelocation relocation;
            READ_UINT32(p, p_end, &relocation.offset);
            READ_UINT32(p, p_end, &relocation.addend);
            READ_UINT32(p, p_end, &relocation.type);
            READ_UINT32(p, p_end, &relocation.symbol_index);

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
#endif

void print_usage(FILE *f) {
    char *usage = 
    "Usage: readaot <option(s)> path/to/file.aot\n"
    " Display information about the contents of AOT format file\n"
    " Options are:\n\n"
    "  -H --help         Display this message\n\n"
    "  -a --all          Equivalent to: -h -i -f -e -r\n\n"
    "  -x --hexdump      Set the current display format to hexdump\n\n"
    "  -p --print        Set the current display format to printable text\n\n"
    "  -h --target-info  Display the AOT target info section according to the current\n"
    "                    display format. If the current display format is not set,\n"
    "                    the section is displayed with printable text format\n\n"
    "  -i --init-data    Display the AOT init data section according to the current\n"
    "                    display format. If the current display format is not set,\n"
    "                    the section is displayed with printable text format\n\n"
    "  -t --text         Display the AOT text section according to the current\n"
    "                    display format. If the current display format is not set,\n"
    "                    the section is displayed with hexdump format\n\n"
    "  -f --function     Display the AOT function section according to the current\n"
    "                    display format. If the current display format is not set,\n"
    "                    the section is displayed with printable text format\n\n"
    "  -e --export       Display the AOT export section according to the current\n"
    "                    display format. If the current display format is not set,\n"
    "                    the section is displayed with printable text format\n\n"
    "  -r --relocation   Display the AOT relocation section according to the current\n"
    "                    display format. If the current display format is not set,\n"
    "                    the section is displayed with printable text format\n\n"
    "  If no option are provided, the default one is --all\n";
    fprintf(f, "%s\n", usage);
}

typedef enum DisplayFormat {
    DISPLAY_FORMAT_DEFAULT,
    DISPLAY_FORMAT_PRINTABLE_TEXT,
    DISPLAY_FORMAT_HEXDUMP,
} DisplayFormat;

typedef struct DisplayAction {
    struct list_head link;
    AOTSectionType section;
    DisplayFormat format;
} DisplayAction;

static void append_action(AOTSectionType section, DisplayFormat format,
                          struct list_head *action_list) {

    DisplayAction *action = xmalloc(sizeof(struct DisplayAction));
    action->section = section;
    if (format == DISPLAY_FORMAT_DEFAULT) {
        if (section == AOT_SECTION_TYPE_TEXT) {
            action->format = DISPLAY_FORMAT_HEXDUMP;
        } else {
            action->format = DISPLAY_FORMAT_PRINTABLE_TEXT;
        }
    }
    action->format = format;
    list_add_tail(&action->link, action_list);
}

static void all_actions(DisplayFormat current_format, struct list_head *action_list) {

    AOTSectionType section;
    section = AOT_SECTION_TYPE_TARGET_INFO;
    append_action(section, current_format, action_list);
    section = AOT_SECTION_TYPE_INIT_DATA;
    append_action(section, current_format, action_list);
    section = AOT_SECTION_TYPE_FUNCTION;
    append_action(section, current_format, action_list);
    section = AOT_SECTION_TYPE_EXPORT;
    append_action(section, current_format, action_list);
    section = AOT_SECTION_TYPE_RELOCATION;
    append_action(section, current_format, action_list);
}

static void check_aot_file_header(uint8_t *start, uint8_t *end) {

    uint8_t *offset = start;
    uint8_t magic0, magic1, magic2, magic3;

    READ_UINT8(offset, end, &magic0);
    READ_UINT8(offset, end, &magic1);
    READ_UINT8(offset, end, &magic2);
    READ_UINT8(offset, end, &magic3);

    if (magic0 != AOT_MAGIC0 || magic1 != AOT_MAGIC1 ||
        magic2 != AOT_MAGIC2 || magic3 != AOT_MAGIC3) {
        fprintf(stderr, "Error: wrong AOT magic number,"
                " expected '%c%c%c%c', got '%c%c%c%c'\n",
                AOT_MAGIC0, AOT_MAGIC1, AOT_MAGIC2, AOT_MAGIC3,
                magic0, magic1, magic2, magic3);
        exit(EXIT_FAILURE);
    }

    uint32_t version;
    READ_UINT32(offset, end, &version);
    if (version != AOT_CURRENT_VERSION) {
        fprintf(stderr, "Error: wrong AOT format version,"
                " expected %u, got %"PRIu32"\n", AOT_CURRENT_VERSION, version);
        exit(EXIT_FAILURE);
    }
}

static void decode_aot_sections(struct list_head *section_list, uint8_t *offset, uint8_t *end) {

    uint32_t section_type;
    uint32_t section_size;

    while (offset < end) {
        offset = ALIGN_PTR(offset, 4);
        READ_UINT32(offset, end, &section_type);
        READ_UINT32(offset, end, &section_size);
        if (!(section_type < AOT_REQUIRED_SECTION_COUNT || section_type == 100)) {
            fprintf(stderr, "Error: %"PRIu32" is an invalid section type\n", section_type);
            exit(EXIT_FAILURE);
        }
        AOTSection *section = xmalloc(sizeof(struct AOTSection));
        section->type = section_type;
        section->start = offset;
        section->end = section->start + section_size;
        list_add_tail(&section->link, section_list);
        offset += section_size;
    }

    if (offset != end) {
        fprintf(stderr, "The sum of the section lengths"
               " is equal to the length of the AOT file\n");
        exit(EXIT_FAILURE);
    }
}

static void check_sections_order(struct list_head *section_list) {

    if (list_empty(section_list)) {
        fprintf(stderr, "Error: the AOT file has no sections\n");
        exit(EXIT_FAILURE);
    }

    struct list_head *iter = list_next(section_list);
    AOTSection *s = container_of(iter, AOTSection, link);
    if (s->type != AOT_SECTION_TYPE_TARGET_INFO) {
        fprintf(stderr, "Error: expected %s section, got %s\n",
                AOTSectionType_to_string(AOT_SECTION_TYPE_TARGET_INFO),
                AOTSectionType_to_string(s->type));
    }

    AOTSection *curr;
    while ((iter = list_next(iter)) != section_list) {

        curr = container_of(iter, AOTSection, link);
        if (curr->type == AOT_SECTION_TYPE_CUSTOM) continue;
        if (curr->type >= AOT_REQUIRED_SECTION_COUNT) {
            fprintf(stderr, "Error: %u is an unknown AOT section type\n", curr->type);
            exit(EXIT_FAILURE);
        }
        if (curr->type != (s->type + 1)) {
            fprintf(stderr, "Error: expected section %s"
                    " after section %s, but got section %s\n",
                    AOTSectionType_to_string(s->type + 1),
                    AOTSectionType_to_string(s->type),
                    AOTSectionType_to_string(curr->type));
            exit(EXIT_FAILURE);
        }
        s = curr;
    }

    if (s->type != AOT_SECTION_TYPE_RELOCATION) {
        for (AOTSectionType t = s->type + 1; t < AOT_REQUIRED_SECTION_COUNT; t++) {
            fprintf(stderr, "Error: missing required section %s\n",
                    AOTSectionType_to_string(t));
        }
        exit(EXIT_FAILURE);
    }
}

static void execute_actions(struct list_head *action_list,
                            struct list_head *section_list) {

    AOTSection *required_sections[AOT_REQUIRED_SECTION_COUNT];
    AOTSection *section;
    list_for_each_entry(section, section_list, link) {
        if (section->type == AOT_SECTION_TYPE_CUSTOM) continue;
        required_sections[section->type] = section;
    }

    DisplayAction *action;
    list_for_each_entry(action, action_list, link) {
        if (action->format == DISPLAY_FORMAT_HEXDUMP) {
            //hexdump(required_sections[action->section]);
            continue;
        }
        AOTSection *s = required_sections[action->section];
        switch (action->section) {
        case AOT_SECTION_TYPE_TARGET_INFO:
            print_target_info(s);
            break;
        case AOT_SECTION_TYPE_INIT_DATA:
            print_init_data(s);
            break;
        case AOT_SECTION_TYPE_TEXT:
            print_text(s);
            break;
        case AOT_SECTION_TYPE_FUNCTION:
            print_function(s);
            break;
        case AOT_SECTION_TYPE_EXPORT:
            print_export(s);
            break;
        case AOT_SECTION_TYPE_RELOCATION:
            //print_relocation(s);
            break;
        default:
            assert(0);
        }
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage(stderr);
        return EXIT_FAILURE;
    }

    DisplayFormat current_format = DISPLAY_FORMAT_DEFAULT;
    AOTSectionType section;
    struct list_head action_list;
    INIT_LIST_HEAD(&action_list);

    static struct option long_options[] = {
        { "help",        no_argument, NULL, 'H' },
        { "all",         no_argument, NULL, 'a' },
        { "hexdump",     no_argument, NULL, 'x' },
        { "print",       no_argument, NULL, 'p' },
        { "target-info", no_argument, NULL, 'h' },
        { "init-data",   no_argument, NULL, 'i' },
        { "text",        no_argument, NULL, 't' },
        { "function",    no_argument, NULL, 'f' },
        { "export",      no_argument, NULL, 'e' },
        { "relocation",  no_argument, NULL, 'r' },
        { NULL,          no_argument, NULL,  0  }
    };

    int c;
    char *optstring = "Haxphitfer";
    while ((c = getopt_long(argc, argv, optstring, long_options, NULL)) != -1) {
        switch (c) {
        case 'H':
            print_usage(stdout);
            return EXIT_SUCCESS;
        case 'a':
            all_actions(current_format, &action_list);
            continue;
        case 'x':
            current_format = DISPLAY_FORMAT_HEXDUMP;
            continue;
        case 'p':
            current_format = DISPLAY_FORMAT_PRINTABLE_TEXT;
            continue;
        case 'h':
            section = AOT_SECTION_TYPE_TARGET_INFO;
            break;
        case 'i':
            section = AOT_SECTION_TYPE_INIT_DATA;
            break;
        case 't':
            section = AOT_SECTION_TYPE_TEXT;
            break;
        case 'f':
            section = AOT_SECTION_TYPE_FUNCTION;
            break;
        case 'e':
            section = AOT_SECTION_TYPE_EXPORT;
            break;
        case 'r':
            section = AOT_SECTION_TYPE_RELOCATION;
            break;
        default:
            return EXIT_FAILURE;
        }
        append_action(section, current_format, &action_list);
    }

    if (optind == argc) {
        fprintf(stderr, "Error: provide an input file.\n");
        return EXIT_FAILURE;
    }

    if(optind != argc-1) {
        fprintf(stderr, "Error: invalid number of arguments,"
                " provide only one input file.\n");
        return EXIT_FAILURE;
    }

    /* If no option are provided, the default one is --all */
    if (list_empty(&action_list)) {
        all_actions(current_format, &action_list);
    }

    char *input_path = argv[optind];
    FILE *input_file;
    if ((input_file = fopen(input_path, "r")) == NULL) {
        fprintf(stderr, "Error: cannot open %s\n", input_path);
        return EXIT_FAILURE;
    }

    fseek(input_file, 0, SEEK_END);
    unsigned long buf_size  = ftell(input_file);
    fseek(input_file, 0, SEEK_SET);
    uint8_t *buf = xmalloc(buf_size);
    if (fread(buf, 1, buf_size, input_file) != buf_size) {
        fprintf(stderr, "Error: cannot read the entire file %s\n", input_path);
        return EXIT_FAILURE;
    }
    fclose(input_file);
    input_file = NULL;

    uint8_t *start = buf;
    uint8_t *end = start + buf_size;

    check_aot_file_header(start, end);
    uint8_t *offset = start + 8;

    struct list_head section_list;
    INIT_LIST_HEAD(&section_list);

    decode_aot_sections(&section_list, offset, end);
    check_sections_order(&section_list);
    execute_actions(&action_list, &section_list);

    DisplayAction *action, *da_iter;
    list_for_each_entry_safe(action, da_iter, &action_list, link) {
        list_del(&action->link);
        free(action);
    }
    AOTSection *s, *s_iter;
    list_for_each_entry_safe(s, s_iter, &section_list, link) {
        list_del(&s->link);
        free(s);
    }
    free(buf);
    return EXIT_SUCCESS;
}
