#!/usr/bin/env python3
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one anchor, found {count}")
    return text.replace(old, new, 1)


root = Path(__file__).resolve().parents[2]

path = root / "src/frontend/ast.h"
text = path.read_text()
text = replace_once(
    text,
    """    MINIC_EXPRESSION_CALL,
    MINIC_EXPRESSION_STATEMENT,
    MINIC_EXPRESSION_BUILTIN_OVERFLOW
""",
    """    MINIC_EXPRESSION_CALL,
    MINIC_EXPRESSION_STATEMENT,
    MINIC_EXPRESSION_BUILTIN_UNARY,
    MINIC_EXPRESSION_BUILTIN_OVERFLOW
""",
    "ast-builtin-unary-kind",
)
text = replace_once(
    text,
    """typedef enum MinicOverflowOperator {
""",
    """typedef enum MinicBuiltinUnaryOperator {
    MINIC_BUILTIN_UNARY_CLZLL = 0
} MinicBuiltinUnaryOperator;

typedef enum MinicOverflowOperator {
""",
    "ast-builtin-unary-operator",
)
text = replace_once(
    text,
    """        struct {
            MinicOverflowOperator operator_kind;
            MinicExpressionId left;
""",
    """        struct {
            MinicBuiltinUnaryOperator operator_kind;
            MinicExpressionId operand;
        } builtin_unary;
        struct {
            MinicOverflowOperator operator_kind;
            MinicExpressionId left;
""",
    "ast-builtin-unary-payload",
)
path.write_text(text)

path = root / "src/frontend/parser_expression.c"
text = path.read_text()
anchor = """static bool parse_builtin_overflow(MinicParser *parser,
"""
addition = """static bool parse_builtin_unary(MinicParser *parser,
                                MinicBuiltinUnaryOperator operator_kind,
                                const char *spelling,
                                MinicExpressionId *expression_id) {
    MinicExpression conversion;
    MinicExpression expression;
    const MinicExpression *operand;
    MinicExpressionId converted_id;
    MinicExpressionId operand_id;
    MinicSourcePosition begin;
    MinicType argument_type;

    if (parser == NULL || spelling == NULL || expression_id == NULL ||
        !generic_token_text_equals(parser, spelling)) {
        return false;
    }
    switch (operator_kind) {
    case MINIC_BUILTIN_UNARY_CLZLL:
        argument_type = minic_type_unsigned_long_long();
        break;
    default:
        return false;
    }

    begin = parser->current.span.begin;
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after unary builtin") ||
        !parse_expression_internal(parser, &operand_id, 0U, true)) {
        return false;
    }
    operand = minic_c0_program_expression(parser->program, operand_id);
    if (operand == NULL || !minic_type_is_integer(operand->type)) {
        minic_parser_error(parser, "unary builtin requires an integer operand");
        return false;
    }
    if (!minic_type_equal(operand->type, argument_type)) {
        (void)memset(&conversion, 0, sizeof(conversion));
        conversion.kind = MINIC_EXPRESSION_CAST;
        conversion.span = operand->span;
        conversion.type = argument_type;
        conversion.value_category = MINIC_VALUE_RVALUE;
        conversion.value.unary.operand = operand_id;
        if (!minic_parser_add_expression(parser, &conversion, &converted_id)) {
            return false;
        }
        operand_id = converted_id;
    }
    if (parser->current.kind != MINIC_TOKEN_RPAREN) {
        minic_parser_error(parser, "expected ')' after unary builtin operand");
        return false;
    }

    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_BUILTIN_UNARY;
    expression.span.begin = begin;
    expression.span.end = parser->current.span.end;
    expression.type = minic_type_int();
    expression.value_category = MINIC_VALUE_RVALUE;
    expression.value.builtin_unary.operator_kind = operator_kind;
    expression.value.builtin_unary.operand = operand_id;
    return minic_parser_advance(parser) &&
           minic_parser_add_expression(parser, &expression, expression_id);
}

"""
text = replace_once(text, anchor, addition + anchor, "parse-builtin-unary-helper")
text = replace_once(
    text,
    """    if (generic_token_text_equals(parser, "__builtin_constant_p")) {
""",
    """    if (generic_token_text_equals(parser, "__builtin_clzll")) {
        if (!parse_builtin_unary(
                parser, MINIC_BUILTIN_UNARY_CLZLL, "__builtin_clzll", &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        return finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (generic_token_text_equals(parser, "__builtin_constant_p")) {
""",
    "parse-primary-clzll",
)
path.write_text(text)

