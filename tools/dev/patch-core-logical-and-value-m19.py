#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected 1 match, found {count}")
    target.write_text(text.replace(old, new, 1))


def write_new(path: str, content: str) -> None:
    target = Path(path)
    if target.exists():
        raise SystemExit(f"{path}: already exists")
    target.write_text(content)


replace_once(
    "src/core/core_lower.c",
    '''static MinicCoreLowerStatus
lower_block(MinicCoreLowerContext *context, const MinicBlock *source_block, bool *terminated);
''',
    '''static MinicCoreLowerStatus
lower_block(MinicCoreLowerContext *context, const MinicBlock *source_block, bool *terminated);
static MinicCoreLowerStatus set_branch(MinicCoreLowerContext *context,
                                       MinicCoreBlockId block_id,
                                       MinicSourceSpan span,
                                       MinicCoreBlockId target);
static MinicCoreLowerStatus lower_condition_branch(MinicCoreLowerContext *context,
                                                   MinicExpressionId expression_id,
                                                   MinicSourceSpan span,
                                                   MinicCoreBlockId when_true,
                                                   MinicCoreBlockId when_false);
''',
    "Core M19 CFG helper declarations",
)

logical_and_lowering = r'''    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_AND) {
        MinicCoreBlockId false_block;
        MinicCoreBlockId merge_block;
        MinicCoreBlockId true_block;
        MinicCoreObjectId result_object;
        MinicCoreValueId address_value;
        MinicCoreValueId constant_value;
        MinicCoreLowerStatus status;
        MinicType result_pointer_type;

        if (!minic_type_equal(expression->type, minic_type_int()) ||
            !minic_type_pointer_to(minic_type_int(), &result_pointer_type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (!minic_core_function_add_object(
                context->function, expression->span, minic_type_int(), &result_object) ||
            !minic_core_function_add_block(context->function, &true_block) ||
            !minic_core_function_add_block(context->function, &false_block) ||
            !minic_core_function_add_block(context->function, &merge_block)) {
            return MINIC_CORE_LOWER_ERROR;
        }

        status = lower_condition_branch(
            context, expression_id, expression->span, true_block, false_block);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }

        context->block_id = false_block;
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
        instruction.span = expression->span;
        instruction.type = minic_type_int();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.integer_value = 0;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &constant_value)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;
        instruction.span = expression->span;
        instruction.type = result_pointer_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.object_id = result_object;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &address_value)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_STORE;
        instruction.span = expression->span;
        instruction.type = minic_type_void();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.store.address = address_value;
        instruction.value.store.stored_value = constant_value;
        instruction.value.store.is_volatile = false;
        if (!minic_core_function_append_effect_instruction(
                context->function, context->block_id, &instruction)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = set_branch(context, context->block_id, expression->span, merge_block);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }

        context->block_id = true_block;
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
        instruction.span = expression->span;
        instruction.type = minic_type_int();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.integer_value = 1;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &constant_value)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;
        instruction.span = expression->span;
        instruction.type = result_pointer_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.object_id = result_object;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &address_value)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_STORE;
        instruction.span = expression->span;
        instruction.type = minic_type_void();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.store.address = address_value;
        instruction.value.store.stored_value = constant_value;
        instruction.value.store.is_volatile = false;
        if (!minic_core_function_append_effect_instruction(
                context->function, context->block_id, &instruction)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = set_branch(context, context->block_id, expression->span, merge_block);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }

        context->block_id = merge_block;
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;
        instruction.span = expression->span;
        instruction.type = result_pointer_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.object_id = result_object;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &address_value)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_LOAD;
        instruction.span = expression->span;
        instruction.type = minic_type_int();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.load.address = address_value;
        instruction.value.load.is_volatile = false;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
'''
replace_once(
    "src/core/core_lower.c",
    '''    if (expression->kind == MINIC_EXPRESSION_ADDRESS_OF) {
''',
    logical_and_lowering + '''    if (expression->kind == MINIC_EXPRESSION_ADDRESS_OF) {
''',
    "Core M19 value-producing logical AND lowering",
)

write_new(
    "tests/compiler/c0/core_logical_and_value.c",
    r'''struct core_m19_node {
    struct core_m19_node *next;
    struct core_m19_node *prev;
};

int core_m19_rhs_calls;

int core_m19_rhs(int value) {
    core_m19_rhs_calls = core_m19_rhs_calls + 1;
    return value;
}

int core_m19_plain(int left, int right) {
    return left && right;
}

int core_m19_short_false(void) {
    core_m19_rhs_calls = 0;
    return 0 && core_m19_rhs(7);
}

int core_m19_short_true(void) {
    core_m19_rhs_calls = 0;
    return 2 && core_m19_rhs(7);
}

int core_m19_get_rhs_calls(void) {
    return core_m19_rhs_calls;
}

int core_m19_nested(int first, int second, int third) {
    return first && second && third;
}

int core_m19_list_empty_careful_shape(const struct core_m19_node *head) {
    struct core_m19_node *next = ({
        struct core_m19_node *value =
            ({ (*(struct core_m19_node * const volatile *)&(head->next)); });
        __asm__ __volatile__("fence r,rw" : : : "memory");
        value;
    });
    return (next == head) &&
           (next == ({ (*(struct core_m19_node * const volatile *)&(head->prev)); }));
}
''',
)

