#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include "aot.h"
#include "wasmc.h"

#define WASMC_OK 0

extern AOTTargetInfo target_info;

#define ALIGN(m, align) (m)->offset = (void *)((((uintptr_t)(m)->offset) + (align-1)) & ~(align-1))

#define TEMPLATE_WRITE(val, offset, end, type) \
    do { \
        if ((long unsigned int)(end - offset) < sizeof(type)) { \
            return WASMC_ERR_AOT_BUFFER_TOO_SMALL; \
        } \
        *((type *)offset) = (val); \
        offset += sizeof(type); \
    } while (0)

#define WRITE_UINT64(m, val) \
    TEMPLATE_WRITE(val, (m)->offset, (m)->buf_end, uint64_t)

#define WRITE_UINT32(m, val) \
    TEMPLATE_WRITE(val, (m)->offset, (m)->buf_end, uint32_t)

#define WRITE_UINT16(m, val) \
    TEMPLATE_WRITE(val, (m)->offset, (m)->buf_end, uint16_t)

#define WRITE_UINT8(m, val) \
    TEMPLATE_WRITE(val, (m)->offset, (m)->buf_end, uint8_t)

#define WRITE_BYTE_ARRAY(m, addr, len) \
    do { \
        if ((long unsigned int)((m)->buf_end - (m)->offset) < len) { \
            return WASMC_ERR_AOT_BUFFER_TOO_SMALL; \
        } \
        memcpy((m)->offset, addr, len); \
        (m)->offset += len; \
    } while (0)

int aot_module_init(AOTModule *m, uint8_t *buf,
                    unsigned int buf_len, WASMModule *wasm_mod) {

    memset(m, 0, sizeof(struct AOTModule));
    m->wasm_mod = wasm_mod;
    m->buf_len = buf_len;
    m->buf = buf;
    m->offset = m->buf;
    m->buf_end = m->buf + m->buf_len;
    INIT_LIST_HEAD(&m->patch_list);
    m->function_text_start = calloc(wasm_mod->function_count, sizeof(uint8_t *));
    if (m->function_text_start == NULL) {
        return WASMC_ERR_OUT_OF_HEAP_MEMORY;
    }
    for (unsigned int i = 0; i < wasm_mod->function_count; i++) {
        m->function_text_start[i] = NULL;
    }

    WRITE_UINT8(m, AOT_MAGIC0);
    WRITE_UINT8(m, AOT_MAGIC1);
    WRITE_UINT8(m, AOT_MAGIC2);
    WRITE_UINT8(m, AOT_MAGIC3);
    WRITE_UINT32(m, AOT_CURRENT_VERSION);
    return WASMC_OK;
}


int emit_target_info(AOTModule *m) {

    ALIGN(m, 4);
    WRITE_UINT32(m, AOT_SECTION_TYPE_TARGET_INFO);
    WRITE_UINT32(m, sizeof(struct AOTTargetInfo));
    WRITE_UINT16(m, target_info.bin_type);
    WRITE_UINT16(m, target_info.abi_type);
    WRITE_UINT16(m, target_info.e_type);
    WRITE_UINT16(m, target_info.e_machine);
    WRITE_UINT32(m, target_info.e_version);
    WRITE_UINT32(m, target_info.e_flags);
    WRITE_UINT64(m, target_info.reserved);
    WRITE_UINT64(m, target_info.feature_flags);
    WRITE_BYTE_ARRAY(m, target_info.arch, sizeof(target_info.arch));
    return WASMC_OK;
}


static WASMExport *get_heap_base(WASMModule *w) {

    for (uint32_t i = 0; i < w->export_count; i++) {
        WASMExport *e = &w->exports[i];
        if (strncmp((char *)e->name, "__heap_base", e->name_len) == 0 &&
            e->export_desc == EXPORT_TYPE_GLOBAL) {
            WASMGlobal *g = &w->globals[e->index];
            if (g->type != WASM_VALTYPE_I32 || g->is_mutable) continue;
            return e;
        }
    }
    return NULL;
}

