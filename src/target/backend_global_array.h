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

static bool minic_multi_emit_recursive_record_global(
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
static bool minic_multi_emit_record_array_global(
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
static bool minic_multi_emit_flexible_record_global(
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
            fprintf(out, "  .zero %zu\\n", offset - cursor) < 0)
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
        fprintf(out, "  .zero %zu\\n", tail_offset - cursor) < 0 ||
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

/* Integer-class LP64 record arguments, shared only as ABI classification.
 * Layout remains target-owned; register placement is ISA-specific.
 * Never claim HFAs, packed/unaligned records, bitfields or vector ABI.
 * For <=16-byte integral-only aggregates, payload bytes travel as one
 * or two complete integer eightbytes, with the last slot padded to 8. */
static bool minic_multi_integer_record_members(
    const MinicC0Program *program, const MinicTargetInfo *target,
    MinicType type, unsigned int depth) {
    const MinicRecord *record;
    size_t total, alignment, i;
    if (program == NULL || target == NULL || depth > 16U ||
        !minic_data_layout_type(target->data_layout, program, type,
                                &total, &alignment) ||
        total == 0U || alignment == 0U || alignment > 8U)
        return false;
    if (minic_type_is_integer(type) || minic_type_is_pointer(type))
        return total <= 8U;
    if (!minic_type_is_record(type)) return false;
    record = minic_c0_program_record(program, type.record_id);
    if (record == NULL || !record->is_complete || record->is_packed ||
        record->pack_alignment != 0U || record->field_count == 0U)
        return false;
    for (i = 0U; i < record->field_count; ++i) {
        const MinicRecordField *field = minic_c0_record_field(record, i);
        size_t offset, field_size, field_align;
        if (field == NULL || field->is_packed || field->is_bit_field ||
            field->is_flexible_array || field->is_array ||
            field->element_count != 1U ||
            !minic_multi_integer_record_members(
                 program, target, field->type, depth + 1U) ||
            !minic_data_layout_type(target->data_layout, program, field->type,
                                    &field_size, &field_align) ||
            !minic_data_layout_record_field_offset(
                 target->data_layout, program, record, i, &offset) ||
            (field_align != 0U && offset % field_align != 0U) ||
            offset > total || field_size > total - offset)
            return false;
    }
    return true;
}

static bool minic_multi_gp_record(const MinicC0Program *program,
                                   const MinicTargetInfo *target,
                                   MinicType type, size_t *bytes,
                                   size_t *eightbytes) {
    size_t size, align;
    if (bytes == NULL || eightbytes == NULL || !minic_type_is_record(type) ||
        !minic_multi_integer_record_members(program, target, type, 0U) ||
        !minic_data_layout_type(target->data_layout, program, type,
                                &size, &align) ||
        size == 0U || size > 16U || align == 0U || align > 8U)
        return false;
    *bytes = size;
    *eightbytes = (size + 7U) / 8U;
    return true;
}

#endif /* MINIC_TARGET_BACKEND_GLOBAL_ARRAY_H */
