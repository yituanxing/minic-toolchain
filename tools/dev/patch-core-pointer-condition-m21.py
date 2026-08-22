#!/usr/bin/env python3
from pathlib import Path

path = Path("src/core/core_lower.c")
text = path.read_text()

old = '''    expression = minic_c0_program_expression(context->body->program, expression_id);\n    if (expression == NULL || !minic_type_is_integer(expression->type)) {\n        return MINIC_CORE_LOWER_UNSUPPORTED;\n    }\n'''
new = '''    expression = minic_c0_program_expression(context->body->program, expression_id);\n    if (expression == NULL ||\n        (!minic_type_is_integer(expression->type) && !minic_type_is_pointer(expression->type))) {\n        return MINIC_CORE_LOWER_UNSUPPORTED;\n    }\n'''
if text.count(old) != 1:
    raise SystemExit(f"expected one condition-entry anchor, found {text.count(old)}")
text = text.replace(old, new, 1)

old = '''        if (operand != NULL && minic_type_is_integer(operand->type)) {\n            return lower_condition_branch(\n                context, expression->value.unary.operand, span, when_false, when_true);\n        }\n'''
new = '''        if (operand != NULL &&\n            (minic_type_is_integer(operand->type) || minic_type_is_pointer(operand->type))) {\n            return lower_condition_branch(\n                context, expression->value.unary.operand, span, when_false, when_true);\n        }\n'''
if text.count(old) != 1:
    raise SystemExit(f"expected one logical-not anchor, found {text.count(old)}")
text = text.replace(old, new, 1)

old = '''    status = lower_expression(context, expression_id, &condition);\n    if (status != MINIC_CORE_LOWER_OK) {\n        return status;\n    }\n    condition_block = context->block_id;\n    if (condition >= context->function->value_count ||\n        !minic_type_is_integer(context->function->values[condition].type)) {\n        return MINIC_CORE_LOWER_ERROR;\n    }\n    (void)memset(&terminator, 0, sizeof(terminator));\n'''
new = '''    status = lower_expression(context, expression_id, &condition);\n    if (status != MINIC_CORE_LOWER_OK) {\n        return status;\n    }\n    if (condition >= context->function->value_count) {\n        return MINIC_CORE_LOWER_ERROR;\n    }\n    if (minic_type_is_pointer(expression->type)) {\n        MinicCoreInstruction zero_test;\n        MinicCoreBlockId original_true;\n\n        if (!minic_type_is_pointer(context->function->values[condition].type)) {\n            return MINIC_CORE_LOWER_ERROR;\n        }\n        (void)memset(&zero_test, 0, sizeof(zero_test));\n        zero_test.kind = MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO;\n        zero_test.span = span;\n        zero_test.type = minic_type_int();\n        zero_test.result = MINIC_CORE_VALUE_INVALID;\n        zero_test.value.operand = condition;\n        if (!minic_core_function_append_value_instruction(\n                context->function, context->block_id, &zero_test, &condition)) {\n            return MINIC_CORE_LOWER_ERROR;\n        }\n        original_true = when_true;\n        when_true = when_false;\n        when_false = original_true;\n    } else if (!minic_type_is_integer(context->function->values[condition].type)) {\n        return MINIC_CORE_LOWER_ERROR;\n    }\n    condition_block = context->block_id;\n    (void)memset(&terminator, 0, sizeof(terminator));\n'''
if text.count(old) != 1:
    raise SystemExit(f"expected one condition-terminator anchor, found {text.count(old)}")
text = text.replace(old, new, 1)

old = '''    if (condition_expression == NULL || then_source == NULL ||\n        !minic_type_is_integer(condition_expression->type)) {\n        return MINIC_CORE_LOWER_UNSUPPORTED;\n    }\n'''
new = '''    if (condition_expression == NULL || then_source == NULL ||\n        (!minic_type_is_integer(condition_expression->type) &&\n         !minic_type_is_pointer(condition_expression->type))) {\n        return MINIC_CORE_LOWER_UNSUPPORTED;\n    }\n'''
if text.count(old) != 1:
    raise SystemExit(f"expected one if-condition anchor, found {text.count(old)}")
