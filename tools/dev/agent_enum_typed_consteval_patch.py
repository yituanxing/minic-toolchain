from pathlib import Path

root = Path(__file__).resolve().parents[2]

parser_path = root / "src/frontend/parser_enum.c"
text = parser_path.read_text()
old = '''static bool parse_enum_integer_value(MinicParser *parser, int *value) {
    int64_t parsed;

    if (parser == NULL || value == NULL ||
        !minic_parser_parse_integer_constant_expression(parser, &parsed)) {
        return false;
    }
    if (parsed < INT_MIN || parsed > INT_MAX) {
        minic_parser_error(parser, "enum constant expression is out of int range");
        return false;
    }
    *value = (int)parsed;
    return true;
}
'''
new = '''static bool parse_enum_integer_value(MinicParser *parser, int *value) {
    const MinicExpression *expression;
    MinicConstValue constant_value;
    MinicExpressionId expression_id;
    int64_t parsed;

    if (parser == NULL || value == NULL ||
        !minic_parser_parse_expression(parser, &expression_id, 0U)) {
        return false;
    }
    expression = minic_c0_program_expression(parser->program, expression_id);
    if (expression == NULL || !minic_type_is_integer(expression->type) ||
        !minic_const_eval_integer(
            parser->program, parser->target_info, expression_id, &constant_value) ||
        !minic_const_value_as_int64(
            parser->program, parser->target_info, &constant_value, &parsed)) {
        minic_parser_error(parser, "enum initializer must be an integer constant expression");
        return false;
    }
    if (parsed < INT_MIN || parsed > INT_MAX) {
        minic_parser_error(parser, "enum constant expression is out of int range");
        return false;
    }
    *value = (int)parsed;
    return true;
}
'''
if text.count(old) != 1:
    raise SystemExit("enum integer initializer anchor mismatch")
parser_path.write_text(text.replace(old, new, 1))

fixture_path = root / "tests/compiler/c0/enum_constant_expression.c"
fixture_path.write_text(r'''enum Token {
    TOKEN_FIRST = (255 + 1),
    TOKEN_SECOND = TOKEN_FIRST + 2,
    TOKEN_THIRD = (2 * 3) + TOKEN_SECOND,
    WORK_OFFQ_POOL_SHIFT_LIKE = 33,
    WORK_OFFQ_LEFT_LIKE = 64 - WORK_OFFQ_POOL_SHIFT_LIKE,
    WORK_OFFQ_POOL_BITS_LIKE =
        WORK_OFFQ_LEFT_LIKE <= 31 ? WORK_OFFQ_LEFT_LIKE : 31,
    TOKEN_LOGICAL = TOKEN_SECOND > 200 && TOKEN_FIRST == 256 ? 9 : 10,
    TOKEN_SHORT_CIRCUIT = 0 ? (1 / 0) : 7
};

_Static_assert(TOKEN_FIRST == 256, "enum arithmetic");
_Static_assert(TOKEN_SECOND == 258, "prior enumerator");
_Static_assert(TOKEN_THIRD == 264, "mixed arithmetic");
_Static_assert(WORK_OFFQ_POOL_BITS_LIKE == 31, "Linux conditional enum initializer");
_Static_assert(TOKEN_LOGICAL == 9, "logical and relational enum initializer");
_Static_assert(TOKEN_SHORT_CIRCUIT == 7, "conditional evaluation selects one branch");

int main(void) {
    return TOKEN_FIRST == 256 && TOKEN_SECOND == 258 && TOKEN_THIRD == 264 &&
                   WORK_OFFQ_POOL_BITS_LIKE == 31 && TOKEN_LOGICAL == 9 &&
                   TOKEN_SHORT_CIRCUIT == 7
               ? 0
               : 1;
}
''')

runner_path = root / "tests/compiler/c0/run-enum-constant-expressions.sh"
runner_path.write_text(r'''#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-enum-constant-expressions

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/enum_constant_expression.c" \
    -o "$work/enum_constant_expression.i"
"$minic" -S "$work/enum_constant_expression.i" \
    -o "$work/enum_constant_expression.s"
test -s "$work/enum_constant_expression.s"

grep -F '  li a0, 256' "$work/enum_constant_expression.s" >/dev/null
grep -F '  li a0, 264' "$work/enum_constant_expression.s" >/dev/null

cat >"$work/nonconstant.c" <<'EOF'
extern int runtime_value;
enum Invalid {
    INVALID_ENUM_VALUE = runtime_value ? 1 : 2,
};
EOF
"$host_cc" -E -P -x c "$work/nonconstant.c" -o "$work/nonconstant.i"
if "$minic" -S "$work/nonconstant.i" -o "$work/nonconstant.s" \
    >"$work/nonconstant.stdout" 2>"$work/nonconstant.stderr"; then
    printf '%s\n' 'FAIL compiler/c0/enum_constant_expression: runtime initializer accepted' >&2
    exit 1
fi
grep -F 'enum initializer must be an integer constant expression' \
    "$work/nonconstant.stderr" >/dev/null

printf '%s\n' \
    'PASS compiler/c0/enum_constant_expression typed-ast-consteval=1 arithmetic=1 prior-enumerator=1 relational=1 logical=1 conditional=linux-shape short-circuit=1 runtime=reject'
''')
runner_path.chmod(0o755)

print("PASS generated typed enum initializer ConstEval slice")
