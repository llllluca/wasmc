#include "aot.h"
#include <string.h>
#include <stdlib.h>

static AOTErr_t emit_init_data(AOTModule *m) {

    WASMModule *w = m->wasm_mod;

    m->funcs = calloc(w->function_count, sizeof(struct AOTFuncSecEntry));
    if (m->funcs == NULL) return AOT_ERR;
    for (uint32_t i = 0; i < w->function_count; i++) {
        m->funcs[i].text_offset = -1;
        m->funcs[i].type_index = -1;
    }

    WRITE_UINT32(m, AOT_SECTION_TYPE_INIT_DATA);
    /* section_size points into the AOT module at the section size field.
     * Now the section size is unknown, we fill the section size field later */
    uint32_t *section_size = (uint32_t *)m->offset;
    m->offset += sizeof(uint32_t);
    uint8_t *section_start = m->offset;

    uint32_t aux_data_end_global_index = (uint32_t)-1;
    uint64_t aux_data_end = 0;
    uint32_t aux_heap_base_global_index = (uint32_t)-1;
    uint64_t aux_heap_base = 0;
    uint32_t aux_stack_top_global_index = (uint32_t)-1;
    uint64_t aux_stack_bottom = 0;
    uint32_t aux_stack_size = 0;

    for (uint32_t i = 0; i < w->export_count; i++) {
        WASMExport *e = &w->exports[i];

        if (strncmp((char *)e->name, "__data_end", e->name_len) == 0 &&
            e->export_desc == EXPORT_TYPE_GLOBAL) {

            WASMGlobal *g = &w->globals[e->index];
            if (g->type != WASM_VALTYPE_I32) continue;
            aux_data_end_global_index = e->index;
            aux_data_end = g->as.i32;

        } else if (strncmp((char *)e->name, "__heap_base", e->name_len) == 0 &&
            e->export_desc == EXPORT_TYPE_GLOBAL) {

            WASMGlobal *g = &w->globals[e->index];
            if (g->type != WASM_VALTYPE_I32) continue;
            aux_heap_base_global_index = e->index;
            aux_heap_base = g->as.i32;
        }
    }

    if (aux_data_end_global_index != (uint32_t)-1 &&
        aux_heap_base_global_index != (uint32_t)-1) {

            aux_stack_top_global_index = 0;
            aux_stack_bottom = aux_heap_base;
            aux_stack_size = aux_heap_base - aux_data_end;
    }

    /* ----- Emit Memory Info subsection ----- */
    uint32_t import_memory_count = 0;
    uint32_t memory_count = 1;
    WRITE_UINT32(m, import_memory_count);
    WRITE_UINT32(m, memory_count);

    uint32_t mem_flags = 0;
    uint32_t mem_num_bytes_per_page = 65536;
    uint32_t mem_min_page_num = 0;
    uint32_t mem_max_page_num = 0;
    if (w->memories_count != 0) {
        mem_flags = w->memory.flags;
        mem_num_bytes_per_page = w->memory.num_bytes_per_page;
        mem_min_page_num = w->memory.init_page_count;
        mem_max_page_num = w->memory.max_page_count;
    }
    if (aux_data_end_global_index != (uint32_t)-1 &&
        aux_heap_base_global_index != (uint32_t)-1) {

            mem_num_bytes_per_page = aux_heap_base;
            mem_min_page_num = 1;
            mem_max_page_num = 1;
    }
    WRITE_UINT32(m, mem_flags);
    WRITE_UINT32(m, mem_num_bytes_per_page);
    WRITE_UINT32(m, mem_min_page_num);
    WRITE_UINT32(m, mem_max_page_num);

    uint32_t is_passive = 0;
    uint32_t memory_index = 0;
    WRITE_UINT32(m, w->data_seg_count);
    for (uint32_t i = 0; i < w->data_seg_count; i++) {
        WASMDataSegment *d = &w->data_segments[i];
        WRITE_UINT32(m, is_passive);
        WRITE_UINT32(m, memory_index);
        WRITE_UINT32(m, WASM_OPCODE_I32_CONST);
        WRITE_UINT32(m, d->offset);
        WRITE_UINT32(m, d->len);
        WRITE_BYTE_ARRAY(m, d->bytes, d->len);
    }

    /* ----- Emit Table Info subsection ----- */
    uint32_t import_table_count = 0;
    uint32_t table_count = 0;
    uint32_t table_init_data_count = 0;
    WRITE_UINT32(m, import_table_count);
    WRITE_UINT32(m, table_count);
    WRITE_UINT32(m, table_init_data_count);

    /* ----- Emit Type Info subsection ----- */
    WRITE_UINT32(m, w->type_count);
    uint16_t type_flag = 0;
    for (uint32_t i = 0; i < w->type_count; i++) {
        m->offset = ALIGN_PTR(m->offset, sizeof(uint32_t));
        WASMFuncType *t = &w->types[i];
        WRITE_UINT16(m, type_flag);
        WRITE_UINT16(m, t->param_count);
        WRITE_UINT16(m, t->result_count);
        if (t->param_count > 0) {
            WRITE_BYTE_ARRAY(m, t->param_types, t->param_count);
        }
        if (t->result_count != 0) {
            if (m->buf_end - m->offset < 1) {
                return AOT_ERR;
            }
            *m->offset = t->result_type;
            m->offset++;
        }
    }

    /* ----- Emit Import Global Info subsection  ----- */
    uint32_t import_global_count = 0;
    WRITE_UINT32(m, import_global_count);

    /* ----- Emit Global Info subsection  ----- */
    WRITE_UINT32(m, w->global_count);
    for (uint32_t i = 0; i < w->global_count; i++) {
        WASMGlobal *g = &w->globals[i];
        WRITE_UINT8(m, g->type);
        WRITE_UINT8(m, g->is_mutable);
        switch (g->type) {
        case WASM_VALTYPE_I32:
            WRITE_UINT32(m, WASM_OPCODE_I32_CONST);
            WRITE_UINT32(m, g->as.i32);
            break;
        default:
            return AOT_ERR;
        }
    }

    /* ----- Emit Import Func Info subsection ----- */
    uint32_t import_func_count = 0;
    WRITE_UINT32(m, import_func_count);

    /* ----- Emit function count and start function index ----- */
    WRITE_UINT32(m, w->function_count);
    uint32_t start_func_index = -1;
    WRITE_UINT32(m, start_func_index);

    /* ----- Emit auxiliary section ----- */
    WRITE_UINT32(m, aux_data_end_global_index);
    WRITE_UINT64(m, aux_data_end);
    WRITE_UINT32(m, aux_heap_base_global_index);
    WRITE_UINT64(m, aux_heap_base);
    WRITE_UINT32(m, aux_stack_top_global_index);
    WRITE_UINT64(m, aux_stack_bottom);
    WRITE_UINT32(m, aux_stack_size);

    /* ----- Emit data sections ----- */
    uint32_t data_section_count = 0;
    WRITE_UINT32(m, data_section_count);

    /* Fix section size */
    *section_size = m->offset - section_start;

    return AOT_OK;
}

