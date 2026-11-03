#include "compiler/global_payload.h"
#include "target/data_layout_source.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The source/DataLayout owner for C aggregate initialization.  Its typed
 * payload plan is captured once into sealed ModuleIR before machine emission. */
static bool minic_payload_scalar_width_supported(size_t width) {
    return width == 1U || width == 2U || width == 4U || width == 8U;
}

static bool minic_payload_visibility_valid(MinicSymbolVisibility visibility) {
    return visibility == MINIC_SYMBOL_VISIBILITY_DEFAULT ||
           visibility == MINIC_SYMBOL_VISIBILITY_HIDDEN ||
           visibility == MINIC_SYMBOL_VISIBILITY_INTERNAL ||
           visibility == MINIC_SYMBOL_VISIBILITY_PROTECTED;
}

bool minic_multi_global_explicit_bss_admitted(const MinicGlobalObject *object) {
    size_t index;
    const char *name;
    if (object == NULL) return false;
    name = object->section_name;
    if (name == NULL || strncmp(name, ".bss", 4U) != 0 ||
        (name[4] != '\0' && name[4] != '.'))
        return true;
    if (object->relocation_count != 0U ||
        (object->initializer_count != 0U && object->initializer_values == NULL))
        return false;
    for (index = 0U; index < object->initializer_count; ++index)
        if (object->initializer_values[index] != 0U) return false;
    return true;
}

static bool minic_multi_visit_record_bit_field_run(
    const MinicMultiGlobalPayloadSink *sink,
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicGlobalObject *object, const MinicRecord *record,
    size_t field_limit, size_t type_size, size_t *field_index,
    size_t *initializer_index, size_t *relocation_index, size_t *cursor);

static bool minic_payload_sink_valid(const MinicMultiGlobalPayloadSink *sink) {
    return sink != NULL && sink->emit_zero != NULL &&
           sink->emit_bits != NULL && sink->emit_relocation != NULL;
}

static bool minic_payload_zero(const MinicMultiGlobalPayloadSink *sink,
                               size_t bytes) {
    return sink->emit_zero(sink->context, bytes);
}

static bool minic_payload_bits(const MinicMultiGlobalPayloadSink *sink,
                               MinicType type, size_t width, uint64_t bits) {
    /* Canonicalize source integer/floating payloads before snapshotting.
     * The historical GAS adapters all consumed these same low width bits. */
    if (width != 1U && width != 2U && width != 4U && width != 8U)
        return false;
    if (width < 8U)
        bits &= (UINT64_C(1) << (width * 8U)) - UINT64_C(1);
    return sink->emit_bits(sink->context, type, width, bits);
}

static bool minic_payload_relocation(const MinicMultiGlobalPayloadSink *sink,
                                     const MinicGlobalRelocation *relocation,
                                     size_t width) {
    return sink->emit_relocation(sink->context, relocation, width);
}

static bool minic_multi_visit_relocatable_scalar_array(
    const MinicMultiGlobalPayloadSink *sink, const MinicC0Program *program,
    const MinicTargetInfo *target, const MinicGlobalObject *obj,
    size_t storage_size) {
    const MinicArrayType *arr;
    size_t element_bytes, alignment, i, relocation_index = 0U;
    if (!minic_payload_sink_valid(sink) || program == NULL || target == NULL ||
        target->data_layout == NULL || obj == NULL) return false;
    if (!minic_type_is_array(obj->type)) return false;
    arr = minic_c0_program_array_type(program, obj->type.array_type_id);
    if (arr == NULL ||
        (!minic_type_is_pointer(arr->element_type) &&
         !minic_type_is_integer(arr->element_type)) ||
        !minic_data_layout_type(target->data_layout, program, arr->element_type,
                                &element_bytes, &alignment) ||
        element_bytes == 0U || arr->element_count > SIZE_MAX / element_bytes ||
        arr->element_count * element_bytes != storage_size ||
        obj->initializer_count > arr->element_count ||
        (obj->initializer_count != 0U && obj->initializer_values == NULL) ||
        (obj->relocation_count != 0U && obj->relocations == NULL))
        return false;
    (void)alignment;
    /* C permits trailing implicit-zero array elements.  The number of
     * explicit initializer slots is not the array's storage extent, and
     * target ELF relocation slots may be interleaved with those values.
     * Serialize the full DataLayout extent without reading past the source
     * initializer vector.  Never invent a relocation for a zero slot. */
    for (i = 0U; i < arr->element_count; ++i) {
        const MinicGlobalRelocation *reloc =
            relocation_index < obj->relocation_count ?
                &obj->relocations[relocation_index] : NULL;
        const uint64_t bits = i < obj->initializer_count ?
            obj->initializer_values[i] : UINT64_C(0);
        if (reloc != NULL &&
            (reloc->location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_ARRAY_ELEMENT ||
             reloc->location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR) &&
            reloc->location_index == i) {
            if (bits != 0U ||
                !minic_payload_relocation(sink, reloc, element_bytes))
                return false;
            ++relocation_index;
        } else if (i >= obj->initializer_count) {
            if (!minic_payload_zero(sink, element_bytes))
                return false;
        } else if (!minic_payload_bits(sink, arr->element_type,
                                       element_bytes, bits))
            return false;
    }
    return relocation_index == obj->relocation_count;
}


