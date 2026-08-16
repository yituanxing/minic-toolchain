#!/usr/bin/env python3
from pathlib import Path
import re


def read(path: str) -> str:
    return Path(path).read_text()


def write(path: str, text: str) -> None:
    Path(path).write_text(text)


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one anchor, found {count}")
    return text.replace(old, new, 1)


def regex_once(text: str, pattern: str, replacement: str, label: str) -> str:
    updated, count = re.subn(pattern, replacement, text, count=1, flags=re.S)
    if count != 1:
        raise SystemExit(f"{label}: expected one regex match, found {count}")
    return updated


# 1. Extend the existing AST unary-builtin family; no new expression kind.
path = "src/frontend/ast.h"
text = read(path)
text = replace_once(
    text,
    "typedef enum MinicBuiltinUnaryOperator { MINIC_BUILTIN_UNARY_CLZLL = 0 } MinicBuiltinUnaryOperator;",
    """typedef enum MinicBuiltinUnaryOperator {
    MINIC_BUILTIN_UNARY_CLZLL = 0,
    MINIC_BUILTIN_UNARY_CTZL,
    MINIC_BUILTIN_UNARY_FFSLL,
    MINIC_BUILTIN_UNARY_ISDIGIT
} MinicBuiltinUnaryOperator;""",
    "ast builtin enum",
)
write(path, text)

# 2. Reuse the canonical unary-builtin parser and its typed conversion owner.
path = "src/frontend/parser_expression.c"
text = read(path)
text = replace_once(
    text,
    """    switch (operator_kind) {
    case MINIC_BUILTIN_UNARY_CLZLL:
        argument_type = minic_type_unsigned_long_long();
        break;
    default:
        return false;
    }
""",
    """    switch (operator_kind) {
    case MINIC_BUILTIN_UNARY_CLZLL:
        argument_type = minic_type_unsigned_long_long();
        break;
    case MINIC_BUILTIN_UNARY_CTZL:
        argument_type = minic_type_unsigned_long();
        break;
    case MINIC_BUILTIN_UNARY_FFSLL:
        argument_type = minic_type_long_long();
        break;
    case MINIC_BUILTIN_UNARY_ISDIGIT:
        argument_type = minic_type_int();
        break;
    default:
        return false;
    }
""",
    "parser builtin argument types",
)
clz_dispatch = """    if (generic_token_text_equals(parser, \"__builtin_clzll\")) {
        if (!parse_builtin_unary(
                parser, MINIC_BUILTIN_UNARY_CLZLL, \"__builtin_clzll\", &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        return finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
"""
text = replace_once(
    text,
    clz_dispatch,
    clz_dispatch
    + """    if (generic_token_text_equals(parser, \"__builtin_ctzl\")) {
        if (!parse_builtin_unary(
                parser, MINIC_BUILTIN_UNARY_CTZL, \"__builtin_ctzl\", &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        return finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (generic_token_text_equals(parser, \"__builtin_ffsll\")) {
        if (!parse_builtin_unary(
                parser, MINIC_BUILTIN_UNARY_FFSLL, \"__builtin_ffsll\", &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        return finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (generic_token_text_equals(parser, \"__builtin_isdigit\")) {
        if (!parse_builtin_unary(
                parser, MINIC_BUILTIN_UNARY_ISDIGIT, \"__builtin_isdigit\", &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        return finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
""",
    "parser builtin dispatch",
)
write(path, text)

