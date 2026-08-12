from pathlib import Path


parser_path = Path("src/frontend/parser_expression.c")
text = parser_path.read_text()
start = text.find("static bool parse_builtin_offsetof(")
end = text.find("\nstatic bool generic_token_text_equals(", start)
if start < 0 or end < 0:
    raise SystemExit(f"offsetof parser region mismatch start={start} end={end}")

replacement = r'''typedef struct MinicOffsetofDesignatorState {
    MinicType type;
    MinicExpressionId offset_id;
    bool is_array;
} MinicOffsetofDesignatorState;

static bool append_offsetof_term(MinicParser *parser,
                                 MinicSourcePosition begin,
                                 MinicExpressionId term_id,
                                 MinicOffsetofDesignatorState *state) {
    const MinicExpression *term;
    MinicExpression sum;

    if (parser == NULL || state == NULL || term_id == MINIC_EXPRESSION_INVALID) {
        return false;
    }
    if (state->offset_id == MINIC_EXPRESSION_INVALID) {
        state->offset_id = term_id;
        return true;
    }
    term = minic_c0_program_expression(parser->program, term_id);
    if (term == NULL || !minic_type_is_integer(term->type)) {
        minic_parser_error(parser, "invalid __builtin_offsetof designator offset term");
        return false;
    }
    (void)memset(&sum, 0, sizeof(sum));
    sum.kind = MINIC_EXPRESSION_BINARY;
    sum.span.begin = begin;
    sum.span.end = term->span.end;
    sum.type = minic_type_unsigned_long();
    sum.value_category = MINIC_VALUE_RVALUE;
    sum.value.binary.operator_kind = MINIC_BINARY_ADD;
    sum.value.binary.left = state->offset_id;
    sum.value.binary.right = term_id;
    return minic_parser_add_expression(parser, &sum, &state->offset_id);
}

static bool parse_offsetof_member_segment(MinicParser *parser,
                                          MinicSourcePosition begin,
                                          MinicType record_type,
                                          MinicOffsetofDesignatorState *state) {
    const MinicRecord *record;
    const MinicRecord *final_record;
    const MinicRecordField *final_field;
    MinicRecordFieldPath path;
    MinicSourceSpan field_span;
    MinicExpression segment;
    MinicExpressionId segment_id;
    size_t anonymous_prefix_offset;
    size_t path_index;

    if (parser == NULL || state == NULL || !minic_type_is_record(record_type) ||
        parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        minic_parser_error(parser, "expected record field in __builtin_offsetof designator");
        return false;
    }
    record = minic_c0_program_record(parser->program, record_type.record_id);
    if (record == NULL || !record->is_complete) {
        minic_parser_error(parser, "__builtin_offsetof member designator requires a complete record");
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
    final_record = minic_c0_program_record(parser->program, path.record_ids[path.depth - 1U]);
    final_field = minic_c0_record_field(final_record, path.field_indices[path.depth - 1U]);
    if (final_field == NULL || final_field->is_bit_field) {
        minic_parser_error(parser, "__builtin_offsetof cannot name a bit-field");
        return false;
    }

    anonymous_prefix_offset = 0U;
    for (path_index = 0U; path_index + 1U < path.depth; ++path_index) {
        const MinicRecord *path_record;
        size_t field_offset;

        path_record = minic_c0_program_record(parser->program, path.record_ids[path_index]);
        if (path_record == NULL ||
            !minic_data_layout_record_field_offset(
                minic_target_info_data_layout(parser->target_info),
                parser->program,
                path_record,
                path.field_indices[path_index],
                &field_offset) ||
            anonymous_prefix_offset > SIZE_MAX - field_offset) {
            minic_parser_error(parser,
                               "cannot lay out anonymous member path in __builtin_offsetof");
            return false;
        }
        anonymous_prefix_offset += field_offset;
    }

    (void)memset(&segment, 0, sizeof(segment));
    segment.kind = MINIC_EXPRESSION_OFFSETOF;
    segment.span.begin = begin;
    segment.span.end = field_span.end;
    segment.type = minic_type_unsigned_long();
    segment.value_category = MINIC_VALUE_RVALUE;
    segment.value.offsetof_value.record_id = path.record_ids[path.depth - 1U];
    segment.value.offsetof_value.field_index = path.field_indices[path.depth - 1U];
    segment.value.offsetof_value.anonymous_prefix_offset = anonymous_prefix_offset;
    if (!minic_parser_add_expression(parser, &segment, &segment_id) ||
        !append_offsetof_term(parser, begin, segment_id, state) || !minic_parser_advance(parser)) {
        return false;
    }
    state->type = final_field->type;
    state->is_array = final_field->is_array;
    return true;
}

static bool parse_offsetof_array_segment(MinicParser *parser,
                                         MinicSourcePosition begin,
                                         MinicOffsetofDesignatorState *state) {
    MinicExpressionId index_id;
    MinicExpressionId stride_id;
    MinicExpressionId scaled_id;
    const MinicExpression *index_expression;
    const MinicArrayType *nested_array;
    MinicExpression stride;
    MinicExpression scaled;
    MinicType selected_type;
    MinicType scaled_type;
    size_t element_size;

    if (parser == NULL || state == NULL || parser->current.kind != MINIC_TOKEN_LBRACKET) {
        return false;
    }
    if (!state->is_array) {
        minic_parser_error(parser, "__builtin_offsetof array designator requires an array field");
        return false;
    }
    selected_type = state->type;
    if (!minic_parser_advance(parser) ||
        !parse_expression_internal(parser, &index_id, 0U, true)) {
        return false;
    }
    index_expression = minic_c0_program_expression(parser->program, index_id);
    if (index_expression == NULL || !minic_type_is_integer(index_expression->type)) {
        minic_parser_error(parser, "__builtin_offsetof array index requires an integer");
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_RBRACKET) {
        minic_parser_error(parser, "expected ']' in __builtin_offsetof array designator");
        return false;
    }
    if (!minic_target_info_sizeof_type(
            parser->target_info, parser->program, selected_type, &element_size) ||
        element_size > (size_t)INT64_MAX) {
        minic_parser_error(parser, "cannot lay out __builtin_offsetof array element");
        return false;
    }

    (void)memset(&stride, 0, sizeof(stride));
    stride.kind = MINIC_EXPRESSION_INTEGER;
    stride.span = parser->current.span;
    stride.type = minic_type_unsigned_long();
    stride.value_category = MINIC_VALUE_RVALUE;
    stride.value.integer_value = (int64_t)element_size;
    if (!minic_parser_add_expression(parser, &stride, &stride_id) ||
        !minic_type_integer_common(index_expression->type, stride.type, &scaled_type)) {
        minic_parser_error(parser, "cannot type __builtin_offsetof array index scale");
        return false;
    }

    (void)memset(&scaled, 0, sizeof(scaled));
    scaled.kind = MINIC_EXPRESSION_BINARY;
    scaled.span.begin = index_expression->span.begin;
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
            minic_parser_error(parser, "invalid nested array type in __builtin_offsetof designator");
            return false;
        }
        state->type = nested_array->element_type;
        state->is_array = true;
    }
    return true;
}

static bool parse_builtin_offsetof(MinicParser *parser, MinicExpressionId *expression_id) {
    MinicSourcePosition begin;
    MinicType record_type;
    MinicOffsetofDesignatorState state;

    begin = parser->current.span.begin;
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after __builtin_offsetof") ||
        !minic_parser_parse_type_name(parser, &record_type)) {
        return false;
    }
    if (!minic_type_is_record(record_type)) {
        minic_parser_error(parser, "__builtin_offsetof requires a record type");
        return false;
    }
    {
        const MinicRecord *record;

        record = minic_c0_program_record(parser->program, record_type.record_id);
        if (record == NULL || !record->is_complete) {
            minic_parser_error(parser, "__builtin_offsetof requires a complete record type");
            return false;
        }
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_COMMA, "expected ',' in __builtin_offsetof") ||
        parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "expected record field in __builtin_offsetof");
        }
        return false;
    }

    state.type = record_type;
    state.offset_id = MINIC_EXPRESSION_INVALID;
    state.is_array = false;
    if (!parse_offsetof_member_segment(parser, begin, record_type, &state)) {
        return false;
    }

    while (parser->current.kind != MINIC_TOKEN_RPAREN) {
        if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
            if (!parse_offsetof_array_segment(parser, begin, &state)) {
                return false;
            }
            continue;
        }
        if (parser->current.kind == MINIC_TOKEN_DOT) {
            MinicType member_record_type;

            if (state.is_array || !minic_type_is_record(state.type)) {
                minic_parser_error(parser,
                                   "__builtin_offsetof nested member designator requires a record");
                return false;
            }
            member_record_type = state.type;
            if (!minic_parser_advance(parser) || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
                minic_parser_error(parser, "expected member name after '.' in __builtin_offsetof");
                return false;
            }
            if (!parse_offsetof_member_segment(parser, begin, member_record_type, &state)) {
                return false;
            }
            continue;
        }
        minic_parser_error(parser, "unsupported __builtin_offsetof designator suffix");
        return false;
    }
    if (state.offset_id == MINIC_EXPRESSION_INVALID) {
        minic_parser_error(parser, "empty __builtin_offsetof designator");
        return false;
    }
    *expression_id = state.offset_id;
    return minic_parser_advance(parser);
}
'''

