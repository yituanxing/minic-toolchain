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
    if (reloc->target_kind == MINIC_GLOBAL_RELOCATION_OBJECT) {
        const MinicGlobalObject *object = minic_c0_program_global_object(program, reloc->target_id);
        if (object != NULL) name = object->name;
    } else if (reloc->target_kind == MINIC_GLOBAL_RELOCATION_FUNCTION) {
        const MinicFunction *function = minic_c0_program_function(program, reloc->target_id);
        if (function != NULL) name = minic_c0_function_symbol_name(function);
    }
    if (name == NULL || name[0] == '\0' ||
        !minic_data_layout_global_relocation_target_addend(target->data_layout,
                                                           program, reloc, &addend)) return false;
    if (addend == 0)
        return fprintf(out, "  %s %s\n", directive, name) >= 0;
    return fprintf(out, "  %s %s%+" PRId64 "\n", directive, name, addend) >= 0;
}

/* Lua 5.5.0 has top-level records with selected nested union members.
 * Reuse Core's flattened initializer slots and selected-member metadata,
 * and the shared DataLayout for the exact target byte offsets. This is a
 * bounded target-owned emitter: bit-fields/flexible fields fail closed. */
static bool x86_emit_recursive_global_value(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicGlobalObject *object, MinicType type, size_t *initializer_index,
    size_t *relocation_index, size_t *matched_union_selections,
    size_t *emitted_size, unsigned int depth) {
    size_t type_size, type_alignment;
    if (out == NULL || program == NULL || target == NULL || object == NULL ||
        initializer_index == NULL || relocation_index == NULL ||
        matched_union_selections == NULL || emitted_size == NULL || depth > 32U ||
        !minic_data_layout_type(target->data_layout, program, type,
                                &type_size, &type_alignment))
        return false;
    (void)type_alignment;
    if (minic_type_is_integer(type) || minic_type_is_pointer(type) ||
        minic_type_is_float(type) || minic_type_is_double(type)) {
        const MinicGlobalRelocation *reloc =
            *relocation_index < object->relocation_count
                ? &object->relocations[*relocation_index] : NULL;
        if (*initializer_index >= object->initializer_count ||
            x86_data_directive(type_size) == NULL)
            return false;
        if (reloc != NULL &&
            reloc->location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR &&
            reloc->location_index == *initializer_index) {
            if (object->initializer_values[*initializer_index] != 0U ||
                (!minic_type_is_integer(type) && !minic_type_is_pointer(type)) ||
                !x86_emit_relocation(out, program, target, reloc, type_size))
                return false;
            ++*relocation_index;
        } else if (!x86_emit_data_bits(
                       out, type_size, object->initializer_values[*initializer_index]))
            return false;
        ++*initializer_index;
        *emitted_size = type_size;
        return true;
    }
    if (minic_type_is_array(type)) {
        const MinicArrayType *arr =
            minic_c0_program_array_type(program, type.array_type_id);
        size_t cursor = 0U, i;
        if (arr == NULL || arr->element_count == 0U) return false;
        for (i = 0U; i < arr->element_count; ++i) {
            size_t emitted;
            if (!x86_emit_recursive_global_value(
                    out, program, target, object, arr->element_type,
                    initializer_index, relocation_index, matched_union_selections,
                    &emitted, depth + 1U) ||
                emitted > type_size - cursor)
                return false;
            cursor += emitted;
        }
        if (fprintf(out, "  .zero %zu\n", type_size - cursor) < 0)
            return false;
        *emitted_size = type_size;
        return true;
    }
    if (minic_type_is_record(type)) {
        const MinicRecord *record =
            minic_c0_program_record(program, type.record_id);
        size_t begin = 0U, end, start_slot = *initializer_index;
        size_t union_span = 0U, cursor = 0U, i;
        if (record == NULL || !record->is_complete || record->field_count == 0U)
            return false;
        end = record->field_count;
        if (record->is_union) {
            size_t selected = 0U;
            if (minic_c0_global_object_union_member_selection(
                    program, object, start_slot, type.record_id, &selected)) {
                if (!minic_c0_global_object_union_member_initializer_span(
                        program, object, start_slot, type.record_id, &union_span))
                    return false;
                ++*matched_union_selections;
            }
            if (selected >= record->field_count) return false;
            begin = selected;
            end = selected + 1U;
        }
        for (i = begin; i < end; ++i) {
            const MinicRecordField *field = minic_c0_record_field(record, i);
            size_t field_offset, element;
            if (field == NULL || field->is_bit_field ||
                field->is_flexible_array || field->element_count == 0U)
                return false;
            if (field->is_zero_length_array) continue;
            if (record->is_union) {
                field_offset = 0U;
            } else if (!minic_data_layout_record_field_offset(
                           target->data_layout, program, record, i,
                           &field_offset)) {
                return false;
            }
            if (field_offset < cursor || field_offset > type_size ||
                fprintf(out, "  .zero %zu\n", field_offset - cursor) < 0)
                return false;
            cursor = field_offset;
            for (element = 0U; element < field->element_count; ++element) {
                size_t emitted;
                if (!x86_emit_recursive_global_value(
                        out, program, target, object, field->type,
                        initializer_index, relocation_index, matched_union_selections,
                        &emitted, depth + 1U) ||
                    emitted > type_size - cursor)
                    return false;
                cursor += emitted;
            }
        }
        if (cursor > type_size ||
            fprintf(out, "  .zero %zu\n", type_size - cursor) < 0)
            return false;
        if (record->is_union && union_span != 0U) {
            size_t end_slot, slot;
            if (start_slot > SIZE_MAX - union_span ||
                (end_slot = start_slot + union_span) > object->initializer_count ||
                *initializer_index > end_slot) return false;
            for (slot = *initializer_index; slot < end_slot; ++slot) {
                const MinicGlobalRelocation *reloc =
                    *relocation_index < object->relocation_count
                        ? &object->relocations[*relocation_index] : NULL;
                if (object->initializer_values[slot] != 0U ||
                    (reloc != NULL &&
                     reloc->location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR &&
                     reloc->location_index < end_slot))
                    return false;
            }
            *initializer_index = end_slot;
        }
        *emitted_size = type_size;
        return true;
    }
    return false;
}