static WASMExport *get_data_end(WASMModule *w) {

    for (uint32_t i = 0; i < w->export_count; i++) {
        WASMExport *e = &w->exports[i];
        if (strncmp((char *)e->name, "__data_end", e->name_len) == 0 &&
            e->export_desc == EXPORT_TYPE_GLOBAL) {
            WASMGlobal *g = &w->globals[e->index];
            if (g->type != WASM_VALTYPE_I32 || g->is_mutable) continue;
            return e;
        }
    }
    return NULL;
}

static uint32_t get_stack_ptr_global_index(WASMModule *w, uint32_t heap_base) {

    for (uint32_t i = 0; i < w->global_count; i++) {
        WASMGlobal *g = &w->globals[i];
        if (g->is_mutable && g->type == WASM_VALTYPE_I32 &&
            (uint32_t)g->as.i32 <= heap_base) {
            return i;
        }
    }
    return -1;
}

static int emit_memory(AOTModule *m) {

    ALIGN(m, 4);
    WASMModule *w = m->wasm_mod;
    uint32_t import_memory_count = 0;
    uint32_t memory_count = 1;
    WRITE_UINT32(m, import_memory_count);
    WRITE_UINT32(m, memory_count);

    uint32_t flags = 0;
    uint32_t num_bytes_per_page = 65536;
    uint32_t min_page_num = 0;
    uint32_t max_page_num = 0;
    /* w->memories_count can only be 0 or 1 */
    if (w->memories_count == 1) {
        flags = w->memory.flags;
        num_bytes_per_page = w->memory.num_bytes_per_page;
        min_page_num = w->memory.init_page_count;
        max_page_num = w->memory.max_page_count;

     /* if (there are no memory.grow and there are no
      *     memory.size operations in any function) { */
            num_bytes_per_page *= min_page_num;
            if (min_page_num > 0) {
                min_page_num = 1;
                max_page_num = 1;
                WASMExport *e;
                e = get_heap_base(w);
                if (e != NULL) {
                    num_bytes_per_page = w->globals[e->index].as.i32;
                }
            }
     /* } */
    }

    WRITE_UINT32(m, flags);
    WRITE_UINT32(m, num_bytes_per_page);
    WRITE_UINT32(m, min_page_num);
    WRITE_UINT32(m, max_page_num);

    uint32_t is_passive = 0;
    uint32_t memory_index = 0;
    WRITE_UINT32(m, w->data_seg_count);
    for (uint32_t i = 0; i < w->data_seg_count; i++) {
        WASMDataSegment *d = &w->data_segments[i];
        ALIGN(m, 4);
        WRITE_UINT32(m, is_passive);
        WRITE_UINT32(m, memory_index);
        WRITE_UINT32(m, WASM_OPCODE_I32_CONST);
        WRITE_UINT32(m, d->offset);
        WRITE_UINT32(m, d->len);
        WRITE_BYTE_ARRAY(m, d->bytes, d->len);
    }

    return WASMC_OK;
}

static int emit_table(AOTModule *m) {

    WASMModule *w = m->wasm_mod;
    ALIGN(m, 4);
    uint32_t import_table_count = 0;
    WRITE_UINT32(m, import_table_count);

    WRITE_UINT32(m, w->table_count);
    if (w->table_count != 0) {
        WRITE_UINT8(m, w->table.elemtype);
        WRITE_UINT8(m, w->table.flags);
        WRITE_UINT8(m, 0);
        WRITE_UINT8(m, 0);
        WRITE_UINT32(m, w->table.init_entry_count);
        WRITE_UINT32(m, w->table.max_entry_count);
    }

    uint32_t table_init_data_count = 0;
    WRITE_UINT32(m, table_init_data_count);
    return WASMC_OK;
}

static int emit_type(AOTModule *m) {

    WASMModule *w = m->wasm_mod;
    ALIGN(m, 4);
    WRITE_UINT32(m, w->type_count);
    uint16_t type_flag = 0;
    for (uint32_t i = 0; i < w->type_count; i++) {
        ALIGN(m, 4);
        WASMFuncType *t = &w->types[i];
        WRITE_UINT16(m, type_flag);
        WRITE_UINT16(m, t->param_count);
        WRITE_UINT16(m, t->result_count);
        if (t->param_count > 0) {
            WRITE_BYTE_ARRAY(m, t->param_types, t->param_count);
        }
        if (t->result_count != 0) {
            WRITE_UINT8(m, t->result_type);
        }
    }

    return WASMC_OK;
}