static bool minic_multi_visit_flat_record_array(
    const MinicMultiGlobalPayloadSink *sink, const MinicC0Program *program,
    const MinicTargetInfo *target, const MinicGlobalObject *object,
    size_t storage_size) {
    const MinicArrayType *array;
    const MinicRecord *record;
    size_t record_bytes, record_align, element, field_index;
    size_t initializer_index = 0U, relocation_index = 0U;
    size_t matched_union_selections = 0U;
    if (!minic_payload_sink_valid(sink) || program == NULL || target == NULL ||
        target->data_layout == NULL || object == NULL) return false;
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
        object->initializer_count > record->field_count * array->element_count ||
        object->initializer_count % record->field_count != 0U ||
        (object->initializer_count != 0U && object->initializer_values == NULL) ||
        (object->relocation_count != 0U && object->relocations == NULL))
        return false;
    (void)record_align;
    for (element = 0U; element < array->element_count; ++element) {
        size_t cursor = 0U;
        /* All absent trailing records are C-zero-initialized, including
         * internal pointer fields and padding.  Do not index an absent
         * flattened slot; the complete extent still comes from DataLayout.
         * Any relocation accidentally targeting this tail is rejected by
         * the final relocation-consumption check below. */
        if (element >= object->initializer_count / record->field_count) {
            if (!minic_payload_zero(sink, record_bytes))
                return false;
            continue;
        }
        for (field_index = 0U; field_index < record->field_count; ++field_index) {
            const MinicRecordField *field = minic_c0_record_field(record, field_index);
            const MinicGlobalRelocation *reloc =
                relocation_index < object->relocation_count
                    ? &object->relocations[relocation_index] : NULL;
            size_t offset, field_bytes, field_align;
            MinicType emitted_type;
            size_t emitted_bytes, emitted_alignment, emitted_offset = 0U;
            /* The flattened record-array fast path uses the same checked
             * DataLayout bitfield packer as recursive records and RV64.
             * This removes its independent <=64-bit/byte-aligned algorithm. */
            if (field != NULL && field->is_bit_field) {
                if (!minic_multi_visit_record_bit_field_run(
                        sink, program, target, object, record, record->field_count,
                        record_bytes, &field_index, &initializer_index,
                        &relocation_index, &cursor))
                    return false;
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
                !minic_payload_zero(sink, offset - cursor))
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
                !minic_payload_scalar_width_supported(emitted_bytes) ||
                emitted_bytes > field_bytes) return false;
            (void)emitted_alignment;
            if (reloc != NULL &&
                reloc->location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR &&
                reloc->location_index == initializer_index) {
                if (object->initializer_values[initializer_index] != 0U ||
                    (!minic_type_is_pointer(emitted_type) &&
                     !minic_type_is_integer(emitted_type)) ||
                    !minic_payload_relocation(sink, reloc, emitted_bytes))
                    return false;
                ++relocation_index;
            } else if (!minic_payload_bits(sink, emitted_type, emitted_bytes,
                                           object->initializer_values[initializer_index])) {
                return false;
            }
            if (!minic_payload_zero(sink, field_bytes - emitted_bytes))
                return false;
            ++initializer_index;
            cursor = offset + field_bytes;
        }
        if (!minic_payload_zero(sink, record_bytes - cursor))
            return false;
    }
    return initializer_index == object->initializer_count &&
           relocation_index == object->relocation_count &&
           matched_union_selections == object->union_selection_count;
}


/* Lua's HOOKKEY/CLIBS/output pointer globals: zero bytes with relocation
 * entries and no explicit scalar initializer. Preserve exact offsets using
 * the same DataLayout algorithm proven by x86. */
static bool minic_multi_visit_zero_relocation_global(
    const MinicMultiGlobalPayloadSink *sink, const MinicC0Program *program,
    const MinicTargetInfo *target, const MinicGlobalObject *obj,
    size_t storage_size) {
    size_t cursor = 0U, i, width;
    if (!minic_payload_sink_valid(sink) || program == NULL ||
        target == NULL || target->data_layout == NULL || obj == NULL ||
        obj->initializer_count != 0U ||
        obj->relocation_count == 0U ||
        (!minic_type_is_pointer(obj->type) &&
         !minic_type_is_array(obj->type) &&
         !minic_type_is_record(obj->type) &&
         !(minic_type_is_integer(obj->type) && storage_size == target->data_layout->pointer_size)))
        return false;
    width = target->data_layout->pointer_size;
    /* The common DataLayout relocation walker supports both LP64 and
     * ILP32: render a relocation at the active target pointer width. */
    if (width != 4U && width != 8U) return false;
    for (i = 0U; i < obj->relocation_count; ++i) {
        const MinicGlobalRelocation *reloc = &obj->relocations[i];
        size_t offset;
        if (!minic_data_layout_global_relocation_offset(
                target->data_layout, program, obj, reloc, &offset) ||
            offset < cursor || offset > storage_size ||
            width > storage_size - offset ||
            !minic_payload_zero(sink, offset - cursor) ||
            !minic_payload_relocation(sink, reloc, width))
            return false;
        cursor = offset + width;
    }
    return minic_payload_zero(sink, storage_size - cursor);
}


