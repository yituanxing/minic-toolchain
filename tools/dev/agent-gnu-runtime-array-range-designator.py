#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[2]
parser_path = root / "src/frontend/parser_statement.c"
fixture_path = root / "tests/compiler/c0/gnu_record_compound_literal.c"
runner_path = root / "tests/compiler/c0/run-gnu-record-compound-literal.sh"

text = parser_path.read_text()
zero_begin = text.index("static bool add_array_object_zero_elements(")
zero_end = text.index("\nstatic bool parse_fixed_runtime_array_initializer", zero_begin)
zero_block = r'''static bool add_array_object_zero_element(MinicParser *parser,
                                          MinicExpressionId base_id,
                                          size_t index,
                                          MinicSourceSpan initializer_span) {
    MinicExpression zero;
    MinicExpressionId zero_id;

    (void)memset(&zero, 0, sizeof(zero));
    zero.kind = MINIC_EXPRESSION_INTEGER;
    zero.span = initializer_span;
    zero.type = minic_type_int();
    zero.value_category = MINIC_VALUE_RVALUE;
    zero.value.integer_value = 0;
    return minic_parser_add_expression(parser, &zero, &zero_id) &&
           add_array_object_element_assignment(parser, base_id, index, zero_id);
}

static bool add_array_object_zero_elements(MinicParser *parser,
                                           MinicExpressionId base_id,
                                           size_t element_count,
                                           MinicSourceSpan initializer_span) {
    size_t index;

    for (index = 0U; index < element_count; ++index) {
        if (!add_array_object_zero_element(parser, base_id, index, initializer_span)) {
            return false;
        }
    }
    return true;
}
'''
text = text[:zero_begin] + zero_block + text[zero_end:]

fixed_begin = text.index("static bool parse_fixed_runtime_array_initializer(")
fixed_end = text.index("\nstatic bool\nparse_local_array_initializer", fixed_begin)
fixed_block = r'''static bool parse_runtime_array_designator(MinicParser *parser,
                                           size_t element_count,
                                           size_t next_index,
                                           size_t *first_index,
                                           size_t *last_index) {
    int64_t first;
    int64_t last;

    if (parser == NULL || first_index == NULL || last_index == NULL ||
        parser->current.kind != MINIC_TOKEN_LBRACKET || !minic_parser_advance(parser) ||
        !minic_parser_parse_integer_constant_expression(parser, &first)) {
        if (parser != NULL && parser->diagnostic != NULL &&
            parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "expected integer array designator index");
        }
        return false;
    }
    last = first;
    if (parser->current.kind == MINIC_TOKEN_ELLIPSIS) {
        if (!minic_parser_advance(parser) ||
            !minic_parser_parse_integer_constant_expression(parser, &last)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "expected GNU array range designator end");
            }
            return false;
        }
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_RBRACKET, "expected ']' after array designator") ||
        !minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '=' after array designator")) {
        return false;
    }
    if (first < 0 || last < first || (uint64_t)last >= (uint64_t)element_count) {
        minic_parser_error(parser, "runtime array designator range is out of bounds");
        return false;
    }
    if ((uint64_t)first < (uint64_t)next_index) {
        minic_parser_error(parser, "backward runtime array designators are not supported yet");
        return false;
    }
    *first_index = (size_t)first;
    *last_index = (size_t)last;
    return true;
}

static bool parse_fixed_runtime_array_initializer(MinicParser *parser,
                                                  MinicExpressionId base_id,
                                                  size_t element_count) {
    MinicSourceSpan initializer_span;
    size_t initializer_count;

    if (parser == NULL || element_count == 0U || parser->current.kind != MINIC_TOKEN_LBRACE) {
        minic_parser_error(parser,
                           "fixed runtime array initializer requires a nonempty array type");
        return false;
    }
    initializer_span.begin = parser->current.span.begin;
    if (!minic_parser_advance(parser)) {
        return false;
    }

    initializer_count = 0U;
    while (parser->current.kind != MINIC_TOKEN_RBRACE) {
        MinicExpressionId value_id;

        if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
            MinicConstValue constant;
            MinicSourceSpan zero_span;
            size_t first_index;
            size_t last_index;
            size_t index;

            if (!parse_runtime_array_designator(parser,
                                                element_count,
                                                initializer_count,
                                                &first_index,
                                                &last_index)) {
                return false;
            }
            zero_span = parser->current.span;
            for (index = initializer_count; index < first_index; ++index) {
                if (!add_array_object_zero_element(parser, base_id, index, zero_span)) {
                    return false;
                }
            }
            if (!minic_parser_parse_expression(parser, &value_id, 0U)) {
                return false;
            }
            if (last_index > first_index &&
                !minic_const_eval_integer(
                    parser->program, parser->target_info, value_id, &constant)) {
                minic_parser_error(
                    parser,
                    "multi-element runtime array range initializer requires an integer constant expression");
                return false;
            }
            for (index = first_index; index <= last_index; ++index) {
                if (!add_array_object_element_assignment(parser, base_id, index, value_id)) {
                    return false;
                }
            }
            initializer_count = last_index + 1U;
        } else {
            if (initializer_count >= element_count) {
                minic_parser_error(parser, "too many runtime array initializer elements");
                return false;
            }
            if (!minic_parser_parse_expression(parser, &value_id, 0U) ||
                !add_array_object_element_assignment(
                    parser, base_id, initializer_count, value_id)) {
                return false;
            }
            initializer_count += 1U;
        }
        if (parser->current.kind == MINIC_TOKEN_COMMA) {
            if (!minic_parser_advance(parser)) {
                return false;
            }
            if (parser->current.kind == MINIC_TOKEN_RBRACE) {
                break;
            }
            continue;
        }
        if (parser->current.kind != MINIC_TOKEN_RBRACE) {
            minic_parser_error(parser, "expected ',' or '}' in runtime array initializer");
            return false;
        }
    }
    initializer_span.end = parser->current.span.end;
    while (initializer_count < element_count) {
        if (!add_array_object_zero_element(
                parser, base_id, initializer_count, initializer_span)) {
            return false;
        }
        initializer_count += 1U;
    }
    return minic_parser_advance(parser);
}
'''
text = text[:fixed_begin] + fixed_block + text[fixed_end:]
parser_path.write_text(text)

