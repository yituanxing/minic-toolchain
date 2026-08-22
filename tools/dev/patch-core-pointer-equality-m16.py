#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected 1 match, found {count}")
    target.write_text(text.replace(old, new, 1))


def replace_region(path: str, start_marker: str, end_marker: str, replacement: str, label: str) -> None:
    target = Path(path)
    text = target.read_text()
    start = text.find(start_marker)
    end = text.find(end_marker, start + len(start_marker)) if start >= 0 else -1
    if start < 0 or end < 0:
        raise SystemExit(f"{label}: cannot locate replacement region")
    target.write_text(text[:start] + replacement + text[end:])


def write_new(path: str, content: str) -> None:
    target = Path(path)
    if target.exists():
        raise SystemExit(f"{path}: already exists")
    target.write_text(content)


replace_once(
    "src/core/core_ir.h",
    "void minic_core_function_initialize(MinicCoreFunction *function);\n",
    "bool minic_core_scalar_bitcast_types_valid(MinicType target_type, MinicType source_type);\n"
    "void minic_core_function_initialize(MinicCoreFunction *function);\n",
    "Core scalar bitcast structural owner declaration",
)

replace_once(
    "src/core/core_ir.c",
    '''static bool core_scalar_bitcast_types_valid(MinicType target_type, MinicType source_type) {
    return (minic_type_is_pointer(target_type) &&
            (minic_type_is_pointer(source_type) || minic_type_is_integer(source_type))) ||
           (minic_type_is_integer(target_type) && minic_type_is_pointer(source_type));
}
''',
    '''bool minic_core_scalar_bitcast_types_valid(MinicType target_type, MinicType source_type) {
    return (minic_type_is_pointer(target_type) &&
            (minic_type_is_pointer(source_type) || minic_type_is_integer(source_type))) ||
           (minic_type_is_integer(target_type) && minic_type_is_pointer(source_type));
}
''',
    "Core scalar bitcast structural owner implementation",
)
replace_once(
    "src/core/core_ir.c",
    "core_scalar_bitcast_types_valid(instruction->type,\n                                               function->values[instruction->value.operand].type)",
    "minic_core_scalar_bitcast_types_valid(\n                   instruction->type, function->values[instruction->value.operand].type)",
    "Core verifier bitcast owner consumer",
)

replace_once(
    "src/core/core_lower.c",
    '''static bool core_scalar_bitcast_types(MinicType target_type, MinicType source_type) {
    return (minic_type_is_pointer(target_type) &&
            (minic_type_is_pointer(source_type) || minic_type_is_integer(source_type))) ||
           (minic_type_is_integer(target_type) && minic_type_is_pointer(source_type));
}
''',
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
    "Core runtime scalar value-type seam",
)
replace_once(
    "src/core/core_lower.c",
    "if (!core_scalar_bitcast_types(target_type, source->type)) {",
    "if (!minic_core_scalar_bitcast_types_valid(target_type, source->type)) {",
    "Core lowering bitcast structural owner consumer",
)