static AOTErr_t init_text(AOTModule *m) {

    WRITE_UINT32(m, AOT_SECTION_TYPE_TEXT);
    /* The text_size points into the AOT module at the section size field of the
     * text section. At this moment the text section size is unknown,
     * we fill the section size field later */
    m->text_size = (uint32_t *)m->offset;
    m->offset += sizeof(uint32_t);
    const uint32_t literal_size = 0;
    WRITE_UINT32(m, literal_size);
    m->text_start = m->offset;
    return AOT_OK;
}

AOTErr_t emit_function(AOTModule *m) {

    WASMModule *w = m->wasm_mod;

    WRITE_UINT32(m, AOT_SECTION_TYPE_FUNCTION);
    uint32_t *section_size = (uint32_t *)m->offset;
    m->offset += sizeof(uint32_t);
    uint8_t *section_start = m->offset;

    for (uint32_t i = 0; i < w->function_count; i++) {
        WRITE_UINT32(m, m->funcs[i].text_offset);
    }

    for (uint32_t i = 0; i < w->function_count; i++) {
        WRITE_UINT32(m, m->funcs[i].type_index);
    }

    uint32_t max_local_cell_num = 0;
    uint32_t max_stack_cell_num = 0;
    for (uint32_t i = 0; i < w->function_count; i++) {
        WRITE_UINT32(m, max_local_cell_num);
        WRITE_UINT32(m, max_stack_cell_num);
    }

    /* Fix section size */
    *section_size = m->offset - section_start;
    return AOT_OK;
}

static AOTErr_t emit_export(AOTModule *m) {

    WASMModule *w = m->wasm_mod;

    WRITE_UINT32(m, AOT_SECTION_TYPE_EXPORT);
    uint32_t *section_size = (uint32_t *)m->offset;
    m->offset += sizeof(uint32_t);
    uint8_t *section_start = m->offset;

    uint32_t num_exports_only_funcs = 0;
    for (uint32_t i = 0; i < w->export_count; i++) {
        if (w->exports[i].export_desc == EXPORT_TYPE_FUNC) {
            num_exports_only_funcs++;
        }
    }

    WRITE_UINT32(m, num_exports_only_funcs);
    for (uint32_t i = 0; i < w->export_count; i++) {
        WASMExport *e = &w->exports[i];
        if (e->export_desc != EXPORT_TYPE_FUNC) {
            continue;
        }
        WRITE_UINT32(m, e->index);
        WRITE_UINT8(m, e->export_desc);
        uint32_t name_len = e->name_len;
        if (e->name[name_len-1] == '\0') {
            WRITE_UINT16(m, name_len);
            WRITE_BYTE_ARRAY(m, e->name, name_len);
        } else {
            WRITE_UINT16(m, name_len + 1);
            WRITE_BYTE_ARRAY(m, e->name, name_len);
            if ((long unsigned int)(m->buf_end - m->offset) < sizeof(char)) {
                return AOT_ERR;
            }
            *m->offset = '\0';
            m->offset += sizeof(char);
        }
    }

    /* Fix section size */
    *section_size = m->offset - section_start;
    return AOT_OK;
}