static bool minic_multi_visit_zero_bytes(
    const MinicMultiGlobalPayloadSink *sink, size_t size) {
    return size == 0U || minic_payload_zero(sink, size);
}

static bool minic_multi_visit_record_bit_field_run(
    const MinicMultiGlobalPayloadSink *sink,
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicGlobalObject *object, const MinicRecord *record,
    size_t field_limit, size_t type_size, size_t *field_index,
    size_t *initializer_index, size_t *relocation_index, size_t *cursor) {
    uint8_t *bytes;
    size_t run_first;
    size_t run_end;
    size_t run_start_offset;
    size_t run_start_bit;
    size_t run_end_offset;
    size_t run_size;
    size_t index;

    if (!minic_payload_sink_valid(sink) || program == NULL || target == NULL ||
        target->data_layout == NULL || object == NULL || record == NULL ||
        field_index == NULL || initializer_index == NULL || relocation_index == NULL ||
        cursor == NULL || *field_index >= field_limit ||
        !record->fields[*field_index].is_bit_field) {
        return false;
    }
    run_first = *field_index;
    if (record->fields[run_first].bit_width == 0U) {
        const MinicGlobalRelocation *relocation;
        size_t slot_index;

        if (*initializer_index >= object->initializer_count) {
            return false;
        }
        slot_index = *initializer_index;
        if (object->initializer_values[slot_index] != 0U) {
            return false;
        }
        relocation = *relocation_index < object->relocation_count
                         ? &object->relocations[*relocation_index]
                         : NULL;
        if (relocation != NULL &&
            relocation->location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR &&
            relocation->location_index <= slot_index) {
            return false;
        }
        *initializer_index += 1U;
        return true;
    }
    run_end = run_first;
    while (run_end < field_limit && record->fields[run_end].is_bit_field &&
           record->fields[run_end].bit_width != 0U) {
        run_end += 1U;
    }
    if (!minic_data_layout_record_field_layout(target->data_layout,
                                               program,
                                               record,
                                               run_first,
                                               &run_start_offset,
                                               &run_start_bit) ||
        run_start_bit >= 8U || run_start_offset < *cursor) {
        return false;
    }
    if (run_end < field_limit) {
        size_t next_bit_offset;

        if (!minic_data_layout_record_field_layout(target->data_layout,
                                                   program,
                                                   record,
                                                   run_end,
                                                   &run_end_offset,
                                                   &next_bit_offset)) {
            return false;
        }
        (void)next_bit_offset;
    } else {
        run_end_offset = type_size;
    }
    if (run_end_offset < run_start_offset || run_end_offset > type_size ||
        !minic_multi_visit_zero_bytes(sink, run_start_offset - *cursor)) {
        return false;
    }
    run_size = run_end_offset - run_start_offset;
    bytes = run_size == 0U ? NULL : (uint8_t *)calloc(run_size, sizeof(*bytes));
    if (run_size != 0U && bytes == NULL) {
        return false;
    }

    for (index = run_first; index < run_end; ++index) {
        const MinicRecordField *field;
        const MinicGlobalRelocation *relocation;
        uint64_t bits;
        uint64_t mask;
        size_t slot_index;
        size_t field_offset;
        size_t bit_offset;
        size_t relative_bit;
        size_t bit_index;

        field = &record->fields[index];
        if (*initializer_index >= object->initializer_count) {
            free(bytes);
            return false;
        }
        slot_index = *initializer_index;
        bits = object->initializer_values[slot_index];
        *initializer_index += 1U;
        relocation = *relocation_index < object->relocation_count
                         ? &object->relocations[*relocation_index]
                         : NULL;
        if (relocation != NULL &&
            relocation->location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR &&
            relocation->location_index <= slot_index) {
            free(bytes);
            return false;
        }
        if (!minic_data_layout_record_field_layout(
                target->data_layout, program, record, index, &field_offset, &bit_offset) ||
            field_offset < run_start_offset || bit_offset >= 8U) {
            free(bytes);
            return false;
        }
        if (field->bit_width == 0U) {
            if (bits != 0U) {
                free(bytes);
                return false;
            }
            continue;
        }
        if (field->bit_width > 64U || field_offset - run_start_offset > SIZE_MAX / 8U) {
            free(bytes);
            return false;
        }
        relative_bit = (field_offset - run_start_offset) * 8U + bit_offset;
        if (run_size > SIZE_MAX / 8U || relative_bit > run_size * 8U ||
            field->bit_width > run_size * 8U - relative_bit) {
            free(bytes);
            return false;
        }
        mask =
            field->bit_width == 64U ? UINT64_MAX : (UINT64_C(1) << field->bit_width) - UINT64_C(1);
        bits &= mask;
        for (bit_index = 0U; bit_index < field->bit_width; ++bit_index) {
            size_t positioned_bit;

            if ((bits & (UINT64_C(1) << bit_index)) == 0U) {
                continue;
            }
            positioned_bit = relative_bit + bit_index;
            bytes[positioned_bit / 8U] |= (uint8_t)(1U << (positioned_bit % 8U));
        }
    }
    for (index = 0U; index < run_size; ++index) {
        const MinicType raw_byte_type = {
            .base_kind = MINIC_TYPE_BASE_INT,
            .integer_sign = MINIC_INTEGER_SIGN_UNSIGNED,
            .integer_rank = MINIC_INTEGER_RANK_CHAR
        };
        if (!minic_payload_bits(sink, raw_byte_type, 1U,
                                (uint64_t)bytes[index])) {
            free(bytes);
            return false;
        }
    }
    free(bytes);
    *cursor = run_end_offset;
    *field_index = run_end - 1U;
    return true;
}

