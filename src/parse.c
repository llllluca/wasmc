#include "all.h"
#include <stdio.h>
#include <string.h>

static void read_value_type(read_struct_t *r, enum wasm_valtype *out) {
    read_u8(r, out);
    if (*out != I32_VALTYPE && *out != I64_VALTYPE &&
        *out != F32_VALTYPE && *out != F64_VALTYPE) {
        panic();
    }
}

static void parse_const_expr(read_struct_t *r, const_expr_t *e) {
    unsigned char opcode;
    read_u8(r, &opcode);
    switch (opcode) {
        case I32_CONST_OPCODE:
            readILEB128_i32(r, &e->as.i32);
            e->type = I32_VALTYPE;
            break;
        case I64_CONST_OPCODE:
        case F32_CONST_OPCODE:
        case F64_CONST_OPCODE:
        default:
            panic();
    }
    read_u8(r, &opcode);
    if (opcode != END_CODE) panic();
}

static void parse_type_section_if_exists(wasm_module *m) {

    if (m->module.offset >= m->module.end) return;
    unsigned char id;
    read_u8(&m->module, &id);
    if (id != TYPE_SECTION_ID) {
        m->module.offset--;
        return;
    }
    uint32_t size, types_len;
    /* read section size in bytes */
    readULEB128_u32(&m->module, &size);
    /* read the length of the vector of function types */
    readULEB128_u32(&m->module, &types_len);
    if (types_len == 0) return;
    m->types = xcalloc(types_len, sizeof(struct wasm_func_type));
    m->types_len = types_len;
    unsigned char fun_start_code;
    uint32_t params_len, return_len;
    for (uint32_t i = 0; i < types_len; i++) {
        /* each function type start with the byte 0x60 */
        read_u8(&m->module, &fun_start_code);
        if (fun_start_code != FUNCTION_START_CODE) {
            panic();
        }
        /* read the length of the vector of parameter types */
        readULEB128_u32(&m->module, &params_len);
        if (params_len > 0) {
            m->types[i].params_type = xcalloc(params_len, sizeof(enum wasm_valtype));
            m->types[i].num_params = params_len;
            for (uint32_t j = 0; j < params_len; j++) {
                /* read the actual parameter types */
                read_value_type(&m->module, &m->types[i].params_type[j]);
            }
        }
        /* read the length of the vector of result types */
        readULEB128_u32(&m->module, &return_len);
        if (return_len == 1) {
            /* read the actual result types */
            read_value_type(&m->module, &m->types[i].return_type);
        } else if (return_len == 0) {
            m->types[i].return_type = NO_VALTYPE;
        } else panic();
    }
}

