from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    file = Path(path)
    text = file.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"expected exactly one anchor in {path}, found {count}")
    file.write_text(text.replace(old, new, 1))


# Branch lowering owns control-flow semantics.  Decompose integer logical-not,
# truth-preserving widening conversions, and logical-and directly into CFG rather
# than inventing value-level boolean temporaries or a LOGICAL_AND Core opcode.
replace_once(
    "src/core/core_lower.c",
    "    if (expression->kind == MINIC_EXPRESSION_BINARY &&\n"
    "        expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_OR) {",
    "    if (expression->kind == MINIC_EXPRESSION_UNARY &&\n"
    "        expression->value.unary.operator_kind == MINIC_UNARY_LOGICAL_NOT) {\n"
    "        const MinicExpression *operand;\n\n"
    "        operand = minic_c0_program_expression(\n"
    "            context->body->program, expression->value.unary.operand);\n"
    "        if (operand != NULL && minic_type_is_integer(operand->type)) {\n"
    "            return lower_condition_branch(context,\n"
    "                                          expression->value.unary.operand,\n"
    "                                          span,\n"
    "                                          when_false,\n"
    "                                          when_true);\n"
    "        }\n"
    "    }\n"
    "    if (expression->kind == MINIC_EXPRESSION_CONVERSION && context->target != NULL) {\n"
    "        const MinicExpression *operand;\n"
    "        unsigned int source_width;\n"
    "        unsigned int destination_width;\n\n"
    "        operand = minic_c0_program_expression(\n"
    "            context->body->program, expression->value.unary.operand);\n"
    "        if (operand != NULL && minic_type_is_integer(operand->type) &&\n"
    "            minic_type_is_integer(expression->type) &&\n"
    "            minic_target_info_integer_width(context->target,\n"
    "                                            context->body->program,\n"
    "                                            operand->type,\n"
    "                                            &source_width) &&\n"
    "            minic_target_info_integer_width(context->target,\n"
    "                                            context->body->program,\n"
    "                                            expression->type,\n"
    "                                            &destination_width) &&\n"
    "            (minic_type_equal(operand->type, expression->type) ||\n"
    "             destination_width > source_width)) {\n"
    "            return lower_condition_branch(context,\n"
    "                                          expression->value.unary.operand,\n"
    "                                          span,\n"
    "                                          when_true,\n"
    "                                          when_false);\n"
    "        }\n"
    "    }\n"
    "    if (expression->kind == MINIC_EXPRESSION_BINARY &&\n"
    "        expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_AND) {\n"
    "        MinicCoreBlockId right_block;\n\n"
    "        if (!minic_core_function_add_block(context->function, &right_block)) {\n"
    "            return MINIC_CORE_LOWER_ERROR;\n"
    "        }\n"
    "        status = lower_condition_branch(\n"
    "            context, expression->value.binary.left, span, right_block, when_false);\n"
    "        if (status != MINIC_CORE_LOWER_OK) {\n"
    "            return status;\n"
    "        }\n"
    "        context->block_id = right_block;\n"
    "        return lower_condition_branch(\n"
    "            context, expression->value.binary.right, span, when_true, when_false);\n"
    "    }\n"
    "    if (expression->kind == MINIC_EXPRESSION_BINARY &&\n"
    "        expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_OR) {",
)

Path("tests/compiler/c0/core_condition_and.c").write_text(
    """int core_m12_rhs(void);

int core_m12_short_circuit_and(int left) {
    if (left && core_m12_rhs())
        return 7;
    return 3;
}

int core_m12_wrapped_pointer_and(int *left, int *middle, int *right) {
    if (__builtin_expect(!!(left == middle && middle == right), 1))
        return 11;
    return 5;
}
"""
)

