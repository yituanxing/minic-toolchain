#include "target/riscv64/rv32_global_emit.h"
#include "target/data_layout.h"
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

static bool rv32_data_symbol(FILE *out, const MinicGlobalObject *obj,
                             size_t alignment) {
    if (obj->section_name != NULL) {
        if (fprintf(out, ".section %s,\"%s\",@progbits\n",
                    obj->section_name, obj->is_read_only ? "a" : "aw") < 0)
            return false;
    } else if (fprintf(out, "%s\n",
                       obj->is_read_only ? ".section .rodata" : ".data") < 0) {
        return false;
    }
    if (!obj->is_internal &&
        fprintf(out, obj->is_weak ? ".weak %s\n" : ".globl %s\n",
                obj->name) < 0) return false;
    if (obj->visibility == MINIC_SYMBOL_VISIBILITY_HIDDEN &&
        fprintf(out, ".hidden %s\n", obj->name) < 0) return false;
    if (obj->visibility == MINIC_SYMBOL_VISIBILITY_INTERNAL &&
        fprintf(out, ".internal %s\n", obj->name) < 0) return false;
    if (obj->visibility == MINIC_SYMBOL_VISIBILITY_PROTECTED &&
        fprintf(out, ".protected %s\n", obj->name) < 0) return false;
    return fprintf(out, ".type %s, @object\n.balign %zu\n%s:\n",
                   obj->name, alignment, obj->name) >= 0;
}
bool minic_riscv32_emit_flat_global(FILE *out,
                                    const MinicC0Program *program,
                                    const MinicTargetInfo *target,
                                    const MinicGlobalObject *obj) {
    const char *directive;
    MinicType scalar;
    size_t bytes, alignment, width, scalar_alignment, i, capacity;
    if (out == NULL || program == NULL || target == NULL ||
        target->riscv_isa.xlen != 32U || obj == NULL ||
        obj->name == NULL || obj->name[0] == '\0') return false;
    if (obj->is_extern) {
        if (obj->alias_target != MINIC_GLOBAL_OBJECT_INVALID ||
            obj->is_block_scope_extern_only) return true;
        return !obj->is_weak || obj->is_internal ||
               fprintf(out, ".weak %s\n", obj->name) >= 0;
    }
    /* Relocation-bearing structures/arrays require the shared recursive
     * aggregate emission and ILP32 relocation writer. Never emit LP64
     * .dword addresses or invent RV32 pointer relocations. */
    if (obj->relocation_count != 0U || obj->union_selection_count != 0U ||
        obj->flexible_array_initializer_count != 0U ||
        obj->alias_target != MINIC_GLOBAL_OBJECT_INVALID ||
        !minic_data_layout_global_object(
            target->data_layout, program, obj, &bytes, &alignment) ||
        alignment == 0U || bytes == 0U) return false;
    scalar = obj->type;
    while (minic_type_is_array(scalar)) {
        const MinicArrayType *arr =
            minic_c0_program_array_type(program, scalar.array_type_id);
        if (arr == NULL) return false;
        scalar = arr->element_type;
    }
    if ((!minic_type_is_integer(scalar) && !minic_type_is_pointer(scalar)) ||
        !minic_data_layout_type(target->data_layout, program, scalar,
                                &width, &scalar_alignment) ||
        (width != 1U && width != 2U && width != 4U) ||
        bytes % width != 0U) return false;
    if (obj->explicit_alignment > alignment)
        alignment = obj->explicit_alignment;
    if ((alignment & (alignment - 1U)) != 0U) return false;
    capacity = bytes / width;
    if (obj->initializer_count > capacity ||
        ((obj->is_zero_initialized || obj->is_tentative) &&
          obj->initializer_count != 0U)) return false;
    if (!rv32_data_symbol(out, obj, alignment)) return false;
    if (obj->is_zero_initialized || obj->is_tentative) {
        return fprintf(out, "  .zero %zu\n.size %s, %zu\n",
                       bytes, obj->name, bytes) >= 0;
    }
    directive = width == 1U ? ".byte" : width == 2U ? ".half" : ".word";
    for (i = 0U; i < obj->initializer_count; ++i) {
        uint64_t value = obj->initializer_values[i];
        if (width == 1U) value &= UINT64_C(255);
        if (width == 2U) value &= UINT64_C(65535);
        if (width == 4U) value &= UINT64_C(4294967295);
        if (fprintf(out, "  %s %" PRIu64 "\n", directive, value) < 0)
            return false;
    }
    if (bytes - obj->initializer_count * width != 0U &&
        fprintf(out, "  .zero %zu\n",
                bytes - obj->initializer_count * width) < 0) return false;
    return fprintf(out, ".size %s, %zu\n", obj->name, bytes) >= 0;
}