static int emit_import_global(AOTModule *m) {

    ALIGN(m, 4);
    uint32_t import_global_count = 0;
    WRITE_UINT32(m, import_global_count);
    return WASMC_OK;
}

static int emit_global(AOTModule *m) {

    WASMModule *w = m->wasm_mod;
    ALIGN(m, 4);
    WRITE_UINT32(m, w->global_count);
    for (uint32_t i = 0; i < w->global_count; i++) {
        WASMGlobal *g = &w->globals[i];
        WRITE_UINT8(m, g->type);
        WRITE_UINT8(m, g->is_mutable);
        ALIGN(m, 4);
        switch (g->type) {
        case WASM_VALTYPE_I32:
            WRITE_UINT32(m, WASM_OPCODE_I32_CONST);
            WRITE_UINT32(m, g->as.i32);
            break;
        default:
            assert(0);
        }
    }
    return WASMC_OK;
}

static int emit_import_function(AOTModule *m) {

    ALIGN(m, 4);
    uint32_t import_function_count = 0;
    WRITE_UINT32(m, import_function_count);
    return WASMC_OK;
}

static int emit_auxiliary(AOTModule *m) {

    WASMModule *w = m->wasm_mod;
    ALIGN(m, 4);
    WRITE_UINT32(m, w->function_count);
    uint32_t start_func_index = -1;
    WRITE_UINT32(m, start_func_index);

    uint32_t data_end_global_index = (uint32_t)-1;
    uint64_t data_end = 0;
    uint32_t heap_base_global_index = (uint32_t)-1;
    uint64_t heap_base = 0;
    uint32_t stack_ptr_global_index = (uint32_t)-1;
    uint64_t stack_ptr = 0;
    uint32_t stack_size = 0;

    WASMExport *d, *h;
    WASMGlobal *g = w->globals;
    d = get_data_end(w);
    h = get_heap_base(w);
    if (d != NULL && h != NULL && g[d->index].as.i32 <= g[h->index].as.i32) {
        data_end_global_index = d->index;
        /* I dont know why 16 byte align is needed, but warmc do it */
        data_end = (w->globals[d->index].as.i32 + 15) & ~15;
        heap_base_global_index = h->index;
        heap_base = w->globals[h->index].as.i32;
        uint32_t sp_index = get_stack_ptr_global_index(w, heap_base);
        if (sp_index != (uint32_t)-1) {
            stack_ptr_global_index = sp_index;
            stack_ptr = g[sp_index].as.i32;
            stack_size = stack_ptr - data_end;
        } else {
            stack_ptr_global_index = heap_base_global_index;
            stack_ptr = heap_base;
            stack_size = 0;
        }
    }

    WRITE_UINT32(m, data_end_global_index);
    WRITE_UINT64(m, data_end);
    WRITE_UINT32(m, heap_base_global_index);
    WRITE_UINT64(m, heap_base);
    WRITE_UINT32(m, stack_ptr_global_index);
    WRITE_UINT64(m, stack_ptr);
    WRITE_UINT32(m, stack_size);
    return WASMC_OK;
}

static int emit_object_data(AOTModule *m) {

    ALIGN(m, 4);
    uint32_t object_data_count = 0;
    WRITE_UINT32(m, object_data_count);
    return WASMC_OK;
}

