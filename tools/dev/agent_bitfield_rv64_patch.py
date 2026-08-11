from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]


def read(path):
    return (ROOT / path).read_text()


def write(path, text):
    (ROOT / path).write_text(text)


def replace_once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one exact match, got {count}")
    return text.replace(old, new, 1)


def sub_once(text, pattern, replacement, label, flags=0):
    new, count = re.subn(pattern, replacement, text, count=1, flags=flags)
    if count != 1:
        raise SystemExit(f"{label}: expected one regex match, got {count}")
    return new


# AST: one constructor for named/unnamed bit-fields, plus one semantic query used by parser/target.
path = "src/frontend/ast.h"
text = read(path)
text = replace_once(
    text,
    "bool minic_c0_record_add_unnamed_bit_field(MinicC0Program *program,\n"
    "                                           MinicRecordId record_id,\n"
    "                                           MinicType type,\n"
    "                                           size_t bit_width);",
    "bool minic_c0_record_add_bit_field(MinicC0Program *program,\n"
    "                                   MinicRecordId record_id,\n"
    "                                   const char *name,\n"
    "                                   size_t name_length,\n"
    "                                   MinicType type,\n"
    "                                   size_t bit_width);\n"
    "bool minic_c0_record_add_unnamed_bit_field(MinicC0Program *program,\n"
    "                                           MinicRecordId record_id,\n"
    "                                           MinicType type,\n"
    "                                           size_t bit_width);",
    "ast bit-field API",
)
text = replace_once(
    text,
    "const MinicExpression *minic_c0_program_expression(const MinicC0Program *program,\n"
    "                                                   MinicExpressionId expression_id);\n"
    "bool minic_c0_record_value_is_address_backed",
    "const MinicExpression *minic_c0_program_expression(const MinicC0Program *program,\n"
    "                                                   MinicExpressionId expression_id);\n"
    "const MinicRecordField *minic_c0_expression_bit_field(const MinicC0Program *program,\n"
    "                                                       MinicExpressionId expression_id);\n"
    "bool minic_c0_record_value_is_address_backed",
    "ast bit-field query declaration",
)
write(path, text)

path = "src/frontend/ast.c"
text = read(path)
text = sub_once(
    text,
    r"bool minic_c0_record_add_unnamed_bit_field\(MinicC0Program \*program,.*?\n}\n\n(?=bool minic_c0_program_finish_record)",
    r'''bool minic_c0_record_add_bit_field(MinicC0Program *program,
                                   MinicRecordId record_id,
                                   const char *name,
                                   size_t name_length,
                                   MinicType type,
                                   size_t bit_width) {
    MinicRecord *record;
    MinicRecordField *field;

    if (program == NULL || record_id >= program->record_count || name == NULL ||
        !minic_type_is_integer(type) || (name_length != 0U && bit_width == 0U) ||
        !minic_c0_record_add_field(program, record_id, name, name_length, type, 1U)) {
        return false;
    }
    record = &program->records[record_id];
    field = &record->fields[record->field_count - 1U];
    field->is_bit_field = true;
    field->bit_width = bit_width;
    field->bit_offset = 0U;
    return true;
}

bool minic_c0_record_add_unnamed_bit_field(MinicC0Program *program,
                                           MinicRecordId record_id,
                                           MinicType type,
                                           size_t bit_width) {
    return minic_c0_record_add_bit_field(program, record_id, "", 0U, type, bit_width);
}

''',
    "ast bit-field constructor implementation",
    flags=re.S,
)
text = replace_once(
    text,
    "const MinicExpression *minic_c0_program_expression(const MinicC0Program *program,\n"
    "                                                   MinicExpressionId expression_id) {\n"
    "    if (program == NULL || expression_id >= program->expression_count) {\n"
    "        return NULL;\n"
    "    }\n"
    "    return &program->expressions[expression_id];\n"
    "}\n",
    "const MinicExpression *minic_c0_program_expression(const MinicC0Program *program,\n"
    "                                                   MinicExpressionId expression_id) {\n"
    "    if (program == NULL || expression_id >= program->expression_count) {\n"
    "        return NULL;\n"
    "    }\n"
    "    return &program->expressions[expression_id];\n"
    "}\n\n"
    "const MinicRecordField *minic_c0_expression_bit_field(const MinicC0Program *program,\n"
    "                                                       MinicExpressionId expression_id) {\n"
    "    const MinicExpression *expression;\n"
    "    const MinicRecord *record;\n"
    "    const MinicRecordField *field;\n\n"
    "    expression = minic_c0_program_expression(program, expression_id);\n"
    "    if (expression == NULL || expression->kind != MINIC_EXPRESSION_MEMBER) {\n"
    "        return NULL;\n"
    "    }\n"
    "    record = minic_c0_program_record(program, expression->value.member.record_id);\n"
    "    field = minic_c0_record_field(record, expression->value.member.field_index);\n"
    "    return field != NULL && field->is_bit_field ? field : NULL;\n"
    "}\n",
    "ast bit-field semantic query",
)
write(path, text)