static bool x86_emit_recursive_record_global(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicGlobalObject *object, size_t storage_size) {
    size_t initializer_index = 0U, relocation_index = 0U;
    size_t matched_union_selections = 0U, emitted_size;
    if (!minic_type_is_record(object->type) ||
        !x86_emit_recursive_global_value(
            out, program, target, object, object->type,
            &initializer_index, &relocation_index, &matched_union_selections,
            &emitted_size, 0U))
        return false;
    return emitted_size == storage_size &&
           initializer_index == object->initializer_count &&
           relocation_index == object->relocation_count &&
           matched_union_selections == object->union_selection_count;
}

static bool x86_emit_record_global(FILE *out, const MinicC0Program *program,
                                   const MinicTargetInfo *target,
                                   const MinicGlobalObject *obj, size_t storage_size) {
    const MinicRecord *record;
    size_t i, cursor = 0U, relocation_index = 0U;
    if (!minic_type_is_record(obj->type)) return false;
    record = minic_c0_program_record(program, obj->type.record_id);
    if (record == NULL || !record->is_complete || record->is_union ||
        record->field_count != obj->initializer_count) return false;
    for (i = 0U; i < record->field_count; ++i) {
        const MinicRecordField *field = minic_c0_record_field(record, i);
        const MinicGlobalRelocation *reloc =
            relocation_index < obj->relocation_count ? &obj->relocations[relocation_index] : NULL;
        size_t off, width, align;
        if (field == NULL || field->is_bit_field || field->is_flexible_array ||
            field->element_count != 1U ||
            (!minic_type_is_integer(field->type) && !minic_type_is_pointer(field->type) &&
             !minic_type_is_float(field->type) && !minic_type_is_double(field->type)) ||
            !minic_data_layout_type(target->data_layout, program, field->type, &width, &align) ||
            x86_data_directive(width) == NULL ||
            !minic_data_layout_record_field_offset(target->data_layout, program, record, i, &off) ||
            off < cursor || off > storage_size || width > storage_size - off ||
            fprintf(out, "  .zero %zu\n", off - cursor) < 0) return false;
        (void)align;
        if (reloc != NULL &&
            (reloc->location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_RECORD_FIELD ||
             reloc->location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR) &&
            reloc->location_index == i) {
            if (obj->initializer_values[i] != 0U ||
                !x86_emit_relocation(out, program, target, reloc, width)) return false;
            ++relocation_index;
        } else if (!x86_emit_data_bits(out, width, obj->initializer_values[i])) {
            return false;
        }
        cursor = off + width;
    }
    return relocation_index == obj->relocation_count && cursor <= storage_size &&
           fprintf(out, "  .zero %zu\n", storage_size - cursor) >= 0;
}

/* A flat array of pointer/scalar constants with per-element ELF relocation.
 * zlib's z_errmsg[] is a const pointer array to ten string literals.
 * Validate every relocation slot before writing an address-sized expression. */
