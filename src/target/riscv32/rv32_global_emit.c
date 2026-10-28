#include "target/riscv32/rv32_global_emit.h"
#include "target/data_layout.h"
#include "target/common/global_emit.h"
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

/* Reuse the same target-independent record/array initializer walk used by
 * AArch64, LoongArch64 and the RV64 refactor. RV32 owns ONLY the four-byte
 * relocation syntax and target data model; no replicated record serializer.
 * This callback also verifies true ILP32 width, preventing LP64 .8byte
 * relocations from silently entering ELF32 .o objects. */
static bool rv32_reloc(FILE *out, const MinicC0Program *program,
                       const MinicTargetInfo *target,
                       const MinicGlobalRelocation *reloc, size_t width) {
    const char *name;
    int64_t addend;
    if (out == NULL || program == NULL || target == NULL || reloc == NULL ||
        target->architecture != MINIC_TARGET_ARCH_RISCV32 ||
        target->data_layout->pointer_size != 4U || width != 4U)
        return false;
    /* GNU computed goto: same source-label identity as RV64.
     * The Core block emitter supplies .Luser_<statement-id> aliases. */
    if (reloc->target_kind == MINIC_GLOBAL_RELOCATION_LABEL) {
        const MinicStatement *stmt =
            minic_c0_program_statement(program, reloc->target_id);
        return stmt != NULL && stmt->kind == MINIC_STATEMENT_LABEL &&
               fprintf(out, "  .4byte .Luser_%zu\n",
                       reloc->target_id) >= 0;
    }
    name = minic_multi_global_relocation_target_name(program, reloc);
    if (name == NULL || name[0] == '\0' ||
        !minic_data_layout_global_relocation_target_addend(
            target->data_layout, program, reloc, &addend)) return false;
    return addend == 0 ?
        fprintf(out, "  .4byte %s\n", name) >= 0 :
        fprintf(out, "  .4byte %s%+" PRId64 "\n", name, addend) >= 0;
}

