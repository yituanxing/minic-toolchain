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
    '''    MINIC_EXPRESSION_CALL,\n    MINIC_EXPRESSION_STATEMENT,\n    MINIC_EXPRESSION_BUILTIN_OVERFLOW\n''',
    '''    MINIC_EXPRESSION_CALL,\n    MINIC_EXPRESSION_STATEMENT,\n    MINIC_EXPRESSION_BUILTIN_UNARY,\n    MINIC_EXPRESSION_BUILTIN_OVERFLOW\n''',
    "ast-builtin-unary-kind",
)
text = replace_once(
    text,
    '''typedef enum MinicOverflowOperator {\n''',
    '''typedef enum MinicBuiltinUnaryOperator {\n    MINIC_BUILTIN_UNARY_CLZLL = 0\n} MinicBuiltinUnaryOperator;\n\ntypedef enum MinicOverflowOperator {\n''',
    "ast-builtin-unary-operator",
)
text = replace_once(
    text,
    '''        struct {\n            MinicOverflowOperator operator_kind;\n            MinicExpressionId left;\n''',
    '''        struct {\n            MinicBuiltinUnaryOperator operator_kind;\n            MinicExpressionId operand;\n        } builtin_unary;\n        struct {\n            MinicOverflowOperator operator_kind;\n            MinicExpressionId left;\n''',
    "ast-builtin-unary-payload",
)
path.write_text(text)

path = root / "src/frontend/parser_expression.c"
text = path.read_text()
anchor = '''static bool parse_builtin_overflow(MinicParser *parser,\n'''
addition = '''static bool parse_builtin_unary(MinicParser *parser,\n                                MinicBuiltinUnaryOperator operator_kind,\n                                const char *spelling,\n                                MinicExpressionId *expression_id) {\n    MinicExpression expression;\n    const MinicExpression *operand;\n    MinicExpressionId operand_id;\n    MinicSourcePosition begin;\n\n    if (parser == NULL || spelling == NULL || expression_id == NULL ||\n        !generic_token_text_equals(parser, spelling)) {\n        return false;\n    }\n    begin = parser->current.span.begin;\n    if (!minic_parser_advance(parser) ||\n        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after unary builtin") ||\n        !parse_expression_internal(parser, &operand_id, 0U, true)) {\n        return false;\n    }\n    operand = minic_c0_program_expression(parser->program, operand_id);\n    if (operand == NULL || !minic_type_is_integer(operand->type)) {\n        minic_parser_error(parser, "unary builtin requires an integer operand");\n        return false;\n    }\n    if (parser->current.kind != MINIC_TOKEN_RPAREN) {\n        minic_parser_error(parser, "expected ')' after unary builtin operand");\n        return false;\n    }\n\n    (void)memset(&expression, 0, sizeof(expression));\n    expression.kind = MINIC_EXPRESSION_BUILTIN_UNARY;\n    expression.span.begin = begin;\n    expression.span.end = parser->current.span.end;\n    expression.type = minic_type_int();\n    expression.value_category = MINIC_VALUE_RVALUE;\n    expression.value.builtin_unary.operator_kind = operator_kind;\n    expression.value.builtin_unary.operand = operand_id;\n    return minic_parser_advance(parser) &&\n           minic_parser_add_expression(parser, &expression, expression_id);\n}\n\n'''
text = replace_once(text, anchor, addition + anchor, "parse-builtin-unary-helper")
text = replace_once(
    text,
    '''    if (generic_token_text_equals(parser, "__builtin_constant_p")) {\n''',
    '''    if (generic_token_text_equals(parser, "__builtin_clzll")) {\n        if (!parse_builtin_unary(\n                parser, MINIC_BUILTIN_UNARY_CLZLL, "__builtin_clzll", &primary_id) ||\n            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {\n            return false;\n        }\n        return finish_value_expression(parser, primary_id, decay_array, expression_id);\n    }\n    if (generic_token_text_equals(parser, "__builtin_constant_p")) {\n''',
    "parse-primary-clzll",
)
text = replace_once(
    text,
    '''    if (expression->kind == MINIC_EXPRESSION_BINARY) {\n''',
    '''    if (expression->kind == MINIC_EXPRESSION_BUILTIN_UNARY) {\n        int64_t operand;\n        uint64_t bits;\n        int count;\n\n        if (expression->value.builtin_unary.operator_kind != MINIC_BUILTIN_UNARY_CLZLL ||\n            !minic_parser_evaluate_integer_constant_expression(\n                program, expression->value.builtin_unary.operand, &operand)) {\n            return false;\n        }\n        bits = (uint64_t)operand;\n        if (bits == 0U) {\n            return false;\n        }\n        count = 0;\n        while ((bits & UINT64_C(0x8000000000000000)) == 0U) {\n            bits <<= 1U;\n            count += 1;\n        }\n        *value = count;\n        return true;\n    }\n    if (expression->kind == MINIC_EXPRESSION_BINARY) {\n''',
    "consteval-builtin-clzll",
)
path.write_text(text)

