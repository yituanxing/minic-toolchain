#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    file_path = Path(path)
    text = file_path.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one exact anchor, found {count}")
    file_path.write_text(text.replace(old, new, 1))


replace_once(
    "src/frontend/parser_statement.c",
    '''    if (minic_type_is_void(function->return_type)) {
        if (parser->current.kind != MINIC_TOKEN_SEMICOLON) {
            minic_parser_error(parser, "void function cannot return a value");
            return false;
        }
        statement.span.end = parser->current.span.end;
    } else {
''',
    '''    if (minic_type_is_void(function->return_type)) {
        if (parser->current.kind == MINIC_TOKEN_SEMICOLON) {
            statement.span.end = parser->current.span.end;
        } else {
            const MinicExpression *returned_expression;

            if (!minic_parser_parse_full_expression(parser, &statement.expression)) {
                return false;
            }
            returned_expression = minic_c0_program_expression(parser->program, statement.expression);
            if (returned_expression == NULL || !minic_type_is_void(returned_expression->type)) {
                minic_parser_error(parser, "void function return expression must have void type");
                return false;
            }
            statement.span.end = returned_expression->span.end;
        }
    } else {
''',
    "parse GNU void return expression",
)

replace_once(
    "src/target/riscv64/codegen_statement.c",
    '''static bool minic_riscv64_emit_return(FILE *file,
                                      const MinicC0Program *program,
                                      const MinicFunction *function,
                                      const MinicStatement *statement) {
    bool has_value;

    has_value = statement->expression != MINIC_EXPRESSION_INVALID;
    if (!has_value) {
        if (!minic_type_is_void(function->return_type)) {
            return false;
        }
    } else {
        const MinicExpression *value;

        value = minic_c0_program_expression(program, statement->expression);
        if (minic_type_is_void(function->return_type) || value == NULL ||
            !minic_c0_assignment_compatible(
                program, function->return_type, statement->expression)) {
            return false;
        }
''',
    '''static bool minic_riscv64_emit_return(FILE *file,
                                      const MinicC0Program *program,
                                      const MinicFunction *function,
                                      const MinicStatement *statement) {
    bool has_expression;
    bool has_value;

    has_expression = statement->expression != MINIC_EXPRESSION_INVALID;
    has_value = has_expression && !minic_type_is_void(function->return_type);
    if (minic_type_is_void(function->return_type)) {
        if (has_expression) {
            const MinicExpression *expression;

            expression = minic_c0_program_expression(program, statement->expression);
            if (expression == NULL || !minic_type_is_void(expression->type) ||
                !minic_riscv64_emit_expression(file, program, function, statement->expression)) {
                return false;
            }
        }
    } else {
        const MinicExpression *value;

        if (!has_expression) {
            return false;
        }
        value = minic_c0_program_expression(program, statement->expression);
        if (value == NULL ||
            !minic_c0_assignment_compatible(
                program, function->return_type, statement->expression)) {
            return false;
        }
''',
    "lower GNU void return expression",
)

Path("tests/compiler/c0/gnu_void_return_expression.c").write_text(
    '''static int trace_value;

static void mark_return_expression(void)
{
    trace_value = trace_value * 10 + 1;
}

static void mark_cleanup(int *value)
{
    (void)value;
    trace_value = trace_value * 10 + 2;
}

static void return_void_call(void)
{
    int guard __attribute__((__cleanup__(mark_cleanup))) = 0;
    return mark_return_expression();
}

static void return_void_cast(int value)
{
    return (void)value;
}

int main(void)
{
    return_void_call();
    return_void_cast(7);
    return trace_value == 12 ? 0 : 1;
}
'''
)

Path("tests/compiler/c0/run-gnu-void-return-expression.sh").write_text(
    '''#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-void-return-expression

rm -rf "$work"
mkdir -p "$work"
"$host_cc" -E -P -std=gnu11 -x c \
  "$root/tests/compiler/c0/gnu_void_return_expression.c" -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$work/output.s"
test -s "$work/output.s"

grep -F 'return_void_call:' "$work/output.s" >/dev/null
grep -F 'return_void_cast:' "$work/output.s" >/dev/null
return_call=$(grep -n -m1 -F '  call mark_return_expression' "$work/output.s" | cut -d: -f1)
cleanup_call=$(grep -n -m1 -F '  call mark_cleanup' "$work/output.s" | cut -d: -f1)
test -n "$return_call"
test -n "$cleanup_call"
test "$return_call" -lt "$cleanup_call"

cat >"$work/nonvoid-return.c" <<'EOF'
void bad(void)
{
    return 1;
}
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/nonvoid-return.c" -o "$work/nonvoid-return.i"
if "$minic" -S "$work/nonvoid-return.i" -o "$work/nonvoid-return.s" \
    2>"$work/nonvoid-return.stderr"; then
    printf '%s\n' 'non-void expression in void return unexpectedly accepted' >&2
    exit 1
fi
grep -F 'void function return expression must have void type' \
    "$work/nonvoid-return.stderr" >/dev/null

cat >"$work/void-in-nonvoid.c" <<'EOF'
static void sink(void) { }
int bad(void)
{
    return sink();
}
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/void-in-nonvoid.c" -o "$work/void-in-nonvoid.i"
if "$minic" -S "$work/void-in-nonvoid.i" -o "$work/void-in-nonvoid.s" \
    2>"$work/void-in-nonvoid.stderr"; then
    printf '%s\n' 'void expression in non-void return unexpectedly accepted' >&2
    exit 1
fi
grep -F 'return expression does not match function return type' \
    "$work/void-in-nonvoid.stderr" >/dev/null

printf '%s\n' \
  'PASS compiler/c0/gnu_void_return_expression void-call=1 void-cast=1 side-effect-before-cleanup=1 nonvoid-expression=reject nonvoid-function=unchanged'
'''
)

replace_once(
    "tests/compiler/c0/run.sh",
    '''expect_compile_failure invalid_return "expected expression"\n\nMINIC="$minic" \\\nHOST_CC="$host_cc" \\\nBUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \\\nsh "$root/tests/compiler/c0/run-comma-operator.sh"\n''',
    '''expect_compile_failure invalid_return "expected expression"\n\nMINIC="$minic" \\\nHOST_CC="$host_cc" \\\nBUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \\\nsh "$root/tests/compiler/c0/run-gnu-void-return-expression.sh"\n\nMINIC="$minic" \\\nHOST_CC="$host_cc" \\\nBUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \\\nsh "$root/tests/compiler/c0/run-comma-operator.sh"\n''',
    "freeze GNU void return regression in C0 gate",
)