int emit_init_data(AOTModule *m) {

    ALIGN(m, 4);
    WRITE_UINT32(m, AOT_SECTION_TYPE_INIT_DATA);

    /* section_size points into the AOT module at the section size field.
     * Now the section size is unknown, we fill the section size field later */
    uint32_t *section_size = (uint32_t *)m->offset;
    m->offset += sizeof(uint32_t);
    uint8_t *section_start = m->offset;

    int err;
    err = emit_memory(m);
    if (err) return err;
    err = emit_table(m);
    if (err) return err;
    err = emit_type(m);
    if (err) return err;
    err = emit_import_global(m);
    if (err) return err;
    err = emit_global(m);
    if (err) return err;
    err = emit_import_function(m);
    if (err) return err;
    err = emit_auxiliary(m);
    if (err) return err;
    err = emit_object_data(m);
    if (err) return err;

    /* Fix section size */
    *section_size = m->offset - section_start;

    ALIGN(m, 4);
    WRITE_UINT32(m, AOT_SECTION_TYPE_TEXT);
    /* The text_size points into the AOT module at the section size field of the
     * text section. At this moment the text section size is unknown,
     * we fill the section size field later */
    m->text_size = (uint32_t *)m->offset;
    m->offset += sizeof(uint32_t);
    const uint32_t literal_size = 0;
    WRITE_UINT32(m, literal_size);
    m->text_start = m->offset;

    return WASMC_OK;
}

int emit_function(AOTModule *m) {

    /* finalize text */
    *m->text_size = (m->offset - m->text_start) + sizeof(uint32_t);

    WASMModule *w = m->wasm_mod;
    ALIGN(m, 4);
    WRITE_UINT32(m, AOT_SECTION_TYPE_FUNCTION);
    uint32_t *section_size = (uint32_t *)m->offset;
    m->offset += sizeof(uint32_t);
    uint8_t *section_start = m->offset;

    for (uint32_t i = 0; i < w->function_count; i++) {
        WRITE_UINT32(m, m->function_text_start[i] - m->text_start);
    }

    for (uint32_t i = 0; i < w->function_count; i++) {
        WRITE_UINT32(m, w->functions[i].typeidx);
    }

    uint32_t max_local_cell_num = 0;
    uint32_t max_stack_cell_num = 0;
    for (uint32_t i = 0; i < w->function_count; i++) {
        WRITE_UINT32(m, max_local_cell_num);
        WRITE_UINT32(m, max_stack_cell_num);
    }

    /* Fix section size */
    *section_size = m->offset - section_start;
    return WASMC_OK;
}

int emit_export(AOTModule *m) {

    WASMModule *w = m->wasm_mod;
    ALIGN(m, 4);
    WRITE_UINT32(m, AOT_SECTION_TYPE_EXPORT);
    uint32_t *section_size = (uint32_t *)m->offset;
    m->offset += sizeof(uint32_t);
    uint8_t *section_start = m->offset;

    WRITE_UINT32(m, w->export_count);
    for (uint32_t i = 0; i < w->export_count; i++) {
        WASMExport *e = &w->exports[i];
        ALIGN(m, 4);
        WRITE_UINT32(m, e->index);
        WRITE_UINT8(m, e->export_desc);
        ALIGN(m, 2);
        uint32_t name_len = e->name_len;
        if (e->name[name_len-1] == '\0') {
            WRITE_UINT16(m, name_len);
            WRITE_BYTE_ARRAY(m, e->name, name_len);
        } else {
            WRITE_UINT16(m, name_len + 1);
            WRITE_BYTE_ARRAY(m, e->name, name_len);
            WRITE_UINT8(m, '\0');
        }
    }

    /* Fix section size */
    *section_size = m->offset - section_start;
    return WASMC_OK;
}

int emit_relocation(AOTModule *m) {

    ALIGN(m, 4);
    WRITE_UINT32(m, AOT_SECTION_TYPE_RELOCATION);
    /* We can't just put 0 as section length because the AOT WAMR runtime
     * try to parse anyway the section even if the section length is 0 */
    uint32_t section_length = 12;
    uint32_t symbol_count = 0;
    uint32_t symbol_table_size = 0;
    uint32_t relocation_group_count = 0;
    WRITE_UINT32(m, section_length);
    WRITE_UINT32(m, symbol_count);
    WRITE_UINT32(m, symbol_table_size);
    ALIGN(m, 4);
    WRITE_UINT32(m, relocation_group_count);
    return WASMC_OK;
}

void aot_module_cleanup(AOTModule *m) {
    if (m->function_text_start != NULL) {
        free(m->function_text_start);
    }
}

