#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one replacement, found {count}")
    target.write_text(text.replace(old, new, 1))


# Record metadata gets an explicit bit-field shape now, even though the active
# codegen capability is intentionally only unnamed fields. Named/partial fields
# can later reuse bit_width/bit_offset instead of changing the record model.
path = Path("src/frontend/ast.h")
text = path.read_text()
field_start = text.find("typedef struct MinicRecordField {")
field_end = text.find("} MinicRecordField;", field_start)
if field_start < 0 or field_end < 0:
    raise SystemExit("unnamed-bit-field: cannot locate MinicRecordField")
field_body = text[field_start:field_end]
anchor = "    size_t storage_offset;\n"
if field_body.count(anchor) != 1:
    raise SystemExit("unnamed-bit-field: cannot locate record storage_offset")
field_body = field_body.replace(
    anchor,
    "    size_t storage_offset;\n    size_t bit_width;\n    size_t bit_offset;\n",
    1,
)
# Insert is_bit_field next to the existing record-field flags without depending
# on which prior Linux/Lua discovery flags are present.
flag_anchor = "    bool is_flexible_array;\n"
if field_body.count(flag_anchor) != 1:
    raise SystemExit("unnamed-bit-field: cannot locate record field flags")
field_body = field_body.replace(
    flag_anchor, "    bool is_bit_field;\n" + flag_anchor, 1
)
text = text[:field_start] + field_body + text[field_end:]

prototype_anchor = '''bool minic_c0_record_add_field(MinicC0Program *program,
                               MinicRecordId record_id,
                               const char *name,
                               size_t name_length,
                               MinicType type,
                               size_t element_count);
'''
prototype = '''bool minic_c0_record_add_unnamed_bit_field(MinicC0Program *program,
                                           MinicRecordId record_id,
                                           MinicType type,
                                           size_t bit_width);
'''
if text.count(prototype_anchor) != 1:
    raise SystemExit("unnamed-bit-field: cannot locate record add-field prototype")
Path("src/frontend/ast.h").write_text(text.replace(prototype_anchor, prototype_anchor + prototype, 1))

# Reuse normal empty-name field ownership. Anonymous record-member staging has
# already made empty internal names non-conflicting, so multiple unnamed fields
# remain legal without a second storage implementation.
path = Path("src/frontend/ast.c")
text = path.read_text()
marker = "bool minic_c0_program_finish_record(MinicC0Program *program, MinicRecordId record_id) {\n"
helper = r'''bool minic_c0_record_add_unnamed_bit_field(MinicC0Program *program,
                                           MinicRecordId record_id,
                                           MinicType type,
                                           size_t bit_width) {
    MinicRecord *record;
    MinicRecordField *field;

    if (program == NULL || record_id >= program->record_count || !minic_type_is_integer(type) ||
        !minic_c0_record_add_field(program, record_id, "", 0U, type, 1U)) {
        return false;
    }
    record = &program->records[record_id];
    field = &record->fields[record->field_count - 1U];
    field->is_bit_field = true;
    field->bit_width = bit_width;
    field->bit_offset = 0U;
    return true;
}

'''
if text.count(marker) != 1:
    raise SystemExit("unnamed-bit-field: cannot locate record finish function")
path.write_text(text.replace(marker, helper + marker, 1))

# Expose the existing shared integer constant-expression parser as a value API.
# This is a small step toward the architecture's one-ConstEval rule: bit-field
# widths do not grow another literal-only evaluator.
replace_once(
    "src/frontend/parser_internal.h",
    "bool minic_parser_parse_fixed_array_bound(MinicParser *parser, size_t *element_count);\n",
    '''bool minic_parser_parse_integer_constant_expression_value(MinicParser *parser, int64_t *value);
bool minic_parser_parse_fixed_array_bound(MinicParser *parser, size_t *element_count);
''',
    "unnamed-bit-field-consteval-prototype",
)
path = Path("src/frontend/parser_core.c")
text = path.read_text()
marker = "bool minic_parser_parse_fixed_array_bound(MinicParser *parser, size_t *element_count) {\n"
helper = r'''bool minic_parser_parse_integer_constant_expression_value(MinicParser *parser, int64_t *value) {
    return value != NULL && parse_array_bound_additive(parser, value);
}

'''
if text.count(marker) != 1:
    raise SystemExit("unnamed-bit-field: cannot locate fixed-array bound parser")
