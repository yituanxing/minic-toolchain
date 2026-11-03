#include "target/riscv32/rv32_global_emit.h"
#include "target/common/global_emit.h"
#include "target/common/module_global_emit.h"

#include <stdio.h>

/* RV32's source-free ModuleIR consumer preserves the distinct ELF32 policies
 * which preceded the common aggregate walker: explicit sections use
 * @progbits, weak zero storage has a .bss binding, and scalar tables retain
 * .half/.word spelling. All byte extents and relocation addends are already
 * resolved at the Core Module ingress rather than by C DataLayout here. */
static const MinicCoreType *rv32_module_core_base(
    const MinicCoreTypeContext *types, MinicCoreTypeId id) {
    return minic_core_type_get(types, minic_core_type_unqualified(types, id));
}

static bool rv32_module_array_of_records(
    const MinicCoreTypeContext *types, const MinicCoreType *type) {
    while (type != NULL && type->kind == MINIC_CORE_TYPE_ARRAY) {
        type = rv32_module_core_base(types, type->value.array.element);
        if (type != NULL && type->kind == MINIC_CORE_TYPE_RECORD) return true;
    }
    return false;
}

static bool rv32_module_scalar_width(
    const MinicCoreTypeContext *types, const MinicCoreType *type,
    size_t *width) {
    while (type != NULL && type->kind == MINIC_CORE_TYPE_ARRAY)
        type = rv32_module_core_base(types, type->value.array.element);
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

static const char *rv32_module_directive(size_t width) {
    return width == 1U ? ".byte" : width == 2U ? ".half" :
           width == 4U ? ".word" : width == 8U ? ".8byte" : NULL;
}

static const char *rv32_module_common_directive(size_t width) {
    return width == 1U ? ".byte" : width == 2U ? ".2byte" :
           width == 4U ? ".4byte" : width == 8U ? ".8byte" : NULL;
}

static bool rv32_module_data_symbol(FILE *out,
                                     const MinicCoreModuleGlobal *global,
                                     size_t alignment, bool weak_zero) {
    const char *section;
    if (global->section_name != NULL) {
        if (!minic_multi_elf_safe_section_name(global->section_name) ||
            fprintf(out, ".section %s,\"%s\",@progbits\n",
                    global->section_name,
                    global->is_read_only ? "a" : "aw") < 0) return false;
    } else {
        section = global->is_read_only ? ".section .rodata" :
                  weak_zero ? ".bss" : ".data";
        if (fprintf(out, "%s\n", section) < 0) return false;
    }
    if (!minic_multi_module_emit_elf_binding(out, global) ||
        !minic_multi_module_emit_elf_symbol_visibility(
            out, global->visibility, global->name)) return false;
    return fprintf(out, weak_zero
                       ? ".type %s,@object\n.balign %zu\n%s:\n"
                       : ".type %s, @object\n.balign %zu\n%s:\n",
                   global->name, alignment, global->name) >= 0;
}

bool minic_riscv32_emit_module_global(FILE *out, const MinicTargetInfo *target,
                                      const MinicCoreModule *module, size_t ordinal) {
    const MinicCoreModuleGlobal *global;
    const MinicCoreType *base;
    bool special_aggregate, shared_style, weak_zero;
    size_t scalar_width = 0U, i;
    const char *fallback;
    if (out == NULL || target == NULL ||
        target->architecture != MINIC_TARGET_ARCH_RISCV32 ||
        target->data_layout == NULL || target->data_layout->pointer_size != 4U ||
        target->riscv_isa.xlen != 32U || module == NULL || !module->finalized ||
        module->type_context == NULL || module->globals == NULL ||
        ordinal >= module->global_count)
        return false;
    global = &module->globals[ordinal];
    if (global->is_extern) return !global->is_alias;
    base = rv32_module_core_base(module->type_context, global->core_type_id);
    if (base == NULL || global->is_alias || global->is_thread_local ||
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
        global->initializer_slot_count == 0U &&
        global->relocation_slot_count == 0U &&
        global->payload_kind == MINIC_CORE_MODULE_PAYLOAD_ZERO;
    shared_style = global->storage_size != 0U &&
        global->section_name == NULL && !global->is_weak &&
        (special_aggregate || global->relocation_slot_count != 0U ||
         global->payload_kind == MINIC_CORE_MODULE_PAYLOAD_RECORD_FLEXIBLE ||
         global->payload_kind == MINIC_CORE_MODULE_PAYLOAD_RECORD_RECURSIVE ||
         global->payload_kind == MINIC_CORE_MODULE_PAYLOAD_ARRAY_RECURSIVE);
    if (!shared_style && !special_aggregate &&
        global->relocation_slot_count == 0U && global->storage_size != 0U &&
        !rv32_module_scalar_width(module->type_context, base, &scalar_width) &&
        !weak_zero) return false;
    if (!shared_style && !special_aggregate &&
        global->relocation_slot_count == 0U && scalar_width != 0U &&
        global->initializer_slot_count > global->storage_size / scalar_width)
        return false;
    if (shared_style) {
        switch (global->default_section) {
        case MINIC_CORE_MODULE_SECTION_BSS: fallback = ".bss"; break;
        case MINIC_CORE_MODULE_SECTION_DATA: fallback = ".data"; break;
        case MINIC_CORE_MODULE_SECTION_RODATA: fallback = ".section .rodata"; break;
        case MINIC_CORE_MODULE_SECTION_RELRO: fallback = ".section .data.rel.ro"; break;
        default: return false;
        }
        if (!minic_multi_module_emit_elf_section(out, global, fallback) ||
            !minic_multi_module_emit_elf_binding(out, global) ||
            (!global->is_internal &&
             !minic_multi_module_emit_elf_symbol_visibility(
                 out, global->visibility, global->name)) ||
            fprintf(out, "  .balign %zu\n.type %s,@object\n%s:\n",
                    global->storage_alignment, global->name, global->name) < 0)
            return false;
    } else if (!rv32_module_data_symbol(
                   out, global, global->storage_alignment,
                   weak_zero)) return false;
    /* Preserve GNU zero-extent symbols without creating fabricated bytes. */
    for (i = 0U; i < global->fragment_count; ++i) {
        const MinicCoreModuleFragment *fragment = &global->fragments[i];
        if (fragment->kind == MINIC_CORE_MODULE_FRAGMENT_RELOCATION &&
            fragment->byte_count != 4U) return false;
    }
    if (global->storage_size != 0U &&
        !minic_multi_module_emit_global_fragments(
            out, global, (shared_style || special_aggregate ||
                          global->relocation_slot_count != 0U)
                             ? rv32_module_common_directive
                             : rv32_module_directive,
            false, 4U)) return false;
    return shared_style || weak_zero
        ? fprintf(out, ".size %s,%zu\n",
                  global->name, global->storage_size) >= 0
        : fprintf(out, ".size %s, %zu\n",
                  global->name, global->storage_size) >= 0;
}