text = text.replace(old, new, 1)

path.write_text(text)

Path("tests/compiler/c0/core_pointer_condition_m21.c").write_text(r'''struct core_m21_node {
    struct core_m21_node *next;
    struct core_m21_node **pprev;
};

int core_m21_pointer_if(int *pointer) {
    if (pointer)
        return 7;
    return 3;
}

int core_m21_pointer_not(int *pointer) {
    if (!pointer)
        return 11;
    return 5;
}

void core_m21_hlist_del(struct core_m21_node *node) {
    struct core_m21_node *next = node->next;
    struct core_m21_node **pprev = node->pprev;

    *pprev = next;
    if (next)
        next->pprev = pprev;
}
''')

Path("tests/compiler/c0/core_pointer_condition_m21_runtime.c").write_text(r'''#include <stdio.h>

struct core_m21_node {
    struct core_m21_node *next;
    struct core_m21_node **pprev;
};

int core_m21_pointer_if(int *pointer);
int core_m21_pointer_not(int *pointer);
void core_m21_hlist_del(struct core_m21_node *node);

int main(void) {
    int value = 1;
    struct core_m21_node *first;
    struct core_m21_node a;
    struct core_m21_node b;
    int first_updated;
    int back_link_updated;
    int tail_cleared;

    printf("if=%d,%d\n", core_m21_pointer_if(&value), core_m21_pointer_if(0));
    printf("not=%d,%d\n", core_m21_pointer_not(&value), core_m21_pointer_not(0));

    first = &a;
    a.next = &b;
    a.pprev = &first;
    b.next = 0;
    b.pprev = &a.next;
    core_m21_hlist_del(&a);
    first_updated = first == &b;
    back_link_updated = b.pprev == &first;

    first = &a;
    a.next = 0;
    a.pprev = &first;
    core_m21_hlist_del(&a);
    tail_cleared = first == 0;

    printf("hlist=%d,%d,%d\n", first_updated, back_link_updated, tail_cleared);
    return 0;
}
''')

Path("tests/compiler/c0/run-core-pointer-condition-m21.sh").write_text(r'''#!/usr/bin/env bash
set -Eeuo pipefail

: "${MINIC:?MINIC is required}"
: "${BUILD_DIR:?BUILD_DIR is required}"
: "${RISCV_CC:=riscv64-linux-gnu-gcc}"
: "${QEMU_RISCV64:=qemu-riscv64}"
: "${HOST_CC:=cc}"

root_dir=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
source_file="$root_dir/tests/compiler/c0/core_pointer_condition_m21.c"
runtime_file="$root_dir/tests/compiler/c0/core_pointer_condition_m21_runtime.c"
mkdir -p "$BUILD_DIR"

"$HOST_CC" -E -P -std=gnu11 "$source_file" -o "$BUILD_DIR/input.i"
MINIC_CORE_IR=strict "$MINIC" -S "$BUILD_DIR/input.i" -o "$BUILD_DIR/strict.s"
MINIC_CORE_CODEGEN=basic-v0 "$MINIC" -S "$BUILD_DIR/input.i" -o "$BUILD_DIR/core.s"

"$RISCV_CC" -static -O2 "$source_file" "$runtime_file" -o "$BUILD_DIR/reference-rv64"
"$RISCV_CC" -static -O2 "$runtime_file" "$BUILD_DIR/core.s" -o "$BUILD_DIR/minic-rv64"

"$QEMU_RISCV64" "$BUILD_DIR/reference-rv64" > "$BUILD_DIR/reference.out"
"$QEMU_RISCV64" "$BUILD_DIR/minic-rv64" > "$BUILD_DIR/minic.out"
diff -u "$BUILD_DIR/reference.out" "$BUILD_DIR/minic.out"
grep -Fx 'if=7,3' "$BUILD_DIR/minic.out" >/dev/null
grep -Fx 'not=5,11' "$BUILD_DIR/minic.out" >/dev/null
grep -Fx 'hlist=1,1,1' "$BUILD_DIR/minic.out" >/dev/null
printf 'PASS compiler/c0/core-pointer-condition-m21\n'
''')
print("staged M21 Core pointer scalar conditions")
