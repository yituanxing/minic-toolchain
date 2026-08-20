#!/usr/bin/env python3
"""Materialize GNU static flexible-array-member object tails once."""
from pathlib import Path


def replace_once(path: Path, before: str, after: str) -> None:
    text = path.read_text()
    if after in text:
        return
    count = text.count(before)
    if count != 1:
        raise SystemExit(f"{path}: expected one materialization anchor, found {count}")
    path.write_text(text.replace(before, after, 1))


ast = Path("src/frontend/ast.h")
replace_once(
    ast,
    """    MinicGlobalRelocation *relocations;
    size_t relocation_count;
    size_t relocation_capacity;
    size_t explicit_alignment;
""",
    """    MinicGlobalRelocation *relocations;
    size_t relocation_count;
    size_t relocation_capacity;
    size_t flexible_array_initializer_count;
    size_t explicit_alignment;
""",
)
replace_once(
    ast,
    """bool minic_c0_global_object_replace_zero_initializer_bits(MinicC0Program *program,
                                                          MinicGlobalObjectId global_object_id,
                                                          size_t initializer_index,
                                                          uint64_t bits);
bool minic_c0_type_initializer_slot_count(const MinicC0Program *program,
""",
    """bool minic_c0_global_object_replace_zero_initializer_bits(MinicC0Program *program,
                                                          MinicGlobalObjectId global_object_id,
                                                          size_t initializer_index,
                                                          uint64_t bits);
bool minic_c0_global_object_set_flexible_array_initializer_count(
    MinicC0Program *program,
    MinicGlobalObjectId global_object_id,
    size_t element_count);
bool minic_c0_type_initializer_slot_count(const MinicC0Program *program,
""",
)

ast_global = Path("src/frontend/ast_global.c")
replace_once(
    ast_global,
    """bool minic_c0_global_object_set_zero_initialized(MinicC0Program *program,
                                                 MinicGlobalObjectId global_object_id) {
""",
    """bool minic_c0_global_object_set_flexible_array_initializer_count(
    MinicC0Program *program,
    MinicGlobalObjectId global_object_id,
    size_t element_count) {
    MinicGlobalObject *object;
    const MinicRecord *record;
    const MinicRecordField *field;

    if (program == NULL || global_object_id >= program->global_object_count ||
        element_count == 0U) {
        return false;
    }
    object = &program->global_objects[global_object_id];
    if (object->is_extern || object->is_tentative || object->is_zero_initialized ||
        object->flexible_array_initializer_count != 0U || !minic_type_is_record(object->type)) {
        return false;
    }
    record = minic_c0_program_record(program, object->type.record_id);
    if (record == NULL || !record->is_complete || record->is_union || record->field_count == 0U) {
        return false;
    }
    field = &record->fields[record->field_count - 1U];
    if (!field->is_flexible_array || field->is_bit_field) {
        return false;
    }
    object->flexible_array_initializer_count = element_count;
    return true;
}

bool minic_c0_global_object_set_zero_initialized(MinicC0Program *program,
                                                 MinicGlobalObjectId global_object_id) {
""",
)

parser = Path("src/frontend/parser_global.c")
replace_once(
    parser,
    """        field = &record->fields[field_index];
        if (field->element_count == 0U || field->is_flexible_array) {
            minic_parser_error(parser, \"unsupported nested static record field\");
            return false;
        }
        overwrite_materialized_field = field_index < materialized_field_limit;
""",
    """        field = &record->fields[field_index];
        if (field->is_flexible_array) {
            const MinicGlobalObject *object;
            size_t flexible_element_count;

            object = minic_c0_program_global_object(parser->program, object_id);
            flexible_element_count = 0U;
            if (object == NULL || !minic_type_is_record(object->type) ||
                minic_c0_program_record(parser->program, object->type.record_id) != record ||
                record->is_union || field_index + 1U != record->field_count ||
                field_index != materialized_field_limit || field->is_bit_field ||
                !minic_type_is_integer(field->type) ||
                (has_designator && (designator.depth != 1U || designator.has_array_index)) ||
                parser->current.kind != MINIC_TOKEN_LBRACE ||
                !minic_parser_inspect_array_initializer_extent(parser, &flexible_element_count) ||
                flexible_element_count == 0U ||
                !parse_static_scalar_array_transaction(parser,
                                                       object_id,
                                                       field->type,
                                                       flexible_element_count,
                                                       false) ||
                !minic_c0_global_object_set_flexible_array_initializer_count(
                    parser->program, object_id, flexible_element_count)) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {
                    minic_parser_error(
                        parser,
                        \"GNU static flexible array initializer requires a direct integer tail\");
                }
                return false;
            }
            materialized_field_limit += 1U;
            field_index += 1U;
            if (parser->current.kind == MINIC_TOKEN_COMMA) {
                if (!minic_parser_advance(parser)) {
                    return false;
                }
                if (parser->current.kind == MINIC_TOKEN_RBRACE) {
                    break;
                }
            } else if (parser->current.kind != MINIC_TOKEN_RBRACE) {
                minic_parser_error(parser,
                                   \"expected ',' or '}' after static flexible array initializer\");
                return false;
            }
            continue;
        }
        if (field->element_count == 0U) {
            minic_parser_error(parser, \"unsupported nested static record field\");
            return false;
        }
        overwrite_materialized_field = field_index < materialized_field_limit;
""",
)

