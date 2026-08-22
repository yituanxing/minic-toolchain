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
    '''static bool core_scalar_expression_value_type(const MinicExpression *expression,
                                              MinicType *value_type) {
    if (expression == NULL || value_type == NULL || !core_memory_scalar_type(expression->type)) {
        return false;
    }
    if (expression->value_category == MINIC_VALUE_LVALUE) {
        return minic_type_unqualified(expression->type, value_type);
    }
    if (expression->value_category != MINIC_VALUE_RVALUE) {
        return false;
    }
    *value_type = expression->type;
    return true;
}
''',
    '''static bool core_scalar_expression_value_type(const MinicFunctionBodyView *body,
                                              const MinicExpression *expression,
                                              MinicType *value_type) {
    const MinicExpression *statement_result;

    if (body == NULL || body->program == NULL || expression == NULL || value_type == NULL ||
        !core_memory_scalar_type(expression->type)) {
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_STATEMENT) {
        if (expression->value.statement_expression.result == MINIC_EXPRESSION_INVALID) {
            return false;
        }
        statement_result = minic_c0_program_expression(
            body->program, expression->value.statement_expression.result);
        return statement_result != NULL &&
               core_scalar_expression_value_type(body, statement_result, value_type);
    }
    if (expression->value_category == MINIC_VALUE_LVALUE) {
        return minic_type_unqualified(expression->type, value_type);
    }
    if (expression->value_category != MINIC_VALUE_RVALUE) {
        return false;
    }
    *value_type = expression->type;
    return true;
}
''',
    "Core runtime scalar value type statement-expression forwarding",
)

replace_once(
    "src/core/core_lower.c",
    '''        !core_scalar_expression_value_type(left_expression, &left_type) ||
        !core_scalar_expression_value_type(right_expression, &right_type)) {
''',
    '''        !core_scalar_expression_value_type(context->body, left_expression, &left_type) ||
        !core_scalar_expression_value_type(context->body, right_expression, &right_type)) {
''',
    "Core equality runtime type consumers",
)
replace_once(
    "src/core/core_lower.c",
    '''        if (!core_scalar_expression_value_type(operand, &operand_value_type) ||
''',
    '''        if (!core_scalar_expression_value_type(context->body, operand, &operand_value_type) ||
''',
    "Core bitcast runtime type consumer",
)

statement_lowering = r'''    if (expression->kind == MINIC_EXPRESSION_STATEMENT) {
        const MinicBlock *statement_block;
        const MinicExpression *statement_result;
        MinicCoreValueId result_value;
        MinicCoreLowerStatus status;
        MinicType result_type;
        bool terminated;

        if (expression->value.statement_expression.result == MINIC_EXPRESSION_INVALID ||
            !core_scalar_expression_value_type(context->body, expression, &result_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        statement_block = minic_c0_program_block(
            context->body->program, expression->value.statement_expression.block);
        statement_result = minic_c0_program_expression(
            context->body->program, expression->value.statement_expression.result);
        if (statement_block == NULL || statement_result == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = lower_block(context, statement_block, &terminated);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (terminated) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_expression(
            context, expression->value.statement_expression.result, &result_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (result_value >= context->function->value_count ||
            !minic_type_equal(context->function->values[result_value].type, result_type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        *value_id = result_value;
        return MINIC_CORE_LOWER_OK;
    }
'''
replace_once(
    "src/core/core_lower.c",
    '''    if (expression->kind == MINIC_EXPRESSION_ADDRESS_OF) {
''',
    statement_lowering + '''    if (expression->kind == MINIC_EXPRESSION_ADDRESS_OF) {
''',
    "Core GNU statement-expression lowering",
)

write_new(
    "tests/compiler/c0/core_statement_expression.c",
    r'''struct core_m17_node {
    struct core_m17_node *next;
    struct core_m17_node *prev;
};

int core_m17_scalar_value(int value) {
    return ({ value; });
}

struct core_m17_node *core_m17_read_once_shape(const struct core_m17_node *head) {
    return ({ (*(struct core_m17_node * const volatile *)&(head->next)); });
}

int core_m17_list_empty(const struct core_m17_node *head) {
    return ({ (*(struct core_m17_node * const volatile *)&(head->next)); }) == head;
}

int core_m17_prefix_store(int *value) {
    return ({
        *value = 17;
        *value;
    });
}

int core_m17_prefix_call_target(int value) {
    return value + 3;
}

int core_m17_prefix_call(int value) {
    return ({
        core_m17_prefix_call_target(value);
        value;
    });
}
''',
)