# 3. Generalize typed ConstEval for the pure unary family.
path = "src/frontend/const_eval.c"
text = read(path)
new_eval = r'''static bool eval_builtin_unary(const MinicC0Program *program,
                               const MinicTargetInfo *target,
                               const MinicExpression *expression,
                               unsigned int depth,
                               MinicConstValue *value) {
    MinicConstValue operand;
    MinicBuiltinUnaryOperator operator_kind;
    uint64_t bits;
    uint64_t count;
    unsigned int width;

    if (program == NULL || target == NULL || expression == NULL || value == NULL ||
        expression->kind != MINIC_EXPRESSION_BUILTIN_UNARY ||
        !eval_expression(
            program, target, expression->value.builtin_unary.operand, depth + 1U, &operand) ||
        !minic_type_is_integer(operand.type) ||
        !integer_width(program, target, operand.type, &width) || width == 0U || width > 64U ||
        !normalize_bits(program, target, operand.type, operand.bits, &bits)) {
        return false;
    }

    operator_kind = expression->value.builtin_unary.operator_kind;
    count = 0U;
    switch (operator_kind) {
    case MINIC_BUILTIN_UNARY_CLZLL:
        if (bits == 0U) {
            return false;
        }
        while ((bits & (UINT64_C(1) << (width - 1U))) == 0U) {
            count += 1U;
            bits <<= 1U;
        }
        break;
    case MINIC_BUILTIN_UNARY_CTZL:
        if (bits == 0U) {
            return false;
        }
        while ((bits & UINT64_C(1)) == 0U) {
            count += 1U;
            bits >>= 1U;
        }
        break;
    case MINIC_BUILTIN_UNARY_FFSLL:
        if (bits == 0U) {
            count = 0U;
            break;
        }
        count = 1U;
        while ((bits & UINT64_C(1)) == 0U) {
            count += 1U;
            bits >>= 1U;
        }
        break;
    case MINIC_BUILTIN_UNARY_ISDIGIT:
        count = bits >= UINT64_C(48) && bits <= UINT64_C(57) ? 1U : 0U;
        break;
    default:
        return false;
    }

    value->type = expression->type;
    return normalize_bits(program, target, value->type, count, &value->bits);
}

static bool eval_binary'''
text = regex_once(
    text,
    r"static bool eval_builtin_unary\(.*?\n\}\n\nstatic bool eval_binary",
    new_eval,
    "consteval builtin family",
)
write(path, text)

# 4. Keep verifier as the single AST invariant owner for builtin operand types.
path = "src/frontend/ast_verifier.c"
text = read(path)
old_case = r'''    case MINIC_EXPRESSION_BUILTIN_UNARY: {
        const MinicExpression *builtin_operand;

        builtin_operand =
            expression_before(program, expression->value.builtin_unary.operand, expression_index);
        return expression->value.builtin_unary.operator_kind == MINIC_BUILTIN_UNARY_CLZLL &&
               builtin_operand != NULL &&
               minic_type_equal(builtin_operand->type, minic_type_unsigned_long_long()) &&
               expression->value_category == MINIC_VALUE_RVALUE &&
               minic_type_equal(expression->type, minic_type_int());
    }'''
new_case = r'''    case MINIC_EXPRESSION_BUILTIN_UNARY: {
        const MinicExpression *builtin_operand;
        MinicType expected_operand_type;

        builtin_operand =
            expression_before(program, expression->value.builtin_unary.operand, expression_index);
        switch (expression->value.builtin_unary.operator_kind) {
        case MINIC_BUILTIN_UNARY_CLZLL:
            expected_operand_type = minic_type_unsigned_long_long();
            break;
        case MINIC_BUILTIN_UNARY_CTZL:
            expected_operand_type = minic_type_unsigned_long();
            break;
        case MINIC_BUILTIN_UNARY_FFSLL:
            expected_operand_type = minic_type_long_long();
            break;
        case MINIC_BUILTIN_UNARY_ISDIGIT:
            expected_operand_type = minic_type_int();
            break;
        default:
            return false;
        }
        return builtin_operand != NULL &&
               minic_type_equal(builtin_operand->type, expected_operand_type) &&
               expression->value_category == MINIC_VALUE_RVALUE &&
               minic_type_equal(expression->type, minic_type_int());
    }'''
text = replace_once(text, old_case, new_case, "verifier builtin family")
write(path, text)

