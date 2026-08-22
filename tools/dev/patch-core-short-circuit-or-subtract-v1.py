from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    file = Path(path)
    text = file.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"expected exactly one anchor in {path}, found {count}")
    file.write_text(text.replace(old, new, 1))


# Third real checked-overflow consumer: extend the already-proven tiny Core op enum.
replace_once(
    "src/core/core_ir.h",
    "    MINIC_CORE_INTEGER_OVERFLOW_ADD = 0,\n    MINIC_CORE_INTEGER_OVERFLOW_MULTIPLY",
    "    MINIC_CORE_INTEGER_OVERFLOW_ADD = 0,\n"
    "    MINIC_CORE_INTEGER_OVERFLOW_SUBTRACT,\n"
    "    MINIC_CORE_INTEGER_OVERFLOW_MULTIPLY",
)
replace_once(
    "src/core/core_ir.c",
    "            (instruction->value.integer_overflow.operator_kind != MINIC_CORE_INTEGER_OVERFLOW_ADD &&\n"
    "             instruction->value.integer_overflow.operator_kind !=\n"
    "                 MINIC_CORE_INTEGER_OVERFLOW_MULTIPLY) ||",
    "            (instruction->value.integer_overflow.operator_kind != MINIC_CORE_INTEGER_OVERFLOW_ADD &&\n"
    "             instruction->value.integer_overflow.operator_kind !=\n"
    "                 MINIC_CORE_INTEGER_OVERFLOW_SUBTRACT &&\n"
    "             instruction->value.integer_overflow.operator_kind !=\n"
    "                 MINIC_CORE_INTEGER_OVERFLOW_MULTIPLY) ||",
)
replace_once(
    "src/core/core_ir.c",
    "        const char *operator_name =\n"
    "            instruction->value.integer_overflow.operator_kind == MINIC_CORE_INTEGER_OVERFLOW_ADD\n"
    "                ? \"add\"\n"
    "                : \"mul\";",
    "        const char *operator_name =\n"
    "            instruction->value.integer_overflow.operator_kind == MINIC_CORE_INTEGER_OVERFLOW_ADD\n"
    "                ? \"add\"\n"
    "                : instruction->value.integer_overflow.operator_kind ==\n"
    "                          MINIC_CORE_INTEGER_OVERFLOW_SUBTRACT\n"
    "                      ? \"sub\"\n"
    "                      : \"mul\";",
)

# AST -> Core checked overflow: ADD/SUB/MUL only, each proven by a real consumer.
replace_once(
    "src/core/core_lower.c",
    "        (expression->value.overflow.operator_kind == MINIC_OVERFLOW_ADD ||\n"
    "         expression->value.overflow.operator_kind == MINIC_OVERFLOW_MULTIPLY)) {",
    "        (expression->value.overflow.operator_kind == MINIC_OVERFLOW_ADD ||\n"
    "         expression->value.overflow.operator_kind == MINIC_OVERFLOW_SUBTRACT ||\n"
    "         expression->value.overflow.operator_kind == MINIC_OVERFLOW_MULTIPLY)) {",
)
replace_once(
    "src/core/core_lower.c",
    "        instruction.value.integer_overflow.operator_kind =\n"
    "            expression->value.overflow.operator_kind == MINIC_OVERFLOW_ADD\n"
    "                ? MINIC_CORE_INTEGER_OVERFLOW_ADD\n"
    "                : MINIC_CORE_INTEGER_OVERFLOW_MULTIPLY;",
    "        instruction.value.integer_overflow.operator_kind =\n"
    "            expression->value.overflow.operator_kind == MINIC_OVERFLOW_ADD\n"
    "                ? MINIC_CORE_INTEGER_OVERFLOW_ADD\n"
    "                : expression->value.overflow.operator_kind == MINIC_OVERFLOW_SUBTRACT\n"
    "                      ? MINIC_CORE_INTEGER_OVERFLOW_SUBTRACT\n"
    "                      : MINIC_CORE_INTEGER_OVERFLOW_MULTIPLY;",
)

