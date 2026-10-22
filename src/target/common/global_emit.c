#include "target/common/global_emit.h"

#include <inttypes.h>
#include <stdint.h>

/* Formerly duplicated as static definitions in two target translation units.
 * Preserve the proven DataLayout walk byte-for-byte: aggregate/union slot
 * checks remain shared, relocation rendering remains target-owned. */
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

bool minic_multi_emit_relocatable_scalar_array(FILE *out, const MinicC0Program *program,
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
            /* BusyBox ifconfig OptArray: two adjacent unsigned bit-fields
             * (6 + 10 bits) share a 16-bit storage run between a pointer
             * relocation and a short. Use DataLayout's exact bit offsets,
             * rather than treating their declared int type as two ints.
             * Admit only complete, contiguous, byte-aligned <=64-bit runs;
             * keep uncommon layouts/bit-field relocations fail-closed. */
            if (field != NULL && field->is_bit_field) {
                size_t begin, first_bit, bit_cursor, next, b;
                uint64_t packed = 0U;
                if (record_bytes > SIZE_MAX / 8U ||
                    !minic_data_layout_record_field_layout(
                         target->data_layout, program, record, field_index,
                         &begin, &first_bit) ||
                    first_bit != 0U || begin < cursor ||
                    begin > record_bytes) return false;
                bit_cursor = begin * 8U;
                for (next = field_index; next < record->field_count; ++next) {
                    const MinicRecordField *bf =
                        minic_c0_record_field(record, next);
                    size_t byte_offset, bit_offset, width, local_bit;
                    uint64_t mask;
                    const MinicGlobalRelocation *next_reloc =
                        relocation_index < object->relocation_count
                            ? &object->relocations[relocation_index] : NULL;
                    if (bf == NULL) return false;
                    if (!bf->is_bit_field) break;
                    if (!minic_type_is_integer(bf->type) ||
                        bf->element_count != 1U || bf->bit_width == 0U ||
                        bf->bit_width > 64U ||
                        initializer_index >= object->initializer_count ||
                        !minic_data_layout_record_field_layout(
                            target->data_layout, program, record, next,
                            &byte_offset, &bit_offset) ||
                        byte_offset > record_bytes ||
                        byte_offset > SIZE_MAX / 8U ||
                        byte_offset * 8U + bit_offset != bit_cursor)
                        return false;
                    local_bit = bit_cursor - begin * 8U;
                    width = bf->bit_width;
                    if (local_bit >= 64U || width > 64U - local_bit)
                        return false;
                    if (next_reloc != NULL &&
                        next_reloc->location_index == initializer_index &&
                        (next_reloc->location_kind ==
                             MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR ||
                         next_reloc->location_kind ==
                             MINIC_GLOBAL_RELOCATION_LOCATION_RECORD_FIELD))
                        return false;
                    mask = width == 64U ? UINT64_MAX :
                        (UINT64_C(1) << width) - UINT64_C(1);
                    packed |= (object->initializer_values[initializer_index] &
                               mask) << local_bit;
                    bit_cursor += width;
                    ++initializer_index;
                }
                if ((bit_cursor & 7U) != 0U ||
                    bit_cursor / 8U > record_bytes ||
                    fprintf(out, "  .zero %zu\n", begin - cursor) < 0)
                    return false;
                for (b = 0U; b < bit_cursor / 8U - begin; ++b)
                    if (!minic_multi_emit_data_bits(
                            out, 1U, (packed >> (8U * b)) & UINT64_C(255)))
                        return false;
                cursor = bit_cursor / 8U;
                field_index = next - 1U;
                continue;
            }
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
bool minic_multi_emit_zero_relocation_global(FILE *out, const MinicC0Program *program,
                                            const MinicTargetInfo *target,
                                            const MinicGlobalObject *obj,
                                            size_t storage_size,
                                            MinicMultiRelocEmitter emit_relocation) {
    size_t cursor = 0U, i, width;
    if (out == NULL || program == NULL || target == NULL || obj == NULL ||
        emit_relocation == NULL ||
        !obj->is_zero_initialized || obj->initializer_count != 0U ||
        obj->relocation_count == 0U ||
        (!minic_type_is_pointer(obj->type) &&
         !minic_type_is_array(obj->type) &&
         !minic_type_is_record(obj->type) &&
         !(minic_type_is_integer(obj->type) && storage_size == target->data_layout->pointer_size)))
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


/* Lua nested static records and selected unions reuse x86's recursive
 * DataLayout walk, with target-specific ELF relocation callbacks. Keep
 * the established slot, union selection, padding and depth guards. */
static bool minic_multi_emit_recursive_global_value(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicGlobalObject *object, MinicType type, size_t *initializer_index,
    size_t *relocation_index, size_t *matched_union_selections,
    size_t *emitted_size, unsigned int depth,
    MinicMultiRelocEmitter emit_relocation) {
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
            minic_multi_data_directive(type_size) == NULL)
            return false;
        if (reloc != NULL &&
            reloc->location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR &&
            reloc->location_index == *initializer_index) {
            if (object->initializer_values[*initializer_index] != 0U ||
                (!minic_type_is_integer(type) && !minic_type_is_pointer(type)) ||
                !emit_relocation(out, program, target, reloc, type_size))
                return false;
            ++*relocation_index;
        } else if (!minic_multi_emit_data_bits(
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
            if (!minic_multi_emit_recursive_global_value(
                    out, program, target, object, arr->element_type,
                    initializer_index, relocation_index, matched_union_selections,
                    &emitted, depth + 1U, emit_relocation) ||
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
                if (!minic_multi_emit_recursive_global_value(
                        out, program, target, object, field->type,
                        initializer_index, relocation_index, matched_union_selections,
                        &emitted, depth + 1U, emit_relocation) ||
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

bool minic_multi_emit_recursive_record_global(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicGlobalObject *object, size_t storage_size,
    MinicMultiRelocEmitter emit_relocation) {
    size_t initializer_index = 0U, relocation_index = 0U;
    size_t matched_union_selections = 0U, emitted_size;
    if (!minic_type_is_record(object->type) ||
        !minic_multi_emit_recursive_global_value(
            out, program, target, object, object->type,
            &initializer_index, &relocation_index, &matched_union_selections,
            &emitted_size, 0U, emit_relocation))
        return false;
    return emitted_size == storage_size &&
           initializer_index == object->initializer_count &&
           relocation_index == object->relocation_count &&
           matched_union_selections == object->union_selection_count;
}


/* One shared recursive layout for arrays of records with nested array,
 * record and union fields. Keep the proven flat-array fast path for zlib
 * and route more complex initializer slot shapes through the x86-derived
 * recursive DataLayout walk (aXformType: 6 records, 60 flattened slots).
 * No backend may guess field offsets or silently truncate initializers. */
bool minic_multi_emit_record_array_global(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicGlobalObject *object, size_t storage_size,
    MinicMultiRelocEmitter emit_relocation) {
    const MinicArrayType *array;
    const MinicRecord *record;
    bool flat;
    size_t i, init = 0U, reloc = 0U, unions = 0U, emitted;
    if (out == NULL || program == NULL || target == NULL || object == NULL ||
        emit_relocation == NULL || !minic_type_is_array(object->type))
        return false;
    array = minic_c0_program_array_type(program, object->type.array_type_id);
    if (array == NULL || !minic_type_is_record(array->element_type))
        return false;
    record = minic_c0_program_record(program, array->element_type.record_id);
    if (record == NULL || !record->is_complete ||
        record->field_count == 0U || array->element_count == 0U)
        return false;
    flat = !record->is_union &&
           record->field_count <= SIZE_MAX / array->element_count &&
           record->field_count * array->element_count == object->initializer_count;
    for (i = 0U; i < record->field_count && flat; ++i) {
        const MinicRecordField *field = minic_c0_record_field(record, i);
        if (field == NULL || field->element_count != 1U ||
            minic_type_is_array(field->type) ||
            (minic_type_is_record(field->type) &&
             !minic_c0_program_record(program, field->type.record_id)->is_union))
            flat = false;
    }
    if (flat)
        return minic_multi_emit_flat_record_array(
            out, program, target, object, storage_size, emit_relocation);
    if (!minic_multi_emit_recursive_global_value(
            out, program, target, object, object->type,
            &init, &reloc, &unions, &emitted, 0U, emit_relocation))
        return false;
    return emitted == storage_size &&
           init == object->initializer_count &&
           reloc == object->relocation_count &&
           unions == object->union_selection_count;
}



/* GNU permits a static record object to initialize its trailing flexible
 * integer array (BusyBox hdparm's masks_labels_t). Core represents the
 * payload as one flattened slot per scalar; DataLayout alone computes the
 * actual storage extent, offsets and alignment. This helper deliberately
 * admits only flat pointer/integer prefix fields followed by a last,
 * non-bitfield flexible integer member. Never guess bitfield packing or
 * nested-object offsets. ELF relocations remain backend-owned. */
bool minic_multi_emit_flexible_record_global(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicGlobalObject *object, size_t storage_size,
    MinicMultiRelocEmitter emit_relocation) {
    const MinicRecord *record;
    const MinicRecordField *tail;
    size_t i, slot = 0U, cursor = 0U, reloc_index = 0U;
    size_t tail_offset, tail_width, tail_align;
    if (out == NULL || program == NULL || target == NULL || object == NULL ||
        emit_relocation == NULL || !minic_type_is_record(object->type) ||
        object->flexible_array_initializer_count == 0U ||
        object->initializer_values == NULL ||
        object->union_selection_count != 0U)
        return false;
    record = minic_c0_program_record(program, object->type.record_id);
    if (record == NULL || !record->is_complete || record->is_union ||
        record->field_count < 2U ||
        record->field_count - 1U > SIZE_MAX - object->flexible_array_initializer_count ||
        object->initializer_count != record->field_count - 1U +
                                     object->flexible_array_initializer_count)
        return false;
    tail = minic_c0_record_field(record, record->field_count - 1U);
    if (tail == NULL || !tail->is_flexible_array || tail->is_bit_field ||
        !minic_type_is_integer(tail->type) ||
        !minic_data_layout_type(target->data_layout, program, tail->type,
                                &tail_width, &tail_align) ||
        minic_multi_data_directive(tail_width) == NULL ||
        !minic_data_layout_record_field_offset(
            target->data_layout, program, record, record->field_count - 1U,
            &tail_offset))
        return false;
    (void)tail_align;
    for (i = 0U; i + 1U < record->field_count; ++i) {
        const MinicRecordField *field = minic_c0_record_field(record, i);
        const MinicGlobalRelocation *reloc = reloc_index < object->relocation_count
            ? &object->relocations[reloc_index] : NULL;
        size_t offset, width, alignment;
        if (field == NULL || field->is_bit_field || field->is_flexible_array ||
            field->is_zero_length_array || field->element_count != 1U ||
            (!minic_type_is_integer(field->type) &&
             !minic_type_is_pointer(field->type)) ||
            !minic_data_layout_type(target->data_layout, program, field->type,
                                    &width, &alignment) ||
            minic_multi_data_directive(width) == NULL ||
            !minic_data_layout_record_field_offset(
                target->data_layout, program, record, i, &offset) ||
            offset < cursor || offset > storage_size ||
            width > storage_size - offset ||
            fprintf(out, "  .zero %zu\n", offset - cursor) < 0)
            return false;
        (void)alignment;
        if (reloc != NULL &&
            (reloc->location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_RECORD_FIELD ||
             reloc->location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR) &&
            reloc->location_index == slot) {
            if (!minic_type_is_pointer(field->type) ||
                object->initializer_values[slot] != 0U ||
                !emit_relocation(out, program, target, reloc, width))
                return false;
            ++reloc_index;
        } else if (!minic_multi_emit_data_bits(
                       out, width, object->initializer_values[slot])) {
            return false;
        }
        ++slot;
        cursor = offset + width;
    }
    if (tail_offset < cursor || tail_offset > storage_size ||
        tail_width > (storage_size - tail_offset) /
                         object->flexible_array_initializer_count ||
        fprintf(out, "  .zero %zu\n", tail_offset - cursor) < 0 ||
        reloc_index != object->relocation_count)
        return false;
    for (i = 0U; i < object->flexible_array_initializer_count; ++i) {
        if (!minic_multi_emit_data_bits(out, tail_width,
                                        object->initializer_values[slot++]))
            return false;
    }
    cursor = tail_offset + object->flexible_array_initializer_count * tail_width;
    return slot == object->initializer_count && cursor == storage_size;
}


/* Shared ELF object path; target relocations remain callback-selectable. */

/* Symbol identity is language-level AST metadata, not an ELF dialect.
 * All target relocation renderers still own directive and addend spelling. */
const char *minic_multi_global_relocation_target_name(
    const MinicC0Program *program, const MinicGlobalRelocation *relocation) {
    if (program == NULL || relocation == NULL)
        return NULL;
    if (relocation->target_kind == MINIC_GLOBAL_RELOCATION_OBJECT) {
        const MinicGlobalObject *object =
            minic_c0_program_global_object(program, relocation->target_id);
        return object != NULL && object->name_length != 0U ? object->name : NULL;
    }
    if (relocation->target_kind == MINIC_GLOBAL_RELOCATION_FUNCTION) {
        const MinicFunction *function =
            minic_c0_program_function(program, relocation->target_id);
        return function != NULL && function->name_length != 0U
            ? minic_c0_function_symbol_name(function) : NULL;
    }
    return NULL;
}

bool minic_multi_emit_elf_relocation(FILE *out, const MinicC0Program *program,
                                   const MinicTargetInfo *target,
                                   const MinicGlobalRelocation *reloc,
                                   size_t width) {
    const char *name = NULL;
    int64_t addend;
    if (reloc == NULL || width != 8U ||
        target->data_layout->pointer_size != 8U) return false;
    if (reloc->target_kind == MINIC_GLOBAL_RELOCATION_LABEL) {
        const MinicStatement *label =
            minic_c0_program_statement(program, reloc->target_id);
        return label != NULL && label->kind == MINIC_STATEMENT_LABEL &&
               fprintf(out, "  .8byte .Luser_%zu\n", reloc->target_id) >= 0;
    }
    name = minic_multi_global_relocation_target_name(program, reloc);
    if (name == NULL || name[0] == '\0' ||
        !minic_data_layout_global_relocation_target_addend(
            target->data_layout, program, reloc, &addend)) return false;
    if (addend == 0)
        return fprintf(out, "  .8byte %s\n", name) >= 0;
    return fprintf(out, "  .8byte %s%+" PRId64 "\n", name, addend) >= 0;
}

static bool minic_multi_emit_flat_record_global(FILE *out,
                                    const MinicC0Program *program,
                                    const MinicTargetInfo *target,
                                    const MinicGlobalObject *obj,
                                    size_t storage_size,
                                           MinicMultiRelocEmitter emit_relocation) {
    const MinicRecord *record =
        minic_c0_program_record(program, obj->type.record_id);
    size_t i, cursor = 0U, reloc_index = 0U;
    if (record == NULL || !record->is_complete || record->is_union ||
        record->field_count == 0U ||
        record->field_count != obj->initializer_count ||
        obj->initializer_values == NULL) return false;
    for (i = 0U; i < record->field_count; ++i) {
        const MinicRecordField *field = minic_c0_record_field(record, i);
        const MinicGlobalRelocation *reloc =
            reloc_index < obj->relocation_count
                ? &obj->relocations[reloc_index] : NULL;
        size_t offset, width, alignment;
        const char *directive;
        uint64_t bits;
        if (field == NULL || field->is_bit_field ||
            field->is_flexible_array || field->element_count != 1U ||
            (!minic_type_is_integer(field->type) &&
             !minic_type_is_pointer(field->type)) ||
            !minic_data_layout_type(target->data_layout, program,
                                    field->type, &width, &alignment) ||
            !minic_data_layout_record_field_offset(
                target->data_layout, program, record, i, &offset) ||
            (width != 1U && width != 2U && width != 4U && width != 8U) ||
            offset < cursor || offset > storage_size ||
            width > storage_size - offset ||
            fprintf(out, "  .zero %zu\n", offset - cursor) < 0)
            return false;
        (void)alignment;
        if (reloc != NULL &&
            (reloc->location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_RECORD_FIELD ||
             reloc->location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR) &&
            reloc->location_index == i) {
            if (!minic_type_is_pointer(field->type) ||
                obj->initializer_values[i] != 0U ||
                !emit_relocation(out, program, target, reloc, width))
                return false;
            ++reloc_index;
        } else {
            bits = obj->initializer_values[i];
            if (width < 8U)
                bits &= (UINT64_C(1) << (width * 8U)) - UINT64_C(1);
            directive = width == 1U ? ".byte" : width == 2U ? ".2byte" :
                        width == 4U ? ".4byte" : ".8byte";
            if (fprintf(out, "  %s %" PRIu64 "\n", directive, bits) < 0)
                return false;
        }
        cursor = offset + width;
    }
    return reloc_index == obj->relocation_count &&
           cursor <= storage_size &&
           fprintf(out, "  .zero %zu\n", storage_size - cursor) >= 0;
}

const char *minic_multi_elf_visibility_directive(MinicSymbolVisibility visibility) {
    switch (visibility) {
    case MINIC_SYMBOL_VISIBILITY_DEFAULT: return NULL;
    case MINIC_SYMBOL_VISIBILITY_HIDDEN: return ".hidden";
    case MINIC_SYMBOL_VISIBILITY_INTERNAL: return ".internal";
    case MINIC_SYMBOL_VISIBILITY_PROTECTED: return ".protected";
    default: return NULL;
    }
}

bool minic_multi_emit_elf_symbol_visibility(FILE *out, MinicSymbolVisibility visibility,
                                   const char *name) {
    const char *directive = minic_multi_elf_visibility_directive(visibility);
    return visibility == MINIC_SYMBOL_VISIBILITY_DEFAULT ||
           (directive != NULL && fprintf(out, "%s %s\n", directive, name) >= 0);
}

bool minic_multi_emit_simple_global(FILE *out, const MinicC0Program *program,
                               const MinicTargetInfo *target,
                               const MinicGlobalObject *obj,
                                    MinicMultiRelocEmitter emit_relocation) {
    MinicType scalar;
    size_t storage_size, align, width, scalar_align, i;
    const char *directive;
    if (obj == NULL || obj->name == NULL || !obj->name[0] ||
        obj->is_weak ||
        (obj->visibility != MINIC_SYMBOL_VISIBILITY_DEFAULT &&
         minic_multi_elf_visibility_directive(obj->visibility) == NULL) ||
        obj->section_name != NULL ||
        obj->alias_target != MINIC_GLOBAL_OBJECT_INVALID ||
        !minic_data_layout_global_object(target->data_layout, program, obj,
                                         &storage_size, &align)) return false;
    /* .internal/.hidden must be emitted even for flat record globals. */
    if (!obj->is_internal &&
        !minic_multi_emit_elf_symbol_visibility(out, obj->visibility, obj->name))
        return false;
    /* An uninitialized file-scope object is zero-filled, including an
     * array of records. Original TinyCC __vstack[257] has five-field
     * SValue records and no explicit initializer. Its bytes are all zero:
     * requiring 257 recursive record initializers is incorrect.
     * A definition that has no bytes and no relocations may use .zero
     * for its entire DataLayout-computed extent, preserving alignment. */
    if (obj->initializer_count == 0U &&
        obj->relocation_count == 0U &&
        obj->union_selection_count == 0U) {
        const char *section = obj->is_read_only ? ".section .rodata" : ".bss";
        return fprintf(out, "%s\n", section) >= 0 &&
            (obj->is_internal ||
             fprintf(out, ".globl %s\n", obj->name) >= 0) &&
            fprintf(out, "  .balign %zu\n.type %s,@object\n%s:\n"
                         "  .zero %zu\n.size %s,%zu\n",
                    align, obj->name, obj->name, storage_size,
                    obj->name, storage_size) >= 0;
    }
    if (obj->is_zero_initialized && obj->initializer_count == 0U &&
        obj->relocation_count != 0U &&
        (minic_type_is_pointer(obj->type) || minic_type_is_array(obj->type))) {
        const char *const section = obj->is_read_only
            ? ".section .data.rel.ro" : ".data";
        if (fprintf(out, "%s\n", section) < 0 ||
            (!obj->is_internal && fprintf(out, ".globl %s\n", obj->name) < 0) ||
            fprintf(out, "  .balign %zu\n.type %s,@object\n%s:\n",
                    align, obj->name, obj->name) < 0 ||
            !minic_multi_emit_zero_relocation_global(out, program, target,
                obj, storage_size, emit_relocation) ||
            fprintf(out, ".size %s,%zu\n", obj->name, storage_size) < 0)
            return false;
        return true;
    }
    /* File-scope records/unions lacking scalar initializers are zero-filled
     * even when an implicit definition has no is_zero_initialized flag.
     * Also accept explicit all-zero initializers, not relocation payloads. */
    if (minic_type_is_record(obj->type) && obj->relocation_count == 0U) {
        bool all_zero = obj->initializer_count == 0U;
        if (obj->initializer_count != 0U && obj->initializer_values != NULL) {
            all_zero = true;
            for (i = 0U; i < obj->initializer_count; ++i)
                if (obj->initializer_values[i] != 0U) {
                    all_zero = false;
                    break;
                }
        }
        if (all_zero) {
            const char *const section = obj->is_read_only
                ? ".section .rodata" : ".bss";
            return fprintf(out, "%s\n", section) >= 0 &&
                (obj->is_internal ||
                 fprintf(out, ".globl %s\n", obj->name) >= 0) &&
                fprintf(out, "  .balign %zu\n.type %s,@object\n%s:\n"
                             "  .zero %zu\n.size %s,%zu\n",
                        align, obj->name, obj->name,
                        storage_size, obj->name, storage_size) >= 0;
        }
    }
    if (minic_type_is_record(obj->type)) {
        const MinicRecord *record =
            minic_c0_program_record(program, obj->type.record_id);
        const bool recursive = record != NULL &&
            (record->is_union ||
             record->field_count != obj->initializer_count ||
             obj->union_selection_count != 0U);
        if (record == NULL) return false;
        if (fprintf(out, "%s\n", obj->is_read_only
                        ? (obj->relocation_count != 0U
                           ? ".section .data.rel.ro" : ".section .rodata")
                        : ".data") < 0 ||
            (!obj->is_internal &&
             fprintf(out, ".globl %s\n", obj->name) < 0) ||
            fprintf(out, "  .balign %zu\n.type %s,@object\n%s:\n",
                    align, obj->name, obj->name) < 0 ||
            !(obj->flexible_array_initializer_count != 0U
                  ? minic_multi_emit_flexible_record_global(
                        out, program, target, obj, storage_size,
                        emit_relocation)
                  : recursive
                    ? minic_multi_emit_recursive_record_global(
                          out, program, target, obj, storage_size,
                          emit_relocation)
                    : minic_multi_emit_flat_record_global(out, program, target, obj,
                                              storage_size, emit_relocation)) ||
            fprintf(out, ".size %s,%zu\n", obj->name, storage_size) < 0)
            return false;
        return true;
    }
    /* zlib real-source pressure: z_errmsg[] is a pointer-relocation
     * array, configuration_table[] an array of records with call targets,
     * and inflate/trees expose large arrays of nested flat records.
     * Share proven DataLayout traversal; preserve ISA-owned ELF relocation. */
    if (minic_type_is_array(obj->type)) {
        const MinicArrayType *array =
            minic_c0_program_array_type(program, obj->type.array_type_id);
        bool array_of_records;
        if (array == NULL) return false;
        array_of_records = minic_type_is_record(array->element_type);
        if (obj->union_selection_count != 0U && !array_of_records) return false;
        if (array_of_records || obj->relocation_count != 0U) {
            const char *section = obj->is_read_only
                ? (obj->relocation_count != 0U
                   ? ".section .data.rel.ro" : ".section .rodata") : ".data";
            if ((obj->initializer_count != 0U &&
                 obj->initializer_values == NULL) ||
                fprintf(out, "%s\n", section) < 0 ||
                (!obj->is_internal &&
                 fprintf(out, ".globl %s\n", obj->name) < 0) ||
                fprintf(out, "  .balign %zu\n.type %s,@object\n%s:\n",
                        align, obj->name, obj->name) < 0 ||
                !(array_of_records
                    ? minic_multi_emit_record_array_global(
                          out, program, target, obj, storage_size,
                          emit_relocation)
                    : minic_multi_emit_relocatable_scalar_array(
                          out, program, target, obj, storage_size,
                          emit_relocation)) ||
                fprintf(out, ".size %s,%zu\n", obj->name, storage_size) < 0)
                return false;
            return true;
        }
    }
    if (obj->union_selection_count != 0U ||
        obj->relocation_count != 0U) return false;
    scalar = obj->type;
    while (minic_type_is_array(scalar)) {
        const MinicArrayType *array =
            minic_c0_program_array_type(program, scalar.array_type_id);
        if (array == NULL || array->element_count == 0U) return false;
        scalar = array->element_type;
    }
    /* Ordinary nullable pointer objects and arrays of pointer scalars
     * have exact pointer-sized zero payloads on these two ELF LP64 targets.
     * Nonzero numeric pointer initializers remain fail-closed. */
    if ((!minic_type_is_integer(scalar) &&
         !minic_type_is_pointer(scalar)) ||
        !minic_data_layout_type(target->data_layout, program, scalar,
                                &width, &scalar_align) ||
        (width != 1U && width != 2U && width != 4U && width != 8U) ||
        width > storage_size ||
        obj->initializer_count > storage_size / width ||
        (obj->initializer_count > 0U && obj->initializer_values == NULL))
        return false;
    (void)scalar_align;
    directive = width == 1U ? ".byte" : width == 2U ? ".2byte" :
                width == 4U ? ".4byte" : ".8byte";
    if (fprintf(out, "%s\n", obj->is_read_only ? ".section .rodata" :
                ((obj->is_zero_initialized || obj->is_tentative) &&
                 obj->initializer_count == 0U ? ".bss" : ".data")) < 0)
        return false;
    if (!obj->is_internal && fprintf(out, ".globl %s\n", obj->name) < 0)
        return false;
    if (fprintf(out, "  .balign %zu\n.type %s,@object\n%s:\n",
                align, obj->name, obj->name) < 0) return false;
    if (obj->initializer_count == 0U) {
        if (fprintf(out, "  .zero %zu\n", storage_size) < 0) return false;
    } else {
        for (i = 0U; i < obj->initializer_count; ++i) {
            uint64_t bits = obj->initializer_values[i];
            if (minic_type_is_pointer(scalar) && bits != 0U)
                return false;
            if (width < 8U)
                bits &= (UINT64_C(1) << (8U * width)) - UINT64_C(1);
            if (fprintf(out, "  %s %" PRIu64 "\n", directive, bits) < 0)
                return false;
        }
        if (storage_size > obj->initializer_count * width &&
            fprintf(out, "  .zero %zu\n",
                    storage_size - obj->initializer_count * width) < 0)
            return false;
    }
    return fprintf(out, ".size %s,%zu\n", obj->name, storage_size) >= 0;
}
