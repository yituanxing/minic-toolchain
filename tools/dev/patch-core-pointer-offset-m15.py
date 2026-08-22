#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one replacement, found {count}: {old[:160]!r}")
    target.write_text(text.replace(old, new, 1))


# Share the C/GNU pointer-arithmetic stride query between legacy codegen and Core.
replace_once(
    "src/frontend/expression_semantics.h",
    '#include "frontend/ast.h"\n#include "target/target_info.h"\n',
    '#include "frontend/ast.h"\n#include "target/data_layout.h"\n#include "target/target_info.h"\n',
)
replace_once(
    "src/frontend/expression_semantics.h",
    "\n#endif\n",
    """
bool minic_c0_pointer_arithmetic_element_size(const MinicC0Program *program,
                                              const MinicDataLayout *layout,
                                              MinicType pointer_type,
                                              size_t *element_size);

#endif
""",
)
replace_once(
    "src/frontend/expression_semantics.c",
    '#include "frontend/expression_semantics.h"\n\n',
    '''#include "frontend/expression_semantics.h"

bool minic_c0_pointer_arithmetic_element_size(const MinicC0Program *program,
                                              const MinicDataLayout *layout,
                                              MinicType pointer_type,
                                              size_t *element_size) {
    MinicType pointee;
    size_t alignment;

    if (program == NULL || layout == NULL || element_size == NULL ||
        !minic_type_pointee(pointer_type, &pointee)) {
        return false;
    }
    /* GNU C gives void* and function-pointer arithmetic a byte stride. */
    if (minic_type_is_void(pointee) || minic_type_is_function(pointee)) {
        *element_size = 1U;
        return true;
    }
    return minic_data_layout_type(layout, program, pointee, element_size, &alignment) &&
           *element_size != 0U;
}

''',
)

legacy = Path("src/target/riscv64/codegen_expression.c")
legacy_text = legacy.read_text()
legacy_helper = '''static bool minic_riscv64_pointer_element_size(const MinicC0Program *program,
                                               MinicType pointer_type,
                                               size_t *element_size) {
    MinicType pointee;
    size_t element_alignment;

    if (element_size == NULL || !minic_type_pointee(pointer_type, &pointee)) {
        return false;
    }
    if (minic_type_is_void(pointee) || minic_type_is_function(pointee)) {
        *element_size = 1U;
        return true;
    }
    return minic_riscv64_type_layout(program, pointee, element_size, &element_alignment);
}

'''
if legacy_text.count(legacy_helper) != 1:
    raise SystemExit("legacy pointer element-size helper shape changed")
legacy_text = legacy_text.replace(legacy_helper, "", 1)
old_call = "minic_riscv64_pointer_element_size(program, "
call_count = legacy_text.count(old_call)
if call_count == 0:
    raise SystemExit("legacy pointer element-size helper has no consumers")
legacy_text = legacy_text.replace(
    old_call,
    "minic_c0_pointer_arithmetic_element_size(program, minic_default_data_layout(), ",
)
legacy.write_text(legacy_text)

# Core IR models resolved C pointer arithmetic as base + integer-index * byte stride.
replace_once(
    "src/core/core_ir.h",
    """    MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS,
    MINIC_CORE_INSTRUCTION_FIELD_ADDRESS,
    MINIC_CORE_INSTRUCTION_LOAD,
""",
    """    MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS,
    MINIC_CORE_INSTRUCTION_FIELD_ADDRESS,
    MINIC_CORE_INSTRUCTION_POINTER_OFFSET,
    MINIC_CORE_INSTRUCTION_LOAD,
""",
)
replace_once(
    "src/core/core_ir.h",
    """        struct {
            MinicCoreValueId base;
            MinicRecordId record_id;
            size_t field_index;
        } field_address;
        struct {
            MinicCoreValueId address;
""",
    """        struct {
            MinicCoreValueId base;
            MinicRecordId record_id;
            size_t field_index;
        } field_address;
        struct {
            MinicCoreValueId base;
            MinicCoreValueId index;
            size_t element_size;
        } pointer_offset;
        struct {
            MinicCoreValueId address;
""",
)

