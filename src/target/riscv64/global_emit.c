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
static bool rv64_module_array_recursive(
    const MinicCoreTypeContext *types, const MinicCoreType *type) {
    const MinicCoreType *element;
    if (type == NULL || type->kind != MINIC_CORE_TYPE_ARRAY) return false;
    element = minic_multi_module_global_core_base(
        types, type->value.array.element);
    return element != NULL && (element->kind == MINIC_CORE_TYPE_RECORD ||
                               element->kind == MINIC_CORE_TYPE_ARRAY);
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
        !minic_core_assembler_symbol_lexically_safe(global->name) ||
        !global->emittable || global->is_extern ||
        global->alias_target_index != SIZE_MAX || global->is_thread_local ||
        (global->is_internal && global->is_weak) ||
        global->storage_alignment == 0U ||
        (type = minic_multi_module_global_core_base(
             module->type_context, global->core_type_id)) == NULL)
        return false;
    zero_extent = global->storage_size == 0U && rv64_module_zero_extent_legal(type);
    if (global->storage_size == 0U && !zero_extent) return false;
    if (zero_extent) {
        if (global->fragment_count != 0U ||
            global->reference_count != 0U) return false;
    } else if (global->is_zero_initialized || global->is_tentative) {
        /* The zero payload may contain a target-width relocation; the
         * fragment verifier has already checked its byte placement. */
    } else if (type->kind == MINIC_CORE_TYPE_RECORD) {
        if (!type->value.record.is_complete) return false;
    } else if (rv64_module_array_recursive(module->type_context, type) ||
               (type->kind == MINIC_CORE_TYPE_ARRAY &&
                global->reference_count != 0U)) {
        if (global->fragment_count == 0U) return false;
    } else if (global->reference_count != 0U ||
               !minic_multi_module_global_scalar_width(
                   module->type_context, type, &scalar_width)) {
        return false;
    }
    return true;
}

bool minic_riscv64_emit_module_global(FILE *file, const MinicTargetInfo *target,
                                      const MinicCoreModule *module, size_t ordinal) {
    const MinicCoreModuleGlobal *global;
    MinicModuleElfGlobalPlan plan;
    unsigned int alignment_power;
    const char *fallback_override;
    if (file == NULL || target == NULL ||
        target->architecture != MINIC_TARGET_ARCH_RISCV64 ||
        target->data_layout == NULL || target->data_layout->pointer_size != 8U ||
        module == NULL || !minic_core_module_verify_global(module, ordinal) ||
        ordinal >= module->global_count || module->globals == NULL)
        return false;
    global = &module->globals[ordinal];
    if (!rv64_module_admit_global(module, global) ||
        !minic_riscv64_alignment_power(global->storage_alignment,
                                        &alignment_power)) return false;
    fallback_override = global->is_read_only
        ? (global->reference_count != 0U
               ? ".section .data.rel.ro" : ".section .rodata")
        : ".data";
    if (!minic_multi_module_elf_global_plan_build(
            global, fallback_override, false,
            minic_riscv64_integer_data_directive, true, 8U, &plan) ||
        !minic_multi_module_elf_global_emit_section(file, &plan) ||
        (!global->is_internal &&
         (!minic_multi_module_elf_global_emit_binding(file, &plan) ||
          !minic_multi_module_elf_global_emit_visibility(file, &plan))))
        return false;
    if (fprintf(file, ".type %s, @object\n.align %u\n%s:\n",
                global->name, alignment_power, global->name) < 0)
        return false;
    /* GNU zero-extent definitions contain no payload directives. */
    if (global->storage_size != 0U &&
        !minic_multi_module_elf_global_emit_fragments(file, &plan))
        return false;
    return fprintf(file, ".size %s, %zu\n",
                   global->name, global->storage_size) >= 0;
}

