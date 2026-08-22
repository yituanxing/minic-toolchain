from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    file = Path(path)
    text = file.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"expected exactly one anchor in {path}, found {count}")
    file.write_text(text.replace(old, new, 1))


# C != does not need a second Core comparison opcode.  Once Sema has normalized
# both operands to the same scalar type, reuse equality and invert its int result.
replace_once(
    "src/core/core_lower.c",
    "    if (expression->kind == MINIC_EXPRESSION_BINARY &&\n"
    "        expression->value.binary.operator_kind == MINIC_BINARY_ADD) {",
    "    if (expression->kind == MINIC_EXPRESSION_BINARY &&\n"
    "        expression->value.binary.operator_kind == MINIC_BINARY_NOT_EQUAL) {\n"
    "        MinicCoreInstruction zero_test_instruction;\n"
    "        MinicCoreValueId equal_value;\n"
    "        MinicCoreValueId left;\n"
    "        MinicCoreValueId right;\n"
    "        MinicCoreLowerStatus status;\n\n"
    "        if (!minic_type_equal(expression->type, minic_type_int())) {\n"
    "            return MINIC_CORE_LOWER_ERROR;\n"
    "        }\n"
    "        status = lower_expression(context, expression->value.binary.left, &left);\n"
    "        if (status != MINIC_CORE_LOWER_OK) {\n"
    "            return status;\n"
    "        }\n"
    "        status = lower_expression(context, expression->value.binary.right, &right);\n"
    "        if (status != MINIC_CORE_LOWER_OK) {\n"
    "            return status;\n"
    "        }\n"
    "        if (left >= context->function->value_count || right >= context->function->value_count ||\n"
    "            (!minic_type_is_integer(context->function->values[left].type) &&\n"
    "             !minic_type_is_pointer(context->function->values[left].type)) ||\n"
    "            !minic_type_equal(context->function->values[left].type,\n"
    "                              context->function->values[right].type)) {\n"
    "            return MINIC_CORE_LOWER_UNSUPPORTED;\n"
    "        }\n"
    "        instruction.kind = MINIC_CORE_INSTRUCTION_SCALAR_EQUAL;\n"
    "        instruction.type = minic_type_int();\n"
    "        instruction.value.binary.left = left;\n"
    "        instruction.value.binary.right = right;\n"
    "        if (!minic_core_function_append_value_instruction(\n"
    "                context->function, context->block_id, &instruction, &equal_value)) {\n"
    "            return MINIC_CORE_LOWER_ERROR;\n"
    "        }\n"
    "        (void)memset(&zero_test_instruction, 0, sizeof(zero_test_instruction));\n"
    "        zero_test_instruction.kind = MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO;\n"
    "        zero_test_instruction.span = expression->span;\n"
    "        zero_test_instruction.type = minic_type_int();\n"
    "        zero_test_instruction.result = MINIC_CORE_VALUE_INVALID;\n"
    "        zero_test_instruction.value.operand = equal_value;\n"
    "        return minic_core_function_append_value_instruction(\n"
    "                   context->function, context->block_id, &zero_test_instruction, value_id)\n"
    "                   ? MINIC_CORE_LOWER_OK\n"
    "                   : MINIC_CORE_LOWER_ERROR;\n"
    "    }\n"
    "    if (expression->kind == MINIC_EXPRESSION_BINARY &&\n"
    "        expression->value.binary.operator_kind == MINIC_BINARY_ADD) {",
)

Path("tests/compiler/c0/core_scalar_not_equal.c").write_text(
    """struct core_m13_node {
    struct core_m13_node *next;
    struct core_m13_node *prev;
};

int core_m13_integer_not_equal(int left, int right) {
    return left != right;
}

int core_m13_pointer_not_equal(int *left, int *right) {
    return left != right;
}

int core_m13_member_pointer_not_equal(struct core_m13_node *node,
                                      struct core_m13_node *other) {
    return node->next != other;
}

int core_m13_list_condition(struct core_m13_node *new_node,
                            struct core_m13_node *prev,
                            struct core_m13_node *next) {
    if (__builtin_expect(!!(next->prev == prev && prev->next == next &&
                            new_node != prev && new_node != next), 1))
        return 1;
    return 0;
}
"""
)