path = root / "src/frontend/ast_verifier.c"
text = path.read_text()
text = replace_once(
    text,
    """    case MINIC_EXPRESSION_BUILTIN_OVERFLOW: {
""",
    """    case MINIC_EXPRESSION_BUILTIN_UNARY: {
        const MinicExpression *operand;

        operand = expression_before(
            program, expression->value.builtin_unary.operand, expression_index);
        return expression->value.builtin_unary.operator_kind == MINIC_BUILTIN_UNARY_CLZLL &&
               operand != NULL &&
               minic_type_equal(operand->type, minic_type_unsigned_long_long()) &&
               expression->value_category == MINIC_VALUE_RVALUE &&
               minic_type_equal(expression->type, minic_type_int());
    }
    case MINIC_EXPRESSION_BUILTIN_OVERFLOW: {
""",
    "verify-builtin-unary",
)
path.write_text(text)

path = root / "src/frontend/cast_normalization.c"
text = path.read_text()
text = replace_once(
    text,
    """    case MINIC_EXPRESSION_BUILTIN_OVERFLOW:
""",
    """    case MINIC_EXPRESSION_BUILTIN_UNARY:
        return remap_expression_id(mapping,
                                   old_expression_count,
                                   current_old_index,
                                   expression->value.builtin_unary.operand,
                                   &expression->value.builtin_unary.operand);
    case MINIC_EXPRESSION_BUILTIN_OVERFLOW:
""",
    "normalize-builtin-unary",
)
path.write_text(text)

path = root / "src/target/riscv64/codegen_expression.c"
text = path.read_text()
anchor = """static bool minic_riscv64_emit_overflow_builtin(FILE *file,
"""
addition = """static bool minic_riscv64_emit_builtin_unary(FILE *file,
                                             const MinicC0Program *program,
                                             const MinicFunction *function,
                                             const MinicExpression *expression,
                                             MinicExpressionId expression_id) {
    const MinicExpression *operand;

    if (file == NULL || program == NULL || expression == NULL ||
        expression->kind != MINIC_EXPRESSION_BUILTIN_UNARY ||
        expression->value.builtin_unary.operator_kind != MINIC_BUILTIN_UNARY_CLZLL ||
        !minic_type_equal(expression->type, minic_type_int())) {
        return false;
    }
    operand = minic_c0_program_expression(program, expression->value.builtin_unary.operand);
    if (operand == NULL ||
        !minic_type_equal(operand->type, minic_type_unsigned_long_long()) ||
        !minic_riscv64_emit_expression(
            file, program, function, expression->value.builtin_unary.operand)) {
        return false;
    }

    /* __builtin_clzll(0) is undefined. For non-zero values this baseline RV64I
     * binary search computes the exact count without requiring Zbb clz. */
    return fprintf(file,
                   "  li t0, 0\n"
                   "  srli t1, a0, 32\n"
                   "  bnez t1, .Lminic_clzll_32_%zu\n"
                   "  addi t0, t0, 32\n"
                   "  slli a0, a0, 32\n"
                   ".Lminic_clzll_32_%zu:\n"
                   "  srli t1, a0, 48\n"
                   "  bnez t1, .Lminic_clzll_16_%zu\n"
                   "  addi t0, t0, 16\n"
                   "  slli a0, a0, 16\n"
                   ".Lminic_clzll_16_%zu:\n"
                   "  srli t1, a0, 56\n"
                   "  bnez t1, .Lminic_clzll_8_%zu\n"
                   "  addi t0, t0, 8\n"
                   "  slli a0, a0, 8\n"
                   ".Lminic_clzll_8_%zu:\n"
                   "  srli t1, a0, 60\n"
                   "  bnez t1, .Lminic_clzll_4_%zu\n"
                   "  addi t0, t0, 4\n"
                   "  slli a0, a0, 4\n"
                   ".Lminic_clzll_4_%zu:\n"
                   "  srli t1, a0, 62\n"
                   "  bnez t1, .Lminic_clzll_2_%zu\n"
                   "  addi t0, t0, 2\n"
                   "  slli a0, a0, 2\n"
                   ".Lminic_clzll_2_%zu:\n"
                   "  srli t1, a0, 63\n"
                   "  bnez t1, .Lminic_clzll_1_%zu\n"
                   "  addi t0, t0, 1\n"
                   ".Lminic_clzll_1_%zu:\n"
                   "  mv a0, t0\n",
                   expression_id,
                   expression_id,
                   expression_id,
                   expression_id,
                   expression_id,
                   expression_id,
                   expression_id,
                   expression_id,
                   expression_id,
                   expression_id,
                   expression_id,
                   expression_id) >= 0;
}

"""
text = replace_once(text, anchor, addition + anchor, "rv64-builtin-unary-helper")
text = replace_once(
    text,
    """    case MINIC_EXPRESSION_BUILTIN_OVERFLOW:
        return minic_riscv64_emit_overflow_builtin(file, program, function, expression);
""",
    """    case MINIC_EXPRESSION_BUILTIN_UNARY:
        return minic_riscv64_emit_builtin_unary(
            file, program, function, expression, expression_id);
    case MINIC_EXPRESSION_BUILTIN_OVERFLOW:
        return minic_riscv64_emit_overflow_builtin(file, program, function, expression);
""",
    "rv64-builtin-unary-switch",
)
path.write_text(text)