static bool minic_multi_visit_recursive_global_value(
    const MinicMultiGlobalPayloadSink *sink,
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicGlobalObject *object, MinicType type, size_t *initializer_index,
    size_t *relocation_index, size_t *matched_union_selections,
    size_t *emitted_size, unsigned int depth) {
    size_t type_size, type_alignment;
    if (!minic_payload_sink_valid(sink) || program == NULL ||
        target == NULL || target->data_layout == NULL || object == NULL ||
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
        /* GNU Linux embeds 128-bit integer state within nested static
         * records. The scalar initializer model currently stores one
         * 64-bit payload per leaf: zero is unambiguous across BOTH 64-bit
         * halves and can be emitted exactly, whereas a nonzero 128-bit
         * initializer must await a full-width payload representation.
         * Never truncate or silently drop a relocation into this slot. */
        if (minic_type_is_int128_integer(type) && type_size == 16U) {
            if (*initializer_index >= object->initializer_count ||
                object->initializer_values == NULL ||
                object->initializer_values[*initializer_index] != 0U ||
                (reloc != NULL &&
                 reloc->location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR &&
                 reloc->location_index == *initializer_index) ||
                !minic_payload_zero(sink, 16U))
                return false;
            ++*initializer_index;
            *emitted_size = 16U;
            return true;
        }
        if (*initializer_index >= object->initializer_count ||
            !minic_payload_scalar_width_supported(type_size))
            return false;
        if (reloc != NULL &&
            reloc->location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR &&
            reloc->location_index == *initializer_index) {
            if (object->initializer_values[*initializer_index] != 0U ||
                (!minic_type_is_integer(type) && !minic_type_is_pointer(type)) ||
                !minic_payload_relocation(sink, reloc, type_size))
                return false;
            ++*relocation_index;
        } else if (!minic_payload_bits(sink, type, type_size,
                                       object->initializer_values[*initializer_index]))
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
            if (!minic_multi_visit_recursive_global_value(
                    sink, program, target, object, arr->element_type,
                    initializer_index, relocation_index, matched_union_selections,
                    &emitted, depth + 1U) ||
                (cursor > type_size || emitted > type_size - cursor))
                return false;
            cursor += emitted;
        }
        if (!minic_payload_zero(sink, type_size - cursor))
            return false;
        *emitted_size = type_size;
        return true;
    }
    if (minic_type_is_record(type)) {
        const MinicRecord *record =
            minic_c0_program_record(program, type.record_id);
        size_t begin = 0U, end, start_slot = *initializer_index;
        size_t union_span = 0U, cursor = 0U, i;
        size_t record_storage_size = type_size;
        if (record == NULL || !record->is_complete)
            return false;
        /* A GNU-initialized trailing flexible array extends the storage
         * beyond sizeof(struct). The global-object DataLayout, not the
         * declared record type size, owns the concrete output extent. */
        if (object->flexible_array_initializer_count != 0U &&
            minic_type_is_record(object->type) &&
            object->type.record_id == type.record_id) {
            size_t object_alignment;
            if (!minic_data_layout_global_object(
                    target->data_layout, program, object,
                    &record_storage_size, &object_alignment))
                return false;
            (void)object_alignment;
        }
        if (record->field_count == 0U) {
            if (type_size != 0U)
                return false;
            *emitted_size = 0U;
            return true;
        }
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
            if (field == NULL)
                return false;
            if (field->is_zero_length_array) continue;
            if (field->is_flexible_array) {
                size_t flexible_count;
                if (record->is_union || i + 1U != end ||
                    !minic_data_layout_record_field_offset(
                        target->data_layout, program, record, i, &field_offset) ||
                    field_offset < cursor ||
                    field_offset > record_storage_size ||
                    !minic_multi_visit_zero_bytes(sink, field_offset - cursor))
                    return false;
                cursor = field_offset;
                flexible_count = minic_type_is_record(object->type) &&
                                 object->type.record_id == type.record_id
                                     ? object->flexible_array_initializer_count
                                     : 0U;
                for (element = 0U; element < flexible_count; ++element) {
                    size_t emitted;
                    if (!minic_multi_visit_recursive_global_value(
                            sink, program, target, object, field->type,
                            initializer_index, relocation_index,
                            matched_union_selections, &emitted, depth + 1U) ||
                        cursor > record_storage_size ||
                        emitted > record_storage_size - cursor)
                        return false;
                    cursor += emitted;
                }
                continue;
            }
            if (field->is_bit_field) {
                if (!minic_multi_visit_record_bit_field_run(
                        sink, program, target, object, record, end, type_size,
                        &i, initializer_index, relocation_index, &cursor))
                    return false;
                if (record->is_union) break;
                continue;
            }
            if (field->element_count == 0U) return false;
            if (record->is_union) {
                field_offset = 0U;
            } else if (!minic_data_layout_record_field_offset(
                           target->data_layout, program, record, i,
                           &field_offset)) {
                return false;
            }
            if (field_offset < cursor || field_offset > type_size ||
                !minic_payload_zero(sink, field_offset - cursor))
                return false;
            cursor = field_offset;
            for (element = 0U; element < field->element_count; ++element) {
                size_t emitted;
                if (!minic_multi_visit_recursive_global_value(
                        sink, program, target, object, field->type,
                        initializer_index, relocation_index, matched_union_selections,
                        &emitted, depth + 1U) ||
                    (cursor > type_size || emitted > type_size - cursor))
                    return false;
                cursor += emitted;
            }
        }
        if (cursor > record_storage_size ||
            !minic_multi_visit_zero_bytes(
                sink, record_storage_size - cursor))
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
        *emitted_size = record_storage_size;
        return true;
    }
    return false;
}