# Parser: share typed width parsing between named and unnamed fields and reject address-taking/offsetof.
path = "src/frontend/parser_record.c"
text = read(path)
anchor = "static bool\nparse_record_field_declarator(MinicParser *parser, MinicRecordId record_id, MinicType base_type) {"
helper = r'''static bool parse_record_bit_field_width(MinicParser *parser,
                                         MinicType field_type,
                                         bool allow_zero,
                                         size_t *bit_width) {
    MinicConstValue width_value;
    MinicExpressionId width_expression;
    unsigned int type_bits;
    int64_t width;

    if (parser == NULL || bit_width == NULL || !minic_type_is_integer(field_type)) {
        if (parser != NULL) {
            minic_parser_error(parser, "bit-field requires an integer type");
        }
        return false;
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_COLON, "expected ':' before bit-field width") ||
        !minic_parser_parse_expression(parser, &width_expression, 0U) ||
        !minic_const_eval_integer(
            parser->program, parser->target_info, width_expression, &width_value) ||
        !minic_const_value_as_int64(
            parser->program, parser->target_info, &width_value, &width)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "bit-field width must be an integer constant expression");
        }
        return false;
    }
    if (!minic_target_info_integer_width(
            parser->target_info, parser->program, field_type, &type_bits)) {
        minic_parser_error(parser, "cannot determine target width of bit-field type");
        return false;
    }
    if (minic_type_is_bool_integer(field_type)) {
        type_bits = 1U;
    }
    if (width < 0 || (uint64_t)width > (uint64_t)type_bits || (!allow_zero && width == 0)) {
        minic_parser_error(parser,
                           allow_zero ? "bit-field width exceeds its integer type"
                                      : "named bit-field width must be positive and fit its integer type");
        return false;
    }
    *bit_width = (size_t)width;
    return true;
}

'''
text = replace_once(text, anchor, helper + anchor, "insert bit-field width helper")
needle = "    if (record_has_field(parser, record, name_span)) {\n        minic_parser_error(parser, \"duplicate record field\");\n        return false;\n    }\n\n    element_count = 1U;"
replacement = "    if (record_has_field(parser, record, name_span)) {\n        minic_parser_error(parser, \"duplicate record field\");\n        return false;\n    }\n    if (parser->current.kind == MINIC_TOKEN_COLON) {\n        size_t bit_width;\n\n        if (!parse_record_bit_field_width(parser, field_type, false, &bit_width) ||\n            !minic_c0_record_add_bit_field(parser->program,\n                                           record_id,\n                                           parser->source + name_span.begin.offset,\n                                           minic_parser_span_length(name_span),\n                                           field_type,\n                                           bit_width)) {\n            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {\n                minic_parser_error(parser, \"cannot add named bit-field\");\n            }\n            return false;\n        }\n        return true;\n    }\n\n    element_count = 1U;"
text = replace_once(text, needle, replacement, "named bit-field parser")
text = sub_once(
    text,
    r"    if \(parser->current.kind == MINIC_TOKEN_COLON\) \{\n        MinicConstValue width_value;.*?        return true;\n    \}\n",
    r'''    if (parser->current.kind == MINIC_TOKEN_COLON) {
        size_t bit_width;

        if (!parse_record_bit_field_width(parser, base_type, true, &bit_width) ||
            !minic_parser_expect(parser, MINIC_TOKEN_SEMICOLON, "expected ';' after bit-field") ||
            !minic_c0_record_add_unnamed_bit_field(
                parser->program, record_id, base_type, bit_width)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot add unnamed bit-field");
            }
            return false;
        }
        return true;
    }
''',
    "unnamed bit-field parser convergence",
    flags=re.S,
)
write(path, text)