lower_scalar_equality_operands = r'''static MinicCoreLowerStatus lower_scalar_equality_operands(MinicCoreLowerContext *context,
                                                            MinicExpressionId left_id,
                                                            MinicExpressionId right_id,
                                                            MinicCoreValueId *left_value,
                                                            MinicCoreValueId *right_value) {
    const MinicExpression *left_expression;
    const MinicExpression *right_expression;
    MinicCoreValueId left_source;
    MinicCoreValueId right_source;
    MinicCoreLowerStatus status;
    MinicType comparison_type;
    MinicType left_type;
    MinicType right_type;
    bool pointer_comparison;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || left_value == NULL || right_value == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    left_expression = minic_c0_program_expression(context->body->program, left_id);
    right_expression = minic_c0_program_expression(context->body->program, right_id);
    if (left_expression == NULL || right_expression == NULL ||
        !core_scalar_expression_value_type(left_expression, &left_type) ||
        !core_scalar_expression_value_type(right_expression, &right_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }

    pointer_comparison = false;
    if (minic_type_is_integer(left_type) && minic_type_is_integer(right_type)) {
        if (!minic_type_equal(left_type, right_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        comparison_type = left_type;
    } else if (minic_type_is_pointer(left_type) && minic_type_is_pointer(right_type)) {
        if (!minic_type_pointer_equality_compatible(left_type, right_type) ||
            !minic_type_conditional_pointer_common(left_type, right_type, &comparison_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        pointer_comparison = true;
    } else if (minic_type_is_pointer(left_type) && minic_type_is_integer(right_type) &&
               minic_c0_expression_is_null_pointer_constant_v0(context->body->program, right_id)) {
        comparison_type = left_type;
        pointer_comparison = true;
    } else if (minic_type_is_integer(left_type) && minic_type_is_pointer(right_type) &&
               minic_c0_expression_is_null_pointer_constant_v0(context->body->program, left_id)) {
        comparison_type = right_type;
        pointer_comparison = true;
    } else {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }

    status = lower_expression(context, left_id, &left_source);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    status = lower_expression(context, right_id, &right_source);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    if (left_source >= context->function->value_count ||
        right_source >= context->function->value_count ||
        !minic_type_equal(context->function->values[left_source].type, left_type) ||
        !minic_type_equal(context->function->values[right_source].type, right_type)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    if (!pointer_comparison) {
        *left_value = left_source;
        *right_value = right_source;
        return MINIC_CORE_LOWER_OK;
    }
    status = append_scalar_bitcast(
        context, left_expression->span, comparison_type, left_source, left_value);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    return append_scalar_bitcast(
        context, right_expression->span, comparison_type, right_source, right_value);
}

'''
replace_once(
    "src/core/core_lower.c",
    "static MinicCoreLowerStatus lower_integer_assignment_value(MinicCoreLowerContext *context,\n",
    lower_scalar_equality_operands
    + "static MinicCoreLowerStatus lower_integer_assignment_value(MinicCoreLowerContext *context,\n",
    "Core scalar equality operand normalization seam",
)

bitcast_block = r'''    if (expression->kind == MINIC_EXPRESSION_BITCAST) {
        const MinicExpression *operand;
        MinicCoreValueId operand_value;
        MinicCoreLowerStatus status;
        MinicType operand_value_type;

        operand =
            minic_c0_program_expression(context->body->program, expression->value.unary.operand);
        if (operand == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (!core_scalar_expression_value_type(operand, &operand_value_type) ||
            !minic_core_scalar_bitcast_types_valid(expression->type, operand_value_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_expression(context, expression->value.unary.operand, &operand_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (operand_value >= context->function->value_count ||
            !minic_type_equal(context->function->values[operand_value].type, operand_value_type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        return append_scalar_bitcast(
            context, expression->span, expression->type, operand_value, value_id);
    }
'''
replace_region(
    "src/core/core_lower.c",
    "    if (expression->kind == MINIC_EXPRESSION_BITCAST) {\n",
    "    if (expression->kind == MINIC_EXPRESSION_BUILTIN_OVERFLOW &&\n",
    bitcast_block,
    "Core explicit scalar bitcast lowering contract",
)

