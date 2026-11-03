#include "target/riscv64/global_emit.h"
#include "target/riscv64/isa.h"
#include "target/common/global_emit.h"
#include "target/common/module_global_emit.h"
#include "core/symbol_identity.h"

#include <stdio.h>

static bool minic_riscv64_alignment_power(size_t alignment, unsigned int *power) {
    unsigned int result;
    size_t value;

    if (alignment == 0U || power == NULL) {
        return false;
    }
    result = 0U;
    value = alignment;
    while (value > 1U) {
        if ((value & 1U) != 0U) {
            return false;
        }
        value >>= 1U;
        result += 1U;
    }
    *power = result;
    return true;
}

static const char *minic_riscv64_integer_data_directive(size_t width) {
    return width == 1U   ? ".byte"
           : width == 2U ? ".half"
           : width == 4U ? ".word"
           : width == 8U ? ".dword"
                         : NULL;
}

bool minic_riscv64_emit_file_asm_bytes(FILE *file, const char *text,
                                       size_t length) {
    if (file == NULL || text == NULL ||
        !minic_riscv64_source_asm_marker_safe(text, length)) {
        return false;
    }
    if (length != 0U && fwrite(text, 1U, length, file) != length) {
        return false;
    }
    return fputc('\n', file) != EOF;
}

/* GNU RV64 globals consume finalized ModuleIR; source-policy and layout
 * are sealed before entering this target writer. */
static const MinicCoreType *rv64_module_core_base(
    const MinicCoreTypeContext *types, MinicCoreTypeId id) {
    return minic_core_type_get(types, minic_core_type_unqualified(types, id));
}

static bool rv64_module_array_recursive(
    const MinicCoreTypeContext *types, const MinicCoreType *type) {
    const MinicCoreType *element;
    if (type == NULL || type->kind != MINIC_CORE_TYPE_ARRAY) return false;
    element = rv64_module_core_base(types, type->value.array.element);
    return element != NULL && (element->kind == MINIC_CORE_TYPE_RECORD ||
                               element->kind == MINIC_CORE_TYPE_ARRAY);
}

static bool rv64_module_scalar_width(
    const MinicCoreTypeContext *types, const MinicCoreType *type,
    size_t *width) {
    while (type != NULL && type->kind == MINIC_CORE_TYPE_ARRAY)
        type = rv64_module_core_base(types, type->value.array.element);
    if (type == NULL || !type->has_layout ||
        (type->kind != MINIC_CORE_TYPE_INTEGER &&
         type->kind != MINIC_CORE_TYPE_POINTER &&
         !(type->kind == MINIC_CORE_TYPE_FLOAT &&
           (type->value.floating.format == MINIC_CORE_FLOAT_BINARY32 ||
            type->value.floating.format == MINIC_CORE_FLOAT_BINARY64))) ||
        (type->size != 1U && type->size != 2U &&
         type->size != 4U && type->size != 8U)) return false;
    *width = type->size;
    return true;
}

static bool rv64_module_zero_extent_legal(const MinicCoreType *type) {
    return type != NULL &&
        ((type->kind == MINIC_CORE_TYPE_RECORD &&
          type->value.record.is_complete && type->value.record.field_count == 0U) ||
         (type->kind == MINIC_CORE_TYPE_ARRAY &&
          type->value.array.is_complete));
}

