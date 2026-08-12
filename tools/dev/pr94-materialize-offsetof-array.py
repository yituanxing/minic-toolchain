#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one exact anchor, found {count}")
    p.write_text(text.replace(old, new, 1))


replace_once(
    "src/frontend/parser_expression.c",
    '''    MinicRecordFieldPath path;\n    size_t anonymous_prefix_offset;\n    size_t path_index;\n''',
    '''    MinicRecordFieldPath path;\n    MinicType final_field_type;\n    bool final_field_is_array;\n    size_t anonymous_prefix_offset;\n    size_t path_index;\n''',
    "snapshot offsetof final field metadata",
)

replace_once(
    "src/frontend/parser_expression.c",
    '''        final_record = minic_c0_program_record(parser->program, path.record_ids[path.depth - 1U]);\n        final_field = minic_c0_record_field(final_record, path.field_indices[path.depth - 1U]);\n        if (final_field == NULL || final_field->is_bit_field) {\n            minic_parser_error(parser, "__builtin_offsetof cannot name a bit-field");\n            return false;\n        }\n    }\n\n    anonymous_prefix_offset = 0U;\n''',
    '''        final_record = minic_c0_program_record(parser->program, path.record_ids[path.depth - 1U]);\n        final_field = minic_c0_record_field(final_record, path.field_indices[path.depth - 1U]);\n        if (final_field == NULL || final_field->is_bit_field) {\n            minic_parser_error(parser, "__builtin_offsetof cannot name a bit-field");\n            return false;\n        }\n        final_field_type = final_field->type;\n        final_field_is_array = final_field->is_array;\n    }\n\n    anonymous_prefix_offset = 0U;\n''',
    "snapshot before expression pool growth",
)

replace_once(
    "src/frontend/parser_expression.c",
    '''    if (!minic_parser_advance(parser) || parser->current.kind != MINIC_TOKEN_RPAREN) {\n        minic_parser_error(parser, "expected ')' after __builtin_offsetof");\n        return false;\n    }\n\n    (void)memset(&expression, 0, sizeof(expression));\n    expression.kind = MINIC_EXPRESSION_OFFSETOF;\n    expression.span.begin = begin;\n    expression.span.end = parser->current.span.end;\n    expression.type = minic_type_unsigned_long();\n    expression.value_category = MINIC_VALUE_RVALUE;\n    expression.value.offsetof_value.record_id = path.record_ids[path.depth - 1U];\n    expression.value.offsetof_value.field_index = path.field_indices[path.depth - 1U];\n    expression.value.offsetof_value.anonymous_prefix_offset = anonymous_prefix_offset;\n    return minic_parser_advance(parser) &&\n           minic_parser_add_expression(parser, &expression, expression_id);\n''',
    '''    if (!minic_parser_advance(parser)) {\n        return false;\n    }\n\n    (void)memset(&expression, 0, sizeof(expression));\n    expression.kind = MINIC_EXPRESSION_OFFSETOF;\n    expression.span.begin = begin;\n    expression.span.end = field_span.end;\n    expression.type = minic_type_unsigned_long();\n    expression.value_category = MINIC_VALUE_RVALUE;\n    expression.value.offsetof_value.record_id = path.record_ids[path.depth - 1U];\n    expression.value.offsetof_value.field_index = path.field_indices[path.depth - 1U];\n    expression.value.offsetof_value.anonymous_prefix_offset = anonymous_prefix_offset;\n    if (!minic_parser_add_expression(parser, &expression, expression_id)) {\n        return false;\n    }\n\n    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {\n        MinicExpressionId base_offset_id;\n        MinicExpressionId index_id;\n        MinicExpressionId stride_id;\n        MinicExpressionId scaled_id;\n        const MinicExpression *index_expression;\n        MinicExpression stride;\n        MinicExpression scaled;\n        MinicExpression adjusted;\n        MinicType scaled_type;\n        size_t element_size;\n\n        base_offset_id = *expression_id;\n        if (!final_field_is_array) {\n            minic_parser_error(\n                parser, "__builtin_offsetof array designator requires an array field");\n            return false;\n        }\n        if (!minic_parser_advance(parser) ||\n            !parse_expression_internal(parser, &index_id, 0U, true)) {\n            return false;\n        }\n        index_expression = minic_c0_program_expression(parser->program, index_id);\n        if (index_expression == NULL || !minic_type_is_integer(index_expression->type)) {\n            minic_parser_error(parser, "__builtin_offsetof array index requires an integer");\n            return false;\n        }\n        if (parser->current.kind != MINIC_TOKEN_RBRACKET) {\n            minic_parser_error(parser, "expected ']' in __builtin_offsetof array designator");\n            return false;\n        }\n        if (!minic_target_info_sizeof_type(\n                parser->target_info, parser->program, final_field_type, &element_size) ||\n            element_size > (size_t)INT64_MAX) {\n            minic_parser_error(parser, "cannot lay out __builtin_offsetof array element");\n            return false;\n        }\n\n        (void)memset(&stride, 0, sizeof(stride));\n        stride.kind = MINIC_EXPRESSION_INTEGER;\n        stride.span = parser->current.span;\n        stride.type = minic_type_unsigned_long();\n        stride.value_category = MINIC_VALUE_RVALUE;\n        stride.value.integer_value = (int64_t)element_size;\n        if (!minic_parser_add_expression(parser, &stride, &stride_id) ||\n            !minic_type_integer_common(\n                index_expression->type, stride.type, &scaled_type)) {\n            minic_parser_error(parser, "cannot type __builtin_offsetof array index scale");\n            return false;\n        }\n\n        (void)memset(&scaled, 0, sizeof(scaled));\n        scaled.kind = MINIC_EXPRESSION_BINARY;\n        scaled.span.begin = index_expression->span.begin;\n        scaled.span.end = parser->current.span.end;\n        scaled.type = scaled_type;\n        scaled.value_category = MINIC_VALUE_RVALUE;\n        scaled.value.binary.operator_kind = MINIC_BINARY_MULTIPLY;\n        scaled.value.binary.left = index_id;\n        scaled.value.binary.right = stride_id;\n        if (!minic_parser_add_expression(parser, &scaled, &scaled_id)) {\n            return false;\n        }\n\n        (void)memset(&adjusted, 0, sizeof(adjusted));\n        adjusted.kind = MINIC_EXPRESSION_BINARY;\n        adjusted.span.begin = begin;\n        adjusted.span.end = parser->current.span.end;\n        adjusted.type = minic_type_unsigned_long();\n        adjusted.value_category = MINIC_VALUE_RVALUE;\n        adjusted.value.binary.operator_kind = MINIC_BINARY_ADD;\n        adjusted.value.binary.left = base_offset_id;\n        adjusted.value.binary.right = scaled_id;\n        if (!minic_parser_add_expression(parser, &adjusted, expression_id) ||\n            !minic_parser_advance(parser)) {\n            return false;\n        }\n    }\n\n    if (parser->current.kind != MINIC_TOKEN_RPAREN) {\n        minic_parser_error(parser, "expected ')' after __builtin_offsetof");\n        return false;\n    }\n    return minic_parser_advance(parser);\n''',
    "normalize offsetof array designator into ordinary arithmetic",
)

