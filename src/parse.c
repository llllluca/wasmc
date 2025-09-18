#include "all.h"
#include <stdio.h>
#include <string.h>

static void read_value_type(wasm_module_t *m, unsigned char *out) {
    read_u8(&m->module, out);
    if (*out != I32_VALTYPE && *out != I64_VALTYPE &&
        *out != F32_VALTYPE && *out != F64_VALTYPE) {
        panic();
    }
}

static void parse_type_section_if_exists(wasm_module_t *m) {
    uint32_t size, types_len, params_len, return_len;
    unsigned char id, fun_start_code;
    wasm_func_type_t *type;

    if (m->module.offset >= m->module.end) return;
    read_u8(&m->module, &id);
    if (id != TYPE_SECTION_ID) {
        m->module.offset--;
        return;
    }
    readULEB128_u32(&m->module, &size);
    readULEB128_u32(&m->module, &types_len);
    if (types_len > 0) {
        m->types = calloc_or_panic(types_len, sizeof(wasm_func_type_t));
        m->types_len = types_len;
    }

    for (uint32_t i = 0; i < types_len; i++) {
        read_u8(&m->module, &fun_start_code);
        if (fun_start_code != FUNCTION_START_CODE) {
            panic();
        }
        type = &m->types[i];
        readULEB128_u32(&m->module, &params_len);
        type->params_type = calloc_or_panic(params_len, sizeof(unsigned char));
        type->num_params = params_len;
        for (uint32_t j = 0; j < params_len; j++) {
            read_value_type(m, &type->params_type[j]);
        }
        readULEB128_u32(&m->module, &return_len);
        if (return_len != 1) {
            panic();
        }
        read_value_type(m, &type->return_type);
    }
}

static void parse_import_section_if_exists(wasm_module_t *m) {
    unsigned char id;

    if (m->module.offset >= m->module.end) return;
    read_u8(&m->module, &id);
    if (id != IMPORT_SECTION_ID) {
        m->module.offset--;
        return;
    }
    printf("TODO: parse_import_section_if_exists() is unimplemented!\n");
    panic();
}

static void parse_function_section_if_exists(wasm_module_t *m) {
    uint32_t size, indexes_len, index;
    unsigned char id;

    if (m->module.offset >= m->module.end) return;
    read_u8(&m->module, &id);
    if (id != FUNCTION_SECTION_ID) {
        m->module.offset--;
        return;
    }
    readULEB128_u32(&m->module, &size);
    readULEB128_u32(&m->module, &indexes_len);
    m->funcs = calloc_or_panic(indexes_len, sizeof(wasm_func_t));
    m->funcs_len = indexes_len;

    for (uint32_t i = 0; i < indexes_len; i++) {
        readULEB128_u32(&m->module, &index);
        if (index >= m->types_len) {
            panic();
        }
        m->funcs[i].type = &m->types[index];
    }
}

static void parse_table_section_if_exists(wasm_module_t *m) {
    unsigned char id;

    if (m->module.offset >= m->module.end) return;
    read_u8(&m->module, &id);
    if (id != TABLE_SECTION_ID) {
        m->module.offset--;
        return;
    }
    printf("TODO: parse_table_section_if_exists() is unimplemented!\n");
    panic();
}

static void parse_memory_section_if_exists(wasm_module_t *m) {
    uint32_t size, num_mems;
    unsigned char id, flag;

    if (m->module.offset >= m->module.end) return;
    read_u8(&m->module, &id);
    if (id != TABLE_SECTION_ID) {
        m->module.offset--;
        return;
    }
    readULEB128_u32(&m->module, &size);
    readULEB128_u32(&m->module, &num_mems);
    if (num_mems != 1) {
        panic();
    }
    read_u8(&m->module, &flag);
    switch (flag) {
        case ONLY_MIN_MEMORY_LIMIT:
            readULEB128_u32(&m->module, &m->mem.min_page_num);
            m->mem.max_page_num = UNLIMITED_MAX_PAGE_NUM;
            break;
        case BOTH_MIN_AND_MAX_MEMORY_LIMIT:
            readULEB128_u32(&m->module, &m->mem.min_page_num);
            readULEB128_u32(&m->module, &m->mem.max_page_num);
            break;
        default:
            panic();
    }
}

static void parse_global_section_if_exists(wasm_module_t *m) {
    unsigned char id;

    if (m->module.offset >= m->module.end) return;
    read_u8(&m->module, &id);
    if (id != GLOBAL_SECTION_ID) {
        m->module.offset--;
        return;
    }
    printf("TODO: parse_global_section_if_exists() is unimplemented!\n");
    panic();
}