path = "src/frontend/parser_expression.c"
text = read(path)
text = replace_once(
    text,
    "            if (operand_expression->value_category != MINIC_VALUE_LVALUE ||\n"
    "                !minic_type_pointer_to(operand_expression->type, &expression.type)) {\n"
    "                minic_parser_error(parser,\n"
    "                                   \"address-of requires an lvalue object or function designator\");\n"
    "                return false;\n"
    "            }",
    "            if (minic_c0_expression_bit_field(parser->program, operand_id) != NULL) {\n"
    "                minic_parser_error(parser, \"cannot take the address of a bit-field\");\n"
    "                return false;\n"
    "            }\n"
    "            if (operand_expression->value_category != MINIC_VALUE_LVALUE ||\n"
    "                !minic_type_pointer_to(operand_expression->type, &expression.type)) {\n"
    "                minic_parser_error(parser,\n"
    "                                   \"address-of requires an lvalue object or function designator\");\n"
    "                return false;\n"
    "            }",
    "reject address of bit-field",
)
# Final promoted member in offsetof must not be a bit-field.
text = replace_once(
    text,
    "    if (path.depth == 0U) {\n"
    "        minic_parser_error(parser, \"empty record field path in __builtin_offsetof\");\n"
    "        return false;\n"
    "    }\n\n"
    "    anonymous_prefix_offset = 0U;",
    "    if (path.depth == 0U) {\n"
    "        minic_parser_error(parser, \"empty record field path in __builtin_offsetof\");\n"
    "        return false;\n"
    "    }\n"
    "    {\n"
    "        const MinicRecord *final_record;\n"
    "        const MinicRecordField *final_field;\n\n"
    "        final_record = minic_c0_program_record(parser->program, path.record_ids[path.depth - 1U]);\n"
    "        final_field = minic_c0_record_field(final_record, path.field_indices[path.depth - 1U]);\n"
    "        if (final_field == NULL || final_field->is_bit_field) {\n"
    "            minic_parser_error(parser, \"__builtin_offsetof cannot name a bit-field\");\n"
    "            return false;\n"
    "        }\n"
    "    }\n\n"
    "    anonymous_prefix_offset = 0U;",
    "reject offsetof bit-field",
)
write(path, text)

# DataLayout: bit precision is canonical here. Materialize both byte offset and bit offset.
path = "src/target/data_layout.h"
text = read(path)
text = replace_once(
    text,
    "bool minic_data_layout_record_field_offset(const MinicDataLayout *layout,\n"
    "                                           const MinicC0Program *program,\n"
    "                                           const MinicRecord *record,\n"
    "                                           size_t field_index,\n"
    "                                           size_t *offset);",
    "bool minic_data_layout_record_field_layout(const MinicDataLayout *layout,\n"
    "                                           const MinicC0Program *program,\n"
    "                                           const MinicRecord *record,\n"
    "                                           size_t field_index,\n"
    "                                           size_t *offset,\n"
    "                                           size_t *bit_offset);\n"
    "bool minic_data_layout_record_field_offset(const MinicDataLayout *layout,\n"
    "                                           const MinicC0Program *program,\n"
    "                                           const MinicRecord *record,\n"
    "                                           size_t field_index,\n"
    "                                           size_t *offset);",
    "data layout field layout API",
)
write(path, text)