# Short-circuit OR is CFG, not a value instruction. Keep this helper narrow: OR plus atomic scalar conditions.
set_branch_anchor = """static MinicCoreLowerStatus
lower_if(MinicCoreLowerContext *context, const MinicStatement *statement, bool *terminated) {
"""
helper = """static MinicCoreLowerStatus lower_condition_branch(MinicCoreLowerContext *context,
                                                         MinicExpressionId expression_id,
                                                         MinicSourceSpan span,
                                                         MinicCoreBlockId when_true,
                                                         MinicCoreBlockId when_false) {
    const MinicExpression *expression;
    MinicCoreBlockId condition_block;
    MinicCoreTerminator terminator;
    MinicCoreValueId condition;
    MinicCoreLowerStatus status;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || expression_id == MINIC_EXPRESSION_INVALID ||
        when_true == MINIC_CORE_BLOCK_INVALID || when_false == MINIC_CORE_BLOCK_INVALID) {
        return MINIC_CORE_LOWER_ERROR;
    }
    expression = minic_c0_program_expression(context->body->program, expression_id);
    if (expression == NULL || !minic_type_is_integer(expression->type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_OR) {
        MinicCoreBlockId right_block;

        if (!minic_core_function_add_block(context->function, &right_block)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = lower_condition_branch(
            context, expression->value.binary.left, span, when_true, right_block);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        context->block_id = right_block;
        return lower_condition_branch(
            context, expression->value.binary.right, span, when_true, when_false);
    }

    condition_block = context->block_id;
    status = lower_expression(context, expression_id, &condition);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    if (condition >= context->function->value_count ||
        !minic_type_is_integer(context->function->values[condition].type)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    (void)memset(&terminator, 0, sizeof(terminator));
    terminator.kind = MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH;
    terminator.span = span;
    terminator.return_value = MINIC_CORE_VALUE_INVALID;
    terminator.conditional.condition = condition;
    terminator.conditional.when_true = when_true;
    terminator.conditional.when_false = when_false;
    return minic_core_function_set_terminator(context->function, condition_block, &terminator)
               ? MINIC_CORE_LOWER_OK
               : MINIC_CORE_LOWER_ERROR;
}

static MinicCoreLowerStatus
lower_if(MinicCoreLowerContext *context, const MinicStatement *statement, bool *terminated) {
"""
replace_once("src/core/core_lower.c", set_branch_anchor, helper)

replace_once(
    "src/core/core_lower.c",
    "    MinicCoreBlockId then_block;\n"
    "    MinicCoreTerminator terminator;\n"
    "    MinicCoreValueId condition;\n"
    "    MinicCoreLowerStatus status;",
    "    MinicCoreBlockId then_block;\n"
    "    MinicCoreBlockId continuation_block;\n"
    "    MinicCoreLowerStatus status;",
)
replace_once(
    "src/core/core_lower.c",
    "    condition_block = context->block_id;\n"
    "    status = lower_expression(context, statement->expression, &condition);\n"
    "    if (status != MINIC_CORE_LOWER_OK) {\n"
    "        return status;\n"
    "    }\n"
    "    if (!minic_type_is_integer(context->function->values[condition].type) ||\n"
    "        !minic_core_function_add_block(context->function, &then_block)) {\n"
    "        return MINIC_CORE_LOWER_ERROR;\n"
    "    }",
    "    condition_block = context->block_id;\n"
    "    if (!minic_core_function_add_block(context->function, &then_block)) {\n"
    "        return MINIC_CORE_LOWER_ERROR;\n"
    "    }",
)
old_tail = """    (void)memset(&terminator, 0, sizeof(terminator));
    terminator.kind = MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH;
    terminator.span = statement->span;
    terminator.return_value = MINIC_CORE_VALUE_INVALID;
    terminator.conditional.condition = condition;
    terminator.conditional.when_true = then_block;
    terminator.conditional.when_false = false_target;
    if (!minic_core_function_set_terminator(context->function, condition_block, &terminator)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    *terminated = !needs_merge;
    return MINIC_CORE_LOWER_OK;
}
"""
new_tail = """    continuation_block = context->block_id;
    context->block_id = condition_block;
    status = lower_condition_branch(
        context, statement->expression, statement->span, then_block, false_target);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    context->block_id = continuation_block;
    *terminated = !needs_merge;
    return MINIC_CORE_LOWER_OK;
}
"""
replace_once("src/core/core_lower.c", old_tail, new_tail)