static void parse_export_section_if_exists(wasm_module_t *m) {
    unsigned char id, *name, export_desc;
    unsigned int size, num_exports, name_len, index;

    if (m->module.offset >= m->module.end) return;
    read_u8(&m->module, &id);
    if (id != EXPORT_SECTION_ID) {
        m->module.offset--;
        return;
    }

    readULEB128_u32(&m->module, &size);
    readULEB128_u32(&m->module, &num_exports);
    for (unsigned int i = 0; i < num_exports; i++) {
        readULEB128_u32(&m->module, &name_len);
        name = m->module.offset;
        m->module.offset += name_len;
        read_u8(&m->module, &export_desc);
        // only functions export are implemented
        if (export_desc != 0x00) {
            panic();
        }
        readULEB128_u32(&m->module, &index);
        if (index >= m->funcs_len) {
            panic();
        }
        m->funcs[index].export_name = calloc_or_panic(name_len+1, sizeof(char));
        memcpy(m->funcs[i].export_name, name, name_len);
        m->funcs[i].export_name[name_len] = '\0';
        // temporary hack
        if (strcmp(m->funcs[i].export_name, "_start") == 0) {
            memcpy(m->funcs[i].export_name, "main", 4+1);
        }
    }
}

static void parse_start_section_if_exists(wasm_module_t *m) {
    unsigned char id;

    if (m->module.offset >= m->module.end) return;
    read_u8(&m->module, &id);
    if (id != START_SECTION_ID) {
        m->module.offset--;
        return;
    }
    printf("TODO: parse_start_section_if_exists() is unimplemented!\n");
    panic();
}

static void parse_element_section_if_exists(wasm_module_t *m) {
    unsigned char id;

    read_u8(&m->module, &id);
    if (id != ELEMENT_SECTION_ID) {
        m->module.offset--;
        return;
    }
    printf("TODO: parse_element_section_if_exists() is unimplemented!\n");
    panic();
}

static void parse_code_section_if_exists(wasm_module_t *m) { 
    uint32_t size, funcs_len, code_len, num_locals, n;
    unsigned char id, *locals_start, func_end;
    wasm_func_t *func;

    if (m->module.offset >= m->module.end) return;
    read_u8(&m->module, &id);
    if (id != CODE_SECTION_ID) {
        m->module.offset--;
        return;
    }
    readULEB128_u32(&m->module, &size);
    readULEB128_u32(&m->module, &funcs_len);
    if (funcs_len != m->funcs_len) {
        panic();
    }
    for (uint32_t i = 0; i < funcs_len; i++) {
        func = &m->funcs[i];
        readULEB128_u32(&m->module, &code_len);

        // parse locals (ERROR, TODO)
        locals_start = m->module.offset;
        readULEB128_u32(&m->module, &num_locals);
        if (num_locals > 0) {
            func->locals_type = calloc_or_panic(num_locals, sizeof(unsigned char));
            func->num_locals = num_locals;
            for (uint32_t j = 0; j < num_locals; j++) {
                readULEB128_u32(&m->module, &n);
                read_value_type(m, &func->locals_type[j]);
            }
        }

        // parse expr (function body)
        func->body.start = m->module.offset;
        func->body.offset = func->body.start;
        func->body.len = code_len - (func->body.start - locals_start) - 1;
        func->body.end = func->body.start + func->body.len;
        m->module.offset = func->body.end;
        read_u8(&m->module, &func_end);
        if (func_end != FUNCTION_END_CODE) {
            panic();
        }
    }
}

static void parse_data_section_if_exists(wasm_module_t *m) {
    unsigned char id;

    if (m->module.offset >= m->module.end) return;
    read_u8(&m->module, &id);
    if (id != DATA_SECTION_ID) {
        m->module.offset--;
        return;
    }
    printf("TODO: parse_data_section_if_exists() is unimplemented!\n");
    panic();
}

wasm_module_t* parse(unsigned char *start, unsigned int len) {
    wasm_module_t *m = malloc_or_panic(sizeof(wasm_module_t));
    m->module.start  = start;
    m->module.end    = start + len;
    m->module.offset = start;
    m->module.len    = len;
    m->types_len     = 0;
    m->types         = NULL;
    m->funcs_len     = 0;
    m->funcs         = NULL;
    m->mem.min_page_num = 0;
    m->mem.max_page_num = 0;
    
    uint32_t magic, version;
    read_u32(&m->module, &magic);
    read_u32(&m->module, &version);
    if (magic != WASM_MAGIC_NUMBER || version != WASM_VERSION){
            panic();
    }
    parse_type_section_if_exists(m);
    parse_import_section_if_exists(m);
    parse_function_section_if_exists(m);
    parse_table_section_if_exists(m);
    parse_memory_section_if_exists(m);
    parse_global_section_if_exists(m);
    parse_export_section_if_exists(m);
    parse_start_section_if_exists(m);
    parse_element_section_if_exists(m);
    parse_code_section_if_exists(m);
    parse_data_section_if_exists(m);

    return m;
}

void free_wasm_module(wasm_module_t *m) {
    wasm_func_type_t *type;
    wasm_func_t *func;

    for (unsigned int i = 0; i < m->types_len; i++) {
        type = &m->types[i];
        if (type->params_type != NULL) {
            free(type->params_type);
        }
    }
    if (m->types != NULL) {
        free(m->types);
        m->types = NULL;
        m->types_len = 0;
    }
    for (unsigned int i = 0; i < m->funcs_len; i++) {
        func = &m->funcs[i];
        if (func->locals_type != NULL) {
            free(func->locals_type);
        }
        if (func->export_name != NULL) {
            free(func->export_name);
        }
    }
    if (m->funcs != NULL) {
        free(m->funcs);
        m->funcs = NULL;
        m->funcs_len = 0;
    }
}