Path("tests/compiler/c0/core_scalar_not_equal_runtime.c").write_text(
    """#include <stdio.h>

struct core_m13_node {
    struct core_m13_node *next;
    struct core_m13_node *prev;
};

int core_m13_integer_not_equal(int left, int right);
int core_m13_pointer_not_equal(int *left, int *right);
int core_m13_member_pointer_not_equal(struct core_m13_node *node,
                                      struct core_m13_node *other);
int core_m13_list_condition(struct core_m13_node *new_node,
                            struct core_m13_node *prev,
                            struct core_m13_node *next);

int main(void) {
    int left = 1;
    int right = 2;
    struct core_m13_node prev;
    struct core_m13_node next;
    struct core_m13_node new_node;

    prev.next = &next;
    prev.prev = &prev;
    next.next = &next;
    next.prev = &prev;
    new_node.next = &new_node;
    new_node.prev = &new_node;

    printf("%d %d %d %d %d %d %d\\n",
           core_m13_integer_not_equal(3, 3),
           core_m13_integer_not_equal(3, 4),
           core_m13_pointer_not_equal(&left, &left),
           core_m13_pointer_not_equal(&left, &right),
           core_m13_member_pointer_not_equal(&prev, &next),
           core_m13_member_pointer_not_equal(&prev, &prev),
           core_m13_list_condition(&new_node, &prev, &next));
    return 0;
}
"""
)

Path("tests/compiler/c0/run-core-scalar-not-equal.sh").write_text(
    """#!/bin/sh
set -eu
: "${MINIC:?MINIC must point to the compiler binary}"
: "${RISCV_CC:=riscv64-linux-gnu-gcc}"
: "${QEMU_RISCV64:=qemu-riscv64}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work="${BUILD_DIR:-$root/build/core-scalar-not-equal}"
source_file="$root/tests/compiler/c0/core_scalar_not_equal.c"
runtime_file="$root/tests/compiler/c0/core_scalar_not_equal_runtime.c"
mkdir -p "$work"
cc -E -P -std=gnu11 "$source_file" -o "$work/input.i"
MINIC_CORE_IR=strict "$MINIC" -S "$work/input.i" -o "$work/strict.s"
MINIC_CORE_CODEGEN=basic-v0 "$MINIC" -S "$work/input.i" -o "$work/core.s"
for symbol in core_m13_integer_not_equal core_m13_pointer_not_equal \
              core_m13_member_pointer_not_equal core_m13_list_condition; do
    grep -q "^${symbol}:" "$work/core.s"
done
"$RISCV_CC" -static -O2 "$source_file" "$runtime_file" -o "$work/reference-rv64"
"$RISCV_CC" -static -O2 "$runtime_file" "$work/core.s" -o "$work/minic-rv64"
"$QEMU_RISCV64" "$work/reference-rv64" >"$work/reference.out"
"$QEMU_RISCV64" "$work/minic-rv64" >"$work/minic.out"
cmp "$work/reference.out" "$work/minic.out"
printf '%s\\n' 'PASS compiler/c0/core-scalar-not-equal'
"""
)

replace_once(
    ".github/scripts/compiler-c0-full-gate.sh",
    "core_condition_and_focused() {\n"
    "    MINIC=\"$root/build/ci-debug/bin/minic\" \\\n"
    "    BUILD_DIR=\"$root/build/ci-core-condition-and\" \\\n"
    "    RISCV_CC=riscv64-linux-gnu-gcc \\\n"
    "    QEMU_RISCV64=qemu-riscv64 \\\n"
    "        sh tests/compiler/c0/run-core-condition-and.sh\n"
    "}\n",
    "core_condition_and_focused() {\n"
    "    MINIC=\"$root/build/ci-debug/bin/minic\" \\\n"
    "    BUILD_DIR=\"$root/build/ci-core-condition-and\" \\\n"
    "    RISCV_CC=riscv64-linux-gnu-gcc \\\n"
    "    QEMU_RISCV64=qemu-riscv64 \\\n"
    "        sh tests/compiler/c0/run-core-condition-and.sh\n"
    "}\n\n"
    "core_scalar_not_equal_focused() {\n"
    "    MINIC=\"$root/build/ci-debug/bin/minic\" \\\n"
    "    BUILD_DIR=\"$root/build/ci-core-scalar-not-equal\" \\\n"
    "    RISCV_CC=riscv64-linux-gnu-gcc \\\n"
    "    QEMU_RISCV64=qemu-riscv64 \\\n"
    "        sh tests/compiler/c0/run-core-scalar-not-equal.sh\n"
    "}\n",
)

replace_once(
    ".github/scripts/compiler-c0-full-gate.sh",
    "start_gate core-condition-and-focused core_condition_and_focused\n"
    "start_gate core-integer-subtract-overflow-focused core_integer_subtract_overflow_focused",
    "start_gate core-condition-and-focused core_condition_and_focused\n"
    "start_gate core-scalar-not-equal-focused core_scalar_not_equal_focused\n"
    "start_gate core-integer-subtract-overflow-focused core_integer_subtract_overflow_focused",
)