path.write_text(text.replace(marker, helper + marker, 1))

# Integrate before the ordinary declarator loop: after type specifiers, a colon
# means the declaration has no declarator/name. Named bit-fields remain a later
# access/codegen milestone and still fail through the ordinary field path.
path = Path("src/frontend/parser_record.c")
text = path.read_text()
function_start = text.find("static bool parse_record_field(MinicParser *parser, MinicRecordId record_id) {")
loop_pos = text.find("    for (;;) {", function_start)
if function_start < 0 or loop_pos < 0:
    raise SystemExit("unnamed-bit-field: cannot locate record declarator loop")
insert = r'''    if (parser->current.kind == MINIC_TOKEN_COLON) {
        int64_t bit_width;

        if (!minic_type_is_integer(base_type)) {
            minic_parser_error(parser, "unnamed bit-field requires an integer type");
            return false;
        }
        if (!minic_parser_advance(parser) ||
            !minic_parser_parse_integer_constant_expression_value(parser, &bit_width)) {
            return false;
        }
        if (bit_width < 0 || (uint64_t)bit_width > (uint64_t)SIZE_MAX) {
            minic_parser_error(parser, "bit-field width is outside the target object range");
            return false;
        }
        if (!minic_parser_expect(parser, MINIC_TOKEN_SEMICOLON, "expected ';' after bit-field") ||
            !minic_c0_record_add_unnamed_bit_field(
                parser->program, record_id, base_type, (size_t)bit_width)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot add unnamed bit-field");
            }
            return false;
        }
        return true;
    }

'''
text = text[:loop_pos] + insert + text[loop_pos:]
path.write_text(text)

# RV64 layout implements the truthful subset currently demanded by Linux:
# - zero-width unnamed field => next storage unit boundary;
# - full-width unnamed integer field => occupies one normal integer storage unit.
# Partial-width packing and named-field read/modify/write remain explicit future
# work rather than being silently laid out incorrectly.
path = Path("src/target/riscv64/layout.c")
text = path.read_text()
function_start = text.find("minic_riscv64_layout_one_record(")
field_assign = text.find("        field = &record->fields[field_index];\n", function_start)
if function_start < 0 or field_assign < 0:
    raise SystemExit("unnamed-bit-field: cannot locate RV64 record field loop")
field_assign_end = field_assign + len("        field = &record->fields[field_index];\n")
layout = r'''        if (field->is_bit_field) {
            size_t storage_bits;

            if (!minic_type_is_integer(field->type) || field->element_count != 1U ||
                field->is_flexible_array ||
                !minic_riscv64_type_layout(
                    program, field->type, &element_size, &field_alignment) ||
                element_size > SIZE_MAX / 8U) {
                return false;
            }
            storage_bits = element_size * 8U;
            if (field->bit_width > storage_bits ||
                (field->bit_width != 0U && field->bit_width != storage_bits)) {
                return false;
            }
            field->bit_offset = 0U;
            if (record->is_union) {
                field_offset = 0U;
                if (field->bit_width != 0U && element_size > storage_size) {
                    storage_size = element_size;
                }
            } else {
                if (record->is_packed) {
                    field_offset = storage_size;
                } else if (!minic_riscv64_align_up(
                               storage_size, field_alignment, &field_offset)) {
                    return false;
                }
                if (field->bit_width != 0U) {
                    if (field_offset > SIZE_MAX - element_size) {
                        return false;
                    }
                    storage_size = field_offset + element_size;
                } else {
                    storage_size = field_offset;
                }
            }
            field->storage_offset = field_offset;
            if (!record->is_packed && field_alignment > record_alignment) {
                record_alignment = field_alignment;
            }
            continue;
        }
'''
path.write_text(text[:field_assign_end] + layout + text[field_assign_end:])

print("staged unnamed integer bit-fields with explicit metadata, shared ICE parsing and RV64 zero/full-unit layout")