equality_blocks = r'''    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        expression->value.binary.operator_kind == MINIC_BINARY_EQUAL) {
        MinicCoreValueId left;
        MinicCoreValueId right;
        MinicCoreLowerStatus status;

        if (!minic_type_equal(expression->type, minic_type_int())) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = lower_scalar_equality_operands(context,
                                                expression->value.binary.left,
                                                expression->value.binary.right,
                                                &left,
                                                &right);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_SCALAR_EQUAL;
        instruction.span = expression->span;
        instruction.type = minic_type_int();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.binary.left = left;
        instruction.value.binary.right = right;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        expression->value.binary.operator_kind == MINIC_BINARY_NOT_EQUAL) {
        MinicCoreInstruction zero_test_instruction;
        MinicCoreValueId equal_value;
        MinicCoreValueId left;
        MinicCoreValueId right;
        MinicCoreLowerStatus status;

        if (!minic_type_equal(expression->type, minic_type_int())) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = lower_scalar_equality_operands(context,
                                                expression->value.binary.left,
                                                expression->value.binary.right,
                                                &left,
                                                &right);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_SCALAR_EQUAL;
        instruction.span = expression->span;
        instruction.type = minic_type_int();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.binary.left = left;
        instruction.value.binary.right = right;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &equal_value)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&zero_test_instruction, 0, sizeof(zero_test_instruction));
        zero_test_instruction.kind = MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO;
        zero_test_instruction.span = expression->span;
        zero_test_instruction.type = minic_type_int();
        zero_test_instruction.result = MINIC_CORE_VALUE_INVALID;
        zero_test_instruction.value.operand = equal_value;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &zero_test_instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
'''
replace_region(
    "src/core/core_lower.c",
    "    if (expression->kind == MINIC_EXPRESSION_BINARY &&\n        expression->value.binary.operator_kind == MINIC_BINARY_EQUAL) {\n",
    "    if (expression->kind == MINIC_EXPRESSION_BINARY &&\n        expression->value.binary.operator_kind == MINIC_BINARY_ADD &&\n        minic_type_is_pointer(expression->type)) {\n",
    equality_blocks,
    "Core scalar equality normalization",
)

replace_once(
    "src/target/riscv64/core_codegen.c",
    "    bool type_pair_valid;\n\n",
    "",
    "RV64 duplicate scalar bitcast structural state",
)
replace_once(
    "src/target/riscv64/core_codegen.c",
    '''    type_pair_valid =
        (minic_type_is_pointer(instruction->type) &&
         (minic_type_is_pointer(source->type) || minic_type_is_integer(source->type))) ||
        (minic_type_is_integer(instruction->type) && minic_type_is_pointer(source->type));
    if (!type_pair_valid ||
''',
    '''    if (!minic_core_scalar_bitcast_types_valid(instruction->type, source->type) ||
''',
    "RV64 Core bitcast structural owner consumer",
)

write_new(
    "tests/compiler/c0/core_pointer_equality_qualifiers.c",
    r'''struct core_m16_node {
    struct core_m16_node *next;
    struct core_m16_node *prev;
};

int core_m16_list_is_first(const struct core_m16_node *list,
                           const struct core_m16_node *head) {
    return list->prev == head;
}

int core_m16_list_is_last(const struct core_m16_node *list,
                          const struct core_m16_node *head) {
    return list->next == head;
}

int core_m16_qualified_equal(struct core_m16_node *left, const struct core_m16_node *right) {
    return left == right;
}

int core_m16_qualified_not_equal(struct core_m16_node *left, const struct core_m16_node *right) {
    return left != right;
}

int core_m16_explicit_qualified_member_cast(const struct core_m16_node *list,
                                            const struct core_m16_node *head) {
    return (const struct core_m16_node *)list->prev == head;
}

int core_m16_null_equal(const struct core_m16_node *node) {
    return node == 0;
}

int core_m16_void_equal(struct core_m16_node *left, const void *right) {
    return left == right;
}
''',
)

write_new(
    "tests/compiler/c0/core_pointer_equality_qualifiers_runtime.c",
    r'''#include <stdio.h>

struct core_m16_node {
    struct core_m16_node *next;
    struct core_m16_node *prev;
};

int core_m16_list_is_first(const struct core_m16_node *list,
                           const struct core_m16_node *head);
int core_m16_list_is_last(const struct core_m16_node *list,
                          const struct core_m16_node *head);
int core_m16_qualified_equal(struct core_m16_node *left, const struct core_m16_node *right);
int core_m16_qualified_not_equal(struct core_m16_node *left, const struct core_m16_node *right);
int core_m16_explicit_qualified_member_cast(const struct core_m16_node *list,
                                            const struct core_m16_node *head);
int core_m16_null_equal(const struct core_m16_node *node);
int core_m16_void_equal(struct core_m16_node *left, const void *right);

int main(void) {
    struct core_m16_node first;
    struct core_m16_node head;

    first.next = &head;
    first.prev = &head;
    head.next = &first;
    head.prev = &first;

    printf("%d %d %d %d %d %d %d %d %d\n",
           core_m16_list_is_first(&first, &head),
           core_m16_list_is_first(&first, &first),
           core_m16_list_is_last(&first, &head),
           core_m16_qualified_equal(&first, &first),
           core_m16_qualified_not_equal(&first, &head),
           core_m16_explicit_qualified_member_cast(&first, &head),
           core_m16_null_equal(&first),
           core_m16_null_equal(0),
           core_m16_void_equal(&first, &first));
    return 0;
}
''',
)

