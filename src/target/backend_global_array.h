#ifndef MINIC_TARGET_BACKEND_GLOBAL_ARRAY_H
#define MINIC_TARGET_BACKEND_GLOBAL_ARRAY_H

/* Two new ISAs share the source/target-neutral DataLayout iteration proven
 * by the mature x86 backend. Relocation *encoding* stays target-owned via
 * the callback; nested unions and aggregate initializer slots remain checked.
 * Introduced only after original zlib 1.3.1 demanded these three layouts. */
#include "frontend/ast.h"
#include "target/target_info.h"
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

typedef bool (*MinicMultiRelocEmitter)(FILE *, const MinicC0Program *,
                                       const MinicTargetInfo *,
                                       const MinicGlobalRelocation *, size_t);

static const char *minic_multi_data_directive(size_t width) {
    return width == 1U ? ".byte" : width == 2U ? ".2byte" :
           width == 4U ? ".4byte" : width == 8U ? ".8byte" : NULL;
}
static bool minic_multi_emit_data_bits(FILE *out, size_t width, uint64_t bits) {
    const char *directive = minic_multi_data_directive(width);
    if (directive == NULL) return false;
    if (width < 8U) bits &= (UINT64_C(1) << (width * 8U)) - UINT64_C(1);
    return fprintf(out, "  %s %" PRIu64 "\n", directive, bits) >= 0;
}

static bool minic_multi_emit_relocatable_scalar_array(FILE *out, const MinicC0Program *program,
                                              const MinicTargetInfo *target,
                                              const MinicGlobalObject *obj,
                                              size_t storage_size,
                                              MinicMultiRelocEmitter emit_relocation) {
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
                !emit_relocation(out, program, target, reloc, element_bytes))
                return false;
            ++relocation_index;
        } else if (!minic_multi_emit_data_bits(out, element_bytes,
                                       obj->initializer_values[i])) return false;
    }
    return relocation_index == obj->relocation_count;
}

/* zlib's level configuration table is an array of flat records containing
 * four shorts and one function pointer. Core already flattens its initializer
 * values and relocation slots in logical field order. Target layout owns
 * record padding and the ELF relocation expressions. This deliberately does
 * NOT claim support for arbitrary nested aggregates, unions or bit-fields. */
static bool minic_multi_emit_flat_record_array(FILE *out, const MinicC0Program *program,
                                       const MinicTargetInfo *target,
                                       const MinicGlobalObject *object,
                                       size_t storage_size,
                                              MinicMultiRelocEmitter emit_relocation) {
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
                minic_multi_data_directive(emitted_bytes) == NULL ||
                emitted_bytes > field_bytes) return false;
            (void)emitted_alignment;
            if (reloc != NULL &&
                reloc->location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR &&
                reloc->location_index == initializer_index) {
                if (object->initializer_values[initializer_index] != 0U ||
                    (!minic_type_is_pointer(emitted_type) &&
                     !minic_type_is_integer(emitted_type)) ||
                    !emit_relocation(out, program, target, reloc, emitted_bytes))
                    return false;
                ++relocation_index;
            } else if (!minic_multi_emit_data_bits(out, emitted_bytes,
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


/* Lua's HOOKKEY/CLIBS/output pointer globals: zero bytes with relocation
 * entries and no explicit scalar initializer. Preserve exact offsets using
 * the same DataLayout algorithm proven by x86. */
static bool minic_multi_emit_zero_relocation_global(FILE *out, const MinicC0Program *program,
                                            const MinicTargetInfo *target,
                                            const MinicGlobalObject *obj,
                                            size_t storage_size,
                                            MinicMultiRelocEmitter emit_relocation) {
    size_t cursor = 0U, i, width;
    if (out == NULL || program == NULL || target == NULL || obj == NULL ||
        emit_relocation == NULL ||
        !obj->is_zero_initialized || obj->initializer_count != 0U ||
        obj->relocation_count == 0U ||
        (!minic_type_is_pointer(obj->type) && !minic_type_is_array(obj->type)))
        return false;
    width = target->data_layout->pointer_size;
    if (width != 8U) return false;
    for (i = 0U; i < obj->relocation_count; ++i) {
        const MinicGlobalRelocation *reloc = &obj->relocations[i];
        size_t offset;
        if (!minic_data_layout_global_relocation_offset(
                target->data_layout, program, obj, reloc, &offset) ||
            offset < cursor || offset > storage_size ||
            width > storage_size - offset ||
            fprintf(out, "  .zero %zu\n", offset - cursor) < 0 ||
            !emit_relocation(out, program, target, reloc, width))
            return false;
        cursor = offset + width;
    }
    return fprintf(out, "  .zero %zu\n", storage_size - cursor) >= 0;
}


#endif /* MINIC_TARGET_BACKEND_GLOBAL_ARRAY_H */