static AOTErr_t emit_relocation(AOTModule *m) {

    WRITE_UINT32(m, AOT_SECTION_TYPE_RELOCATION);
    uint32_t *section_size = (uint32_t *)m->offset;
    m->offset += sizeof(uint32_t);
    uint8_t *section_start = m->offset;

    uint32_t symbol_count = 0;
    for (AOTRelocation *i = m->reloc_list; i != NULL; i = i->next) {
        i->representative = NULL;
        for (AOTRelocation *j = m->reloc_list; j != i; j = j->next) {
            if (i->symbol_name == j->symbol_name) {
                i->representative = j;
                break;
            }
        }
        if (i->representative == NULL) {
            symbol_count++;
        }
    }
    if (m->reloc_list != NULL) {
        symbol_count++;
    }
    WRITE_UINT32(m, symbol_count);
    uint32_t *symbol_offset = (uint32_t *)m->offset;
    uint32_t symbol_index = 0;
    m->offset += symbol_count * sizeof(uint32_t);
    uint32_t *symbol_buf_size = (uint32_t *)m->offset;
    m->offset += sizeof(uint32_t);
    uint8_t *symbol_buf_start = m->offset;

    if (m->reloc_list != NULL) {
        char *group_section_name = ".rela.text";
        *symbol_offset = m->offset - symbol_buf_start;
        symbol_offset++;
        symbol_index++;
        uint16_t len = strlen(group_section_name) + 1;
        WRITE_UINT16(m, len);
        WRITE_BYTE_ARRAY(m, group_section_name, len);
        m->offset = ALIGN_PTR(m->offset, 2);
    }

    for (AOTRelocation *i = m->reloc_list; i != NULL; i = i->next) {
        if (i->representative != NULL) continue;
        *symbol_offset = m->offset - symbol_buf_start;
        symbol_offset++;
        i->symbol_index = symbol_index++;
        uint16_t len = strlen(i->symbol_name) + 1;
        WRITE_UINT16(m, len);
        WRITE_BYTE_ARRAY(m, i->symbol_name, len);
        m->offset = ALIGN_PTR(m->offset, 2);
    }
    *symbol_buf_size = m->offset - symbol_buf_start;

    if (m->reloc_list != NULL) {
        uint32_t group_count = 1;
        WRITE_UINT32(m, group_count);
        uint32_t group_name_index = 0;
        WRITE_UINT32(m, group_name_index);
        WRITE_UINT32(m, m->reloc_count);
        for (AOTRelocation *i = m->reloc_list; i != NULL; i = i->next) {
            WRITE_UINT32(m, i->offset);
            WRITE_UINT32(m, i->addend);
            WRITE_UINT32(m, i->type);
            if (i->representative != NULL) {
                WRITE_UINT32(m, i->representative->symbol_index);
            } else {
                WRITE_UINT32(m, i->symbol_index);
            }
        }
    } else {
        uint32_t group_count = 0;
        WRITE_UINT32(m, group_count);
    }

    /* Fix section size */
    *section_size = m->offset - section_start;
    return AOT_OK;
}

void aot_module_cleanup(AOTModule *m) {
    if (m->buf != NULL) free(m->buf);
    if (m->funcs != NULL) free(m->funcs);
    AOTRelocation *r = m->reloc_list;
    while (r != NULL) {
        AOTRelocation *next = r->next;
        free(r);
        r = next;
    }
}

AOTErr_t aot_module_init(AOTModule *m, WASMModule *w, Target *t) {

    memset(m, 0, sizeof(struct AOTModule));
    m->wasm_mod = w;
    m->buf_len = 16 * 1024;
    m->buf = calloc(m->buf_len, sizeof(uint8_t));
    if (m->buf == NULL) goto ERROR;
    m->offset = m->buf;
    m->buf_end = m->buf + m->buf_len;

    WRITE_UINT32(m, AOT_MAGIC_NUMBER);
    WRITE_UINT32(m, AOT_CURRENT_VERSION);

    if (t->emit_target_info(m)) goto ERROR;
    if (emit_init_data(m)) goto ERROR;
    if (init_text(m)) goto ERROR;

    return AOT_OK;

ERROR:
    aot_module_cleanup(m);
    return AOT_ERR;
}



AOTErr_t aot_module_finalize(AOTModule *m, uint8_t **buf, uint32_t *len) {

    /* finalize text */
    *m->text_size = (m->offset - m->text_start) + sizeof(uint32_t);

    if (emit_function(m)) goto ERROR;
    if (emit_export(m)) goto ERROR;
    if (emit_relocation(m)) goto ERROR;

    *len = m->offset - m->buf;
    *buf = realloc(m->buf, *len);
    m->buf = NULL;
    aot_module_cleanup(m);
    return AOT_OK;

ERROR:
    aot_module_cleanup(m);
    return AOT_ERR;
}