path = "src/target/data_layout.c"
text = read(path)
record_fn = r'''static bool minic_data_layout_record_depth(const MinicDataLayout *layout,
                                           const MinicC0Program *program,
                                           const MinicRecord *record,
                                           unsigned int depth,
                                           size_t requested_field,
                                           size_t *requested_offset,
                                           size_t *requested_bit_offset,
                                           size_t *size,
                                           size_t *alignment) {
    size_t storage_bits;
    size_t record_alignment;
    size_t index;

    if (layout == NULL || program == NULL || record == NULL || size == NULL || alignment == NULL ||
        !record->is_complete || depth > MINIC_DATA_LAYOUT_MAX_DEPTH) {
        return false;
    }
    storage_bits = 0U;
    record_alignment = 1U;
    for (index = 0U; index < record->field_count; ++index) {
        const MinicRecordField *field;
        size_t element_size;
        size_t field_size;
        size_t field_alignment;
        size_t field_offset;
        size_t field_bit_offset;

        field = &record->fields[index];
        if (field->element_count == 0U ||
            !minic_data_layout_type_depth(
                layout, program, field->type, depth + 1U, &element_size, &field_alignment) ||
            element_size > SIZE_MAX / field->element_count) {
            return false;
        }
        field_bit_offset = 0U;
        if (field->is_bit_field) {
            size_t type_bits;
            size_t alignment_bits;
            size_t field_start_bits;

            if (!minic_type_is_integer(field->type) || field->element_count != 1U ||
                field->is_array || field->is_flexible_array || field->is_zero_length_array ||
                element_size == 0U || element_size > SIZE_MAX / 8U ||
                field_alignment == 0U || field_alignment > SIZE_MAX / 8U) {
                return false;
            }
            type_bits = element_size * 8U;
            alignment_bits = field_alignment * 8U;
            if (minic_type_is_bool_integer(field->type)) {
                type_bits = 1U;
            }
            if (field->bit_width > type_bits ||
                (field->name_length != 0U && field->bit_width == 0U)) {
                return false;
            }
            if (record->is_union) {
                field_start_bits = 0U;
                if (field->bit_width > storage_bits) {
                    storage_bits = field->bit_width;
                }
            } else if (field->bit_width == 0U) {
                if (!minic_data_layout_align_up(storage_bits, alignment_bits, &field_start_bits)) {
                    return false;
                }
                storage_bits = field_start_bits;
            } else {
                field_start_bits = storage_bits;
                if (!record->is_packed) {
                    size_t within_boundary;

                    within_boundary = field_start_bits % alignment_bits;
                    if (within_boundary > type_bits || field->bit_width > type_bits - within_boundary) {
                        if (!minic_data_layout_align_up(
                                field_start_bits, alignment_bits, &field_start_bits)) {
                            return false;
                        }
                    }
                }
                if (field_start_bits > SIZE_MAX - field->bit_width) {
                    return false;
                }
                storage_bits = field_start_bits + field->bit_width;
            }
            field_offset = field_start_bits / 8U;
            field_bit_offset = field_start_bits % 8U;
            if (!record->is_packed && field->name_length != 0U && field->bit_width != 0U &&
                field_alignment > record_alignment) {
                record_alignment = field_alignment;
            }
        } else {
            size_t storage_size;

            if (storage_bits > SIZE_MAX - 7U) {
                return false;
            }
            storage_size = (storage_bits + 7U) / 8U;
            field_size = (field->is_flexible_array || field->is_zero_length_array)
                             ? 0U
                             : element_size * field->element_count;
            if (field->explicit_alignment != 0U) {
                if ((field->explicit_alignment & (field->explicit_alignment - 1U)) != 0U) {
                    return false;
                }
                if (field->explicit_alignment > field_alignment) {
                    field_alignment = field->explicit_alignment;
                }
            }
            if (record->is_union) {
                field_offset = 0U;
                if (field_size > storage_size) {
                    storage_size = field_size;
                }
            } else if (record->is_packed && field->explicit_alignment == 0U) {
                field_offset = storage_size;
                if (field_offset > SIZE_MAX - field_size) {
                    return false;
                }
                storage_size = field_offset + field_size;
            } else {
                if (!minic_data_layout_align_up(storage_size, field_alignment, &field_offset) ||
                    field_offset > SIZE_MAX - field_size) {
                    return false;
                }
                storage_size = field_offset + field_size;
            }
            if (storage_size > SIZE_MAX / 8U) {
                return false;
            }
            if (record->is_union) {
                size_t union_bits;

                union_bits = storage_size * 8U;
                if (union_bits > storage_bits) {
                    storage_bits = union_bits;
                }
            } else {
                storage_bits = storage_size * 8U;
            }
            if ((!record->is_packed || field->explicit_alignment != 0U) &&
                field_alignment > record_alignment) {
                record_alignment = field_alignment;
            }
        }
        if (requested_offset != NULL && index == requested_field) {
            *requested_offset = field_offset;
            if (requested_bit_offset != NULL) {
                *requested_bit_offset = field_bit_offset;
            }
        }
    }
    if (record->explicit_alignment != 0U) {
        if ((record->explicit_alignment & (record->explicit_alignment - 1U)) != 0U) {
            return false;
        }
        if (record->explicit_alignment > record_alignment) {
            record_alignment = record->explicit_alignment;
        }
    }
    if (storage_bits > SIZE_MAX - 7U) {
        return false;
    }
    if (!minic_data_layout_align_up((storage_bits + 7U) / 8U, record_alignment, size)) {
        return false;
    }
    *alignment = record_alignment;
    return true;
}
'''
text = sub_once(
    text,
    r"static bool minic_data_layout_record_depth\(.*?\n}\n\n(?=static bool minic_data_layout_type_depth)",
    record_fn + "\n",
    "replace canonical record layout",
    flags=re.S,
)
# Update recursive call from type layout.
text = text.replace(
    "layout, program, record, depth + 1U, SIZE_MAX, NULL, size, alignment)",
    "layout, program, record, depth + 1U, SIZE_MAX, NULL, NULL, size, alignment)",
)
text = sub_once(
    text,
    r"bool minic_data_layout_record_field_offset\(const MinicDataLayout \*layout,.*?\n}\n?$",
    r'''bool minic_data_layout_record_field_layout(const MinicDataLayout *layout,
                                           const MinicC0Program *program,
                                           const MinicRecord *record,
                                           size_t field_index,
                                           size_t *offset,
                                           size_t *bit_offset) {
    size_t size;
    size_t alignment;

    if (record == NULL || offset == NULL || bit_offset == NULL || field_index >= record->field_count) {
        return false;
    }
    return minic_data_layout_record_depth(layout,
                                          program,
                                          record,
                                          0U,
                                          field_index,
                                          offset,
                                          bit_offset,
                                          &size,
                                          &alignment);
}

bool minic_data_layout_record_field_offset(const MinicDataLayout *layout,
                                           const MinicC0Program *program,
                                           const MinicRecord *record,
                                           size_t field_index,
                                           size_t *offset) {
    size_t bit_offset;

    return minic_data_layout_record_field_layout(
        layout, program, record, field_index, offset, &bit_offset);
}
''',
    "data layout field API implementation",
    flags=re.S,
)
write(path, text)