static bool rv32_data_symbol(FILE *out, const MinicGlobalObject *obj,
                             size_t alignment) {
    if (obj->section_name != NULL) {
        if (!minic_multi_elf_safe_section_name(obj->section_name) ||
            fprintf(out, ".section %s,\"%s\",@progbits\n",
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
/* A file-scope table of records is not a flat scalar array even if
 * there are no relocations: zlib's fixed Huffman code arrays hold 1536
 * initializer scalars and must go through the shared DataLayout walker. */
static bool rv32_array_of_records(const MinicC0Program *program, MinicType type) {
    while (minic_type_is_array(type)) {
        const MinicArrayType *array =
            minic_c0_program_array_type(program, type.array_type_id);
        if (array == NULL) return false;
        type = array->element_type;
        if (minic_type_is_record(type)) return true;
    }
    return false;
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
    /* An explicitly empty GNU array (common in config-disabled Linux
     * tables) is a defined ELF symbol of size zero. DataLayout still owns
     * its extent: do not invent an element or reject the definition. */
    if (obj->alias_target == MINIC_GLOBAL_OBJECT_INVALID &&
        obj->initializer_count == 0U && obj->relocation_count == 0U &&
        obj->union_selection_count == 0U &&
        obj->flexible_array_initializer_count == 0U &&
        minic_data_layout_global_object(target->data_layout, program, obj,
                                        &bytes, &alignment) &&
        bytes == 0U && alignment != 0U) {
        if (obj->explicit_alignment > alignment)
            alignment = obj->explicit_alignment;
        return (alignment & (alignment - 1U)) == 0U &&
               rv32_data_symbol(out, obj, alignment) &&
               fprintf(out, ".size %s, 0\n", obj->name) >= 0;
    }
    /* Linux exports weak, uninitialized protocol records as definitions.
     * The common flat serializer deliberately owns only strong symbols;
     * keep weak ELF binding here while reusing its DataLayout byte count.
     * This applies equally to weak zero arrays and scalar declarations. */
    if (obj->is_weak && obj->section_name == NULL &&
        obj->alias_target == MINIC_GLOBAL_OBJECT_INVALID &&
        obj->initializer_count == 0U && obj->relocation_count == 0U &&
        obj->union_selection_count == 0U &&
        obj->flexible_array_initializer_count == 0U &&
        minic_data_layout_global_object(target->data_layout, program, obj,
                                        &bytes, &alignment) &&
        alignment != 0U && (alignment & (alignment - 1U)) == 0U) {
        if (obj->explicit_alignment > alignment)
            alignment = obj->explicit_alignment;
        if ((alignment & (alignment - 1U)) != 0U ||
            fprintf(out, "%s\n.weak %s\n",
                    obj->is_read_only ? ".section .rodata" : ".bss",
                    obj->name) < 0 ||
            !minic_multi_emit_elf_symbol_visibility(out, obj->visibility, obj->name) ||
            fprintf(out, ".type %s,@object\n.balign %zu\n%s:\n"
                         "  .zero %zu\n.size %s,%zu\n",
                    obj->name, alignment, obj->name, bytes,
                    obj->name, bytes) < 0)
            return false;
        return true;
    }
    /* Linux __ADDRESSABLE emits a pointer initialized by relocation into an
     * explicit .discard.addressable ELF section. The already-certified
     * common zero-relocation walker owns the storage layout/offset checks;
     * RV32 owns only section flags and .4byte target symbol rendering.
     * This is not a one-off Linux symbol special case. */
    if (obj->section_name != NULL &&
        obj->is_zero_initialized && obj->initializer_count == 0U &&
        obj->relocation_count != 0U &&
        obj->alias_target == MINIC_GLOBAL_OBJECT_INVALID &&
        obj->union_selection_count == 0U &&
        obj->flexible_array_initializer_count == 0U &&
        minic_data_layout_global_object(
            target->data_layout, program, obj, &bytes, &alignment) &&
        bytes != 0U && alignment != 0U &&
        (alignment & (alignment - 1U)) == 0U) {
        if (obj->explicit_alignment > alignment)
            alignment = obj->explicit_alignment;
        if ((alignment & (alignment - 1U)) != 0U ||
            !rv32_data_symbol(out, obj, alignment) ||
            !minic_multi_emit_zero_relocation_global(
                out, program, target, obj, bytes, rv32_reloc))
            return false;
        return fprintf(out, ".size %s, %zu\n", obj->name, bytes) >= 0;
    }
    /* Explicit-section records and weak initialized definitions both need
     * target-owned ELF binding, but the same shared DataLayout payload walk.
     * This includes Linux __setup records and weak namespace initializers. */
    if ((obj->section_name != NULL || obj->is_weak) &&
        (minic_type_is_record(obj->type) ||
         rv32_array_of_records(program, obj->type)) &&
        obj->alias_target == MINIC_GLOBAL_OBJECT_INVALID &&
        minic_data_layout_global_object(
            target->data_layout, program, obj, &bytes, &alignment) &&
        alignment != 0U) {
        bool emitted;
        if (obj->explicit_alignment > alignment)
            alignment = obj->explicit_alignment;
        if ((alignment & (alignment - 1U)) != 0U ||
            !rv32_data_symbol(out, obj, alignment))
            return false;
        /* A section attribute does not make an uninitialized aggregate a
         * recursive initializer. Linux's .data boot pageset and the usual
         * .bss aggregates are bytewise zero, including nested records and
         * their DataLayout padding. Keep explicit section ownership while
         * using the same whole-object zero rule as the common serializer. */
        if (bytes == 0U && obj->initializer_count == 0U &&
            obj->relocation_count == 0U &&
            obj->union_selection_count == 0U &&
            obj->flexible_array_initializer_count == 0U) {
            /* GNU permits zero-sized record definitions. Emit their section,
             * alignment and ELF symbol, but no storage bytes. */
            emitted = true;
        } else if (obj->initializer_count == 0U && obj->relocation_count == 0U &&
            obj->union_selection_count == 0U &&
            obj->flexible_array_initializer_count == 0U) {
            emitted = fprintf(out, "  .zero %zu\n", bytes) >= 0;
        } else if (obj->is_tentative) {
            return false;
        } else if (minic_type_is_record(obj->type)) {
            const MinicRecord *record =
                minic_c0_program_record(program, obj->type.record_id);
            if (record == NULL) return false;
            emitted = minic_multi_record_initializer_needs_recursive(record, obj)
                ? minic_multi_emit_recursive_aggregate_global(
                      out, program, target, obj, bytes, rv32_reloc, NULL)
                : minic_multi_emit_flat_record_global(
                      out, program, target, obj, bytes, rv32_reloc, NULL);
        } else {
            emitted = minic_multi_emit_record_array_global(
                out, program, target, obj, bytes, rv32_reloc);
        }
        return emitted &&
               fprintf(out, ".size %s, %zu\n", obj->name, bytes) >= 0;
    }
    /* Linux initcall_levels and initcall_level_names are section-owned
     * arrays of pointers. The common relocation walker already emits
     * each scalar slot with validated ILP32 offsets; the RV32 target only
     * emits .init.data section and ELF32 relocation syntax. */
    if (obj->section_name != NULL &&
        minic_type_is_array(obj->type) &&
        !rv32_array_of_records(program, obj->type) &&
        obj->relocation_count != 0U &&
        obj->alias_target == MINIC_GLOBAL_OBJECT_INVALID &&
        obj->union_selection_count == 0U &&
        obj->flexible_array_initializer_count == 0U &&
        minic_data_layout_global_object(
            target->data_layout, program, obj, &bytes, &alignment) &&
        bytes != 0U && alignment != 0U) {
        if (obj->explicit_alignment > alignment)
            alignment = obj->explicit_alignment;
        if ((alignment & (alignment - 1U)) != 0U ||
            !rv32_data_symbol(out, obj, alignment) ||
            !minic_multi_emit_relocatable_scalar_array(
                out, program, target, obj, bytes, rv32_reloc, NULL))
            return false;
        return fprintf(out, ".size %s, %zu\n", obj->name, bytes) >= 0;
    }
    /* Shared global records and relocatable arrays are handled with the
     * canonical recursive DataLayout walker, including pointer-function
     * symbol fixups and ABI-specific field padding. */
    if ((minic_type_is_record(obj->type) ||
         rv32_array_of_records(program, obj->type) ||
         obj->relocation_count != 0U ||
         obj->union_selection_count != 0U ||
         obj->flexible_array_initializer_count != 0U) &&
        obj->alias_target == MINIC_GLOBAL_OBJECT_INVALID &&
        obj->section_name == NULL && !obj->is_weak) {
        return minic_multi_emit_simple_global(
            out, program, target, obj, rv32_reloc);
    }
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
    if ((!minic_type_is_integer(scalar) && !minic_type_is_pointer(scalar) &&
         !minic_type_is_float(scalar) && !minic_type_is_double(scalar)) ||
        !minic_data_layout_type(target->data_layout, program, scalar,
                                &width, &scalar_alignment) ||
        (width != 1U && width != 2U && width != 4U && width != 8U) ||
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
    directive = width == 1U ? ".byte" : width == 2U ? ".half" :
                width == 4U ? ".word" : ".8byte";
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