layout = Path("src/target/data_layout.c")
replace_once(
    layout,
    """    if (object->explicit_alignment != 0U) {
""",
    """    if (object->flexible_array_initializer_count != 0U) {
        const MinicRecord *record;
        const MinicRecordField *field;
        size_t element_size;
        size_t element_alignment;
        size_t tail_size;

        if (!minic_type_is_record(object->type)) {
            return false;
        }
        record = minic_c0_program_record(program, object->type.record_id);
        if (record == NULL || !record->is_complete || record->is_union ||
            record->field_count == 0U) {
            return false;
        }
        field = &record->fields[record->field_count - 1U];
        if (!field->is_flexible_array || field->is_bit_field ||
            !minic_data_layout_type(
                layout, program, field->type, &element_size, &element_alignment) ||
            element_size == 0U ||
            object->flexible_array_initializer_count > SIZE_MAX / element_size) {
            return false;
        }
        (void)element_alignment;
        tail_size = object->flexible_array_initializer_count * element_size;
        if (object_size > SIZE_MAX - tail_size) {
            return false;
        }
        object_size += tail_size;
    }
    if (object->explicit_alignment != 0U) {
""",
)

codegen = Path("src/target/riscv64/codegen_function.c")
replace_once(
    codegen,
    """        const MinicRecord *record;
        size_t cursor;
        size_t field_index;
        size_t field_limit;

        record = minic_c0_program_record(program, type.record_id);
        if (record == NULL || !record->is_complete) {
            return false;
        }
""",
    """        const MinicRecord *record;
        size_t cursor;
        size_t field_index;
        size_t field_limit;
        size_t record_storage_size;

        record = minic_c0_program_record(program, type.record_id);
        if (record == NULL || !record->is_complete) {
            return false;
        }
        record_storage_size = type_size;
        if (object->flexible_array_initializer_count != 0U &&
            minic_type_is_record(object->type) && object->type.record_id == type.record_id) {
            size_t object_alignment;

            if (!minic_data_layout_global_object(minic_default_data_layout(),
                                                 program,
                                                 object,
                                                 &record_storage_size,
                                                 &object_alignment)) {
                return false;
            }
            (void)object_alignment;
        }
""",
)
replace_once(
    codegen,
    """            if (field->is_flexible_array) {
                /* DataLayout gives a trailing FAM zero storage bytes. Its semantic
                 * initializer likewise owns zero scalar slots, so emit nothing. */
                if (record->is_union || field_index + 1U != field_limit) {
                    return false;
                }
                continue;
            }
""",
    """            if (field->is_flexible_array) {
                size_t element_index;
                size_t field_offset;
                size_t flexible_element_count;

                if (record->is_union || field_index + 1U != field_limit ||
                    !minic_data_layout_record_field_offset(minic_default_data_layout(),
                                                           program,
                                                           record,
                                                           field_index,
                                                           &field_offset) ||
                    field_offset < cursor || field_offset > record_storage_size ||
                    !minic_riscv64_emit_zero_bytes(file, field_offset - cursor)) {
                    return false;
                }
                cursor = field_offset;
                flexible_element_count =
                    minic_type_is_record(object->type) && object->type.record_id == type.record_id
                        ? object->flexible_array_initializer_count
                        : 0U;
                for (element_index = 0U; element_index < flexible_element_count; ++element_index) {
                    size_t element_emitted;

                    if (!minic_riscv64_emit_constant_value(file,
                                                           program,
                                                           object,
                                                           field->type,
                                                           initializer_index,
                                                           relocation_index,
                                                           &element_emitted) ||
                        cursor > record_storage_size - element_emitted) {
                        return false;
                    }
                    cursor += element_emitted;
                }
                continue;
            }
""",
)
replace_once(
    codegen,
    """        if (cursor > type_size || !minic_riscv64_emit_zero_bytes(file, type_size - cursor)) {
            return false;
        }
        *emitted_size = type_size;
        return true;
""",
    """        if (cursor > record_storage_size ||
            !minic_riscv64_emit_zero_bytes(file, record_storage_size - cursor)) {
            return false;
        }
        *emitted_size = record_storage_size;
        return true;
""",
)

test = Path("tests/compiler/c0/run-static-aggregate-initializers.sh")
replace_once(
    test,
    """grep -E '^\\.size __minic_static_local_[0-9_]+, 0$' \\
    \"$build_dir/static_zero_size_local_array.s\" >/dev/null

if \"$minic\" -S \"$root/tests/compiler/c0/invalid_static_record_compound_literal_type.c\" \\
""",
    """grep -E '^\\.size __minic_static_local_[0-9_]+, 0$' \\
    \"$build_dir/static_zero_size_local_array.s\" >/dev/null

cat >\"$build_dir/static_flexible_array.c\" <<'EOF'
struct StaticFlexibleArray {
    int tag;
    unsigned long tail[];
};
struct StaticFlexibleArray static_flexible_array = {
    .tag = 7,
    .tail = { [0 ... 2] = 11UL },
};
EOF
\"$minic\" -S \"$build_dir/static_flexible_array.c\" \\
    -o \"$build_dir/static_flexible_array.s\"
grep -F '.size static_flexible_array, 32' \"$build_dir/static_flexible_array.s\" >/dev/null
test \"$(grep -c '  .dword 11' \"$build_dir/static_flexible_array.s\")\" -eq 3

if \"$minic\" -S \"$root/tests/compiler/c0/invalid_static_record_compound_literal_type.c\" \\
""",
)
replace_once(
    test,
    """    'PASS compiler/c0/static-aggregate-initializers compound-literal=record nested-nonzero=recursive zero-size-local-array=accepted designated-inner=shared mismatch=fail-closed'
""",
    """    'PASS compiler/c0/static-aggregate-initializers compound-literal=record nested-nonzero=recursive zero-size-local-array=accepted static-fam=gnu-range+extended-storage designated-inner=shared mismatch=fail-closed'
""",
)