static bool x86_emit_relocatable_scalar_array(FILE *out, const MinicC0Program *program,
                                              const MinicTargetInfo *target,
                                              const MinicGlobalObject *obj,
                                              size_t storage_size) {
    const MinicArrayType *arr;
    size_t element_bytes, alignment, i, relocation_index = 0U;
    if (!minic_type_is_array(obj->type)) return false;
    arr = minic_c0_program_array_type(program, obj->type.array_type_id);
    if (arr == NULL ||
        (!minic_type_is_pointer(arr->element_type) &&
         !minic_type_is_integer(arr->element_type)) ||
        !minic_data_layout_type(target->data_layout, program, arr->element_type,
                                &element_bytes, &alignment) ||
        element_bytes == 0U || arr->element_count > SIZE_MAX / element_bytes ||
        arr->element_count * element_bytes != storage_size ||
        arr->element_count != obj->initializer_count)
        return false;
    (void)alignment;
    for (i = 0U; i < arr->element_count; ++i) {
        const MinicGlobalRelocation *reloc =
            relocation_index < obj->relocation_count ?
                &obj->relocations[relocation_index] : NULL;
        if (reloc != NULL &&
            (reloc->location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_ARRAY_ELEMENT ||
             reloc->location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR) &&
            reloc->location_index == i) {
            if (obj->initializer_values[i] != 0U ||
                !x86_emit_relocation(out, program, target, reloc, element_bytes))
                return false;
            ++relocation_index;
        } else if (!x86_emit_data_bits(out, element_bytes,
                                       obj->initializer_values[i])) return false;
    }
    return relocation_index == obj->relocation_count;
}

/* zlib's level configuration table is an array of flat records containing
 * four shorts and one function pointer. Core already flattens its initializer
 * values and relocation slots in logical field order. Target layout owns
 * record padding and the ELF relocation expressions. This deliberately does
 * NOT claim support for arbitrary nested aggregates, unions or bit-fields. */
static bool x86_emit_flat_record_array(FILE *out, const MinicC0Program *program,
                                       const MinicTargetInfo *target,
                                       const MinicGlobalObject *object,
                                       size_t storage_size) {
    const MinicArrayType *array;
    const MinicRecord *record;
    size_t record_bytes, record_align, element, field_index;
    size_t initializer_index = 0U, relocation_index = 0U;
    size_t matched_union_selections = 0U;
    if (!minic_type_is_array(object->type)) return false;
    array = minic_c0_program_array_type(program, object->type.array_type_id);
    if (array == NULL || array->element_count == 0U ||
        !minic_type_is_record(array->element_type)) return false;
    record = minic_c0_program_record(program, array->element_type.record_id);
    if (record == NULL || !record->is_complete || record->is_union ||
        record->field_count == 0U ||
        !minic_data_layout_type(target->data_layout, program, array->element_type,
                                &record_bytes, &record_align) ||
        record_bytes == 0U || array->element_count > SIZE_MAX / record_bytes ||
        array->element_count * record_bytes != storage_size ||
        record->field_count > SIZE_MAX / array->element_count ||
        record->field_count * array->element_count != object->initializer_count)
        return false;
    (void)record_align;
    for (element = 0U; element < array->element_count; ++element) {
        size_t cursor = 0U;
        for (field_index = 0U; field_index < record->field_count; ++field_index) {
            const MinicRecordField *field = minic_c0_record_field(record, field_index);
            const MinicGlobalRelocation *reloc =
                relocation_index < object->relocation_count
                    ? &object->relocations[relocation_index] : NULL;
            size_t offset, field_bytes, field_align;
            MinicType emitted_type;
            size_t emitted_bytes, emitted_alignment, emitted_offset = 0U;
            if (field == NULL || field->is_bit_field || field->is_flexible_array ||
                field->element_count != 1U ||
                !minic_data_layout_type(target->data_layout, program, field->type,
                                        &field_bytes, &field_align) ||
                !minic_data_layout_record_field_offset(target->data_layout, program,
                                                       record, field_index, &offset) ||
                offset < cursor || offset > record_bytes ||
                field_bytes > record_bytes - offset ||
                fprintf(out, "  .zero %zu\n", offset - cursor) < 0)
                return false;
            (void)field_align;
            emitted_type = field->type;
            if (minic_type_is_record(field->type)) {
                const MinicRecord *nested = minic_c0_program_record(
                    program, field->type.record_id);
                const MinicRecordField *member;
                size_t selected = 0U, selection_span = 0U;
                if (nested == NULL || !nested->is_complete || !nested->is_union ||
                    nested->field_count == 0U) return false;
                if (minic_c0_global_object_union_member_selection(
                        program, object, initializer_index, field->type.record_id,
                        &selected)) {
                    if (!minic_c0_global_object_union_member_initializer_span(
                            program, object, initializer_index, field->type.record_id,
                            &selection_span) ||
                        (selection_span != 0U && selection_span != 1U)) return false;
                    ++matched_union_selections;
                }
                if (selected >= nested->field_count) return false;
                member = minic_c0_record_field(nested, selected);
                if (member == NULL || member->element_count != 1U ||
                    member->is_bit_field || member->is_flexible_array ||
                    !minic_data_layout_record_field_offset(
                        target->data_layout, program, nested, selected, &emitted_offset) ||
                    emitted_offset != 0U) return false;
                emitted_type = member->type;
            }
            if ((!minic_type_is_integer(emitted_type) &&
                 !minic_type_is_pointer(emitted_type) &&
                 !minic_type_is_float(emitted_type) &&
                 !minic_type_is_double(emitted_type)) ||
                !minic_data_layout_type(target->data_layout, program, emitted_type,
                                        &emitted_bytes, &emitted_alignment) ||
                x86_data_directive(emitted_bytes) == NULL ||
                emitted_bytes > field_bytes) return false;
            (void)emitted_alignment;
            if (reloc != NULL &&
                reloc->location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR &&
                reloc->location_index == initializer_index) {
                if (object->initializer_values[initializer_index] != 0U ||
                    (!minic_type_is_pointer(emitted_type) &&
                     !minic_type_is_integer(emitted_type)) ||
                    !x86_emit_relocation(out, program, target, reloc, emitted_bytes))
                    return false;
                ++relocation_index;
            } else if (!x86_emit_data_bits(out, emitted_bytes,
                                            object->initializer_values[initializer_index])) {
                return false;
            }
            if (fprintf(out, "  .zero %zu\n", field_bytes - emitted_bytes) < 0)
                return false;
            ++initializer_index;
            cursor = offset + field_bytes;
        }
        if (fprintf(out, "  .zero %zu\n", record_bytes - cursor) < 0)
            return false;
    }
    return initializer_index == object->initializer_count &&
           relocation_index == object->relocation_count &&
           matched_union_selections == object->union_selection_count;
}