write_new(
    "tests/compiler/c0/run-core-pointer-equality-qualifiers.sh",
    r'''#!/bin/sh
set -eu
: "${MINIC:?MINIC must point to the compiler binary}"
: "${RISCV_CC:=riscv64-linux-gnu-gcc}"
: "${QEMU_RISCV64:=qemu-riscv64}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work="${BUILD_DIR:-$root/build/core-pointer-equality-qualifiers}"
source_file="$root/tests/compiler/c0/core_pointer_equality_qualifiers.c"
runtime_file="$root/tests/compiler/c0/core_pointer_equality_qualifiers_runtime.c"
mkdir -p "$work"
cc -E -P -std=gnu11 "$source_file" -o "$work/input.i"
MINIC_CORE_IR=strict "$MINIC" -S "$work/input.i" -o "$work/strict.s"
MINIC_CORE_CODEGEN=basic-v0 "$MINIC" -S "$work/input.i" -o "$work/core.s"
for symbol in core_m16_list_is_first core_m16_list_is_last core_m16_qualified_equal \
              core_m16_qualified_not_equal core_m16_explicit_qualified_member_cast \
              core_m16_null_equal core_m16_void_equal; do
    grep -q "^${symbol}:" "$work/core.s"
done
"$RISCV_CC" -static -O2 "$source_file" "$runtime_file" -o "$work/reference-rv64"
"$RISCV_CC" -static -O2 "$runtime_file" "$work/core.s" -o "$work/minic-rv64"
"$QEMU_RISCV64" "$work/reference-rv64" >"$work/reference.out"
"$QEMU_RISCV64" "$work/minic-rv64" >"$work/minic.out"
cmp "$work/reference.out" "$work/minic.out"
printf '%s\n' 'PASS compiler/c0/core-pointer-equality-qualifiers'
''',
)

replace_once(
    ".github/scripts/compiler-c0-full-gate.sh",
    '''core_pointer_offset_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \\
    BUILD_DIR="$root/build/ci-core-pointer-offset" \\
    RISCV_CC=riscv64-linux-gnu-gcc \\
    QEMU_RISCV64=qemu-riscv64 \\
        sh tests/compiler/c0/run-core-pointer-offset.sh
}

''',
    '''core_pointer_offset_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \\
    BUILD_DIR="$root/build/ci-core-pointer-offset" \\
    RISCV_CC=riscv64-linux-gnu-gcc \\
    QEMU_RISCV64=qemu-riscv64 \\
        sh tests/compiler/c0/run-core-pointer-offset.sh
}

core_pointer_equality_qualifiers_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \\
    BUILD_DIR="$root/build/ci-core-pointer-equality-qualifiers" \\
    RISCV_CC=riscv64-linux-gnu-gcc \\
    QEMU_RISCV64=qemu-riscv64 \\
        sh tests/compiler/c0/run-core-pointer-equality-qualifiers.sh
}

''',
    "C0 focused function registration",
)
replace_once(
    ".github/scripts/compiler-c0-full-gate.sh",
    "start_gate core-pointer-offset-focused core_pointer_offset_focused\n",
    "start_gate core-pointer-offset-focused core_pointer_offset_focused\n"
    "start_gate core-pointer-equality-qualifiers-focused core_pointer_equality_qualifiers_focused\n",
    "C0 focused gate registration",
)

print("staged M16 Core scalar value-type contract and pointer equality normalization")