static bool minic_multi_visit_recursive_aggregate_global(
    const MinicMultiGlobalPayloadSink *sink,
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicGlobalObject *object, size_t storage_size);


static bool minic_multi_recursive_shape_supported_at(
    const MinicC0Program *program, MinicType type, unsigned int depth) {
    if (program == NULL || depth > 32U)
        return false;
    if (minic_type_is_integer(type) || minic_type_is_pointer(type) ||
        minic_type_is_float(type) || minic_type_is_double(type))
        return true;
    if (minic_type_is_array(type)) {
        const MinicArrayType *array =
            minic_c0_program_array_type(program, type.array_type_id);
        return array != NULL && !array->is_zero_length &&
               array->element_count != 0U &&
               minic_multi_recursive_shape_supported_at(
                   program, array->element_type, depth + 1U);
    }
    if (minic_type_is_record(type)) {
        const MinicRecord *record = minic_c0_program_record(program, type.record_id);
        size_t index;
        /* Complete empty records have zero bytes/slots; they are valid
         * GNU aggregate members on RV64 and do not require a private walk. */
        if (record == NULL || !record->is_complete)
            return false;
        for (index = 0U; index < record->field_count; ++index) {
            const MinicRecordField *field = minic_c0_record_field(record, index);
            if (field == NULL)
                return false;
            if (field->is_zero_length_array)
                continue;
            if (field->is_flexible_array) {
                if (record->is_union || index + 1U != record->field_count ||
                    !minic_multi_recursive_shape_supported_at(
                        program, field->type, depth + 1U))
                    return false;
                continue;
            }
            if (field->is_bit_field) {
                if (!minic_type_is_integer(field->type) ||
                    field->bit_width > 64U || field->element_count != 1U)
                    return false;
                continue;
            }
            if (field->element_count == 0U ||
                !minic_multi_recursive_shape_supported_at(
                    program, field->type, depth + 1U))
                return false;
        }
        return true;
    }
    return false;
}

static bool minic_multi_recursive_shape_supported(
    const MinicC0Program *program, MinicType type) {
    return minic_multi_recursive_shape_supported_at(program, type, 0U);
}

static bool minic_multi_visit_recursive_aggregate_global(
    const MinicMultiGlobalPayloadSink *sink,
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicGlobalObject *object, size_t storage_size) {
    size_t initializer_index = 0U, relocation_index = 0U;
    size_t matched_union_selections = 0U, emitted_size;
    if (!minic_payload_sink_valid(sink) || program == NULL ||
        target == NULL || object == NULL ||
        (!minic_type_is_record(object->type) &&
         !minic_type_is_array(object->type)) ||
        !minic_multi_recursive_shape_supported(program, object->type) ||
        !minic_multi_visit_recursive_global_value(
            sink, program, target, object, object->type,
            &initializer_index, &relocation_index, &matched_union_selections,
            &emitted_size, 0U))
        return false;
    return emitted_size == storage_size &&
           initializer_index == object->initializer_count &&
           relocation_index == object->relocation_count &&
           matched_union_selections == object->union_selection_count;
}