/* Lua's HOOKKEY/CLIBS/progname pointers are zero-filled objects whose
 * initializer is represented by an ELF relocation, not by a scalar literal
 * slot. The shared DataLayout validates each relocation byte offset. Keep
 * symbol expression emission target-owned; do not invent a generic ABI. */
/* First real-project pressure point: tiny-AES declares read-only byte tables.
 * Emit only plain scalar/array initializers; aggregate/relocation cases remain
 * explicit failures until a project requires them. */
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
    if (minic_type_is_record(obj->type) && obj->relocation_count == 0U &&
        obj->initializer_count != 0U) {
        all_zero_aggregate = true;
        for (i = 0U; i < obj->initializer_count; ++i) {
            if (obj->initializer_values[i] != 0U) {
                all_zero_aggregate = false;
                break;
            }
        }
    }
    if (obj->section_name != NULL) {
        if (fprintf(out, ".section %s,\"%s\",@progbits\n",
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
        if (obj->union_selection_count != 0U ||
            obj->initializer_count != record->field_count) {
            if (!x86_emit_recursive_record_global(
                    out, program, target, obj, storage_size)) return false;
        } else if (!x86_emit_record_global(
                       out, program, target, obj, storage_size)) return false;
    } else if (array_of_records) {
        const MinicArrayType *array = minic_c0_program_array_type(
            program, obj->type.array_type_id);
        const MinicRecord *element_record = array != NULL
            ? minic_c0_program_record(program, array->element_type.record_id)
            : NULL;
        if (array == NULL || element_record == NULL ||
            array->element_count == 0U ||
            element_record->field_count > SIZE_MAX / array->element_count)
            return false;
        if (element_record->field_count * array->element_count !=
            obj->initializer_count) {
            /* SQLite aXformType keeps an inline character array in each
             * record. Its extra flattened slots are already represented in
             * shared initializer metadata; use the existing recursive
             * layout path instead of pretending each record is flat. */
            size_t initializer_index = 0U, relocation_index = 0U;
            size_t matched_union_selections = 0U, emitted_size = 0U;
            if (!x86_emit_recursive_global_value(
                    out, program, target, obj, obj->type, &initializer_index,
                    &relocation_index, &matched_union_selections,
                    &emitted_size, 0U) ||
                emitted_size != storage_size ||
                initializer_index != obj->initializer_count ||
                relocation_index != obj->relocation_count ||
                matched_union_selections != obj->union_selection_count)
                return false;
        } else if (!x86_emit_flat_record_array(
                       out, program, target, obj, storage_size))
            return false;
    } else if (minic_type_is_array(obj->type) && obj->relocation_count != 0U) {
        if (!x86_emit_relocatable_scalar_array(out, program, target, obj,
                                               storage_size)) return false;
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