static void parse_import_section_if_exists(wasm_module *m) {
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

static void parse_function_section_if_exists(wasm_module *m) {
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
    m->num_funcs = 0;
    if (indexes_len > 0) {
        m->func_decls = xcalloc(indexes_len, sizeof(wasm_func_decl));
        m->num_funcs = indexes_len;
    }

    for (uint32_t i = 0; i < indexes_len; i++) {
        readULEB128_u32(&m->module, &index);
        if (index >= m->types_len) {
            panic();
        }
        m->func_decls[i].type = &m->types[index];
    }
}

static void parse_table_section_if_exists(wasm_module *m) {
    unsigned char id;

    if (m->module.offset >= m->module.end) return;
    read_u8(&m->module, &id);
    if (id != TABLE_SECTION_ID) {
        m->module.offset--;
        return;
    }
    uint32_t size;
    /* read section size in bytes */
    readULEB128_u32(&m->module, &size);
    /* skip the whole table section */
    m->module.offset += size;
}

static void parse_memory_section_if_exists(wasm_module *m) {
    unsigned char id;

    if (m->module.offset >= m->module.end) return;
    read_u8(&m->module, &id);
    if (id != MEMORY_SECTION_ID) {
        m->module.offset--;
        return;
    }

    uint32_t size, num_mems;
    readULEB128_u32(&m->module, &size);
    readULEB128_u32(&m->module, &num_mems);
    if (num_mems != 1) {
        panic();
    }
    unsigned char flag;
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

static void parse_global_section_if_exists(wasm_module *m) {
    read_struct_t *r = &m->module;
    if (m->module.offset >= m->module.end) return;
    unsigned char id;
    read_u8(&m->module, &id);
    if (id != GLOBAL_SECTION_ID) {
        m->module.offset--;
        return;
    }
    uint32_t size;
    /* read section size in bytes */
    readULEB128_u32(r, &size);
    /* read the length of the vector of globals */
    readULEB128_u32(r, &m->globals_len);
    if (m->globals_len > 0) {
         m->globals = xcalloc(m->globals_len, sizeof(global_t));
    }
    for (uint32_t i = 0; i < m->globals_len; i++) {
        global_t *g = &m->globals[i];
        enum wasm_valtype type;
        read_value_type(&m->module, &type);
        read_u8(r, (unsigned char *)&g->is_mutable);
        if (g->is_mutable != 0 && g->is_mutable != 1) {
            panic();
        }
        parse_const_expr(r, &g->expr);
        if (g->expr.type != type) panic();

        int n = snprintf((char *)g->name, GLOBAL_NAME_LEN, "g%"PRIu32, i);
        if (n >= GLOBAL_NAME_LEN) panic();

    }
}

static void parse_export_section_if_exists(wasm_module *m) {
    unsigned char id;
    uint32_t size, num_exports;

    if (m->module.offset >= m->module.end) return;
    read_u8(&m->module, &id);
    if (id != EXPORT_SECTION_ID) {
        m->module.offset--;
        return;
    }

    readULEB128_u32(&m->module, &size);
    readULEB128_u32(&m->module, &num_exports);
    for (unsigned int i = 0; i < num_exports; i++) {
        uint32_t name_len;
        readULEB128_u32(&m->module, &name_len);
        unsigned char *name = m->module.offset;
        m->module.offset += name_len;
        unsigned char export_desc;
        read_u8(&m->module, &export_desc);
        // only functions export are implemented
        switch (export_desc) {
            case 0x00: {
                uint32_t funcidx;
                readULEB128_u32(&m->module, &funcidx);
                if (funcidx >= m-> num_funcs) {
                    panic();
                }
                m->func_decls[funcidx].is_exported = true;
                if (m->func_decls[funcidx].name != NULL) {
                    free(m->func_decls[funcidx].name);
                }
                m->func_decls[funcidx].name = xcalloc(name_len+1, sizeof(char));
                memcpy(m->func_decls[funcidx].name, name, name_len);
                m->func_decls[funcidx].name[name_len] = '\0';
                // temporary hack
                if (strcmp(m->func_decls[funcidx].name, "_start") == 0) {
                    memcpy(m->func_decls[funcidx].name, "main", 4+1);
                }
            } break;
            case 0x01: {
                uint32_t tableidx;
                readULEB128_u32(&m->module, &tableidx);
            } break;
            case 0x02: {
                uint32_t memidx;
                readULEB128_u32(&m->module, &memidx);
            } break;
            case 0x03: {
                uint32_t globalidx;
                readULEB128_u32(&m->module, &globalidx);
            } break;
            default:
                panic();
        }
    }
}

static void parse_start_section_if_exists(wasm_module *m) {
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

static void parse_element_section_if_exists(wasm_module *m) {
    unsigned char id;

    read_u8(&m->module, &id);
    if (id != ELEMENT_SECTION_ID) {
        m->module.offset--;
        return;
    }
    printf("TODO: parse_element_section_if_exists() is unimplemented!\n");
    panic();
}

static void parse_code_section_if_exists(wasm_module *m) { 
    uint32_t size, funcs_len;
    unsigned char id;

    if (m->module.offset >= m->module.end) return;
    read_u8(&m->module, &id);
    if (id != CODE_SECTION_ID) {
        m->module.offset--;
        return;
    }
    /* read the size of the whole section */
    readULEB128_u32(&m->module, &size);
    /* read the length of the vector of function codes */
    unsigned char *start_code_section = m->module.offset;
    readULEB128_u32(&m->module, &funcs_len);
    if (funcs_len != m->num_funcs) {
        panic();
    }
    m->next_func_body.start = m->module.offset;
    m->next_func_body.offset = m->next_func_body.start;
    m->next_func_body.end = start_code_section + size;
    m->next_func_body.len = m->next_func_body.end - m->next_func_body.start;
    m->module.offset = start_code_section + size;
}

static void parse_data_section_if_exists(wasm_module *m) {
    unsigned char id;

    if (m->module.offset >= m->module.end) return;
    read_u8(&m->module, &id);
    if (id != DATA_SECTION_ID) {
        m->module.offset--;
        return;
    }
    uint32_t size, memidx;
    /* read the size of the whole section */
    readULEB128_u32(&m->module, &size);
    /* read the length of the vector of data segments */
    readULEB128_u32(&m->module, &m->num_data_segments);
    m->data_segments = xcalloc(m->num_data_segments, sizeof(data_segment_t));
    for (uint32_t i = 0; i < m->num_data_segments; i++) {
        readULEB128_u32(&m->module, &memidx);
        if (memidx != 0) {
            panic();
        }
        data_segment_t *d = &m->data_segments[i];
        const_expr_t e;
        parse_const_expr(&m->module, &e);
        if (e.type != I32_VALTYPE || e.as.i32 < 0) panic();
        d->offset = (uint32_t) e.as.i32;
        readULEB128_u32(&m->module, &d->init_len);
        d->init_bytes = m->module.offset;
        m->module.offset += d->init_len;
    }
}

wasm_module* parse(unsigned char *start, unsigned int len) {
    wasm_module *m = xmalloc(sizeof(wasm_module));
    memset(m, 0, sizeof(wasm_module));
    m->module.start  = start;
    m->module.end    = start + len;
    m->module.offset = start;
    m->module.len    = len;

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

    /* Give an explicit name to not exported functions.
     * It is not a good solution, internal names can clash with exported name */
    unsigned int id = 0;
    for (uint32_t i = 0; i < m->num_funcs; i++) {
        if (!m->func_decls[i].is_exported) {
            int n = snprintf(NULL, 0, "_f%d", id);
            if (n < 0) panic();
            size_t size = n + 1;
            m->func_decls[i].name = xcalloc(size, sizeof(char));
            snprintf(m->func_decls[i].name, size, "_f%d", id++);
        }
    }

    return m;
}

wasm_func_body *parse_next_func_body(wasm_module *m) {
    uint32_t code_len;
    readULEB128_u32(&m->next_func_body, &code_len);

    unsigned char *locals_start = m->next_func_body.offset;

    uint32_t len;
    uint32_t num_locals = 0;
    // read the length of the vector of locals
    readULEB128_u32(&m->next_func_body, &len);
    unsigned char *start_vec_locals = m->next_func_body.offset;
    for (uint32_t j = 0; j < len; j++) {
        uint32_t n;
        // read the number of variables with the same type
        readULEB128_u32(&m->next_func_body, &n);
        // skip the type
        m->next_func_body.offset += 1;
        num_locals += n;
    }
    size_t size = sizeof(wasm_func_body) + num_locals * sizeof(wasm_valtype);
    wasm_func_body *body = xmalloc(size);
    body->num_locals = num_locals;

    if (num_locals > 0) {
        m->next_func_body.offset = start_vec_locals;
        unsigned int count = 0;
        for (uint32_t j = 0; j < len; j++) {
            uint32_t n;
            wasm_valtype type;
            // read the number of variables with the same type
            readULEB128_u32(&m->next_func_body, &n);
            // read the type
            read_value_type(&m->next_func_body, &type);
            for (unsigned int h = 0; h < n; h++) {
                body->locals_type[count++] = type;
            }
        }
    }

    // parse expr (function body)
    body->expr.start = m->next_func_body.offset;
    body->expr.offset = body->expr.start;
    body->expr.end = locals_start + (code_len - 1);
    body->expr.len = body->expr.end - body->expr.start;
    m->next_func_body.offset = body->expr.end;
    unsigned char func_end;
    read_u8(&m->next_func_body, &func_end);
    if (func_end != END_CODE) {
        panic();
    }

    return body;
}

void free_wasm_module(wasm_module *m) {
    wasm_func_type *type;

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
    if (m->globals != NULL) {
        free(m->globals);
        m->globals = NULL;
        m->globals_len = 0;
    }
    if (m->data_segments != NULL) {
        free(m->data_segments);
        m->data_segments = NULL;
        m->num_data_segments = 0;
    }
    if (m->func_decls != NULL) {
        for (uint32_t i = 0; i < m->num_funcs; i++) {
            if (m->func_decls[i].name != NULL) {
                free(m->func_decls[i].name);
            }
        }
        free(m->func_decls);
        m->func_decls = NULL;
    }
    free(m);
}