static bool minic_multi_visit_record_array_global(
    const MinicMultiGlobalPayloadSink *sink,
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicGlobalObject *object, size_t storage_size) {
    const MinicArrayType *array;
    const MinicRecord *record;
    bool flat;
    size_t i;
    if (!minic_payload_sink_valid(sink) || program == NULL ||
        target == NULL || object == NULL ||
        !minic_type_is_array(object->type))
        return false;
    array = minic_c0_program_array_type(program, object->type.array_type_id);
    if (array == NULL || !minic_type_is_record(array->element_type))
        return false;
    record = minic_c0_program_record(program, array->element_type.record_id);
    if (record == NULL || !record->is_complete ||
        record->field_count == 0U || array->element_count == 0U)
        return false;
    /* The flat walker can serialize fully specified initial records
     * followed by whole implicit-zero records.  Partial final records and
     * nested shapes retain the recursive DataLayout owner. */
    flat = !record->is_union &&
           record->field_count <= SIZE_MAX / array->element_count &&
           object->initializer_count <= record->field_count * array->element_count &&
           object->initializer_count % record->field_count == 0U;
    for (i = 0U; i < record->field_count && flat; ++i) {
        const MinicRecordField *field = minic_c0_record_field(record, i);
        if (field == NULL || field->element_count != 1U ||
            minic_type_is_array(field->type) ||
            minic_type_is_record(field->type))
            flat = false;
    }
    if (flat)
        return minic_multi_visit_flat_record_array(
            sink, program, target, object, storage_size);
    return minic_multi_visit_recursive_aggregate_global(
        sink, program, target, object, storage_size);
}


static bool minic_multi_visit_flexible_record_global(
    const MinicMultiGlobalPayloadSink *sink,
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicGlobalObject *object, size_t storage_size) {
    return object != NULL && minic_type_is_record(object->type) &&
           object->flexible_array_initializer_count != 0U &&
           minic_multi_visit_recursive_aggregate_global(
               sink, program, target, object, storage_size);
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


bool minic_multi_record_initializer_needs_recursive(
    const MinicRecord *record, const MinicGlobalObject *object) {
    size_t index;
    if (record == NULL || object == NULL || record->is_union ||
        record->field_count != object->initializer_count ||
        object->union_selection_count != 0U ||
        object->flexible_array_initializer_count != 0U)
        return true;
    for (index = 0U; index < record->field_count; ++index) {
        const MinicRecordField *field = &record->fields[index];
        if (field->is_bit_field || field->is_flexible_array ||
            field->is_zero_length_array || field->element_count != 1U ||
            minic_type_is_array(field->type) || minic_type_is_record(field->type))
            return true;
    }
    return false;
}

static bool minic_multi_visit_flat_record_global(
    const MinicMultiGlobalPayloadSink *sink,
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicGlobalObject *obj, size_t storage_size) {
    const MinicRecord *record;
    size_t i, cursor = 0U, reloc_index = 0U;
    if (!minic_payload_sink_valid(sink) || program == NULL || target == NULL ||
        target->data_layout == NULL || obj == NULL) return false;
    record = minic_c0_program_record(program, obj->type.record_id);
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
        if (field == NULL || field->is_bit_field ||
            field->is_flexible_array || field->element_count != 1U ||
            (!minic_type_is_integer(field->type) &&
             !minic_type_is_pointer(field->type) &&
             !minic_type_is_float(field->type) &&
             !minic_type_is_double(field->type)) ||
            !minic_data_layout_type(target->data_layout, program,
                                    field->type, &width, &alignment) ||
            !minic_data_layout_record_field_offset(
                target->data_layout, program, record, i, &offset) ||
            (width != 1U && width != 2U && width != 4U && width != 8U) ||
            offset < cursor || offset > storage_size ||
            width > storage_size - offset ||
            !minic_payload_zero(sink, offset - cursor))
            return false;
        (void)alignment;
        if (reloc != NULL &&
            (reloc->location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_RECORD_FIELD ||
             reloc->location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR) &&
            reloc->location_index == i) {
            if ((!minic_type_is_pointer(field->type) &&
                 !minic_type_is_integer(field->type)) ||
                obj->initializer_values[i] != 0U ||
                !minic_payload_relocation(sink, reloc, width))
                return false;
            ++reloc_index;
        } else if (!minic_payload_bits(sink, field->type, width,
                                       obj->initializer_values[i]))
            return false;
        cursor = offset + width;
    }
    return reloc_index == obj->relocation_count &&
           cursor <= storage_size &&
           minic_payload_zero(sink, storage_size - cursor);
}