path = root / "src/frontend/ast_verifier.c"
text = path.read_text()
text = replace_once(
    text,
    '''    case MINIC_EXPRESSION_BUILTIN_OVERFLOW: {\n''',
    '''    case MINIC_EXPRESSION_BUILTIN_UNARY: {\n        const MinicExpression *operand;\n\n        operand = expression_before(\n            program, expression->value.builtin_unary.operand, expression_index);\n        return expression->value.builtin_unary.operator_kind == MINIC_BUILTIN_UNARY_CLZLL &&\n               operand != NULL && minic_type_is_integer(operand->type) &&\n               expression->value_category == MINIC_VALUE_RVALUE &&\n               minic_type_equal(expression->type, minic_type_int());\n    }\n    case MINIC_EXPRESSION_BUILTIN_OVERFLOW: {\n''',
    "verify-builtin-unary",
)
path.write_text(text)

path = root / "src/frontend/cast_normalization.c"
text = path.read_text()
text = replace_once(
    text,
    '''    case MINIC_EXPRESSION_BUILTIN_OVERFLOW:\n''',
    '''    case MINIC_EXPRESSION_BUILTIN_UNARY:\n        return remap_expression_id(mapping,\n                                   old_expression_count,\n                                   current_old_index,\n                                   expression->value.builtin_unary.operand,\n                                   &expression->value.builtin_unary.operand);\n    case MINIC_EXPRESSION_BUILTIN_OVERFLOW:\n''',
    "normalize-builtin-unary",
)
path.write_text(text)

path = root / "src/target/riscv64/codegen_expression.c"
text = path.read_text()
anchor = '''static bool minic_riscv64_emit_overflow_builtin(FILE *file,\n'''
addition = '''static bool minic_riscv64_emit_builtin_unary(FILE *file,\n                                             const MinicC0Program *program,\n                                             const MinicFunction *function,\n                                             const MinicExpression *expression,\n                                             MinicExpressionId expression_id) {\n    const MinicExpression *operand;\n\n    if (file == NULL || program == NULL || expression == NULL ||\n        expression->kind != MINIC_EXPRESSION_BUILTIN_UNARY ||\n        expression->value.builtin_unary.operator_kind != MINIC_BUILTIN_UNARY_CLZLL ||\n        !minic_type_equal(expression->type, minic_type_int())) {\n        return false;\n    }\n    operand = minic_c0_program_expression(program, expression->value.builtin_unary.operand);\n    if (operand == NULL || !minic_type_is_integer(operand->type) ||\n        !minic_riscv64_emit_expression(\n            file, program, function, expression->value.builtin_unary.operand) ||\n        !minic_riscv64_emit_integer_conversion(file, minic_type_unsigned_long_long(), "a0")) {\n        return false;\n    }\n\n    /* GCC defines __builtin_clzll(0) as undefined. For non-zero values this\n     * baseline RV64I binary search computes the exact count without requiring\n     * the optional Zbb clz instruction. */\n    return fprintf(file,\n                   "  li t0, 0\\n"\n                   "  srli t1, a0, 32\\n"\n                   "  bnez t1, .Lminic_clzll_32_%zu\\n"\n                   "  addi t0, t0, 32\\n"\n                   "  slli a0, a0, 32\\n"\n                   ".Lminic_clzll_32_%zu:\\n"\n                   "  srli t1, a0, 48\\n"\n                   "  bnez t1, .Lminic_clzll_16_%zu\\n"\n                   "  addi t0, t0, 16\\n"\n                   "  slli a0, a0, 16\\n"\n                   ".Lminic_clzll_16_%zu:\\n"\n                   "  srli t1, a0, 56\\n"\n                   "  bnez t1, .Lminic_clzll_8_%zu\\n"\n                   "  addi t0, t0, 8\\n"\n                   "  slli a0, a0, 8\\n"\n                   ".Lminic_clzll_8_%zu:\\n"\n                   "  srli t1, a0, 60\\n"\n                   "  bnez t1, .Lminic_clzll_4_%zu\\n"\n                   "  addi t0, t0, 4\\n"\n                   "  slli a0, a0, 4\\n"\n                   ".Lminic_clzll_4_%zu:\\n"\n                   "  srli t1, a0, 62\\n"\n                   "  bnez t1, .Lminic_clzll_2_%zu\\n"\n                   "  addi t0, t0, 2\\n"\n                   "  slli a0, a0, 2\\n"\n                   ".Lminic_clzll_2_%zu:\\n"\n                   "  srli t1, a0, 63\\n"\n                   "  bnez t1, .Lminic_clzll_1_%zu\\n"\n                   "  addi t0, t0, 1\\n"\n                   ".Lminic_clzll_1_%zu:\\n"\n                   "  mv a0, t0\\n",\n                   expression_id,\n                   expression_id,\n                   expression_id,\n                   expression_id,\n                   expression_id,\n                   expression_id,\n                   expression_id,\n                   expression_id,\n                   expression_id,\n                   expression_id,\n                   expression_id,\n                   expression_id) >= 0;\n}\n\n'''
text = replace_once(text, anchor, addition + anchor, "rv64-builtin-unary-helper")
text = replace_once(
    text,
    '''    case MINIC_EXPRESSION_BUILTIN_OVERFLOW:\n        return minic_riscv64_emit_overflow_builtin(file, program, function, expression);\n''',
    '''    case MINIC_EXPRESSION_BUILTIN_UNARY:\n        return minic_riscv64_emit_builtin_unary(\n            file, program, function, expression, expression_id);\n    case MINIC_EXPRESSION_BUILTIN_OVERFLOW:\n        return minic_riscv64_emit_overflow_builtin(file, program, function, expression);\n''',
    "rv64-builtin-unary-switch",
)
path.write_text(text)

