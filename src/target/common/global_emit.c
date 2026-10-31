#include "target/common/global_emit.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Private recursive bitfield helper is needed before its first invocation.
 * It is no longer an exported Common ABI endpoint. */
static bool minic_multi_emit_record_bit_field_run(
    FILE *file, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicGlobalObject *object, const MinicRecord *record,
    size_t field_limit, size_t type_size, size_t *field_index,
    size_t *initializer_index, size_t *relocation_index, size_t *cursor);

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
                                              MinicMultiRelocEmitter emit_relocation,
                                               MinicMultiScalarBitsEmitter emit_bits) {
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
                !emit_relocation(out, program, target, reloc, element_bytes))
                return false;
            ++relocation_index;
        } else if (i >= obj->initializer_count) {
            if (fprintf(out, "  .zero %zu\n", element_bytes) < 0)
                return false;
        } else if (!(emit_bits != NULL
                        ? emit_bits(out, program, target, arr->element_type, bits)
                        : minic_multi_emit_data_bits(out, element_bytes, bits)))
            return false;
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
            if (fprintf(out, "  .zero %zu\n", record_bytes) < 0)
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
                if (!minic_multi_emit_record_bit_field_run(
                        out, program, target, object, record, record->field_count,
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
/* Bitfield packing is a DataLayout operation, not an ISA-codegen task.
 * The RV64 legacy walker and the shared recursive walker invoke this same
 * checked byte-range implementation. */
static bool minic_multi_emit_zero_bytes(FILE *file, size_t size) {
    return size == 0U || fprintf(file, "  .zero %zu\n", size) >= 0;
}

static bool minic_multi_emit_record_bit_field_run(FILE *file,
                                                    const MinicC0Program *program,
                                                    const MinicTargetInfo *target,
                                                    const MinicGlobalObject *object,
                                                    const MinicRecord *record,
                                                    size_t field_limit,
                                                    size_t type_size,
                                                    size_t *field_index,
                                                    size_t *initializer_index,
                                                    size_t *relocation_index,
                                                    size_t *cursor) {
    uint8_t *bytes;
    size_t run_first;
    size_t run_end;
    size_t run_start_offset;
    size_t run_start_bit;
    size_t run_end_offset;
    size_t run_size;
    size_t index;

    if (file == NULL || program == NULL || target == NULL ||
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
        !minic_multi_emit_zero_bytes(file, run_start_offset - *cursor)) {
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
        if (fprintf(file, "  .byte %u\n", (unsigned int)bytes[index]) < 0) {
            free(bytes);
            return false;
        }
    }
    free(bytes);
    *cursor = run_end_offset;
    *field_index = run_end - 1U;
    return true;
}

static bool minic_multi_emit_recursive_global_value(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicGlobalObject *object, MinicType type, size_t *initializer_index,
    size_t *relocation_index, size_t *matched_union_selections,
    size_t *emitted_size, unsigned int depth,
    MinicMultiRelocEmitter emit_relocation,
    MinicMultiScalarBitsEmitter emit_bits) {
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
                fprintf(out, "  .zero 16\n") < 0)
                return false;
            ++*initializer_index;
            *emitted_size = 16U;
            return true;
        }
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
        } else if (!(emit_bits != NULL
                         ? emit_bits(out, program, target, type,
                                     object->initializer_values[*initializer_index])
                         : minic_multi_emit_data_bits(
                               out, type_size, object->initializer_values[*initializer_index])))
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
                    &emitted, depth + 1U, emit_relocation, emit_bits) ||
                (cursor > type_size || emitted > type_size - cursor))
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
                    !minic_multi_emit_zero_bytes(out, field_offset - cursor))
                    return false;
                cursor = field_offset;
                flexible_count = minic_type_is_record(object->type) &&
                                 object->type.record_id == type.record_id
                                     ? object->flexible_array_initializer_count
                                     : 0U;
                for (element = 0U; element < flexible_count; ++element) {
                    size_t emitted;
                    if (!minic_multi_emit_recursive_global_value(
                            out, program, target, object, field->type,
                            initializer_index, relocation_index,
                            matched_union_selections, &emitted, depth + 1U,
                            emit_relocation, emit_bits) ||
                        cursor > record_storage_size ||
                        emitted > record_storage_size - cursor)
                        return false;
                    cursor += emitted;
                }
                continue;
            }
            if (field->is_bit_field) {
                if (!minic_multi_emit_record_bit_field_run(
                        out, program, target, object, record, end, type_size,
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
                fprintf(out, "  .zero %zu\n", field_offset - cursor) < 0)
                return false;
            cursor = field_offset;
            for (element = 0U; element < field->element_count; ++element) {
                size_t emitted;
                if (!minic_multi_emit_recursive_global_value(
                        out, program, target, object, field->type,
                        initializer_index, relocation_index, matched_union_selections,
                        &emitted, depth + 1U, emit_relocation, emit_bits) ||
                    (cursor > type_size || emitted > type_size - cursor))
                    return false;
                cursor += emitted;
            }
        }
        if (cursor > record_storage_size ||
            !minic_multi_emit_zero_bytes(
                out, record_storage_size - cursor))
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

bool minic_multi_emit_recursive_record_global(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicGlobalObject *object, size_t storage_size,
    MinicMultiRelocEmitter emit_relocation) {
    return object != NULL && minic_type_is_record(object->type) &&
           minic_multi_emit_recursive_aggregate_global(
               out, program, target, object, storage_size,
               emit_relocation, NULL);
}

/* Recursive aggregate shape inspection must be side-effect free. All four
 * backends now share this layout walk, including RV64's GNU flexible/empty
 * record extensions and bitfield byte packing. Unsupported shapes fail
 * before any assembly is emitted, rather than fabricating a zero payload. */
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

bool minic_multi_emit_recursive_aggregate_global(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicGlobalObject *object, size_t storage_size,
    MinicMultiRelocEmitter emit_relocation,
    MinicMultiScalarBitsEmitter emit_bits) {
    size_t initializer_index = 0U, relocation_index = 0U;
    size_t matched_union_selections = 0U, emitted_size;
    if (out == NULL || program == NULL || target == NULL || object == NULL ||
        emit_relocation == NULL ||
        (!minic_type_is_record(object->type) &&
         !minic_type_is_array(object->type)) ||
        !minic_multi_recursive_shape_supported(program, object->type) ||
        !minic_multi_emit_recursive_global_value(
            out, program, target, object, object->type,
            &initializer_index, &relocation_index, &matched_union_selections,
            &emitted_size, 0U, emit_relocation, emit_bits))
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
    size_t i;
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
        return minic_multi_emit_flat_record_array(
            out, program, target, object, storage_size, emit_relocation);
    return minic_multi_emit_recursive_aggregate_global(
        out, program, target, object, storage_size,
        emit_relocation, NULL);
}


/* GNU permits a static record object to initialize a trailing flexible
 * array (BusyBox hdparm's masks_labels_t). The parser owns the flattened
 * scalar slots, while GlobalObject DataLayout owns concrete storage size.
 * Flexible record tails are ordinary recursive fields whose concrete
 * extent comes from the complete GlobalObject DataLayout, not sizeof(record).
 * Preserve the public entry point for callers; there is no separate payload
 * traversal/relocation order to maintain. */
static bool minic_multi_emit_flexible_record_global(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicGlobalObject *object, size_t storage_size,
    MinicMultiRelocEmitter emit_relocation) {
    return object != NULL && minic_type_is_record(object->type) &&
           object->flexible_array_initializer_count != 0U &&
           minic_multi_emit_recursive_aggregate_global(
               out, program, target, object, storage_size,
               emit_relocation, NULL);
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

/* A record with ABI bit-fields (including musl zero-width timespec
 * barriers) cannot use the flat scalar field walk. Select the already
 * verified recursive DataLayout walker across all four architectures. */
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

bool minic_multi_emit_flat_record_global(FILE *out,
                                    const MinicC0Program *program,
                                    const MinicTargetInfo *target,
                                    const MinicGlobalObject *obj,
                                    size_t storage_size,
                                           MinicMultiRelocEmitter emit_relocation,
                                           MinicMultiScalarBitsEmitter emit_bits) {
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
            fprintf(out, "  .zero %zu\n", offset - cursor) < 0)
            return false;
        (void)alignment;
        if (reloc != NULL &&
            (reloc->location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_RECORD_FIELD ||
             reloc->location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR) &&
            reloc->location_index == i) {
            if ((!minic_type_is_pointer(field->type) &&
                 !minic_type_is_integer(field->type)) ||
                obj->initializer_values[i] != 0U ||
                !emit_relocation(out, program, target, reloc, width))
                return false;
            ++reloc_index;
        } else if (!(emit_bits != NULL
                         ? emit_bits(out, program, target, field->type,
                                     obj->initializer_values[i])
                         : minic_multi_emit_data_bits(
                               out, width, obj->initializer_values[i])))
            return false;
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

/* GNU original Linux named object sections are ELF metadata, not ISA ABI.
 * Disallow arbitrary GAS directives/flags supplied by C attributes. */
bool minic_multi_elf_safe_section_name(const char *name) {
    size_t i;
    if (name == NULL || name[0] == '\0') return false;
    for (i = 0U; name[i] != '\0'; ++i) {
        const char c = name[i];
        if (i >= 255U ||
            !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '.' ||
              c == '$' || c == '-'))
            return false;
    }
    return true;
}
static bool minic_multi_elf_section_is_bss(const char *name) {
    return name != NULL && strncmp(name, ".bss", 4U) == 0 &&
           (name[4] == '\0' || name[4] == '.');
}
bool minic_multi_emit_elf_global_section(FILE *out,
    const MinicGlobalObject *obj, const char *fallback) {
    if (out == NULL || obj == NULL || fallback == NULL) return false;
    if (obj->section_name == NULL)
        return fprintf(out, "%s\n", fallback) >= 0;
    if (!minic_multi_elf_safe_section_name(obj->section_name))
        return false;
    return fprintf(out, ".section %s,\"%s\",@%s\n",
        obj->section_name, obj->is_read_only ? "a" : "aw",
        minic_multi_elf_section_is_bss(obj->section_name)
            ? "nobits" : "progbits") >= 0;
}
bool minic_multi_emit_elf_global_binding(FILE *out,
    const MinicGlobalObject *obj) {
    if (out == NULL || obj == NULL) return false;
    if (obj->is_internal) return !obj->is_weak;
    return fprintf(out, "%s %s\n",
        obj->is_weak ? ".weak" : ".globl", obj->name) >= 0;
}

bool minic_multi_global_emission_plan_build(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicGlobalObject *obj, MinicMultiGlobalEmissionPlan *plan) {
    MinicType scalar;
    size_t size, alignment, width, scalar_alignment, index;
    if (plan == NULL || program == NULL || target == NULL ||
        target->data_layout == NULL || obj == NULL || obj->name == NULL ||
        obj->name[0] == '\0' ||
        (obj->visibility != MINIC_SYMBOL_VISIBILITY_DEFAULT &&
         minic_multi_elf_visibility_directive(obj->visibility) == NULL) ||
        obj->alias_target != MINIC_GLOBAL_OBJECT_INVALID ||
        (obj->initializer_count != 0U && obj->initializer_values == NULL) ||
        (obj->relocation_count != 0U && obj->relocations == NULL) ||
        !minic_data_layout_global_object(target->data_layout, program, obj,
                                         &size, &alignment))
        return false;
    memset(plan, 0, sizeof(*plan));
    plan->storage_size = size;
    plan->storage_alignment = alignment;

    /* GNU C rejects nonzero values and symbolic relocations in .bss / .bss.*
     * even when explicitly selected by a section attribute. A NOBITS ELF
     * object must have a bytewise zero payload; fail before writing GAS. */
    if (minic_multi_elf_section_is_bss(obj->section_name)) {
        if (obj->relocation_count != 0U) {
            return false;
        }
        for (index = 0U; index < obj->initializer_count; ++index) {
            if (obj->initializer_values[index] != 0U) {
                return false;
            }
        }
    }

    /* Zero payloads need no fabricated record/array initializer slots.
     * A fully zero record remains bytewise zero including bitfield padding. */
    if (obj->initializer_count == 0U && obj->relocation_count == 0U &&
        obj->union_selection_count == 0U) {
        plan->payload_kind = MINIC_MULTI_GLOBAL_PAYLOAD_ZERO;
        plan->default_elf_section = obj->is_read_only ? ".section .rodata" : ".bss";
        return true;
    }
    if (obj->initializer_count == 0U && obj->relocation_count != 0U &&
        (minic_type_is_pointer(obj->type) || minic_type_is_array(obj->type) ||
         minic_type_is_record(obj->type) ||
         (minic_type_is_integer(obj->type) &&
          size == target->data_layout->pointer_size))) {
        plan->payload_kind = MINIC_MULTI_GLOBAL_PAYLOAD_ZERO_RELOCATIONS;
        plan->default_elf_section = obj->is_read_only
            ? ".section .data.rel.ro" : ".data";
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
            plan->payload_kind = MINIC_MULTI_GLOBAL_PAYLOAD_ZERO;
            plan->default_elf_section = obj->is_read_only
                ? ".section .rodata" : ".bss";
            return true;
        }
    }
    if (minic_type_is_record(obj->type)) {
        const MinicRecord *record =
            minic_c0_program_record(program, obj->type.record_id);
        if (record == NULL) return false;
        plan->payload_kind = obj->flexible_array_initializer_count != 0U
            ? MINIC_MULTI_GLOBAL_PAYLOAD_RECORD_FLEXIBLE
            : minic_multi_record_initializer_needs_recursive(record, obj)
            ? MINIC_MULTI_GLOBAL_PAYLOAD_RECORD_RECURSIVE
            : MINIC_MULTI_GLOBAL_PAYLOAD_RECORD_FLAT;
        plan->default_elf_section = obj->is_read_only
            ? (obj->relocation_count != 0U
               ? ".section .data.rel.ro" : ".section .rodata")
            : ".data";
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
                ? MINIC_MULTI_GLOBAL_PAYLOAD_ARRAY_RECORD
                : nested_record || nested_relocatable
                ? MINIC_MULTI_GLOBAL_PAYLOAD_ARRAY_RECURSIVE
                : MINIC_MULTI_GLOBAL_PAYLOAD_ARRAY_SCALAR_RELOCATIONS;
            plan->default_elf_section = obj->is_read_only
                ? (obj->relocation_count != 0U
                   ? ".section .data.rel.ro" : ".section .rodata")
                : ".data";
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
        plan->payload_kind = MINIC_MULTI_GLOBAL_PAYLOAD_SCALAR_RELOCATION;
        plan->scalar_type = obj->type;
        plan->scalar_width = size;
        plan->default_elf_section = obj->is_read_only
            ? ".section .data.rel.ro" : ".data";
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
    plan->payload_kind = MINIC_MULTI_GLOBAL_PAYLOAD_SCALAR_BITS;
    plan->scalar_type = scalar;
    plan->scalar_width = width;
    plan->default_elf_section = obj->is_read_only ? ".section .rodata"
        : ((obj->is_zero_initialized || obj->is_tentative) &&
           obj->initializer_count == 0U ? ".bss" : ".data");
    return true;
}

bool minic_multi_emit_global_payload(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicGlobalObject *obj, const MinicMultiGlobalEmissionPlan *plan,
    MinicMultiRelocEmitter emit_relocation,
    MinicMultiScalarBitsEmitter emit_bits) {
    size_t i;

    if (out == NULL || program == NULL || target == NULL || obj == NULL ||
        plan == NULL || emit_relocation == NULL) {
        return false;
    }
    switch (plan->payload_kind) {
    case MINIC_MULTI_GLOBAL_PAYLOAD_ZERO:
        if (fprintf(out, "  .zero %zu\n", plan->storage_size) < 0) return false;
        break;
    case MINIC_MULTI_GLOBAL_PAYLOAD_ZERO_RELOCATIONS:
        if (!minic_multi_emit_zero_relocation_global(
                out, program, target, obj, plan->storage_size,
                emit_relocation)) return false;
        break;
    case MINIC_MULTI_GLOBAL_PAYLOAD_RECORD_FLAT:
        if (!minic_multi_emit_flat_record_global(
                out, program, target, obj, plan->storage_size,
                emit_relocation, emit_bits)) return false;
        break;
    case MINIC_MULTI_GLOBAL_PAYLOAD_RECORD_RECURSIVE:
        if (!minic_multi_emit_recursive_record_global(
                out, program, target, obj, plan->storage_size,
                emit_relocation)) return false;
        break;
    case MINIC_MULTI_GLOBAL_PAYLOAD_RECORD_FLEXIBLE:
        if (!minic_multi_emit_flexible_record_global(
                out, program, target, obj, plan->storage_size,
                emit_relocation)) return false;
        break;
    case MINIC_MULTI_GLOBAL_PAYLOAD_ARRAY_RECORD:
        if (!minic_multi_emit_record_array_global(
                out, program, target, obj, plan->storage_size,
                emit_relocation)) return false;
        break;
    case MINIC_MULTI_GLOBAL_PAYLOAD_ARRAY_RECURSIVE:
        if (!minic_multi_emit_recursive_aggregate_global(
                out, program, target, obj, plan->storage_size,
                emit_relocation, emit_bits)) return false;
        break;
    case MINIC_MULTI_GLOBAL_PAYLOAD_ARRAY_SCALAR_RELOCATIONS:
        if (!minic_multi_emit_relocatable_scalar_array(
                out, program, target, obj, plan->storage_size,
                emit_relocation, emit_bits)) return false;
        break;
    case MINIC_MULTI_GLOBAL_PAYLOAD_SCALAR_RELOCATION:
        if (obj->relocation_count != 1U ||
            !emit_relocation(out, program, target, &obj->relocations[0],
                             plan->scalar_width)) return false;
        break;
    case MINIC_MULTI_GLOBAL_PAYLOAD_SCALAR_BITS:
        if (obj->initializer_count == 0U) {
            if (fprintf(out, "  .zero %zu\n", plan->storage_size) < 0)
                return false;
        } else {
            for (i = 0U; i < obj->initializer_count; ++i) {
                if (!(emit_bits != NULL
                        ? emit_bits(out, program, target, plan->scalar_type,
                                    obj->initializer_values[i])
                        : minic_multi_emit_data_bits(
                              out, plan->scalar_width, obj->initializer_values[i])))
                    return false;
            }
            if (plan->storage_size > obj->initializer_count * plan->scalar_width &&
                fprintf(out, "  .zero %zu\n",
                        plan->storage_size - obj->initializer_count * plan->scalar_width) < 0)
                return false;
        }
        break;
    default:
        return false;
    }
    return true;
}

bool minic_multi_emit_simple_global(FILE *out, const MinicC0Program *program,
                               const MinicTargetInfo *target,
                               const MinicGlobalObject *obj,
                               MinicMultiRelocEmitter emit_relocation) {
    MinicMultiGlobalEmissionPlan plan;
    if (out == NULL || emit_relocation == NULL ||
        !minic_multi_global_emission_plan_build(program, target, obj, &plan))
        return false;
    /* Complete classification precedes *all* writes, including ELF
     * visibility and section selection. Native relocation details remain
     * opaque callbacks rather than a second data-layout classifier. */
    if (!obj->is_internal &&
        !minic_multi_emit_elf_symbol_visibility(out, obj->visibility, obj->name))
        return false;
    if (!minic_multi_emit_elf_global_section(out, obj, plan.default_elf_section) ||
        !minic_multi_emit_elf_global_binding(out, obj) ||
        fprintf(out, "  .balign %zu\n.type %s,@object\n%s:\n",
                plan.storage_alignment, obj->name, obj->name) < 0 ||
        !minic_multi_emit_global_payload(out, program, target, obj, &plan,
                                         emit_relocation, NULL))
        return false;
    return fprintf(out, ".size %s,%zu\n", obj->name, plan.storage_size) >= 0;
}