replace_once(
    "src/core/core_ir.c",
    """    case MINIC_CORE_INSTRUCTION_LOAD: {
""",
    """    case MINIC_CORE_INSTRUCTION_POINTER_OFFSET: {
        MinicCoreValueId base;
        MinicCoreValueId index;

        base = instruction->value.pointer_offset.base;
        index = instruction->value.pointer_offset.index;
        return instruction_result_is_valid(function, instruction) &&
               minic_type_is_pointer(instruction->type) && base < function->value_count &&
               index < function->value_count && available_values[base] && available_values[index] &&
               minic_type_equal(function->values[base].type, instruction->type) &&
               minic_type_is_integer(function->values[index].type) &&
               instruction->value.pointer_offset.element_size != 0U;
    }
    case MINIC_CORE_INSTRUCTION_LOAD: {
""",
)
replace_once(
    "src/core/core_ir.c",
    """    case MINIC_CORE_INSTRUCTION_LOAD:
""",
    """    case MINIC_CORE_INSTRUCTION_POINTER_OFFSET:
        return fprintf(output,
                       "  %%%" PRIu32 " = pointer.offset %%%" PRIu32 ", %%%" PRIu32 ", stride=%zu\\n",
                       instruction->result,
                       instruction->value.pointer_offset.base,
                       instruction->value.pointer_offset.index,
                       instruction->value.pointer_offset.element_size) >= 0;
    case MINIC_CORE_INSTRUCTION_LOAD:
""",
)

# Lower both pointer+integer and integer+pointer without encoding C scaling in the backend.
pointer_lower = r'''    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        expression->value.binary.operator_kind == MINIC_BINARY_ADD &&
        minic_type_is_pointer(expression->type)) {
        const MinicExpression *left_expression;
        const MinicExpression *pointer_expression;
        const MinicExpression *right_expression;
        const MinicExpression *index_expression;
        MinicExpressionId pointer_id;
        MinicExpressionId index_id;
        MinicCoreValueId pointer_value;
        MinicCoreValueId index_value;
        MinicCoreLowerStatus status;
        size_t element_size;

        left_expression =
            minic_c0_program_expression(context->body->program, expression->value.binary.left);
        right_expression =
            minic_c0_program_expression(context->body->program, expression->value.binary.right);
        if (left_expression == NULL || right_expression == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (minic_type_is_pointer(left_expression->type) &&
            minic_type_is_integer(right_expression->type)) {
            pointer_expression = left_expression;
            index_expression = right_expression;
            pointer_id = expression->value.binary.left;
            index_id = expression->value.binary.right;
        } else if (minic_type_is_integer(left_expression->type) &&
                   minic_type_is_pointer(right_expression->type)) {
            pointer_expression = right_expression;
            index_expression = left_expression;
            pointer_id = expression->value.binary.right;
            index_id = expression->value.binary.left;
        } else {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        if (!minic_type_equal(pointer_expression->type, expression->type) ||
            !minic_c0_pointer_arithmetic_element_size(context->body->program,
                                                      minic_default_data_layout(),
                                                      expression->type,
                                                      &element_size)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_expression(context, pointer_id, &pointer_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = lower_expression(context, index_id, &index_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (pointer_value >= context->function->value_count ||
            index_value >= context->function->value_count ||
            !minic_type_equal(context->function->values[pointer_value].type,
                              pointer_expression->type) ||
            !minic_type_equal(context->function->values[index_value].type,
                              index_expression->type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        instruction.kind = MINIC_CORE_INSTRUCTION_POINTER_OFFSET;
        instruction.value.pointer_offset.base = pointer_value;
        instruction.value.pointer_offset.index = index_value;
        instruction.value.pointer_offset.element_size = element_size;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
'''
replace_once(
    "src/core/core_lower.c",
    """    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        expression->value.binary.operator_kind == MINIC_BINARY_ADD) {
""",
    pointer_lower + """    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        expression->value.binary.operator_kind == MINIC_BINARY_ADD) {
""",
)

replace_once(
    "src/target/riscv64/core_codegen.c",
    """    case MINIC_CORE_INSTRUCTION_LOAD:
    case MINIC_CORE_INSTRUCTION_STORE:
        return true;
""",
    """    case MINIC_CORE_INSTRUCTION_POINTER_OFFSET:
    case MINIC_CORE_INSTRUCTION_LOAD:
    case MINIC_CORE_INSTRUCTION_STORE:
        return true;
""",
)
replace_once(
    "src/target/riscv64/core_codegen.c",
    """    case MINIC_CORE_INSTRUCTION_PARAMETER:
        return emit_parameter(file, program, function, frame, instruction);
""",
    """    case MINIC_CORE_INSTRUCTION_POINTER_OFFSET:
        if (!load_core_value(file, frame, instruction->value.pointer_offset.base, "t0") ||
            !load_core_value(file, frame, instruction->value.pointer_offset.index, "t1")) {
            return false;
        }
        if (instruction->value.pointer_offset.element_size != 1U &&
            fprintf(file,
                    "  li t2, %zu\\n"
                    "  mul t1, t1, t2\\n",
                    instruction->value.pointer_offset.element_size) < 0) {
            return false;
        }
        if (fprintf(file, "  add t0, t0, t1\\n") < 0) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_PARAMETER:
        return emit_parameter(file, program, function, frame, instruction);
""",
)