path = "src/target/riscv64/layout.c"
text = read(path)
text = replace_once(
    text,
    "            size_t field_offset;\n\n"
    "            field = &record->fields[field_index];\n"
    "            if (!minic_data_layout_record_field_offset(\n"
    "                    layout, program, record, field_index, &field_offset)) {\n"
    "                return false;\n"
    "            }\n"
    "            field->storage_offset = field_offset;\n"
    "            if (field->is_bit_field) {\n"
    "                field->bit_offset = 0U;\n"
    "            }",
    "            size_t field_offset;\n"
    "            size_t bit_offset;\n\n"
    "            field = &record->fields[field_index];\n"
    "            if (!minic_data_layout_record_field_layout(\n"
    "                    layout, program, record, field_index, &field_offset, &bit_offset)) {\n"
    "                return false;\n"
    "            }\n"
    "            field->storage_offset = field_offset;\n"
    "            field->bit_offset = bit_offset;",
    "materialize bit offsets",
)
write(path, text)

# RV64: byte-precise little-endian bit-field access so adjacent ordinary fields are never clobbered.
path = "src/target/riscv64/codegen_expression.c"
text = read(path)
insert_anchor = "static bool minic_riscv64_emit_update(FILE *file,"
helpers = r'''static bool minic_riscv64_emit_bit_field_load_from_address(FILE *file,
                                                             const MinicRecordField *field,
                                                             const char *result_register,
                                                             const char *address_register) {
    size_t byte_count;
    size_t index;
    unsigned int shift;

    if (file == NULL || field == NULL || result_register == NULL || address_register == NULL ||
        !field->is_bit_field || field->bit_width == 0U || field->bit_width > 64U ||
        field->bit_offset >= 8U || field->bit_offset > SIZE_MAX - field->bit_width) {
        return false;
    }
    byte_count = (field->bit_offset + field->bit_width + 7U) / 8U;
    if (byte_count == 0U || byte_count > 8U || fprintf(file, "  mv t5, %s\n", address_register) < 0 ||
        fprintf(file, "  li %s, 0\n", result_register) < 0) {
        return false;
    }
    for (index = 0U; index < byte_count; ++index) {
        if (fprintf(file, "  lbu t6, %zu(t5)\n", index) < 0) {
            return false;
        }
        if (index != 0U && fprintf(file, "  slli t6, t6, %zu\n", index * 8U) < 0) {
            return false;
        }
        if (fprintf(file, "  or %s, %s, t6\n", result_register, result_register) < 0) {
            return false;
        }
    }
    if (field->bit_offset != 0U &&
        fprintf(file, "  srli %s, %s, %zu\n", result_register, result_register, field->bit_offset) <
            0) {
        return false;
    }
    if (field->bit_width == 64U) {
        return true;
    }
    shift = 64U - (unsigned int)field->bit_width;
    if (fprintf(file, "  slli %s, %s, %u\n", result_register, result_register, shift) < 0) {
        return false;
    }
    return fprintf(file,
                   minic_type_is_signed_integer(field->type) &&
                           !minic_type_is_bool_integer(field->type)
                       ? "  srai %s, %s, %u\n"
                       : "  srli %s, %s, %u\n",
                   result_register,
                   result_register,
                   shift) >= 0;
}

static bool minic_riscv64_emit_bit_field_store_to_address(FILE *file,
                                                           const MinicRecordField *field,
                                                           const char *value_register,
                                                           const char *address_register) {
    uint64_t value_mask;
    uint64_t positioned_mask;
    size_t byte_count;
    size_t index;
    unsigned int shift;

    if (file == NULL || field == NULL || value_register == NULL || address_register == NULL ||
        !field->is_bit_field || field->bit_width == 0U || field->bit_width > 64U ||
        field->bit_offset >= 8U || field->bit_offset > SIZE_MAX - field->bit_width) {
        return false;
    }
    byte_count = (field->bit_offset + field->bit_width + 7U) / 8U;
    if (byte_count == 0U || byte_count > 8U ||
        fprintf(file, "  mv t5, %s\n  li t2, 0\n", address_register) < 0) {
        return false;
    }
    for (index = 0U; index < byte_count; ++index) {
        if (fprintf(file, "  lbu t6, %zu(t5)\n", index) < 0 ||
            (index != 0U && fprintf(file, "  slli t6, t6, %zu\n", index * 8U) < 0) ||
            fprintf(file, "  or t2, t2, t6\n") < 0) {
            return false;
        }
    }
    if (field->bit_width == 64U) {
        if (field->bit_offset != 0U || fprintf(file, "  mv t2, %s\n", value_register) < 0) {
            return false;
        }
    } else {
        value_mask = (UINT64_C(1) << field->bit_width) - UINT64_C(1);
        positioned_mask = value_mask << field->bit_offset;
        if (fprintf(file,
                    "  li t3, %" PRIu64 "\n"
                    "  and t4, %s, t3\n",
                    value_mask,
                    value_register) < 0 ||
            (field->bit_offset != 0U &&
             fprintf(file, "  slli t4, t4, %zu\n", field->bit_offset) < 0) ||
            fprintf(file,
                    "  li t3, %" PRIu64 "\n"
                    "  not t3, t3\n"
                    "  and t2, t2, t3\n"
                    "  or t2, t2, t4\n",
                    positioned_mask) < 0) {
            return false;
        }
    }
    for (index = 0U; index < byte_count; ++index) {
        if (index == 0U) {
            if (fprintf(file, "  sb t2, 0(t5)\n") < 0) {
                return false;
            }
        } else if (fprintf(file,
                           "  srli t6, t2, %zu\n"
                           "  sb t6, %zu(t5)\n",
                           index * 8U,
                           index) < 0) {
            return false;
        }
    }
    if (field->bit_width == 64U) {
        return true;
    }
    shift = 64U - (unsigned int)field->bit_width;
    if (fprintf(file, "  slli %s, %s, %u\n", value_register, value_register, shift) < 0) {
        return false;
    }
    return fprintf(file,
                   minic_type_is_signed_integer(field->type) &&
                           !minic_type_is_bool_integer(field->type)
                       ? "  srai %s, %s, %u\n"
                       : "  srli %s, %s, %u\n",
                   value_register,
                   value_register,
                   shift) >= 0;
}

static bool minic_riscv64_emit_lvalue_load_from_address(FILE *file,
                                                        const MinicC0Program *program,
                                                        MinicExpressionId expression_id,
                                                        MinicType type,
                                                        const char *result_register,
                                                        const char *address_register) {
    const MinicRecordField *field;

    field = minic_c0_expression_bit_field(program, expression_id);
    if (field != NULL) {
        return minic_riscv64_emit_bit_field_load_from_address(
            file, field, result_register, address_register);
    }
    return minic_riscv64_emit_scalar_load(file, type, result_register, address_register);
}

static bool minic_riscv64_emit_lvalue_store_to_address(FILE *file,
                                                        const MinicC0Program *program,
                                                        MinicExpressionId expression_id,
                                                        MinicType type,
                                                        const char *value_register,
                                                        const char *address_register) {
    const MinicRecordField *field;

    field = minic_c0_expression_bit_field(program, expression_id);
    if (field != NULL) {
        return minic_riscv64_emit_bit_field_store_to_address(
            file, field, value_register, address_register);
    }
    return minic_riscv64_emit_scalar_store(file, type, value_register, address_register);
}

'''
text = replace_once(text, insert_anchor, helpers + insert_anchor, "insert RV64 bit-field helpers")
text = replace_once(
    text,
    "        !minic_riscv64_emit_scalar_load(file, operand->type, \"t0\", \"a0\") ||",
    "        !minic_riscv64_emit_lvalue_load_from_address(file,\n"
    "                                                       program,\n"
    "                                                       expression->value.unary.operand,\n"
    "                                                       operand->type,\n"
    "                                                       \"t0\",\n"
    "                                                       \"a0\") ||",
    "bit-field update load",
)
text = replace_once(
    text,
    "        !minic_riscv64_emit_scalar_store(file, operand->type, \"t0\", \"t1\")) {",
    "        !minic_riscv64_emit_lvalue_store_to_address(file,\n"
    "                                                        program,\n"
    "                                                        expression->value.unary.operand,\n"
    "                                                        operand->type,\n"
    "                                                        \"t0\",\n"
    "                                                        \"t1\")) {",
    "bit-field update store",
)
text = replace_once(
    text,
    "        return minic_riscv64_emit_scalar_load(file, expression->type, \"a0\", \"a0\");\n"
    "    }\n"
    "    case MINIC_EXPRESSION_LVALUE_READ:\n"
    "        return minic_riscv64_emit_lvalue_address(\n"
    "                   file, program, function, expression->value.unary.operand) &&\n"
    "               minic_riscv64_emit_scalar_load(file, expression->type, \"a0\", \"a0\");",
    "        return minic_riscv64_emit_lvalue_load_from_address(\n"
    "            file, program, expression_id, expression->type, \"a0\", \"a0\");\n"
    "    }\n"
    "    case MINIC_EXPRESSION_LVALUE_READ:\n"
    "        return minic_riscv64_emit_lvalue_address(\n"
    "                   file, program, function, expression->value.unary.operand) &&\n"
    "               minic_riscv64_emit_lvalue_load_from_address(file,\n"
    "                                                            program,\n"
    "                                                            expression->value.unary.operand,\n"
    "                                                            expression->type,\n"
    "                                                            \"a0\",\n"
    "                                                            \"a0\");",
    "bit-field member/lvalue reads",
)
text = replace_once(
    text,
    "               minic_riscv64_emit_scalar_store(file, target->type, \"t0\", \"t1\") &&\n"
    "               fprintf(file, \"  mv a0, t0\\n\") >= 0;",
    "               minic_riscv64_emit_lvalue_store_to_address(file,\n"
    "                                                           program,\n"
    "                                                           expression->value.binary.left,\n"
    "                                                           target->type,\n"
    "                                                           \"t0\",\n"
    "                                                           \"t1\") &&\n"
    "               fprintf(file, \"  mv a0, t0\\n\") >= 0;",
    "bit-field simple assignment store",
)
text = replace_once(
    text,
    "            !minic_riscv64_emit_scalar_load(file, target->type, \"a0\", \"a0\")) {",
    "            !minic_riscv64_emit_lvalue_load_from_address(file,\n"
    "                                                           program,\n"
    "                                                           expression->value.binary.left,\n"
    "                                                           target->type,\n"
    "                                                           \"a0\",\n"
    "                                                           \"a0\")) {",
    "bit-field compound assignment load",
)
text = replace_once(
    text,
    "               minic_riscv64_emit_scalar_store(file, target->type, \"t0\", \"t1\") &&\n"
    "               fprintf(file, \"  mv a0, t0\\n\") >= 0;",
    "               minic_riscv64_emit_lvalue_store_to_address(file,\n"
    "                                                           program,\n"
    "                                                           expression->value.binary.left,\n"
    "                                                           target->type,\n"
    "                                                           \"t0\",\n"
    "                                                           \"t1\") &&\n"
    "               fprintf(file, \"  mv a0, t0\\n\") >= 0;",
    "bit-field compound assignment store",
)
write(path, text)