parser_path.write_text(text[:start] + replacement + text[end:])

fixture = Path("tests/compiler/c0/builtin_offsetof.c")
fixture_text = fixture.read_text()
addition = r'''

struct Flow4 {
    unsigned short family;
    unsigned int mark;
};

struct Flowi {
    char lead;
    union {
        struct Flow4 ip4;
        unsigned long raw;
    } u;
};

_Static_assert(__builtin_offsetof(struct Flowi, u.ip4) == 8,
               "nested member offsetof Linux shape");
_Static_assert(__builtin_offsetof(struct Flowi, u.ip4.mark) == 12,
               "nested member offsetof accumulates record offsets");

struct NestedElement {
    char lead;
    unsigned int value;
};

struct NestedGrid {
    char lead;
    struct NestedElement rows[3];
};

_Static_assert(__builtin_offsetof(struct NestedGrid, rows[2].value) == 24,
               "array then nested member offsetof");

unsigned long nested_indexed_offset(unsigned int idx) {
    return __builtin_offsetof(struct NestedGrid, rows[idx].value);
}

struct MatrixOffset {
    char lead;
    unsigned short cell[2][3];
};

_Static_assert(__builtin_offsetof(struct MatrixOffset, cell[1][2]) == 12,
               "multidimensional offsetof designator");
'''
if "nested member offsetof Linux shape" in fixture_text:
    raise SystemExit("nested offsetof fixture already materialized")