static bool rv64_module_admit_global(const MinicCoreModule *module,
                                    const MinicCoreModuleGlobal *global) {
    const MinicCoreType *type;
    size_t scalar_width;
    bool zero_extent;
    if (module == NULL || module->type_context == NULL || global == NULL ||
        !minic_core_assembler_symbol_lexically_safe(global->name) || global->is_extern ||
        global->is_alias || global->is_thread_local ||
        (global->is_internal && global->is_weak) ||
        global->storage_alignment == 0U ||
        (type = rv64_module_core_base(module->type_context,
                                      global->core_type_id)) == NULL)
        return false;
    zero_extent = global->storage_size == 0U && rv64_module_zero_extent_legal(type);
    if (global->storage_size == 0U && !zero_extent) return false;
    if (zero_extent) {
        if (global->initializer_slot_count != 0U ||
            global->relocation_slot_count != 0U) return false;
    } else if (global->is_zero_initialized || global->is_tentative) {
        if (global->initializer_slot_count != 0U) return false;
    } else if (type->kind == MINIC_CORE_TYPE_RECORD) {
        if (!type->value.record.is_complete ||
            global->initializer_slot_count == 0U) return false;
    } else if (rv64_module_array_recursive(module->type_context, type) ||
               (type->kind == MINIC_CORE_TYPE_ARRAY &&
                global->relocation_slot_count != 0U)) {
        if (global->initializer_slot_count == 0U) return false;
    } else if (global->relocation_slot_count != 0U ||
               !rv64_module_scalar_width(module->type_context, type,
                                          &scalar_width) ||
               global->initializer_slot_count >
                    global->storage_size / scalar_width) {
        return false;
    }
    return true;
}

static const char *rv64_module_visibility(MinicCoreModuleVisibility visibility) {
    switch (visibility) {
    case MINIC_CORE_MODULE_VISIBILITY_DEFAULT: return NULL;
    case MINIC_CORE_MODULE_VISIBILITY_HIDDEN: return ".hidden";
    case MINIC_CORE_MODULE_VISIBILITY_INTERNAL: return ".internal";
    case MINIC_CORE_MODULE_VISIBILITY_PROTECTED: return ".protected";
    default: return NULL;
    }
}

bool minic_riscv64_emit_module_global(FILE *file, const MinicTargetInfo *target,
                                      const MinicCoreModule *module, size_t ordinal) {
    const MinicCoreModuleGlobal *global;
    unsigned int alignment_power;
    const char *visibility;
    if (file == NULL || target == NULL ||
        target->architecture != MINIC_TARGET_ARCH_RISCV64 ||
        target->data_layout == NULL || target->data_layout->pointer_size != 8U ||
        module == NULL || !module->finalized ||
        ordinal >= module->global_count || module->globals == NULL)
        return false;
    global = &module->globals[ordinal];
    if (!rv64_module_admit_global(module, global) ||
        !minic_riscv64_alignment_power(global->storage_alignment, &alignment_power) ||
        (global->visibility != MINIC_CORE_MODULE_VISIBILITY_DEFAULT &&
         rv64_module_visibility(global->visibility) == NULL)) return false;
    if (global->section_name != NULL) {
        if (!minic_multi_elf_safe_section_name(global->section_name) ||
            fprintf(file, ".section %s,\"%s\",@progbits\n",
                    global->section_name,
                    global->is_read_only ? "a" : "aw") < 0) return false;
    } else if (fprintf(file, "%s\n",
                       global->is_read_only
                           ? (global->relocation_slot_count != 0U
                                ? ".section .data.rel.ro" : ".section .rodata")
                           : ".data") < 0) {
        return false;
    }
    if (!global->is_internal) {
        visibility = rv64_module_visibility(global->visibility);
        if (fprintf(file, "%s %s\n", global->is_weak ? ".weak" : ".globl",
                    global->name) < 0 ||
            (visibility != NULL &&
             fprintf(file, "%s %s\n", visibility, global->name) < 0))
            return false;
    }
    if (fprintf(file, ".type %s, @object\n.align %u\n%s:\n",
                global->name, alignment_power, global->name) < 0)
        return false;
    /* GNU zero-extent definitions contain no payload directives. */
    if (global->storage_size != 0U &&
        !minic_multi_module_emit_global_fragments(
            file, global, minic_riscv64_integer_data_directive, true, 8U))
        return false;
    return fprintf(file, ".size %s, %zu\n",
                   global->name, global->storage_size) >= 0;
}