# RV64 checked subtract.
replace_once(
    "src/target/riscv64/core_codegen.c",
    "        (instruction->value.integer_overflow.operator_kind != MINIC_CORE_INTEGER_OVERFLOW_ADD &&\n"
    "         instruction->value.integer_overflow.operator_kind !=\n"
    "             MINIC_CORE_INTEGER_OVERFLOW_MULTIPLY) ||",
    "        (instruction->value.integer_overflow.operator_kind != MINIC_CORE_INTEGER_OVERFLOW_ADD &&\n"
    "         instruction->value.integer_overflow.operator_kind !=\n"
    "             MINIC_CORE_INTEGER_OVERFLOW_SUBTRACT &&\n"
    "         instruction->value.integer_overflow.operator_kind !=\n"
    "             MINIC_CORE_INTEGER_OVERFLOW_MULTIPLY) ||",
)
marker = """        } else if (result_size < 8U) {
            if (fprintf(file, "  mul t2, t0, t1\\n  mv t4, t2\\n") < 0 ||
"""
sub_branch = """        } else if (instruction->value.integer_overflow.operator_kind ==
                   MINIC_CORE_INTEGER_OVERFLOW_SUBTRACT) {
            if (result_size < 8U) {
                if (fprintf(file, "  sub t2, t0, t1\\n  mv t4, t2\\n") < 0 ||
                    !minic_riscv64_emit_integer_conversion_for_program(
                        file, program, result_type, "t2") ||
                    fprintf(file, "  xor t4, t4, t2\\n  snez t4, t4\\n") < 0) {
                    return false;
                }
            } else if (is_unsigned) {
                if (fprintf(file,
                            "  sub t2, t0, t1\\n"
                            "  sltu t4, t0, t1\\n") < 0) {
                    return false;
                }
            } else if (fprintf(file,
                               "  sub t2, t0, t1\\n"
                               "  xor t4, t0, t1\\n"
                               "  xor t5, t0, t2\\n"
                               "  and t4, t4, t5\\n"
                               "  srli t4, t4, 63\\n") < 0) {
                return false;
            }
        } else if (result_size < 8U) {
            if (fprintf(file, "  mul t2, t0, t1\\n  mv t4, t2\\n") < 0 ||
"""
replace_once("src/target/riscv64/core_codegen.c", marker, sub_branch)

# Focused short-circuit OR runtime contract. RHS is external so side effects are observable.
Path("tests/compiler/c0/core_short_circuit_or.c").write_text("""int core_m9_rhs(void);

int core_m9_short_circuit_or(int left) {
    if (left || core_m9_rhs())
        return 7;
    return 3;
}
""")
Path("tests/compiler/c0/core_short_circuit_or_runtime.c").write_text("""#include <stdio.h>

int core_m9_short_circuit_or(int left);
static int rhs_calls;

int core_m9_rhs(void) {
    ++rhs_calls;
    return 0;
}

int main(void) {
    int first = core_m9_short_circuit_or(1);
    int calls_after_true = rhs_calls;
    int second = core_m9_short_circuit_or(0);
    printf("%d %d %d %d\\n", first, calls_after_true, second, rhs_calls);
    return 0;
}
""")
Path("tests/compiler/c0/run-core-short-circuit-or.sh").write_text("""#!/bin/sh
set -eu
: "${MINIC:?MINIC must point to the compiler binary}"
: "${RISCV_CC:=riscv64-linux-gnu-gcc}"
: "${QEMU_RISCV64:=qemu-riscv64}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work="${BUILD_DIR:-$root/build/core-short-circuit-or}"
source_file="$root/tests/compiler/c0/core_short_circuit_or.c"
runtime_file="$root/tests/compiler/c0/core_short_circuit_or_runtime.c"
mkdir -p "$work"
cc -E -P -std=gnu11 "$source_file" -o "$work/core_short_circuit_or.i"
MINIC_CORE_IR=strict "$MINIC" -S "$work/core_short_circuit_or.i" -o "$work/strict.s"
MINIC_CORE_CODEGEN=basic-v0 "$MINIC" -S "$work/core_short_circuit_or.i" -o "$work/core.s"
"$RISCV_CC" -static -O2 "$source_file" "$runtime_file" -o "$work/reference-rv64"
"$RISCV_CC" -static -O2 "$runtime_file" "$work/core.s" -o "$work/minic-rv64"
"$QEMU_RISCV64" "$work/reference-rv64" >"$work/reference.out"
"$QEMU_RISCV64" "$work/minic-rv64" >"$work/minic.out"
cmp "$work/reference.out" "$work/minic.out"
printf '%s\\n' 'PASS compiler/c0/core-short-circuit-or'
""")