path = root / "tests/compiler/c0/builtin_clzll.c"
path.write_text("""static int runtime_clzll_ull(unsigned long long value) {
    return __builtin_clzll(value);
}

static int runtime_clzll_uint(unsigned int value) {
    return __builtin_clzll(value);
}

static int runtime_clzll_int(int value) {
    return __builtin_clzll(value);
}

int main(void) {
    return runtime_clzll_ull(1ULL) == 63 &&
                   runtime_clzll_ull(16ULL) == 59 &&
                   runtime_clzll_ull(0x8000000000000000ULL) == 0 &&
                   runtime_clzll_ull(0x00f0000000000000ULL) == 8 &&
                   runtime_clzll_uint(0x80000000U) == 32 &&
                   runtime_clzll_int(-1) == 0
               ? 0
               : 1;
}
""")

path = root / "tests/compiler/c0/run-builtin-clzll.sh"
path.write_text("""#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-builtin-clzll

rm -rf "$work"
mkdir -p "$work"
"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/builtin_clzll.c" -o "$work/builtin_clzll.i"
"$minic" -S "$work/builtin_clzll.i" -o "$work/builtin_clzll.s"
test -s "$work/builtin_clzll.s"
grep -F 'runtime_clzll_ull:' "$work/builtin_clzll.s" >/dev/null
grep -F 'runtime_clzll_uint:' "$work/builtin_clzll.s" >/dev/null
grep -F '.Lminic_clzll_32_' "$work/builtin_clzll.s" >/dev/null
grep -F '  srli t1, a0, 32' "$work/builtin_clzll.s" >/dev/null
if grep -Eq '(^|[[:space:]])clz([[:space:]]|$)' "$work/builtin_clzll.s"; then
    printf '%s\n' 'unexpected Zbb clz dependency' >&2
    exit 1
fi
printf '%s\n' 'PASS compiler/c0/builtin_clzll ast=unary-builtin argument=ull-normalized runtime-lowering=rv64i-binary-search consteval=deferred zbb=none'
""")

path = root / "tests/compiler/c0/run-builtin-clzll-rv64.sh"
path.write_text("""#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
riscv_cc=${RISCV_CC:-riscv64-linux-gnu-gcc}
qemu=${QEMU_RISCV64:-qemu-riscv64}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-builtin-clzll-rv64
source="$root/tests/compiler/c0/builtin_clzll.c"

rm -rf "$work"
mkdir -p "$work"
"$riscv_cc" -E -P -std=gnu11 -x c "$source" -o "$work/probe.i"
"$minic" -S "$work/probe.i" -o "$work/minic.s"
if grep -Eq '(^|[[:space:]])clz([[:space:]]|$)' "$work/minic.s"; then
    printf '%s\n' 'unexpected Zbb clz dependency' >&2
    exit 1
fi
"$riscv_cc" -static "$work/minic.s" -o "$work/minic.elf"
"$riscv_cc" -static -std=gnu11 "$source" -o "$work/gcc.elf"

set +e
"$qemu" "$work/gcc.elf"
gcc_status=$?
"$qemu" "$work/minic.elf"
minic_status=$?
set -e
if test "$gcc_status" -ne 0 || test "$minic_status" -ne "$gcc_status"; then
    printf '%s\n' "FAIL compiler/c0/builtin_clzll_rv64 gcc=$gcc_status minic=$minic_status" >&2
    exit 1
fi
printf '%s\n' 'PASS compiler/c0/builtin_clzll_rv64 gcc=minic ull=4 uint32=1 signed-int=1 baseline=rv64i qemu=1'
""")

path = root / "tools/dev/pr76-focused.sh"
text = path.read_text()
text = replace_once(
    text,
    """sh tests/compiler/c0/run-static-assert-declaration.sh
""",
    """sh tests/compiler/c0/run-static-assert-declaration.sh
sh tests/compiler/c0/run-builtin-clzll.sh
""",
    "focused-clzll-gate",
)
path.write_text(text)
