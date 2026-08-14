from pathlib import Path

root = Path(__file__).resolve().parents[2]


def replace_once(path: Path, old: str, new: str, label: str) -> None:
    text = path.read_text()
    if old in text:
        if text.count(old) != 1:
            raise SystemExit(f"{label}: expected one old anchor, found {text.count(old)}")
        path.write_text(text.replace(old, new, 1))
        return
    if new in text:
        return
    raise SystemExit(f"{label}: neither old nor new anchor found")


def replace_function(path: Path, start_marker: str, end_marker: str, new_body: str, done_marker: str) -> None:
    text = path.read_text()
    start = text.find(start_marker)
    end = text.find(end_marker, start)
    if start < 0 or end < 0:
        raise SystemExit(f"cannot locate function region {start_marker!r}")
    current = text[start:end]
    if done_marker in current:
        return
    path.write_text(text[:start] + new_body + text[end:])


# Semantic AST: offsetof stores only source-level field identity.
ast = root / "src/frontend/ast.h"
replace_once(
    ast,
    '''        struct {\n            MinicRecordId record_id;\n            size_t field_index;\n            size_t anonymous_prefix_offset;\n        } offsetof_value;\n''',
    '''        struct {\n            MinicRecordId record_id;\n            size_t field_index;\n        } offsetof_value;\n''',
    "offsetof AST payload",
)

# Parser: promoted anonymous paths become a sum of semantic offsetof leaves.
expr = root / "src/frontend/parser_expression.c"
member_body = r'''static bool parse_offsetof_member_segment(MinicParser *parser,
                                          MinicSourcePosition begin,
                                          MinicType record_type,
                                          MinicOffsetofDesignatorState *state) {
    const MinicRecord *record;
    const MinicRecordField *final_field;
    MinicRecordFieldPath path;
    MinicSourceSpan field_span;
    size_t path_index;

    if (parser == NULL || state == NULL || !minic_type_is_record(record_type) ||
        parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        minic_parser_error(parser, "expected record field in __builtin_offsetof designator");
        return false;
    }
    record = minic_c0_program_record(parser->program, record_type.record_id);
    if (record == NULL || !record->is_complete) {
        minic_parser_error(parser,
                           "__builtin_offsetof member designator requires a complete record");
        return false;
    }
    field_span = parser->current.span;
    if (!minic_parser_find_record_field_path(parser, record, field_span, &path)) {
        minic_parser_error(parser,
                           path.ambiguous ? "record field is ambiguous in __builtin_offsetof"
                                          : "record has no such field in __builtin_offsetof");
        return false;
    }
    if (path.depth == 0U) {
        minic_parser_error(parser, "empty record field path in __builtin_offsetof");
        return false;
    }

    final_field = NULL;
    for (path_index = 0U; path_index < path.depth; ++path_index) {
        const MinicRecord *path_record;
        const MinicRecordField *path_field;
        MinicExpression segment;
        MinicExpressionId segment_id;

        path_record = minic_c0_program_record(parser->program, path.record_ids[path_index]);
        path_field = minic_c0_record_field(path_record, path.field_indices[path_index]);
        if (path_record == NULL || path_field == NULL) {
            minic_parser_error(parser, "invalid record field path in __builtin_offsetof");
            return false;
        }
        if (path_index + 1U == path.depth) {
            if (path_field->is_bit_field) {
                minic_parser_error(parser, "__builtin_offsetof cannot name a bit-field");
                return false;
            }
            final_field = path_field;
        }

        (void)memset(&segment, 0, sizeof(segment));
        segment.kind = MINIC_EXPRESSION_OFFSETOF;
        segment.span.begin = begin;
        segment.span.end = field_span.end;
        segment.type = minic_type_unsigned_long();
        segment.value_category = MINIC_VALUE_RVALUE;
        segment.value.offsetof_value.record_id = path.record_ids[path_index];
        segment.value.offsetof_value.field_index = path.field_indices[path_index];
        if (!minic_parser_add_expression(parser, &segment, &segment_id) ||
            !append_offsetof_term(parser, begin, segment_id, state)) {
            return false;
        }
    }
    if (final_field == NULL || !minic_parser_advance(parser)) {
        return false;
    }
    state->type = final_field->type;
    state->is_array = final_field->is_array;
    return true;
}

'''
replace_function(
    expr,
    "static bool parse_offsetof_member_segment(",
    "static bool parse_offsetof_array_segment(",
    member_body,
    "path_index < path.depth",
)