# Focused checked subtract, including the exact size_sub semantic shape.
Path("tests/compiler/c0/core_integer_subtract_overflow.c").write_text("""int core_m9_checked_int(int left, int right, int *result) {
    return __builtin_sub_overflow(left, right, result);
}

long core_m9_checked_long(long left, long right, long *result) {
    return __builtin_sub_overflow(left, right, result);
}

unsigned int core_m9_checked_uint(unsigned int left, unsigned int right, unsigned int *result) {
    return __builtin_sub_overflow(left, right, result);
}

unsigned long
core_m9_checked_ulong(unsigned long left, unsigned long right, unsigned long *result) {
    return __builtin_sub_overflow(left, right, result);
}

unsigned long core_m9_size_sub(unsigned long left, unsigned long right) {
    unsigned long bytes;
    if (left == ~0UL || right == ~0UL || __builtin_sub_overflow(left, right, &bytes))
        return ~0UL;
    return bytes;
}
""")
Path("tests/compiler/c0/core_integer_subtract_overflow_runtime.c").write_text("""#include <limits.h>
#include <stdio.h>
int core_m9_checked_int(int, int, int *);
long core_m9_checked_long(long, long, long *);
unsigned int core_m9_checked_uint(unsigned int, unsigned int, unsigned int *);
unsigned long core_m9_checked_ulong(unsigned long, unsigned long, unsigned long *);
unsigned long core_m9_size_sub(unsigned long, unsigned long);
int main(void) {
    int si = 0; long sl = 0; unsigned int ui = 0; unsigned long ul = 0;
    int oi = core_m9_checked_int(INT_MIN, 1, &si);
    int ol = core_m9_checked_long(LONG_MAX, -1L, &sl);
    int oui = core_m9_checked_uint(0U, 1U, &ui);
    int oul = core_m9_checked_ulong(0UL, 1UL, &ul);
    printf("%d %d %d %u %d %ld %d %lu %lu %lu %lu\\n",
           oi, si, oui, ui, ol, sl, oul, ul,
           core_m9_size_sub(~0UL, 1UL), core_m9_size_sub(1UL, ~0UL),
           core_m9_size_sub(42UL, 2UL));
    return 0;
}
""")
Path("tests/compiler/c0/run-core-integer-subtract-overflow.sh").write_text("""#!/bin/sh
set -eu
: "${MINIC:?MINIC must point to the compiler binary}"
: "${RISCV_CC:=riscv64-linux-gnu-gcc}"
: "${QEMU_RISCV64:=qemu-riscv64}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work="${BUILD_DIR:-$root/build/core-integer-subtract-overflow}"
source_file="$root/tests/compiler/c0/core_integer_subtract_overflow.c"
runtime_file="$root/tests/compiler/c0/core_integer_subtract_overflow_runtime.c"
mkdir -p "$work"
cc -E -P -std=gnu11 "$source_file" -o "$work/input.i"
MINIC_CORE_IR=strict "$MINIC" -S "$work/input.i" -o "$work/strict.s"
MINIC_CORE_CODEGEN=basic-v0 "$MINIC" -S "$work/input.i" -o "$work/core.s"
grep -q 'sltu t4, t0, t1' "$work/core.s"
grep -q 'sub t2, t0, t1' "$work/core.s"
"$RISCV_CC" -static -O2 "$source_file" "$runtime_file" -o "$work/reference-rv64"
"$RISCV_CC" -static -O2 "$runtime_file" "$work/core.s" -o "$work/minic-rv64"
"$QEMU_RISCV64" "$work/reference-rv64" >"$work/reference.out"
"$QEMU_RISCV64" "$work/minic-rv64" >"$work/minic.out"
cmp "$work/reference.out" "$work/minic.out"
printf '%s\\n' 'PASS compiler/c0/core-integer-subtract-overflow'
""")

# Permanent C0 gate registration for both new semantics.
replace_once(
    ".github/scripts/compiler-c0-full-gate.sh",
    "core_integer_bitwise_not_focused() {",
    "core_short_circuit_or_focused() {\n"
    "    MINIC=\"$root/build/ci-debug/bin/minic\" \\\n"
    "    BUILD_DIR=\"$root/build/ci-core-short-circuit-or\" \\\n"
    "    RISCV_CC=riscv64-linux-gnu-gcc \\\n"
    "    QEMU_RISCV64=qemu-riscv64 \\\n"
    "        sh tests/compiler/c0/run-core-short-circuit-or.sh\n"
    "}\n\n"
    "core_integer_subtract_overflow_focused() {\n"
    "    MINIC=\"$root/build/ci-debug/bin/minic\" \\\n"
    "    BUILD_DIR=\"$root/build/ci-core-integer-subtract-overflow\" \\\n"
    "    RISCV_CC=riscv64-linux-gnu-gcc \\\n"
    "    QEMU_RISCV64=qemu-riscv64 \\\n"
    "        sh tests/compiler/c0/run-core-integer-subtract-overflow.sh\n"
    "}\n\n"
    "core_integer_bitwise_not_focused() {",
)
replace_once(
    ".github/scripts/compiler-c0-full-gate.sh",
    "start_gate core-integer-add-overflow-focused core_integer_add_overflow_focused\n"
    "start_gate core-integer-bitwise-not-focused core_integer_bitwise_not_focused",
    "start_gate core-integer-add-overflow-focused core_integer_add_overflow_focused\n"
    "start_gate core-short-circuit-or-focused core_short_circuit_or_focused\n"
    "start_gate core-integer-subtract-overflow-focused core_integer_subtract_overflow_focused\n"
    "start_gate core-integer-bitwise-not-focused core_integer_bitwise_not_focused",
)
