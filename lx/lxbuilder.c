/* ============================================================================
 * tools/lxbuilder.c  —  LX Executable Builder Implementation
 *
 * Creates OS/2 LX format executables and DLLs from code and data sections.
 * ========================================================================== */

#include "lxformat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_OBJECTS 64
#define MAX_EXPORTS 256
#define MAX_IMPORTS 256
#define MAX_FIXUPS  1024
#define PAGE_SIZE   4096

/* ── Internal Structures ─────────────────────────────────────────────────── */

typedef struct {
    char     name[64];
    uint8_t *data;
    uint32_t size;
    uint32_t virtual_size;
    uint32_t flags;
    uint32_t base_addr;
} LXObjectData;

typedef struct {
    char     name[128];
    uint16_t ordinal;
    uint32_t object;
    uint32_t offset;
} LXExportData;

typedef struct {
    char     module[32];
    char     function[128];
    uint16_t ordinal;
} LXImportData;

typedef struct {
    uint32_t source_object;
    uint32_t source_offset;
    uint32_t target_object;
    uint32_t target_offset;
    uint8_t  type;
} LXFixupData;

struct LXBuilder {
    /* Module info */
    char module_name[256];
    char description[256];
    uint32_t module_version;
    uint32_t module_flags;
    uint16_t cpu_type;
    
    /* Entry point */
    uint32_t entry_object;
    uint32_t entry_offset;
    
    /* Stack */
    uint32_t stack_size;
    
    /* Objects (sections) */
    LXObjectData objects[MAX_OBJECTS];
    uint32_t object_count;
    
    /* Exports */
    LXExportData exports[MAX_EXPORTS];
    uint32_t export_count;
    
    /* Imports */
    LXImportData imports[MAX_IMPORTS];
    uint32_t import_count;
    
    /* Fixups */
    LXFixupData fixups[MAX_FIXUPS];
    uint32_t fixup_count;
};

/* ══════════════════════════════════════════════════════════════════════════
   BUILDER API IMPLEMENTATION
   ══════════════════════════════════════════════════════════════════════════ */

LXBuilder* lx_builder_new(void)
{
    LXBuilder *builder = calloc(1, sizeof(LXBuilder));
    if (!builder) return NULL;
    
    /* Default values */
    builder->module_flags = LXMF_PROGRAM;
    builder->cpu_type = LXCPU_ARM64;
    builder->module_version = 0x00010000;  /* Version 1.0 */
    builder->stack_size = 0x10000;         /* 64KB stack */
    builder->entry_object = 1;             /* Object 1 by default */
    builder->entry_offset = 0;
    
    strcpy(builder->module_name, "UNNAMED");
    
    return builder;
}

void lx_builder_free(LXBuilder *builder)
{
    if (!builder) return;
    
    /* Free object data */
    for (uint32_t i = 0; i < builder->object_count; i++) {
        if (builder->objects[i].data) {
            free(builder->objects[i].data);
        }
    }
    
    free(builder);
}

void lx_builder_set_type(LXBuilder *builder, uint32_t flags)
{
    if (builder) {
        builder->module_flags = flags;
    }
}

void lx_builder_set_name(LXBuilder *builder, const char *name)
{
    if (builder && name) {
        strncpy(builder->module_name, name, sizeof(builder->module_name) - 1);
    }
}

void lx_builder_set_description(LXBuilder *builder, const char *desc)
{
    if (builder && desc) {
        strncpy(builder->description, desc, sizeof(builder->description) - 1);
    }
}

void lx_builder_set_version(LXBuilder *builder, uint32_t version)
{
    if (builder) {
        builder->module_version = version;
    }
}

void lx_builder_set_cpu(LXBuilder *builder, uint16_t cpu_type)
{
    if (builder) {
        builder->cpu_type = cpu_type;
    }
}

int lx_builder_add_object(LXBuilder *builder, const char *name,
                          const void *data, uint32_t size, uint32_t flags)
{
    if (!builder || builder->object_count >= MAX_OBJECTS) {
        return -1;
    }
    
    LXObjectData *obj = &builder->objects[builder->object_count];
    
    /* Copy name */
    if (name) {
        strncpy(obj->name, name, sizeof(obj->name) - 1);
    } else {
        snprintf(obj->name, sizeof(obj->name), "OBJECT%u", 
                 builder->object_count + 1);
    }
    
    /* Allocate and copy data */
    obj->size = size;
    obj->virtual_size = (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);  /* Round up */
    obj->flags = flags;
    
    if (data && size > 0) {
        obj->data = malloc(obj->virtual_size);
        if (!obj->data) return -1;
        
        memcpy(obj->data, data, size);
        /* Zero-fill remainder */
        if (obj->virtual_size > size) {
            memset(obj->data + size, 0, obj->virtual_size - size);
        }
    } else {
        obj->data = NULL;
    }
    
    /* Assign base address (sequential) */
    if (builder->object_count == 0) {
        obj->base_addr = 0x10000;  /* Start at 64KB */
    } else {
        LXObjectData *prev = &builder->objects[builder->object_count - 1];
        obj->base_addr = prev->base_addr + prev->virtual_size;
    }
    
    builder->object_count++;
    return builder->object_count - 1;  /* Return object index */
}

