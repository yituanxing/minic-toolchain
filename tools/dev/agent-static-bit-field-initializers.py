from pathlib import Path
import re

codegen = Path('src/target/riscv64/codegen_function.c')
text = codegen.read_text()
if '#include <stdlib.h>\n' not in text:
    text = text.replace('#include <stdio.h>\n', '#include <stdio.h>\n#include <stdlib.h>\n', 1)

anchor = '''static bool minic_riscv64_emit_direct_record_values(FILE *file,
                                                    const MinicC0Program *program,
                                                    const MinicGlobalObject *object,
                                                    const MinicRecord *record) {
'''
helper = r'''static bool minic_riscv64_record_has_bit_fields(const MinicRecord *record) {
    size_t field_index;

    if (record == NULL) {
        return false;
    }
    for (field_index = 0U; field_index < record->field_count; ++field_index) {
        if (record->fields[field_index].is_bit_field) {
            return true;
        }
    }
    return false;
}

static bool minic_riscv64_emit_record_bit_field_run(
    FILE *file,
    const MinicC0Program *program,
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

    if (file == NULL || program == NULL || object == NULL || record == NULL ||
        field_index == NULL || initializer_index == NULL || relocation_index == NULL ||
        cursor == NULL || *field_index >= field_limit ||
        !record->fields[*field_index].is_bit_field) {
        return false;
    }
    run_first = *field_index;
    run_end = run_first;
    while (run_end < field_limit && record->fields[run_end].is_bit_field) {
        run_end += 1U;
    }
    if (!minic_data_layout_record_field_layout(minic_default_data_layout(),
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

        if (!minic_data_layout_record_field_layout(minic_default_data_layout(),
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
        !minic_riscv64_emit_zero_bytes(file, run_start_offset - *cursor)) {
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
        if (!minic_data_layout_record_field_layout(minic_default_data_layout(),
                                                   program,
                                                   record,
                                                   index,
                                                   &field_offset,
                                                   &bit_offset) ||
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
        mask = field->bit_width == 64U
                   ? UINT64_MAX
                   : (UINT64_C(1) << field->bit_width) - UINT64_C(1);
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

''' + anchor
if text.count(anchor) != 1:
    raise SystemExit('direct-record anchor mismatch')
text = text.replace(anchor, helper, 1)

old = '''        if (!record->is_union && object->initializer_count == record->field_count &&
            !has_recursive_relocation) {
            return minic_riscv64_emit_direct_record_values(file, program, object, record);
        }
'''
new = '''        if (!record->is_union && object->initializer_count == record->field_count &&
            !has_recursive_relocation && !minic_riscv64_record_has_bit_fields(record)) {
            return minic_riscv64_emit_direct_record_values(file, program, object, record);
        }
'''
if text.count(old) != 1:
    raise SystemExit('direct-record fast-path anchor mismatch')
text = text.replace(old, new, 1)

old = '''            field = minic_c0_record_field(record, field_index);
            if (field == NULL || field->element_count == 0U || field->is_flexible_array) {
                return false;
            }
            if (record->is_union) {
'''
new = '''            field = minic_c0_record_field(record, field_index);
            if (field == NULL || field->element_count == 0U || field->is_flexible_array) {
                return false;
            }
            if (field->is_bit_field) {
                if (!minic_riscv64_emit_record_bit_field_run(file,
                                                             program,
                                                             object,
                                                             record,
                                                             field_limit,
                                                             type_size,
                                                             &field_index,
                                                             initializer_index,
                                                             relocation_index,
                                                             &cursor)) {
                    return false;
                }
                if (record->is_union) {
                    break;
                }
                continue;
            }
            if (record->is_union) {
'''
if text.count(old) != 1:
    raise SystemExit('recursive record field anchor mismatch')
text = text.replace(old, new, 1)
codegen.write_text(text)

source = Path('tests/compiler/c0/unnamed_bit_fields.c')
text = source.read_text()
anchor = '''struct named_zero_barrier {
    unsigned int first : 1;
    unsigned int :0;
    unsigned int second : 1;
    char tail;
};
'''
addition = anchor + r'''
static struct bool_bits static_bool_bits = {
    .second = 1,
    .first = 1,
    .tail = 5,
};

static struct int_bits static_int_bits = {
    .high = 0xabc,
    .low = 0x155,
    .tail = 7,
};
'''
if text.count(anchor) != 1:
    raise SystemExit('bit-field source anchor mismatch')
source.write_text(text.replace(anchor, addition, 1))

script = Path('tests/compiler/c0/run-unnamed-bit-fields.sh')
text = script.read_text()
anchor = '''sed -n '/increment_barrier_second:/,/^\\.size/p' "$assembly" | grep -F 'addi a0, a0, 4' >/dev/null
'''
addition = anchor + r'''
static_bool_section=$(sed -n '/^static_bool_bits:/,/^\.size/p' "$assembly")
printf '%s\n' "$static_bool_section" | grep -F '  .byte 3' >/dev/null
printf '%s\n' "$static_bool_section" | grep -F '  .byte 5' >/dev/null

static_int_section=$(sed -n '/^static_int_bits:/,/^\.size/p' "$assembly")
printf '%s\n' "$static_int_section" | grep -F '  .byte 85' >/dev/null
printf '%s\n' "$static_int_section" | grep -F '  .byte 241' >/dev/null
printf '%s\n' "$static_int_section" | grep -F '  .byte 42' >/dev/null
printf '%s\n' "$static_int_section" | grep -F '  .byte 7' >/dev/null
'''
if text.count(anchor) != 1:
    raise SystemExit('bit-field script anchor mismatch')
text = text.replace(anchor, addition, 1)
text = text.replace(
    'packing=little-endian boundary=type-alignment signed-extension=1 access=byte-rmw',
    'packing=little-endian static-pack=bool+cross-byte boundary=type-alignment signed-extension=1 access=byte-rmw',
    1,
)
script.write_text(text)
