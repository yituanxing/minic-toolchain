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
    "src/frontend/const_eval.c",
    '''static bool eval_expression(const MinicC0Program *program,\n                            const MinicTargetInfo *target,\n                            MinicExpressionId expression_id,\n                            unsigned int depth,\n                            MinicConstValue *value);\n\nstatic bool eval_binary''',
    '''static bool eval_expression(const MinicC0Program *program,\n                            const MinicTargetInfo *target,\n                            MinicExpressionId expression_id,\n                            unsigned int depth,\n                            MinicConstValue *value);\n\nstatic bool eval_builtin_unary(const MinicC0Program *program,\n                               const MinicTargetInfo *target,\n                               const MinicExpression *expression,\n                               unsigned int depth,\n                               MinicConstValue *value) {\n    MinicConstValue operand;\n    uint64_t bits;\n    uint64_t count;\n    unsigned int width;\n\n    if (program == NULL || target == NULL || expression == NULL || value == NULL ||\n        expression->kind != MINIC_EXPRESSION_BUILTIN_UNARY ||\n        expression->value.builtin_unary.operator_kind != MINIC_BUILTIN_UNARY_CLZLL ||\n        !eval_expression(\n            program, target, expression->value.builtin_unary.operand, depth + 1U, &operand) ||\n        !minic_type_is_integer(operand.type) ||\n        !integer_width(program, target, operand.type, &width) || width == 0U || width > 64U ||\n        !normalize_bits(program, target, operand.type, operand.bits, &bits) || bits == 0U) {\n        return false;\n    }\n\n    count = 0U;\n    while ((bits & (UINT64_C(1) << (width - 1U))) == 0U) {\n        count += 1U;\n        bits <<= 1U;\n    }\n    value->type = expression->type;\n    return normalize_bits(program, target, value->type, count, &value->bits);\n}\n\nstatic bool eval_binary''',
    "typed clzll consteval helper",
)

replace_once(
    "src/frontend/const_eval.c",
    '''    case MINIC_EXPRESSION_BINARY:\n        return eval_binary(program, target, expression, depth, value);\n    case MINIC_EXPRESSION_CONDITIONAL: {\n''',
    '''    case MINIC_EXPRESSION_BUILTIN_UNARY:\n        return eval_builtin_unary(program, target, expression, depth, value);\n    case MINIC_EXPRESSION_BINARY:\n        return eval_binary(program, target, expression, depth, value);\n    case MINIC_EXPRESSION_CONDITIONAL: {\n''',
    "dispatch builtin unary consteval",
)

replace_once(
    "tests/compiler/c0/builtin_clzll.c",
    '''static int runtime_clzll_ull(unsigned long long value) {\n''',
    '''_Static_assert(__builtin_clzll(1ULL) == 63, "clzll one");\n_Static_assert(__builtin_clzll(16ULL) == 59, "clzll sixteen");\n_Static_assert(__builtin_clzll(0x8000000000000000ULL) == 0, "clzll top bit");\n\nstruct MutexLike {\n    long state;\n};\n\nstruct LinuxShape {\n    struct MutexLike open_file_mutex[\n        1 << (2 * (__builtin_constant_p(64 < 32 ? 64 : 32)\n                       ? ((64 < 32 ? 64 : 32) < 2\n                              ? 0\n                              : 63 - __builtin_clzll(64 < 32 ? 64 : 32))\n                       : 0))];\n};\n\n_Static_assert(sizeof(((struct LinuxShape *)0)->open_file_mutex) / sizeof(struct MutexLike) == 1024,\n               "Linux kernfs lock array bound");\n\nstatic int runtime_clzll_ull(unsigned long long value) {\n''',
    "clzll typed consteval regressions",
)

replace_once(
    "tests/compiler/c0/run-builtin-clzll.sh",
    '''if grep -Eq '(^|[[:space:]])clz([[:space:]]|$)' "$work/builtin_clzll.s"; then\n    printf '%s\\n' 'unexpected Zbb clz dependency' >&2\n    exit 1\nfi\nprintf '%s\\n' 'PASS compiler/c0/builtin_clzll ast=unary-builtin argument=ull-normalized runtime-lowering=rv64i-binary-search consteval=deferred zbb=none'\n''',
    '''if grep -Eq '(^|[[:space:]])clz([[:space:]]|$)' "$work/builtin_clzll.s"; then\n    printf '%s\\n' 'unexpected Zbb clz dependency' >&2\n    exit 1\nfi\n\ncat >"$work/clzll-zero.c" <<'EOF'\nint invalid_bound[__builtin_clzll(0ULL)];\nEOF\n"$host_cc" -E -P -std=gnu11 -x c "$work/clzll-zero.c" -o "$work/clzll-zero.i"\nif "$minic" -S "$work/clzll-zero.i" -o "$work/clzll-zero.s" 2>"$work/clzll-zero.stderr"; then\n    printf '%s\\n' '__builtin_clzll(0) unexpectedly accepted as an integer constant expression' >&2\n    exit 1\nfi\ngrep -F 'expected integer constant expression' "$work/clzll-zero.stderr" >/dev/null\n\nprintf '%s\\n' 'PASS compiler/c0/builtin_clzll ast=unary-builtin argument=ull-normalized runtime-lowering=rv64i-binary-search typed-consteval=nonzero linux-array-bound=1 zero=fail-closed zbb=none'\n''',
    "clzll consteval test contract",
)