void lx_builder_set_entry_point(LXBuilder *builder, uint32_t object, 
                                uint32_t offset)
{
    if (builder) {
        builder->entry_object = object;
        builder->entry_offset = offset;
    }
}

void lx_builder_set_stack_size(LXBuilder *builder, uint32_t size)
{
    if (builder) {
        builder->stack_size = size;
    }
}

int lx_builder_add_export(LXBuilder *builder, const char *name,
                          uint32_t object, uint32_t offset, uint16_t ordinal)
{
    if (!builder || !name || builder->export_count >= MAX_EXPORTS) {
        return -1;
    }
    
    LXExportData *exp = &builder->exports[builder->export_count];
    
    strncpy(exp->name, name, sizeof(exp->name) - 1);
    exp->ordinal = ordinal ? ordinal : (builder->export_count + 1);
    exp->object = object;
    exp->offset = offset;
    
    builder->export_count++;
    return 0;
}

int lx_builder_add_import(LXBuilder *builder, const char *module,
                          const char *function, uint16_t ordinal)
{
    if (!builder || !module || !function || 
        builder->import_count >= MAX_IMPORTS) {
        return -1;
    }
    
    LXImportData *imp = &builder->imports[builder->import_count];
    
    strncpy(imp->module, module, sizeof(imp->module) - 1);
    strncpy(imp->function, function, sizeof(imp->function) - 1);
    imp->ordinal = ordinal;
    
    builder->import_count++;
    return 0;
}

int lx_builder_add_fixup(LXBuilder *builder, uint32_t source_object,
                         uint32_t source_offset, uint32_t target_object,
                         uint32_t target_offset)
{
    if (!builder || builder->fixup_count >= MAX_FIXUPS) {
        return -1;
    }
    
    LXFixupData *fixup = &builder->fixups[builder->fixup_count];
    
    fixup->source_object = source_object;
    fixup->source_offset = source_offset;
    fixup->target_object = target_object;
    fixup->target_offset = target_offset;
    fixup->type = 0x07;  /* 32-bit offset */
    
    builder->fixup_count++;
    return 0;
}

/* ══════════════════════════════════════════════════════════════════════════
   LX FILE WRITER
   ══════════════════════════════════════════════════════════════════════════ */