replace_once(
    "tests/compiler/c0/builtin_offsetof.c",
    '''_Static_assert(__builtin_offsetof(struct BranchData, miss_hit) == 24,\n               "promoted anonymous array member offsetof");\n\nint main(void) {\n''',
    '''_Static_assert(__builtin_offsetof(struct BranchData, miss_hit) == 24,\n               "promoted anonymous array member offsetof");\n\nstruct IndexedOffset {\n    char lead;\n    unsigned long node[2];\n};\n\n_Static_assert(__builtin_offsetof(struct IndexedOffset, node[1]) == 16,\n               "constant indexed offsetof");\n\nunsigned long indexed_offset(unsigned int idx) {\n    return __builtin_offsetof(struct IndexedOffset, node[idx]);\n}\n\nint main(void) {\n''',
    "offsetof constant and runtime array designators",
)

replace_once(
    "tests/compiler/c0/builtin_offsetof.c",
    '''                   __builtin_offsetof(struct BranchData, hit) == 32 &&\n                   __builtin_offsetof(struct BranchData, miss_hit) == 24\n''',
    '''                   __builtin_offsetof(struct BranchData, hit) == 32 &&\n                   __builtin_offsetof(struct BranchData, miss_hit) == 24 &&\n                   indexed_offset(0) == 8 && indexed_offset(1) == 16\n''',
    "exercise runtime indexed offsetof",
)

replace_once(
    "tests/compiler/c0/run-builtin-offsetof.sh",
    '''grep -F '  li a0, 24' "$work/builtin_offsetof.s" >/dev/null\nprintf '%s\\n' 'PASS compiler/c0/builtin_offsetof direct-member=1 typedef=1 promoted-anonymous=2 shared-member-resolver=1 target-layout=1 array-bound=8'\n''',
    '''grep -F '  li a0, 24' "$work/builtin_offsetof.s" >/dev/null\ngrep -F 'indexed_offset:' "$work/builtin_offsetof.s" >/dev/null\n\ncat >"$work/non-array-index.c" <<'EOF'\nstruct ScalarOnly { unsigned long value; };\nunsigned long bad(unsigned int idx)\n{\n    return __builtin_offsetof(struct ScalarOnly, value[idx]);\n}\nEOF\n"$host_cc" -E -P -std=gnu11 -x c "$work/non-array-index.c" -o "$work/non-array-index.i"\nif "$minic" -S "$work/non-array-index.i" -o "$work/non-array-index.s" \\\n    2>"$work/non-array-index.stderr"; then\n    printf '%s\\n' 'offsetof array designator unexpectedly accepted on scalar field' >&2\n    exit 1\nfi\ngrep -F '__builtin_offsetof array designator requires an array field' \\\n    "$work/non-array-index.stderr" >/dev/null\n\nprintf '%s\\n' 'PASS compiler/c0/builtin_offsetof direct-member=1 typedef=1 promoted-anonymous=2 shared-member-resolver=1 target-layout=1 array-bound=8 array-designator=constant+runtime normalized=base+index*stride scalar-index=reject'\n''',
    "offsetof array designator runner",
)
