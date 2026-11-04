#include "target/riscv32/rv32_global_emit.h"
#include "target/common/global_emit.h"
#include "target/common/module_global_emit.h"

#include <stdio.h>

/* RV32's source-free ModuleIR consumer preserves the distinct ELF32 policies
 * which preceded the common aggregate walker: explicit sections use
 * @progbits, weak zero storage has a .bss binding, and scalar tables retain
 * .half/.word spelling. All byte extents and relocation addends are already
 * resolved at the Core Module ingress rather than by C DataLayout here. */
static bool rv32_module_array_of_records(
    const MinicCoreTypeContext *types, const MinicCoreType *type) {
    while (type != NULL && type->kind == MINIC_CORE_TYPE_ARRAY) {
        type = minic_multi_module_global_core_base(
            types, type->value.array.element);
        if (type != NULL && type->kind == MINIC_CORE_TYPE_RECORD) return true;
    }
    return false;
}

static const char *rv32_module_directive(size_t width) {
    return width == 1U ? ".byte" : width == 2U ? ".half" :
           width == 4U ? ".word" : width == 8U ? ".8byte" : NULL;
}

static const char *rv32_module_common_directive(size_t width) {
    return width == 1U ? ".byte" : width == 2U ? ".2byte" :
           width == 4U ? ".4byte" : width == 8U ? ".8byte" : NULL;
}

/* A tentative or explicit empty initializer has no semantic scalar slots.
 * The C ingress separately records that definition status; Core fragments
 * prove that its entire materialized payload is zero. An explicitly listed
 * {0} record is byte-identical but must NOT acquire this weak-zero policy. */
static bool rv32_module_weak_zero_payload(const MinicCoreModuleGlobal *global) {
    const MinicCoreModuleFragment *fragment;
    if (global == NULL ||
        (!global->is_tentative && !global->is_zero_initialized) ||
        global->reference_count != 0U)
        return false;
    if (global->storage_size == 0U) return global->fragment_count == 0U;
    if (global->fragment_count != 1U || global->fragments == NULL)
        return false;
    fragment = &global->fragments[0];
    return fragment->kind == MINIC_CORE_MODULE_FRAGMENT_ZERO &&
        fragment->byte_offset == 0U &&
        fragment->byte_count == global->storage_size;
}

bool minic_riscv32_emit_module_global(FILE *out, const MinicTargetInfo *target,
                                      const MinicCoreModule *module, size_t ordinal) {
    const MinicCoreModuleGlobal *global;
    const MinicCoreType *base;
    MinicModuleElfGlobalPlan plan;
    bool special_aggregate, shared_style, weak_zero;
    size_t scalar_width = 0U;
    const char *fallback_override;
    MinicModuleDataDirectiveFn data_directive;
    if (out == NULL || target == NULL ||
        target->architecture != MINIC_TARGET_ARCH_RISCV32 ||
        target->data_layout == NULL || target->data_layout->pointer_size != 4U ||
        target->riscv_isa.xlen != 32U || module == NULL || !module->finalized ||
        module->type_context == NULL || module->globals == NULL ||
        ordinal >= module->global_count)
        return false;
    global = &module->globals[ordinal];
    if (global->is_extern) return global->alias_target_index == SIZE_MAX;
    base = minic_multi_module_global_core_base(
        module->type_context, global->core_type_id);
    if (base == NULL || global->alias_target_index != SIZE_MAX || global->is_thread_local ||
        (global->fragment_count != 0U && global->fragments == NULL) ||
        global->name == NULL || global->name[0] == '\0' ||
        global->storage_alignment == 0U ||
        (global->storage_alignment & (global->storage_alignment - 1U)) != 0U)
        return false;
    special_aggregate = base->kind == MINIC_CORE_TYPE_RECORD ||
        rv32_module_array_of_records(module->type_context, base);
    /* The preexisting RV32 zero-size and weak-undefined definitions are
     * distinct ELF symbol policies, not C layout computations. */
    weak_zero = global->is_weak && global->section_name == NULL &&
        rv32_module_weak_zero_payload(global);
    shared_style = global->storage_size != 0U &&
        global->section_name == NULL && !global->is_weak &&
        (special_aggregate || global->reference_count != 0U);
    if (!shared_style && !special_aggregate &&
        global->reference_count == 0U && global->storage_size != 0U &&
        !minic_multi_module_global_scalar_width(
            module->type_context, base, &scalar_width) &&
        !weak_zero) return false;
    fallback_override = shared_style ? NULL :
        global->is_read_only ? ".section .rodata" :
        weak_zero ? ".bss" : ".data";
    data_directive = (shared_style || special_aggregate ||
                      global->reference_count != 0U)
                         ? rv32_module_common_directive
                         : rv32_module_directive;
    if (!minic_multi_module_elf_global_plan_build(
            global, fallback_override, shared_style,
            data_directive, false, 4U, &plan) ||
        !minic_multi_module_elf_global_emit_section(out, &plan) ||
        !minic_multi_module_elf_global_emit_binding(out, &plan) ||
        ((!global->is_internal || !shared_style) &&
         !minic_multi_module_elf_global_emit_visibility(out, &plan)))
        return false;
    if (shared_style) {
        if (fprintf(out, "  .balign %zu\n.type %s,@object\n%s:\n",
                    global->storage_alignment, global->name, global->name) < 0)
            return false;
    } else if (fprintf(out, weak_zero
                           ? ".type %s,@object\n.balign %zu\n%s:\n"
                           : ".type %s, @object\n.balign %zu\n%s:\n",
                       global->name, global->storage_alignment,
                       global->name) < 0) {
        return false;
    }
    /* A zero-extent GNU object has a real symbol but no invented bytes. */
    if (global->storage_size != 0U &&
        !minic_multi_module_elf_global_emit_fragments(out, &plan)) return false;
    return shared_style || weak_zero
        ? fprintf(out, ".size %s,%zu\n",
                  global->name, global->storage_size) >= 0
        : fprintf(out, ".size %s, %zu\n",
                  global->name, global->storage_size) >= 0;
}