# 5. Extend existing RV64I builtin emitter without requiring Zbb.
path = "src/target/riscv64/codegen_expression.c"
text = read(path)
old_prefix = r'''    const MinicExpression *operand;

    if (file == NULL || program == NULL || expression == NULL ||
        expression->kind != MINIC_EXPRESSION_BUILTIN_UNARY ||
        expression->value.builtin_unary.operator_kind != MINIC_BUILTIN_UNARY_CLZLL ||
        !minic_type_equal(expression->type, minic_type_int())) {
        return false;
    }
    operand = minic_c0_program_expression(program, expression->value.builtin_unary.operand);
    if (operand == NULL || !minic_type_equal(operand->type, minic_type_unsigned_long_long()) ||
        !minic_riscv64_emit_expression(
            file, program, function, function_layout, expression->value.builtin_unary.operand)) {
        return false;
    }

'''
new_prefix = r'''    const MinicExpression *operand;
    MinicBuiltinUnaryOperator operator_kind;
    MinicType expected_operand_type;

    if (file == NULL || program == NULL || expression == NULL ||
        expression->kind != MINIC_EXPRESSION_BUILTIN_UNARY ||
        !minic_type_equal(expression->type, minic_type_int())) {
        return false;
    }
    operator_kind = expression->value.builtin_unary.operator_kind;
    switch (operator_kind) {
    case MINIC_BUILTIN_UNARY_CLZLL:
        expected_operand_type = minic_type_unsigned_long_long();
        break;
    case MINIC_BUILTIN_UNARY_CTZL:
        expected_operand_type = minic_type_unsigned_long();
        break;
    case MINIC_BUILTIN_UNARY_FFSLL:
        expected_operand_type = minic_type_long_long();
        break;
    case MINIC_BUILTIN_UNARY_ISDIGIT:
        expected_operand_type = minic_type_int();
        break;
    default:
        return false;
    }
    operand = minic_c0_program_expression(program, expression->value.builtin_unary.operand);
    if (operand == NULL || !minic_type_equal(operand->type, expected_operand_type) ||
        !minic_riscv64_emit_expression(
            file, program, function, function_layout, expression->value.builtin_unary.operand)) {
        return false;
    }

    switch (operator_kind) {
    case MINIC_BUILTIN_UNARY_CTZL:
        return fprintf(file,
                       "  beqz a0, .Lminic_ctzl_zero_%zu\n"
                       "  li t0, 0\n"
                       ".Lminic_ctzl_loop_%zu:\n"
                       "  andi t1, a0, 1\n"
                       "  bnez t1, .Lminic_ctzl_done_%zu\n"
                       "  addi t0, t0, 1\n"
                       "  srli a0, a0, 1\n"
                       "  j .Lminic_ctzl_loop_%zu\n"
                       ".Lminic_ctzl_zero_%zu:\n"
                       "  li t0, 64\n"
                       ".Lminic_ctzl_done_%zu:\n"
                       "  mv a0, t0\n",
                       expression_id,
                       expression_id,
                       expression_id,
                       expression_id,
                       expression_id,
                       expression_id) >= 0;
    case MINIC_BUILTIN_UNARY_FFSLL:
        return fprintf(file,
                       "  beqz a0, .Lminic_ffsll_zero_%zu\n"
                       "  li t0, 1\n"
                       ".Lminic_ffsll_loop_%zu:\n"
                       "  andi t1, a0, 1\n"
                       "  bnez t1, .Lminic_ffsll_done_%zu\n"
                       "  addi t0, t0, 1\n"
                       "  srli a0, a0, 1\n"
                       "  j .Lminic_ffsll_loop_%zu\n"
                       ".Lminic_ffsll_zero_%zu:\n"
                       "  li t0, 0\n"
                       ".Lminic_ffsll_done_%zu:\n"
                       "  mv a0, t0\n",
                       expression_id,
                       expression_id,
                       expression_id,
                       expression_id,
                       expression_id,
                       expression_id) >= 0;
    case MINIC_BUILTIN_UNARY_ISDIGIT:
        return fprintf(file, "  addi t0, a0, -48\n  sltiu a0, t0, 10\n") >= 0;
    case MINIC_BUILTIN_UNARY_CLZLL:
        break;
    default:
        return false;
    }

'''
text = replace_once(text, old_prefix, new_prefix, "rv64 builtin family")
write(path, text)