bool minic_multi_global_payload_plan_build(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicGlobalObject *obj, MinicMultiGlobalEmissionPlan *plan) {
    MinicType scalar;
    size_t size, alignment, width, scalar_alignment, index;
    if (plan == NULL || program == NULL || target == NULL ||
        target->data_layout == NULL || obj == NULL || obj->name == NULL ||
        obj->name[0] == '\0' ||
        (obj->visibility != MINIC_SYMBOL_VISIBILITY_DEFAULT &&
         !minic_payload_visibility_valid(obj->visibility)) ||
        obj->alias_target != MINIC_GLOBAL_OBJECT_INVALID ||
        (obj->initializer_count != 0U && obj->initializer_values == NULL) ||
        (obj->relocation_count != 0U && obj->relocations == NULL) ||
        !minic_data_layout_global_object(target->data_layout, program, obj,
                                         &size, &alignment))
        return false;
    memset(plan, 0, sizeof(*plan));
    plan->storage_size = size;
    plan->storage_alignment = alignment;

    /* Zero payloads need no fabricated record/array initializer slots.
     * A fully zero record remains bytewise zero including bitfield padding. */
    if (obj->initializer_count == 0U && obj->relocation_count == 0U &&
        obj->union_selection_count == 0U) {
        plan->payload_kind = MINIC_CORE_MODULE_PAYLOAD_ZERO;
        plan->default_section = obj->is_read_only
            ? MINIC_CORE_MODULE_SECTION_RODATA : MINIC_CORE_MODULE_SECTION_BSS;
        return true;
    }
    if (obj->initializer_count == 0U && obj->relocation_count != 0U &&
        (minic_type_is_pointer(obj->type) || minic_type_is_array(obj->type) ||
         minic_type_is_record(obj->type) ||
         (minic_type_is_integer(obj->type) &&
          size == target->data_layout->pointer_size))) {
        plan->payload_kind = MINIC_CORE_MODULE_PAYLOAD_ZERO_RELOCATIONS;
        plan->default_section = obj->is_read_only
            ? MINIC_CORE_MODULE_SECTION_RELRO : MINIC_CORE_MODULE_SECTION_DATA;
        return true;
    }
    if (minic_type_is_record(obj->type) && obj->relocation_count == 0U) {
        bool all_zero = obj->initializer_count == 0U;
        if (obj->initializer_count != 0U) {
            all_zero = true;
            for (index = 0U; index < obj->initializer_count; ++index) {
                if (obj->initializer_values[index] != 0U) {
                    all_zero = false;
                    break;
                }
            }
        }
        if (all_zero) {
            plan->payload_kind = MINIC_CORE_MODULE_PAYLOAD_ZERO;
            plan->default_section = obj->is_read_only
                ? MINIC_CORE_MODULE_SECTION_RODATA : MINIC_CORE_MODULE_SECTION_BSS;
            return true;
        }
    }
    if (minic_type_is_record(obj->type)) {
        const MinicRecord *record =
            minic_c0_program_record(program, obj->type.record_id);
        if (record == NULL) return false;
        plan->payload_kind = obj->flexible_array_initializer_count != 0U
            ? MINIC_CORE_MODULE_PAYLOAD_RECORD_FLEXIBLE
            : minic_multi_record_initializer_needs_recursive(record, obj)
            ? MINIC_CORE_MODULE_PAYLOAD_RECORD_RECURSIVE
            : MINIC_CORE_MODULE_PAYLOAD_RECORD_FLAT;
        plan->default_section = obj->is_read_only
            ? (obj->relocation_count != 0U
               ? MINIC_CORE_MODULE_SECTION_RELRO : MINIC_CORE_MODULE_SECTION_RODATA)
            : MINIC_CORE_MODULE_SECTION_DATA;
        return true;
    }
    if (minic_type_is_array(obj->type)) {
        const MinicArrayType *array =
            minic_c0_program_array_type(program, obj->type.array_type_id);
        bool record_array, nested_record = false;
        bool nested_relocatable;
        MinicType leaf;
        if (array == NULL) return false;
        record_array = minic_type_is_record(array->element_type);
        nested_relocatable = minic_type_is_array(array->element_type) &&
            obj->relocation_count != 0U;
        leaf = array->element_type;
        while (minic_type_is_array(leaf)) {
            const MinicArrayType *nested =
                minic_c0_program_array_type(program, leaf.array_type_id);
            if (nested == NULL) return false;
            leaf = nested->element_type;
            if (minic_type_is_record(leaf)) nested_record = true;
        }
        if (obj->union_selection_count != 0U && !record_array &&
            !nested_record) return false;
        if (record_array || nested_record || obj->relocation_count != 0U) {
            plan->payload_kind = record_array
                ? MINIC_CORE_MODULE_PAYLOAD_ARRAY_RECORD
                : nested_record || nested_relocatable
                ? MINIC_CORE_MODULE_PAYLOAD_ARRAY_RECURSIVE
                : MINIC_CORE_MODULE_PAYLOAD_ARRAY_SCALAR_RELOCATIONS;
            plan->default_section = obj->is_read_only
                ? (obj->relocation_count != 0U
                   ? MINIC_CORE_MODULE_SECTION_RELRO : MINIC_CORE_MODULE_SECTION_RODATA)
                : MINIC_CORE_MODULE_SECTION_DATA;
            return true;
        }
    }
    /* Relocatable scalar storage has a semantic slot and pointer-width
     * representation regardless of the backend's GAS directive spelling.
     * Keep it distinct from zero-initialized relocation-only aggregates. */
    if (obj->relocation_count != 0U &&
        (minic_type_is_pointer(obj->type) || minic_type_is_integer(obj->type))) {
        const MinicGlobalRelocation *reloc;

        if (obj->relocation_count != 1U || obj->initializer_count != 1U ||
            obj->initializer_values[0] != 0U ||
            obj->union_selection_count != 0U ||
            size != target->data_layout->pointer_size) {
            return false;
        }
        reloc = &obj->relocations[0];
        if (reloc->location_kind != MINIC_GLOBAL_RELOCATION_LOCATION_SCALAR ||
            reloc->location_index != 0U) {
            return false;
        }
        plan->payload_kind = MINIC_CORE_MODULE_PAYLOAD_SCALAR_RELOCATION;
        plan->scalar_type = obj->type;
        plan->scalar_width = size;
        plan->default_section = obj->is_read_only
            ? MINIC_CORE_MODULE_SECTION_RELRO : MINIC_CORE_MODULE_SECTION_DATA;
        return true;
    }
    if (obj->union_selection_count != 0U || obj->relocation_count != 0U)
        return false;
    scalar = obj->type;
    while (minic_type_is_array(scalar)) {
        const MinicArrayType *array =
            minic_c0_program_array_type(program, scalar.array_type_id);
        if (array == NULL || array->element_count == 0U) return false;
        scalar = array->element_type;
    }
    /* Integer-to-pointer sentinels are scalar bits, not symbol relocations. */
    if ((!minic_type_is_integer(scalar) && !minic_type_is_pointer(scalar) &&
         !minic_type_is_float(scalar) && !minic_type_is_double(scalar)) ||
        !minic_data_layout_type(target->data_layout, program, scalar,
                                &width, &scalar_alignment) ||
        (width != 1U && width != 2U && width != 4U && width != 8U) ||
        width > size || obj->initializer_count > size / width)
        return false;
    (void)scalar_alignment;
    plan->payload_kind = MINIC_CORE_MODULE_PAYLOAD_SCALAR_BITS;
    plan->scalar_type = scalar;
    plan->scalar_width = width;
    plan->default_section = obj->is_read_only ? MINIC_CORE_MODULE_SECTION_RODATA
        : ((obj->is_zero_initialized || obj->is_tentative) &&
           obj->initializer_count == 0U ? MINIC_CORE_MODULE_SECTION_BSS
                                        : MINIC_CORE_MODULE_SECTION_DATA);
    return true;
}