# Parser: array stride is semantic sizeof(type), not a target byte literal.
array_body = r'''static bool parse_offsetof_array_segment(MinicParser *parser,
                                         MinicSourcePosition begin,
                                         MinicOffsetofDesignatorState *state) {
    MinicExpressionId index_id;
    MinicExpressionId stride_id;
    MinicExpressionId scaled_id;
    const MinicExpression *index_expression;
    const MinicArrayType *nested_array;
    MinicExpression stride;
    MinicExpression scaled;
    MinicSourceSpan index_span;
    MinicType index_type;
    MinicType selected_type;
    MinicType scaled_type;

    if (parser == NULL || state == NULL || parser->current.kind != MINIC_TOKEN_LBRACKET) {
        return false;
    }
    if (!state->is_array) {
        minic_parser_error(parser, "__builtin_offsetof array designator requires an array field");
        return false;
    }
    selected_type = state->type;
    if (!minic_parser_advance(parser) || !parse_expression_internal(parser, &index_id, 0U, true)) {
        return false;
    }
    index_expression = minic_c0_program_expression(parser->program, index_id);
    if (index_expression == NULL || !minic_type_is_integer(index_expression->type)) {
        minic_parser_error(parser, "__builtin_offsetof array index requires an integer");
        return false;
    }
    /* The expression pool may grow while adding the stride/scaled nodes below.
     * Snapshot semantic data before any append instead of retaining a pool pointer. */
    index_type = index_expression->type;
    index_span = index_expression->span;
    if (parser->current.kind != MINIC_TOKEN_RBRACKET) {
        minic_parser_error(parser, "expected ']' in __builtin_offsetof array designator");
        return false;
    }

    (void)memset(&stride, 0, sizeof(stride));
    stride.kind = MINIC_EXPRESSION_SIZEOF;
    stride.span = parser->current.span;
    stride.type = minic_type_unsigned_long();
    stride.value_category = MINIC_VALUE_RVALUE;
    stride.value.sizeof_type = selected_type;
    if (!minic_parser_add_expression(parser, &stride, &stride_id) ||
        !minic_target_info_integer_common(
            parser->target_info, index_type, stride.type, &scaled_type)) {
        minic_parser_error(parser, "cannot type __builtin_offsetof array index scale");
        return false;
    }

    (void)memset(&scaled, 0, sizeof(scaled));
    scaled.kind = MINIC_EXPRESSION_BINARY;
    scaled.span.begin = index_span.begin;
    scaled.span.end = parser->current.span.end;
    scaled.type = scaled_type;
    scaled.value_category = MINIC_VALUE_RVALUE;
    scaled.value.binary.operator_kind = MINIC_BINARY_MULTIPLY;
    scaled.value.binary.left = index_id;
    scaled.value.binary.right = stride_id;
    if (!minic_parser_add_expression(parser, &scaled, &scaled_id) ||
        !append_offsetof_term(parser, begin, scaled_id, state) || !minic_parser_advance(parser)) {
        return false;
    }

    state->type = selected_type;
    state->is_array = false;
    if (minic_type_is_array(selected_type)) {
        nested_array = minic_c0_program_array_type(parser->program, selected_type.array_type_id);
        if (nested_array == NULL || nested_array->element_count == 0U) {
            minic_parser_error(parser,
                               "invalid nested array type in __builtin_offsetof designator");
            return false;
        }
        state->type = nested_array->element_type;
        state->is_array = true;
    }
    return true;
}

'''
replace_function(
    expr,
    "static bool parse_offsetof_array_segment(",
    "static bool parse_builtin_offsetof(",
    array_body,
    "stride.kind = MINIC_EXPRESSION_SIZEOF",
)

# ConstEval: one offsetof node means exactly one DataLayout field query.
consteval = root / "src/frontend/const_eval.c"
replace_once(
    consteval,
    '''        if (record == NULL ||\n            !minic_data_layout_record_field_offset(minic_target_info_data_layout(target),\n                                                   program,\n                                                   record,\n                                                   expression->value.offsetof_value.field_index,\n                                                   &offset) ||\n            expression->value.offsetof_value.anonymous_prefix_offset > SIZE_MAX - offset) {\n            return false;\n        }\n        offset += expression->value.offsetof_value.anonymous_prefix_offset;\n        value->type = expression->type;\n''',
    '''        if (record == NULL ||\n            !minic_data_layout_record_field_offset(minic_target_info_data_layout(target),\n                                                   program,\n                                                   record,\n                                                   expression->value.offsetof_value.field_index,\n                                                   &offset)) {\n            return false;\n        }\n        value->type = expression->type;\n''',
    "offsetof ConstEval cache removal",
)

# RV64: likewise resolve only the semantic field query; sums/sizeof are ordinary expressions.
codegen = root / "src/target/riscv64/codegen_expression.c"
replace_once(
    codegen,
    '''        if (record == NULL || field == NULL || !record->is_complete ||\n            !minic_type_equal(expression->type, minic_type_unsigned_long()) ||\n            !minic_data_layout_record_field_offset(minic_default_data_layout(),\n                                                   program,\n                                                   record,\n                                                   expression->value.offsetof_value.field_index,\n                                                   &offset) ||\n            expression->value.offsetof_value.anonymous_prefix_offset > SIZE_MAX - offset) {\n            return false;\n        }\n        offset = expression->value.offsetof_value.anonymous_prefix_offset + offset;\n        return fprintf(file, "  li a0, %zu\\n", offset) >= 0;\n''',
    '''        if (record == NULL || field == NULL || !record->is_complete ||\n            !minic_type_equal(expression->type, minic_type_unsigned_long()) ||\n            !minic_data_layout_record_field_offset(minic_default_data_layout(),\n                                                   program,\n                                                   record,\n                                                   expression->value.offsetof_value.field_index,\n                                                   &offset)) {\n            return false;\n        }\n        return fprintf(file, "  li a0, %zu\\n", offset) >= 0;\n''',
    "offsetof RV64 cache removal",
)

runner = root / "tests/compiler/c0/run-builtin-offsetof.sh"
replace_once(
    runner,
    "normalized=offset-terms+index*stride scalar-index=reject'\n",
    "semantic=field-offset-terms+index*sizeof scalar-index=reject'\n",
    "offsetof focused summary",
)

# Staging-only architecture assertions. The final product does not keep this generator.
if "anonymous_prefix_offset" in ast.read_text():
    raise SystemExit("target-derived offsetof prefix still present in AST")
member_region = expr.read_text().split("static bool parse_offsetof_member_segment(", 1)[1].split(
    "static bool parse_offsetof_array_segment(", 1
)[0]
if "minic_data_layout_record_field_offset" in member_region:
    raise SystemExit("offsetof member parser still performs DataLayout queries")
array_region = expr.read_text().split("static bool parse_offsetof_array_segment(", 1)[1].split(
    "static bool parse_builtin_offsetof(", 1
)[0]
if "minic_target_info_sizeof_type" in array_region or "element_size" in array_region:
    raise SystemExit("offsetof array parser still materializes target byte stride")