Path("tests/compiler/c0/core_pointer_offset.c").write_text(
    r'''struct core_m15_node {
    struct core_m15_node *next;
    struct core_m15_node *prev;
};

void *core_m15_void_add(void *base, long index) {
    return base + index;
}

int *core_m15_int_add(int *base, long index) {
    return base + index;
}

int *core_m15_int_add_commuted(long index, int *base) {
    return index + base;
}

void core_m15_list_poison(struct core_m15_node *entry) {
    entry->next = (void *)0x100 + 0UL;
    entry->prev = (void *)0x122 + 0UL;
}
'''
)
Path("tests/compiler/c0/core_pointer_offset_runtime.c").write_text(
    r'''#include <stdint.h>
#include <stdio.h>

struct core_m15_node {
    struct core_m15_node *next;
    struct core_m15_node *prev;
};

void *core_m15_void_add(void *base, long index);
int *core_m15_int_add(int *base, long index);
int *core_m15_int_add_commuted(long index, int *base);
void core_m15_list_poison(struct core_m15_node *entry);

int main(void) {
    int values[8] = {0};
    struct core_m15_node node = {0};
    char *byte_base = (char *)&values[2];

    core_m15_list_poison(&node);
    printf("%td %td %td %lu %lu\n",
           (char *)core_m15_void_add(byte_base, 3) - byte_base,
           core_m15_int_add(&values[4], -2) - &values[4],
           core_m15_int_add_commuted(3, &values[1]) - &values[1],
           (unsigned long)(uintptr_t)node.next,
           (unsigned long)(uintptr_t)node.prev);
    return 0;
}
'''
)
Path("tests/compiler/c0/run-core-pointer-offset.sh").write_text(
    r'''#!/bin/sh
set -eu
: "${MINIC:?MINIC must point to the compiler binary}"
: "${RISCV_CC:=riscv64-linux-gnu-gcc}"
: "${QEMU_RISCV64:=qemu-riscv64}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work="${BUILD_DIR:-$root/build/core-pointer-offset}"
source_file="$root/tests/compiler/c0/core_pointer_offset.c"
runtime_file="$root/tests/compiler/c0/core_pointer_offset_runtime.c"
mkdir -p "$work"
cc -E -P -std=gnu11 "$source_file" -o "$work/input.i"
MINIC_CORE_IR=strict "$MINIC" -S "$work/input.i" -o "$work/strict.s"
MINIC_CORE_CODEGEN=basic-v0 "$MINIC" -S "$work/input.i" -o "$work/core.s"
for symbol in core_m15_void_add core_m15_int_add core_m15_int_add_commuted core_m15_list_poison; do
    grep -q "^${symbol}:" "$work/core.s"
done
"$RISCV_CC" -static -O2 -std=gnu11 "$source_file" "$runtime_file" -o "$work/reference-rv64"
"$RISCV_CC" -static -O2 "$runtime_file" "$work/core.s" -o "$work/minic-rv64"
"$QEMU_RISCV64" "$work/reference-rv64" >"$work/reference.out"
"$QEMU_RISCV64" "$work/minic-rv64" >"$work/minic.out"
cmp "$work/reference.out" "$work/minic.out"
printf '%s\n' 'PASS compiler/c0/core-pointer-offset'
'''
)

replace_once(
    ".github/scripts/compiler-c0-full-gate.sh",
    """core_scalar_not_equal_focused() {
""",
    """core_pointer_offset_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \\
    BUILD_DIR="$root/build/ci-core-pointer-offset" \\
    RISCV_CC=riscv64-linux-gnu-gcc \\
    QEMU_RISCV64=qemu-riscv64 \\
        sh tests/compiler/c0/run-core-pointer-offset.sh
}

core_scalar_not_equal_focused() {
""",
)
replace_once(
    ".github/scripts/compiler-c0-full-gate.sh",
    """start_gate core-integer-bitwise-and-assignment-focused core_integer_bitwise_and_assignment_focused
""",
    """start_gate core-integer-bitwise-and-assignment-focused core_integer_bitwise_and_assignment_focused
start_gate core-pointer-offset-focused core_pointer_offset_focused
""",
)

print("staged M15 shared pointer stride ownership and Core pointer offsets")