bool minic_multi_global_payload_visit(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicGlobalObject *obj, const MinicMultiGlobalEmissionPlan *plan,
    const MinicMultiGlobalPayloadSink *sink) {
    size_t i;

    if (!minic_payload_sink_valid(sink) || program == NULL ||
        target == NULL || obj == NULL || plan == NULL) {
        return false;
    }
    switch (plan->payload_kind) {
    case MINIC_CORE_MODULE_PAYLOAD_ZERO:
        if (!minic_payload_zero(sink, plan->storage_size)) return false;
        break;
    case MINIC_CORE_MODULE_PAYLOAD_ZERO_RELOCATIONS:
        if (!minic_multi_visit_zero_relocation_global(
                sink, program, target, obj, plan->storage_size)) return false;
        break;
    case MINIC_CORE_MODULE_PAYLOAD_RECORD_FLAT:
        if (!minic_multi_visit_flat_record_global(
                sink, program, target, obj, plan->storage_size)) return false;
        break;
    case MINIC_CORE_MODULE_PAYLOAD_RECORD_RECURSIVE:
        if (!minic_type_is_record(obj->type) ||
            !minic_multi_visit_recursive_aggregate_global(
                sink, program, target, obj, plan->storage_size)) return false;
        break;
    case MINIC_CORE_MODULE_PAYLOAD_RECORD_FLEXIBLE:
        if (!minic_multi_visit_flexible_record_global(
                sink, program, target, obj, plan->storage_size)) return false;
        break;
    case MINIC_CORE_MODULE_PAYLOAD_ARRAY_RECORD:
        if (!minic_multi_visit_record_array_global(
                sink, program, target, obj, plan->storage_size)) return false;
        break;
    case MINIC_CORE_MODULE_PAYLOAD_ARRAY_RECURSIVE:
        if (!minic_multi_visit_recursive_aggregate_global(
                sink, program, target, obj, plan->storage_size)) return false;
        break;
    case MINIC_CORE_MODULE_PAYLOAD_ARRAY_SCALAR_RELOCATIONS:
        if (!minic_multi_visit_relocatable_scalar_array(
                sink, program, target, obj, plan->storage_size)) return false;
        break;
    case MINIC_CORE_MODULE_PAYLOAD_SCALAR_RELOCATION:
        if (obj->relocation_count != 1U ||
            !minic_payload_relocation(sink, &obj->relocations[0],
                                      plan->scalar_width)) return false;
        break;
    case MINIC_CORE_MODULE_PAYLOAD_SCALAR_BITS:
        if (obj->initializer_count == 0U) {
            if (!minic_payload_zero(sink, plan->storage_size))
                return false;
        } else {
            for (i = 0U; i < obj->initializer_count; ++i) {
                if (!minic_payload_bits(sink, plan->scalar_type,
                                         plan->scalar_width,
                                         obj->initializer_values[i]))
                    return false;
            }
            if (plan->storage_size > obj->initializer_count * plan->scalar_width &&
                !minic_payload_zero(
                    sink, plan->storage_size -
                              obj->initializer_count * plan->scalar_width))
                return false;
        }
        break;
    default:
        return false;
    }
    return true;
}
