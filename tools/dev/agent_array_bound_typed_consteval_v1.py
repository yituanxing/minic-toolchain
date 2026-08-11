#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(path):
    return (ROOT / path).read_text()


def write(path, content):
    (ROOT / path).write_text(content)


def one(content, old, new, label):
    count = content.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected 1 match, found {count}")
    return content.replace(old, new, 1)


p = "src/frontend/parser_core.c"
s = read(p)
s = one(
    s,
    '''bool minic_parser_parse_integer_constant_expression_value(MinicParser *parser, int64_t *value) {
    return value != NULL && parse_array_bound_additive(parser, value);
}

bool minic_parser_parse_fixed_array_bound(MinicParser *parser, size_t *element_count) {
''',
    '''bool minic_parser_parse_integer_constant_expression_value(MinicParser *parser, int64_t *value) {
    return value != NULL && parse_array_bound_additive(parser, value);
}

static bool minic_parser_parse_typed_integer_constant_expression(MinicParser *parser,
                                                                  int64_t *value) {
    MinicConstValue constant;
    MinicExpressionId expression_id;

    if (parser == NULL || value == NULL ||
        !minic_parser_parse_expression(parser, &expression_id, 0U)) {
        return false;
    }
    if (!minic_const_eval_integer(parser->program, parser->target_info, expression_id, &constant)) {
        minic_parser_error(parser, "expected integer constant expression");
        return false;
    }
    if (!minic_const_value_as_int64(parser->program, parser->target_info, &constant, value)) {
        minic_parser_error(parser, "integer constant expression exceeds supported 64-bit range");
        return false;
    }
    return true;
}

bool minic_parser_parse_fixed_array_bound(MinicParser *parser, size_t *element_count) {
''',
    "typed ICE seam",
)
s = one(
    s,
    '''    if (element_count == NULL || !minic_parser_parse_integer_constant_expression(parser, &value)) {
        return false;
    }
''',
    '''    if (element_count == NULL ||
        !minic_parser_parse_typed_integer_constant_expression(parser, &value)) {
        return false;
    }
''',
    "fixed array typed ICE",
)
s = one(
    s,
    '''    if (element_count == NULL || is_zero_length == NULL ||
        !minic_parser_parse_integer_constant_expression(parser, &value)) {
        return false;
    }
''',
    '''    if (element_count == NULL || is_zero_length == NULL ||
        !minic_parser_parse_typed_integer_constant_expression(parser, &value)) {
        return false;
    }
''',
    "record array typed ICE",
)
write(p, s)

p = "tests/compiler/c0/array_bound_constant_expression.c"
s = read(p)
s = one(
    s,
    '''int main(void) {
    return multiplied_size() == 1048576 && grouped_size() == 35 && divided_size() == 44 ? 0 : 1;
}
''',
    '''int linux_siginfo_bound(void) {
    char dummy[(__alignof__(void *) < sizeof(short) ? sizeof(short) : __alignof__(void *))];
    return (int)sizeof(dummy);
}

struct RecordBound {
    char dummy[(__alignof__(void *) < sizeof(short) ? sizeof(short) : __alignof__(void *))];
    int tail;
};

int linux_siginfo_record_bound(void) {
    return (int)sizeof(((struct RecordBound *)0)->dummy);
}

int main(void) {
    return multiplied_size() == 1048576 && grouped_size() == 35 && divided_size() == 44 &&
                   linux_siginfo_bound() == 8 && linux_siginfo_record_bound() == 8
               ? 0
               : 1;
}
''',
    "Linux conditional alignof fixture",
)
write(p, s)

p = "tests/compiler/c0/run-array-bound-constant-expressions.sh"
s = read(p)
s = one(
    s,
    '''grep -F 'li a0, 44' "$work/array_bound_constant_expression.s" >/dev/null
printf '%s\\n' 'PASS compiler/c0/array_bound_constant_expression scope=local operators=+,-,*,/,% parentheses=1'
''',
    '''grep -F 'li a0, 44' "$work/array_bound_constant_expression.s" >/dev/null
linux8=$(grep -c -F '  li a0, 8' "$work/array_bound_constant_expression.s" || true)
test "$linux8" -ge 2
printf '%s\\n' 'PASS compiler/c0/array_bound_constant_expression scope=local+record typed-ast-consteval=1 operators=+,-,*,/,% relational=1 conditional=linux-alignof-shape alignof=1 sizeof=1 parentheses=1'
''',
    "array bound runner summary",
)
write(p, s)

p = "tests/compiler/c0/run-array-bound-integer-casts.sh"
s = read(p)
s = one(
    s,
    "shared-constant-evaluator=1",
    "typed-ast-consteval=1",
    "array cast runner summary",
)
write(p, s)