write_new(
    "tests/compiler/c0/core_statement_expression_runtime.c",
    r'''#include <stdio.h>

struct core_m17_node {
    struct core_m17_node *next;
    struct core_m17_node *prev;
};

int core_m17_scalar_value(int value);
struct core_m17_node *core_m17_read_once_shape(const struct core_m17_node *head);
int core_m17_list_empty(const struct core_m17_node *head);
int core_m17_prefix_store(int *value);
int core_m17_prefix_call(int value);

int main(void) {
    struct core_m17_node head;
    struct core_m17_node other;
    int stored = 4;

    head.next = &head;
    head.prev = &head;
    other.next = &head;
    other.prev = &head;

    printf("%d %d %d %d %d %d\n",
           core_m17_scalar_value(9),
           core_m17_read_once_shape(&head) == &head,
           core_m17_list_empty(&head),
           core_m17_list_empty(&other),
           core_m17_prefix_store(&stored),
           core_m17_prefix_call(8));
    printf("stored=%d\n", stored);
    return 0;
}
''',
)

write_new(
    "tests/compiler/c0/run-core-statement-expression.sh",
    r'''#!/bin/sh
set -eu
: "${MINIC:?MINIC must point to the compiler binary}"
: "${RISCV_CC:=riscv64-linux-gnu-gcc}"
: "${QEMU_RISCV64:=qemu-riscv64}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work="${BUILD_DIR:-$root/build/core-statement-expression}"
source_file="$root/tests/compiler/c0/core_statement_expression.c"
runtime_file="$root/tests/compiler/c0/core_statement_expression_runtime.c"
mkdir -p "$work"
cc -E -P -std=gnu11 "$source_file" -o "$work/input.i"
MINIC_CORE_IR=strict "$MINIC" -S "$work/input.i" -o "$work/strict.s"
MINIC_CORE_CODEGEN=basic-v0 "$MINIC" -S "$work/input.i" -o "$work/core.s"
for symbol in core_m17_scalar_value core_m17_read_once_shape core_m17_list_empty \
              core_m17_prefix_store core_m17_prefix_call; do
    grep -q "^${symbol}:" "$work/core.s"
done
"$RISCV_CC" -static -O2 "$source_file" "$runtime_file" -o "$work/reference-rv64"
"$RISCV_CC" -static -O2 "$runtime_file" "$work/core.s" -o "$work/minic-rv64"
"$QEMU_RISCV64" "$work/reference-rv64" >"$work/reference.out"
"$QEMU_RISCV64" "$work/minic-rv64" >"$work/minic.out"
cmp "$work/reference.out" "$work/minic.out"
printf '%s\n' 'PASS compiler/c0/core-statement-expression'
''',
)

replace_once(
    ".github/scripts/compiler-c0-full-gate.sh",
    '''core_pointer_equality_qualifiers_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \\
    BUILD_DIR="$root/build/ci-core-pointer-equality-qualifiers" \\
    RISCV_CC=riscv64-linux-gnu-gcc \\
    QEMU_RISCV64=qemu-riscv64 \\
        sh tests/compiler/c0/run-core-pointer-equality-qualifiers.sh
}

''',
    '''core_pointer_equality_qualifiers_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \\
    BUILD_DIR="$root/build/ci-core-pointer-equality-qualifiers" \\
    RISCV_CC=riscv64-linux-gnu-gcc \\
    QEMU_RISCV64=qemu-riscv64 \\
        sh tests/compiler/c0/run-core-pointer-equality-qualifiers.sh
}

core_statement_expression_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \\
    BUILD_DIR="$root/build/ci-core-statement-expression" \\
    RISCV_CC=riscv64-linux-gnu-gcc \\
    QEMU_RISCV64=qemu-riscv64 \\
        sh tests/compiler/c0/run-core-statement-expression.sh
}

''',
    "C0 statement-expression focused function registration",
)
replace_once(
    ".github/scripts/compiler-c0-full-gate.sh",
    "start_gate core-pointer-equality-qualifiers-focused core_pointer_equality_qualifiers_focused\n",
    "start_gate core-pointer-equality-qualifiers-focused core_pointer_equality_qualifiers_focused\n"
    "start_gate core-statement-expression-focused core_statement_expression_focused\n",
    "C0 statement-expression focused gate registration",
)

print("staged M17 Core GNU statement-expression result forwarding")