# Upgrade the existing frozen bit-field gate instead of adding a parallel model.
path = "tests/compiler/c0/unnamed_bit_fields.c"
write(
    path,
    r'''typedef _Bool bool;

struct full_unit_pad {
    char tag;
    int :32;
    char tail;
};

struct zero_width_barrier {
    char tag;
    int :0;
    char tail;
};

struct bool_bits {
    bool first : 1;
    bool second : 1;
    char tail;
};

struct int_bits {
    unsigned int low : 10;
    unsigned int high : 12;
    char tail;
};

struct short_boundary_bits {
    unsigned short low : 10;
    unsigned short high : 12;
    char tail;
};

struct named_zero_barrier {
    unsigned int first : 1;
    unsigned int :0;
    unsigned int second : 1;
    char tail;
};

unsigned long full_unit_tail_offset(void) {
    return __builtin_offsetof(struct full_unit_pad, tail);
}

unsigned long zero_width_tail_offset(void) {
    return __builtin_offsetof(struct zero_width_barrier, tail);
}

unsigned long bool_tail_offset(void) {
    return __builtin_offsetof(struct bool_bits, tail);
}

unsigned long int_tail_offset(void) {
    return __builtin_offsetof(struct int_bits, tail);
}

unsigned long short_boundary_tail_offset(void) {
    return __builtin_offsetof(struct short_boundary_bits, tail);
}

unsigned long named_zero_tail_offset(void) {
    return __builtin_offsetof(struct named_zero_barrier, tail);
}

int read_bool_second(struct bool_bits *bits) {
    return bits->second;
}

void write_bool_second(struct bool_bits *bits, int value) {
    bits->second = value;
}

unsigned int read_int_high(struct int_bits *bits) {
    return bits->high;
}

void add_int_high(struct int_bits *bits, unsigned int value) {
    bits->high += value;
}

unsigned int increment_barrier_second(struct named_zero_barrier *bits) {
    return ++bits->second;
}

int main(void) {
    return full_unit_tail_offset() == 8UL && zero_width_tail_offset() == 4UL &&
                   bool_tail_offset() == 1UL && int_tail_offset() == 3UL &&
                   short_boundary_tail_offset() == 4UL && named_zero_tail_offset() == 5UL
               ? 0
               : 1;
}
''',
)