Path("tests/compiler/c0/core_condition_and_runtime.c").write_text(
    """#include <stdio.h>

int core_m12_short_circuit_and(int left);
int core_m12_wrapped_pointer_and(int *left, int *middle, int *right);
static int rhs_calls;

int core_m12_rhs(void) {
    ++rhs_calls;
    return 1;
}

int main(void) {
    int a = 1;
    int b = 2;
    int first = core_m12_short_circuit_and(0);
    int calls_after_false = rhs_calls;
    int second = core_m12_short_circuit_and(1);
    int wrapped_true = core_m12_wrapped_pointer_and(&a, &a, &a);
    int wrapped_false = core_m12_wrapped_pointer_and(&a, &a, &b);
    printf("%d %d %d %d %d %d\\n",
           first,
           calls_after_false,
           second,
           rhs_calls,
           wrapped_true,
           wrapped_false);
    return 0;
}
"""
)

Path("tests/compiler/c0/run-core-condition-and.sh").write_text(
    """#!/bin/sh
set -eu
: "${MINIC:?MINIC must point to the compiler binary}"
: "${RISCV_CC:=riscv64-linux-gnu-gcc}"
: "${QEMU_RISCV64:=qemu-riscv64}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work="${BUILD_DIR:-$root/build/core-condition-and}"
source_file="$root/tests/compiler/c0/core_condition_and.c"
runtime_file="$root/tests/compiler/c0/core_condition_and_runtime.c"
mkdir -p "$work"
cc -E -P -std=gnu11 "$source_file" -o "$work/input.i"
MINIC_CORE_IR=strict "$MINIC" -S "$work/input.i" -o "$work/strict.s"
MINIC_CORE_CODEGEN=basic-v0 "$MINIC" -S "$work/input.i" -o "$work/core.s"
grep -q '^core_m12_short_circuit_and:' "$work/core.s"
grep -q '^core_m12_wrapped_pointer_and:' "$work/core.s"
"$RISCV_CC" -static -O2 "$source_file" "$runtime_file" -o "$work/reference-rv64"
"$RISCV_CC" -static -O2 "$runtime_file" "$work/core.s" -o "$work/minic-rv64"
"$QEMU_RISCV64" "$work/reference-rv64" >"$work/reference.out"
"$QEMU_RISCV64" "$work/minic-rv64" >"$work/minic.out"
cmp "$work/reference.out" "$work/minic.out"
printf '%s\\n' 'PASS compiler/c0/core-condition-and'
"""
)

replace_once(
    ".github/scripts/compiler-c0-full-gate.sh",
    "core_short_circuit_or_focused() {\n"
    "    MINIC=\"$root/build/ci-debug/bin/minic\" \\\n"
    "    BUILD_DIR=\"$root/build/ci-core-short-circuit-or\" \\\n"
    "    RISCV_CC=riscv64-linux-gnu-gcc \\\n"
    "    QEMU_RISCV64=qemu-riscv64 \\\n"
    "        sh tests/compiler/c0/run-core-short-circuit-or.sh\n"
    "}\n",
    "core_short_circuit_or_focused() {\n"
    "    MINIC=\"$root/build/ci-debug/bin/minic\" \\\n"
    "    BUILD_DIR=\"$root/build/ci-core-short-circuit-or\" \\\n"
    "    RISCV_CC=riscv64-linux-gnu-gcc \\\n"
    "    QEMU_RISCV64=qemu-riscv64 \\\n"
    "        sh tests/compiler/c0/run-core-short-circuit-or.sh\n"
    "}\n\n"
    "core_condition_and_focused() {\n"
    "    MINIC=\"$root/build/ci-debug/bin/minic\" \\\n"
    "    BUILD_DIR=\"$root/build/ci-core-condition-and\" \\\n"
    "    RISCV_CC=riscv64-linux-gnu-gcc \\\n"
    "    QEMU_RISCV64=qemu-riscv64 \\\n"
    "        sh tests/compiler/c0/run-core-condition-and.sh\n"
    "}\n",
)

replace_once(
    ".github/scripts/compiler-c0-full-gate.sh",
    "start_gate core-short-circuit-or-focused core_short_circuit_or_focused\n"
    "start_gate core-integer-subtract-overflow-focused core_integer_subtract_overflow_focused",
    "start_gate core-short-circuit-or-focused core_short_circuit_or_focused\n"
    "start_gate core-condition-and-focused core_condition_and_focused\n"
    "start_gate core-integer-subtract-overflow-focused core_integer_subtract_overflow_focused",
)