path = root / "tests/compiler/c0/builtin_clzll.c"
path.write_text('''static int runtime_clzll(unsigned long long value) {\n    return __builtin_clzll(value);\n}\n\n_Static_assert(__builtin_clzll(1ULL) == 63, "clzll one");\n_Static_assert(__builtin_clzll(16ULL) == 59, "clzll sixteen");\n\nint main(void) {\n    return runtime_clzll(1ULL) == 63 &&\n                   runtime_clzll(16ULL) == 59 &&\n                   runtime_clzll(0x8000000000000000ULL) == 0 &&\n                   runtime_clzll(0x00f0000000000000ULL) == 8\n               ? 0\n               : 1;\n}\n''')

path = root / "tests/compiler/c0/run-builtin-clzll.sh"
path.write_text('''#!/bin/sh\nset -eu\n\nroot=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)\nminic=${MINIC:-"$root/build/debug/bin/minic"}\nhost_cc=${HOST_CC:-${CC:-cc}}\nwork=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-builtin-clzll\n\nrm -rf "$work"\nmkdir -p "$work"\n"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/builtin_clzll.c" -o "$work/builtin_clzll.i"\n"$minic" -S "$work/builtin_clzll.i" -o "$work/builtin_clzll.s"\ntest -s "$work/builtin_clzll.s"\ngrep -F 'runtime_clzll:' "$work/builtin_clzll.s" >/dev/null\ngrep -F '.Lminic_clzll_32_' "$work/builtin_clzll.s" >/dev/null\ngrep -F '  srli t1, a0, 32' "$work/builtin_clzll.s" >/dev/null\nif grep -Eq '(^|[[:space:]])clz([[:space:]]|$)' "$work/builtin_clzll.s"; then\n    printf '%s\\n' 'unexpected Zbb clz dependency' >&2\n    exit 1\nfi\nprintf '%s\\n' 'PASS compiler/c0/builtin_clzll ast=unary-builtin consteval=1 runtime-lowering=rv64i-binary-search zbb=none'\n''')

path = root / "tests/compiler/c0/run-builtin-clzll-rv64.sh"
path.write_text('''#!/bin/sh\nset -eu\n\nroot=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)\nminic=${MINIC:-"$root/build/debug/bin/minic"}\nriscv_cc=${RISCV_CC:-riscv64-linux-gnu-gcc}\nqemu=${QEMU_RISCV64:-qemu-riscv64}\nwork=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-builtin-clzll-rv64\nsource="$root/tests/compiler/c0/builtin_clzll.c"\n\nrm -rf "$work"\nmkdir -p "$work"\n"$riscv_cc" -E -P -std=gnu11 -x c "$source" -o "$work/probe.i"\n"$minic" -S "$work/probe.i" -o "$work/minic.s"\nif grep -Eq '(^|[[:space:]])clz([[:space:]]|$)' "$work/minic.s"; then\n    printf '%s\\n' 'unexpected Zbb clz dependency' >&2\n    exit 1\nfi\n"$riscv_cc" -static "$work/minic.s" -o "$work/minic.elf"\n"$riscv_cc" -static -std=gnu11 "$source" -o "$work/gcc.elf"\n\nset +e\n"$qemu" "$work/gcc.elf"\ngcc_status=$?\n"$qemu" "$work/minic.elf"\nminic_status=$?\nset -e\nif test "$gcc_status" -ne 0 || test "$minic_status" -ne "$gcc_status"; then\n    printf '%s\\n' "FAIL compiler/c0/builtin_clzll_rv64 gcc=$gcc_status minic=$minic_status" >&2\n    exit 1\nfi\nprintf '%s\\n' 'PASS compiler/c0/builtin_clzll_rv64 gcc=minic runtime-values=1,16,highbit,midbyte baseline=rv64i qemu=1'\n''')

path = root / "tools/dev/pr76-focused.sh"
text = path.read_text()
text = replace_once(
    text,
    '''sh tests/compiler/c0/run-static-assert-declaration.sh\n''',
    '''sh tests/compiler/c0/run-static-assert-declaration.sh\nsh tests/compiler/c0/run-builtin-clzll.sh\n''',
    "focused-clzll-gate",
)
path.write_text(text)