write(
    "tests/compiler/c0/invalid_bit_field_address.c",
    r'''struct Bits {
    unsigned int flag : 1;
};

unsigned int *bad(struct Bits *bits) {
    return &bits->flag;
}
''',
)
write(
    "tests/compiler/c0/invalid_named_zero_bit_field.c",
    r'''struct Bits {
    unsigned int flag : 0;
};
''',
)
write(
    "tests/compiler/c0/invalid_bit_field_width.c",
    r'''struct Bits {
    unsigned char flag : 9;
};
''',
)

path = "tests/compiler/c0/run-unnamed-bit-fields.sh"
text = read(path)
text = replace_once(
    text,
    "grep -F '  li a0, 8' \"$assembly\" >/dev/null\n"
    "grep -F '  li a0, 4' \"$assembly\" >/dev/null\n\n"
    "printf '%s\\n' 'PASS compiler/c0/unnamed_bit_fields full-unit=int:32 tail-offset=8 zero-width=int:0 tail-offset=4 metadata=explicit partial=nonsupported'",
    "grep -F '  li a0, 8' \"$assembly\" >/dev/null\n"
    "grep -F '  li a0, 4' \"$assembly\" >/dev/null\n"
    "grep -F '  li a0, 1' \"$assembly\" >/dev/null\n"
    "grep -F '  li a0, 3' \"$assembly\" >/dev/null\n"
    "grep -F '  li a0, 5' \"$assembly\" >/dev/null\n"
    "sed -n '/read_bool_second:/,/^\\.size/p' \"$assembly\" | grep -F 'lbu t6, 0(t5)' >/dev/null\n"
    "sed -n '/read_bool_second:/,/^\\.size/p' \"$assembly\" | grep -F 'srli a0, a0, 1' >/dev/null\n"
    "sed -n '/write_bool_second:/,/^\\.size/p' \"$assembly\" | grep -F 'sb t2, 0(t5)' >/dev/null\n"
    "sed -n '/read_int_high:/,/^\\.size/p' \"$assembly\" | grep -F 'lbu t6, 0(t5)' >/dev/null\n"
    "sed -n '/read_int_high:/,/^\\.size/p' \"$assembly\" | grep -F 'lbu t6, 1(t5)' >/dev/null\n"
    "sed -n '/add_int_high:/,/^\\.size/p' \"$assembly\" | grep -F 'sb t2, 0(t5)' >/dev/null\n"
    "sed -n '/increment_barrier_second:/,/^\\.size/p' \"$assembly\" | grep -F 'addi a0, a0, 4' >/dev/null\n\n"
    "for invalid in invalid_bit_field_address invalid_named_zero_bit_field invalid_bit_field_width; do\n"
    "    \"$host_cc\" -E -P -std=gnu11 -x c \"$root/tests/compiler/c0/$invalid.c\" -o \"$work/$invalid.i\"\n"
    "    if \"$minic\" -S \"$work/$invalid.i\" -o \"$work/$invalid.s\" >\"$work/$invalid.out\" 2>\"$work/$invalid.err\"; then\n"
    "        printf '%s\\n' \"expected $invalid to fail\" >&2\n"
    "        exit 1\n"
    "    fi\n"
    "done\n"
    "grep -F 'cannot take the address of a bit-field' \"$work/invalid_bit_field_address.err\" >/dev/null\n"
    "grep -F 'named bit-field width must be positive' \"$work/invalid_named_zero_bit_field.err\" >/dev/null\n"
    "grep -F 'named bit-field width must be positive and fit its integer type' \"$work/invalid_bit_field_width.err\" >/dev/null\n\n"
    "printf '%s\\n' 'PASS compiler/c0/unnamed_bit_fields full-unit=1 zero-width=1 named-partial=bool+uint+ushort packing=little-endian boundary=type-alignment access=byte-rmw address-of=reject width=checked'",
    "upgrade bit-field runner",
)
write(path, text)

print("PASS generated RV64 bit-field slice")
