#include "target/x86_64/global_emit.h"
#include "target/common/global_emit.h"
#include "target/data_layout.h"
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Proven x86 SysV global emitter, now separate from Core machine codegen.
 * Use shared verified DataLayout/ELF helper only where semantics match. */
static const char *x86_data_directive(size_t size) {
    return size == 1U ? ".byte" : size == 2U ? ".word" :
           size == 4U ? ".long" : size == 8U ? ".quad" : NULL;
}

static bool x86_emit_data_bits(FILE *out, size_t size, uint64_t bits) {
    const char *directive = x86_data_directive(size);
    if (directive == NULL) return false;
    if (size < 8U) bits &= (UINT64_C(1) << (size * 8U)) - UINT64_C(1);
    return fprintf(out, "  %s %" PRIu64 "\n", directive, bits) >= 0;
}

static bool x86_emit_relocation(FILE *out, const MinicC0Program *program,
                                const MinicTargetInfo *target,
                                const MinicGlobalRelocation *reloc, size_t size) {
    const char *name = NULL;
    const char *directive = x86_data_directive(size);
    int64_t addend;
    if (reloc == NULL || directive == NULL || size != 8U) return false;
    if (reloc->target_kind == MINIC_GLOBAL_RELOCATION_LABEL) {
        /* Lua lvm.c's dispatch table has one relocation for each local
         * GNU label. Core already records the statement ID and emits its
         * .Luser_<id> alias at the owning basic block, just as RV64 does. */
        const MinicStatement *label = minic_c0_program_statement(program, reloc->target_id);
        return label != NULL && label->kind == MINIC_STATEMENT_LABEL &&
               fprintf(out, "  %s .Luser_%zu\n", directive, reloc->target_id) >= 0;
    }
    name = minic_multi_global_relocation_target_name(program, reloc);
    if (name == NULL || name[0] == '\0' ||
        !minic_data_layout_global_relocation_target_addend(target->data_layout,
                                                           program, reloc, &addend)) return false;
    if (addend == 0)
        return fprintf(out, "  %s %s\n", directive, name) >= 0;
    return fprintf(out, "  %s %s%+" PRId64 "\n", directive, name, addend) >= 0;
}

/* Flat records now use one shared layout walk.  Retain x86's legacy
 * .word/.long/.quad scalar spelling in the target-owned callback. */
static bool x86_emit_typed_bits(FILE *out, const MinicC0Program *program,
                                const MinicTargetInfo *target, MinicType type,
                                uint64_t bits) {
    size_t width, alignment;
    return target != NULL &&
           minic_data_layout_type(target->data_layout, program, type,
                                  &width, &alignment) &&
           x86_emit_data_bits(out, width, bits);
}

/* A flat array of pointer/scalar constants with per-element ELF relocation.
 * zlib's z_errmsg[] is a const pointer array to ten string literals.
 * Validate every relocation slot before writing an address-sized expression. */