# 6. Focused semantic + runtime-lowering regression.
test_source = r'''_Static_assert(__builtin_ctzl(8UL) == 3, "ctzl eight");
_Static_assert(__builtin_ctzl(0x100UL) == 8, "ctzl 256");
_Static_assert(__builtin_ffsll(0LL) == 0, "ffsll zero");
_Static_assert(__builtin_ffsll(8LL) == 4, "ffsll eight");
_Static_assert(__builtin_ffsll(-1LL) == 1, "ffsll negative one");
_Static_assert(__builtin_isdigit('7') == 1, "isdigit digit");
_Static_assert(__builtin_isdigit('x') == 0, "isdigit nondigit");

static int runtime_ctzl(unsigned long value) {
    return __builtin_ctzl(value);
}

static int runtime_ffsll(long long value) {
    return __builtin_ffsll(value);
}

static int runtime_isdigit(int value) {
    return __builtin_isdigit(value);
}

int main(void) {
    return runtime_ctzl(8UL) == 3 && runtime_ffsll(8LL) == 4 &&
                   runtime_ffsll(0LL) == 0 && runtime_isdigit('7') &&
                   !runtime_isdigit('x')
               ? 0
               : 1;
}
'''
write("tests/compiler/c0/builtin_unary_family.c", test_source)

runner = r'''#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-builtin-unary-family

rm -rf "$work"
mkdir -p "$work"
"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/builtin_unary_family.c" \
    -o "$work/builtin_unary_family.i"
"$minic" -S "$work/builtin_unary_family.i" -o "$work/builtin_unary_family.s"
test -s "$work/builtin_unary_family.s"
grep -F 'runtime_ctzl:' "$work/builtin_unary_family.s" >/dev/null
grep -F 'runtime_ffsll:' "$work/builtin_unary_family.s" >/dev/null
grep -F 'runtime_isdigit:' "$work/builtin_unary_family.s" >/dev/null
grep -F '.Lminic_ctzl_loop_' "$work/builtin_unary_family.s" >/dev/null
grep -F '.Lminic_ffsll_loop_' "$work/builtin_unary_family.s" >/dev/null
grep -F '  addi t0, a0, -48' "$work/builtin_unary_family.s" >/dev/null
grep -F '  sltiu a0, t0, 10' "$work/builtin_unary_family.s" >/dev/null

cat >"$work/ctzl-zero.c" <<'EOF'
int invalid_bound[__builtin_ctzl(0UL)];
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/ctzl-zero.c" -o "$work/ctzl-zero.i"
if "$minic" -S "$work/ctzl-zero.i" -o "$work/ctzl-zero.s" 2>"$work/ctzl-zero.stderr"; then
    printf '%s\n' '__builtin_ctzl(0) unexpectedly accepted as an integer constant expression' >&2
    exit 1
fi
grep -F 'expected integer constant expression' "$work/ctzl-zero.stderr" >/dev/null

printf '%s\n' 'PASS compiler/c0/builtin_unary_family ast=shared-unary-builtin consteval=typed ctzl=rv64i-loop ffsll=rv64i-loop isdigit=range-check zero-ctz=fail-closed'
'''
write("tests/compiler/c0/run-builtin-unary-family.sh", runner)

# Keep the broad C0 suite responsible for the focused regression too.
path = "tests/compiler/c0/run.sh"
text = read(path)
invocation = r'''
MINIC="$minic" \
HOST_CC="$host_cc" \
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
sh "$root/tests/compiler/c0/run-builtin-unary-family.sh"
'''
if "run-builtin-unary-family.sh" not in text:
    text = text.rstrip() + "\n" + invocation + "\n"
write(path, text)