fixture.write_text(fixture_text + addition)

runner = Path("tests/compiler/c0/run-builtin-offsetof.sh")
runner_text = runner.read_text()
needle = "grep -F 'indexed_offset:' \"$work/builtin_offsetof.s\" >/dev/null\n"
extra = needle + "grep -F 'nested_indexed_offset:' \"$work/builtin_offsetof.s\" >/dev/null\n"
if runner_text.count(needle) != 1:
    raise SystemExit(f"offsetof runner label anchor mismatch: {runner_text.count(needle)}")
runner_text = runner_text.replace(needle, extra, 1)

summary = "array-designator=constant+runtime normalized=base+index*stride scalar-index=reject"
summary_replacement = (
    "array-designator=constant+runtime nested-member=record-path array-then-member=1 "
    "multidimensional-index=1 normalized=offset-terms+index*stride scalar-index=reject"
)
if runner_text.count(summary) != 1:
    raise SystemExit(f"offsetof summary anchor mismatch: {runner_text.count(summary)}")
runner_text = runner_text.replace(summary, summary_replacement, 1)

insert_before = "printf '%s\\n' 'PASS compiler/c0/builtin_offsetof"
negative = r'''cat >"$work/non-record-member.c" <<'EOF'
struct ScalarPath { unsigned long value; };
unsigned long bad(void)
{
    return __builtin_offsetof(struct ScalarPath, value.child);
}
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/non-record-member.c" -o "$work/non-record-member.i"
if "$minic" -S "$work/non-record-member.i" -o "$work/non-record-member.s" \
    2>"$work/non-record-member.stderr"; then
    printf '%s\n' 'offsetof nested member unexpectedly accepted through scalar field' >&2
    exit 1
fi
grep -F '__builtin_offsetof nested member designator requires a record' \
    "$work/non-record-member.stderr" >/dev/null

cat >"$work/nested-bit-field.c" <<'EOF'
struct BitInner { unsigned int flag : 1; };
struct BitOuter { struct BitInner inner; };
unsigned long bad(void)
{
    return __builtin_offsetof(struct BitOuter, inner.flag);
}
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/nested-bit-field.c" -o "$work/nested-bit-field.i"
if "$minic" -S "$work/nested-bit-field.i" -o "$work/nested-bit-field.s" \
    2>"$work/nested-bit-field.stderr"; then
    printf '%s\n' 'offsetof nested bit-field unexpectedly accepted' >&2
    exit 1
fi
grep -F '__builtin_offsetof cannot name a bit-field' "$work/nested-bit-field.stderr" >/dev/null

'''
pos = runner_text.find(insert_before)
if pos < 0:
    raise SystemExit("offsetof PASS anchor missing")
runner.write_text(runner_text[:pos] + negative + runner_text[pos:])