bool minic_x86_64_emit_global(FILE *out, const MinicC0Program *program,
                            const MinicTargetInfo *target, const MinicGlobalObject *obj) {
    MinicType scalar;
    size_t storage_size, alignment, width, scalar_align, i;
    bool array_of_records, all_zero_aggregate = false;
    const char *directive;
    if (obj == NULL || obj->name == NULL || obj->name[0] == '\0' ||
        obj->alias_target != MINIC_GLOBAL_OBJECT_INVALID ||
        !minic_data_layout_global_object(target->data_layout, program, obj,
                                         &storage_size, &alignment)) return false;
    scalar = obj->type;
    while (minic_type_is_array(scalar)) {
        const MinicArrayType *arr = minic_c0_program_array_type(program, scalar.array_type_id);
        if (arr == NULL) return false;
        scalar = arr->element_type;
    }
    array_of_records = minic_type_is_array(obj->type) && minic_type_is_record(scalar);
    if (obj->union_selection_count != 0U && !array_of_records &&
        !minic_type_is_record(obj->type)) return false;
    if (minic_type_is_record(scalar)) {
        /* Record and flat record-array payloads are emitted separately. */
        width = 0U;
    } else {
        if ((!minic_type_is_integer(scalar) && !minic_type_is_pointer(scalar) &&
             !minic_type_is_float(scalar) && !minic_type_is_double(scalar)) ||
            !minic_data_layout_type(target->data_layout, program, scalar,
                                    &width, &scalar_align))
            return false;
        (void)scalar_align;
    }
    directive = x86_data_directive(width);
    if (!minic_type_is_record(obj->type) && !array_of_records &&
        (directive == NULL || width == 0U ||
         obj->initializer_count > storage_size / width)) return false;
    if (obj->initializer_count != 0U && obj->initializer_values == NULL) return false;
    /* A fully zero aggregate is bytewise zero for this x86 target even
     * when its type contains nested records, bit-fields and padding.
     * Never take this shortcut for relocations or a nonzero slot. */
    /* C static-storage records have an implicit all-zero initializer
     * even when no aggregate initializer token exists. Model that through
     * the same byte-exact DataLayout path as explicit zeros. */
    if (minic_type_is_record(obj->type) && obj->relocation_count == 0U) {
        all_zero_aggregate = obj->initializer_count == 0U;
        if (obj->initializer_count != 0U) {
            all_zero_aggregate = true;
            for (i = 0U; i < obj->initializer_count; ++i) {
                if (obj->initializer_values[i] != 0U) {
                    all_zero_aggregate = false;
                    break;
                }
            }
        }
    }
    if (obj->section_name != NULL) {
        if (!minic_multi_elf_safe_section_name(obj->section_name) ||
            fprintf(out, ".section %s,\"%s\",@progbits\n",
                    obj->section_name, obj->is_read_only ? "a" : "aw") < 0) return false;
    } else if (fprintf(out, "%s\n", obj->is_read_only ?
                       (obj->relocation_count != 0U ? ".section .data.rel.ro" : ".section .rodata") :
                       (obj->is_zero_initialized || obj->is_tentative) &&
                       obj->initializer_count == 0U && obj->relocation_count == 0U ?
                           ".bss" : ".data") < 0)
        return false;
    if (!obj->is_internal) {
        const char *visibility_directive = NULL;
        if (fprintf(out, obj->is_weak ? ".weak %s\n" : ".globl %s\n", obj->name) < 0)
            return false;
        visibility_directive =
            minic_multi_elf_visibility_directive(obj->visibility);
        if (obj->visibility != MINIC_SYMBOL_VISIBILITY_DEFAULT &&
            visibility_directive == NULL) return false;
        if (visibility_directive != NULL &&
            fprintf(out, "%s %s\n", visibility_directive, obj->name) < 0)
            return false;
    }
    if (fprintf(out, "  .balign %zu\n.type %s,@object\n%s:\n",
                alignment, obj->name, obj->name) < 0) return false;
    if (all_zero_aggregate ||
        ((obj->is_zero_initialized || obj->is_tentative) &&
         obj->initializer_count == 0U && obj->relocation_count == 0U)) {
        if (fprintf(out, "  .zero %zu\n", storage_size) < 0) return false;
    } else if (obj->relocation_count != 0U && obj->is_zero_initialized &&
               obj->initializer_count == 0U) {
        if (!minic_multi_emit_zero_relocation_global(
                out, program, target, obj, storage_size, x86_emit_relocation)) return false;
    } else if (minic_type_is_record(obj->type)) {
        const MinicRecord *record = minic_c0_program_record(
            program, obj->type.record_id);
        /* Use the already-shared flattened initializer slot representation
         * for nested records/arrays, even without a selected union.
         * Preserve the established flat-record emission for legacy
         * RECORD_FIELD relocation locations. */
        if (record == NULL) return false;
        if (minic_multi_record_initializer_needs_recursive(record, obj)) {
            if (!minic_multi_emit_recursive_record_global(
                    out, program, target, obj, storage_size,
                    x86_emit_relocation)) return false;
        } else if (!minic_multi_emit_flat_record_global(
                       out, program, target, obj, storage_size,
                       x86_emit_relocation, x86_emit_typed_bits)) return false;
    } else if (array_of_records) {
        /* Shared layout owns flattened/recursive initializer slots,
         * including selected nested unions. ELF relocations remain x86. */
        if (!minic_multi_emit_record_array_global(
                out, program, target, obj, storage_size,
                x86_emit_relocation)) return false;
    } else if (minic_type_is_array(obj->type) && obj->relocation_count != 0U) {
        const MinicArrayType *outer =
            minic_c0_program_array_type(program, obj->type.array_type_id);
        if (outer == NULL) return false;
        /* GNU [row][column] static pointer tables flatten their relocations
         * across both dimensions.  The one-dimensional scalar array emitter
         * cannot walk the nested DataLayout, while the common recursive
         * aggregate serializer already verifies every leaf and byte offset. */
        if (minic_type_is_array(outer->element_type)) {
            if (!minic_multi_emit_recursive_aggregate_global(
                    out, program, target, obj, storage_size,
                    x86_emit_relocation, x86_emit_typed_bits)) return false;
        } else if (!minic_multi_emit_relocatable_scalar_array(
                       out, program, target, obj, storage_size,
                       x86_emit_relocation, NULL)) return false;
    } else if (obj->relocation_count != 0U) {
        /* Simple pointer globals: relocatable target address or function. */
        if (obj->initializer_count != 1U || obj->relocation_count != 1U ||
            obj->relocations[0].location_kind != MINIC_GLOBAL_RELOCATION_LOCATION_SCALAR ||
            obj->relocations[0].location_index != 0U ||
            obj->initializer_values[0] != 0U || width != storage_size ||
            !x86_emit_relocation(out, program, target, &obj->relocations[0], width)) return false;
    } else {
        for (i = 0U; i < obj->initializer_count; ++i) {
            if (!x86_emit_data_bits(out, width, obj->initializer_values[i])) return false;
        }
        if (storage_size > obj->initializer_count * width &&
            fprintf(out, "  .zero %zu\n",
                    storage_size - obj->initializer_count * width) < 0) return false;
    }
    return fprintf(out, ".size %s,%zu\n", obj->name, storage_size) >= 0;
}