int lx_builder_write(LXBuilder *builder, const char *filename)
{
    if (!builder || !filename) return -1;
    
    FILE *f = fopen(filename, "wb");
    if (!f) {
        perror("Failed to create LX file");
        return -1;
    }
    
    /* Calculate offsets */
    uint32_t header_size = sizeof(LXHeader);
    uint32_t object_table_offset = header_size;
    uint32_t object_table_size = builder->object_count * sizeof(LXObject);
    
    uint32_t page_table_offset = object_table_offset + object_table_size;
    uint32_t total_pages = 0;
    for (uint32_t i = 0; i < builder->object_count; i++) {
        total_pages += (builder->objects[i].virtual_size + PAGE_SIZE - 1) / PAGE_SIZE;
    }
    uint32_t page_table_size = total_pages * sizeof(LXPage);
    
    uint32_t data_offset = page_table_offset + page_table_size;
    data_offset = (data_offset + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);  /* Align */
    
    /* Build header */
    LXHeader header = {0};
    header.signature[0] = LX_MAGIC1;
    header.signature[1] = LX_MAGIC2;
    header.byte_order = 0;  /* Little endian */
    header.word_order = 0;
    header.format_level = 0;
    header.cpu_type = builder->cpu_type;
    header.os_type = LXOS_OS2;
    header.module_version = builder->module_version;
    header.module_flags = builder->module_flags;
    header.module_pages = total_pages;
    header.eip_object = builder->entry_object;
    header.eip = builder->entry_offset;
    header.esp_object = builder->object_count;  /* Last object for stack */
    header.esp = builder->stack_size;
    header.page_size = PAGE_SIZE;
    header.object_table_offset = object_table_offset;
    header.object_count = builder->object_count;
    header.object_page_table_offset = page_table_offset;
    header.data_pages_offset = data_offset;
    header.stack_size = builder->stack_size;
    
    /* Write header */
    fwrite(&header, sizeof(header), 1, f);
    
    /* Write object table */
    uint32_t page_index = 0;
    for (uint32_t i = 0; i < builder->object_count; i++) {
        LXObject obj = {0};
        obj.virtual_size = builder->objects[i].virtual_size;
        obj.reloc_base_addr = builder->objects[i].base_addr;
        obj.object_flags = builder->objects[i].flags;
        obj.page_table_index = page_index + 1;  /* 1-based */
        obj.page_table_entries = (builder->objects[i].virtual_size + PAGE_SIZE - 1) / PAGE_SIZE;
        
        fwrite(&obj, sizeof(obj), 1, f);
        page_index += obj.page_table_entries;
    }
    
    /* Write page table */
    uint32_t current_data_offset = 0;
    for (uint32_t i = 0; i < builder->object_count; i++) {
        uint32_t pages = (builder->objects[i].virtual_size + PAGE_SIZE - 1) / PAGE_SIZE;
        
        for (uint32_t p = 0; p < pages; p++) {
            LXPage page = {0};
            uint32_t page_offset = p * PAGE_SIZE;
            uint32_t remaining = builder->objects[i].size > page_offset ?
                                builder->objects[i].size - page_offset : 0;
            
            page.data_size = (remaining > PAGE_SIZE) ? PAGE_SIZE : remaining;
            page.page_data_offset = data_offset + current_data_offset;
            page.flags = (page.data_size > 0) ? LXPAGE_VALID : LXPAGE_ZEROED;
            
            fwrite(&page, sizeof(page), 1, f);
            
            if (page.data_size > 0) {
                current_data_offset += PAGE_SIZE;
            }
        }
    }
    
    /* Pad to data offset */
    long current_pos = ftell(f);
    while (current_pos < data_offset) {
        fputc(0, f);
        current_pos++;
    }
    
    /* Write page data */
    for (uint32_t i = 0; i < builder->object_count; i++) {
        if (builder->objects[i].data && builder->objects[i].size > 0) {
            fwrite(builder->objects[i].data, 1, builder->objects[i].virtual_size, f);
        }
    }
    
    fclose(f);
    
    printf("✓ Created LX %s: %s (%u objects, %u pages)\n",
           (builder->module_flags & LXMF_LIBRARY) ? "DLL" : "EXE",
           filename, builder->object_count, total_pages);
    
    return 0;
}

/* ══════════════════════════════════════════════════════════════════════════
   UTILITY FUNCTIONS
   ══════════════════════════════════════════════════════════════════════════ */

int lx_create_exe(const char *filename, const void *code, uint32_t code_size,
                  uint32_t entry_offset, uint32_t stack_size)
{
    LXBuilder *builder = lx_builder_new();
    if (!builder) return -1;
    
    /* Set as executable */
    lx_builder_set_type(builder, LXMF_PROGRAM);
    lx_builder_set_stack_size(builder, stack_size);
    
    /* Add code section */
    lx_builder_add_object(builder, ".text", code, code_size,
                         LXOBJ_READABLE | LXOBJ_EXECUTABLE);
    
    /* Add data section (empty) */
    lx_builder_add_object(builder, ".data", NULL, 4096,
                         LXOBJ_READABLE | LXOBJ_WRITABLE);
    
    /* Set entry point */
    lx_builder_set_entry_point(builder, 1, entry_offset);
    
    /* Write file */
    int result = lx_builder_write(builder, filename);
    
    lx_builder_free(builder);
    return result;
}

int lx_create_dll(const char *filename, const char *dll_name,
                  const void *code, uint32_t code_size,
                  const char **exports, uint32_t export_count)
{
    LXBuilder *builder = lx_builder_new();
    if (!builder) return -1;
    
    /* Set as DLL */
    lx_builder_set_type(builder, LXMF_LIBRARY | LXMF_INIT_INSTANCE);
    lx_builder_set_name(builder, dll_name);
    
    /* Add code section */
    lx_builder_add_object(builder, ".text", code, code_size,
                         LXOBJ_READABLE | LXOBJ_EXECUTABLE);
    
    /* Add exports */
    for (uint32_t i = 0; i < export_count && i < MAX_EXPORTS; i++) {
        lx_builder_add_export(builder, exports[i], 1, i * 16, i + 1);
    }
    
    /* Write file */
    int result = lx_builder_write(builder, filename);
    
    lx_builder_free(builder);
    return result;
}