write_new(
    "tests/compiler/c0/core_logical_and_value_runtime.c",
    r'''#include <stdio.h>

struct core_m19_node {
    struct core_m19_node *next;
    struct core_m19_node *prev;
};

int core_m19_plain(int left, int right);
int core_m19_short_false(void);
int core_m19_short_true(void);
int core_m19_get_rhs_calls(void);
int core_m19_nested(int first, int second, int third);
int core_m19_list_empty_careful_shape(const struct core_m19_node *head);

int main(void) {
    struct core_m19_node empty;
    struct core_m19_node not_empty;
    struct core_m19_node other;
    int false_result;
    int false_calls;
    int true_result;
    int true_calls;

    false_result = core_m19_short_false();
    false_calls = core_m19_get_rhs_calls();
    true_result = core_m19_short_true();
    true_calls = core_m19_get_rhs_calls();

    empty.next = &empty;
    empty.prev = &empty;
    not_empty.next = &other;
    not_empty.prev = &not_empty;
    other.next = &empty;
    other.prev = &empty;

    printf("plain=%d,%d,%d nested=%d,%d\n",
           core_m19_plain(0, 9),
           core_m19_plain(3, 0),
           core_m19_plain(3, 9),
           core_m19_nested(1, 2, 3),
           core_m19_nested(1, 0, 3));
    printf("short=%d/%d,%d/%d\n", false_result, false_calls, true_result, true_calls);
    printf("list=%d,%d\n",
           core_m19_list_empty_careful_shape(&empty),
           core_m19_list_empty_careful_shape(&not_empty));
    return 0;
}
''',
)

write_new(
    "tests/compiler/c0/run-core-logical-and-value.sh",
    r'''#!/bin/sh
set -eu
: "${MINIC:?MINIC must point to the compiler binary}"
: "${RISCV_CC:=riscv64-linux-gnu-gcc}"
: "${QEMU_RISCV64:=qemu-riscv64}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work="${BUILD_DIR:-$root/build/core-logical-and-value}"
source_file="$root/tests/compiler/c0/core_logical_and_value.c"
runtime_file="$root/tests/compiler/c0/core_logical_and_value_runtime.c"
mkdir -p "$work"
cc -E -P -std=gnu11 "$source_file" -o "$work/input.i"
MINIC_CORE_IR=strict "$MINIC" -S "$work/input.i" -o "$work/strict.s"
MINIC_CORE_CODEGEN=basic-v0 "$MINIC" -S "$work/input.i" -o "$work/core.s"
for symbol in core_m19_plain core_m19_short_false core_m19_short_true \
              core_m19_get_rhs_calls core_m19_nested core_m19_list_empty_careful_shape; do
    grep -q "^${symbol}:" "$work/core.s"
done
"$RISCV_CC" -static -O2 "$source_file" "$runtime_file" -o "$work/reference-rv64"
"$RISCV_CC" -static -O2 "$runtime_file" "$work/core.s" -o "$work/minic-rv64"
"$QEMU_RISCV64" "$work/reference-rv64" >"$work/reference.out"
"$QEMU_RISCV64" "$work/minic-rv64" >"$work/minic.out"
cmp "$work/reference.out" "$work/minic.out"
grep -F 'plain=0,0,1 nested=1,0' "$work/minic.out" >/dev/null
grep -F 'short=0/0,1/1' "$work/minic.out" >/dev/null
grep -F 'list=1,0' "$work/minic.out" >/dev/null
printf '%s\n' 'PASS compiler/c0/core-logical-and-value'
''',
)

replace_once(
    ".github/scripts/compiler-c0-full-gate.sh",
    '''core_integer_bitwise_and_assignment_focused() {
''',
    '''core_logical_and_value_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \\
    BUILD_DIR="$root/build/ci-core-logical-and-value" \\
    RISCV_CC=riscv64-linux-gnu-gcc \\
    QEMU_RISCV64=qemu-riscv64 \\
        sh tests/compiler/c0/run-core-logical-and-value.sh
}

core_integer_bitwise_and_assignment_focused() {
''',
    "Core M19 C0 focused function",
)
replace_once(
    ".github/scripts/compiler-c0-full-gate.sh",
    '''start_gate core-condition-and-focused core_condition_and_focused
''',
    '''start_gate core-condition-and-focused core_condition_and_focused
start_gate core-logical-and-value-focused core_logical_and_value_focused
''',
    "Core M19 C0 gate registration",
)

print("staged M19 Core short-circuit logical AND value lowering")