fixture = fixture_path.read_text()
fixture += r'''

struct RangeMask {
    unsigned long bits[4];
};

static unsigned long range_effect(void)
{
    return 13UL;
}

/* Linux nodemask shape: GNU range designator inside a nested array initializer. */
void assign_range_mask(struct RangeMask *out)
{
    *out = (struct RangeMask) { { [1 ... 2] = 7UL, 9UL } };
}

/* A one-element range preserves normal runtime-expression evaluation. */
void assign_single_range_effect(struct RangeMask *out)
{
    *out = (struct RangeMask) { { [2 ... 2] = range_effect() } };
}
'''
fixture_path.write_text(fixture)

runner = runner_path.read_text()
needle = "grep -F 'nested_designated_braces:' \"$work/output.s\" >/dev/null\n"
insert = needle + "grep -F 'assign_range_mask:' \"$work/output.s\" >/dev/null\n" + \
    "grep -F 'assign_single_range_effect:' \"$work/output.s\" >/dev/null\n" + \
    "grep -F '  call range_effect' \"$work/output.s\" >/dev/null\n"
if needle not in runner:
    raise SystemExit("runner insertion point not found")
runner = runner.replace(needle, insert, 1)
negative_marker = "cat >\"$work/scalar.c\" <<'EOF'\n"
negative = r'''cat >"$work/range-side-effect.c" <<'EOF'
struct RangeMask {
    unsigned long bits[4];
};
static unsigned long effect(void)
{
    return 5UL;
}
void bad_range(struct RangeMask *out)
{
    *out = (struct RangeMask) { { [0 ... 1] = effect() } };
}
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/range-side-effect.c" -o "$work/range-side-effect.i"
if "$minic" -S "$work/range-side-effect.i" -o "$work/range-side-effect.s" \
    2>"$work/range-side-effect.stderr"; then
  printf '%s\n' 'FAIL compiler/c0/gnu_record_compound_literal: repeated side-effecting range accepted' >&2
  exit 1
fi
grep -F 'multi-element runtime array range initializer requires an integer constant expression' \
    "$work/range-side-effect.stderr" >/dev/null

'''
if negative_marker not in runner:
    raise SystemExit("negative insertion point not found")
runner = runner.replace(negative_marker, negative + negative_marker, 1)
runner = runner.replace(
    "nested-designated-braces=1 remaining-zero-fill=1 evaluation-order=preserved scalar=bounded-reject",
    "nested-designated-braces=1 array-designator=single+range range-constant-repeat=1 range-side-effect=fail-closed remaining-zero-fill=1 evaluation-order=preserved scalar=bounded-reject",
    1,
)
runner_path.write_text(runner)
