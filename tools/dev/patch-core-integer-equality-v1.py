from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly one match, found {count}")
    return text.replace(old, new, 1)


root = Path(__file__).resolve().parents[2]
header_path = root / "src/core/core_ir.h"
ir_path = root / "src/core/core_ir.c"
lower_path = root / "src/core/core_lower.c"
codegen_path = root / "src/target/riscv64/core_codegen.c"
gate_path = root / ".github/scripts/compiler-c0-full-gate.sh"
source_path = root / "tests/compiler/c0/core_integer_equality.c"
runtime_path = root / "tests/compiler/c0/core_integer_equality_runtime.c"
runner_path = root / "tests/compiler/c0/run-core-integer-equality.sh"

header = header_path.read_text()
header = replace_once(
    header,
    "    MINIC_CORE_INSTRUCTION_INTEGER_ADD,\n    MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION,\n",
    "    MINIC_CORE_INSTRUCTION_INTEGER_ADD,\n    MINIC_CORE_INSTRUCTION_INTEGER_EQUAL,\n    MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION,\n",
    "integer equality opcode",
)
header_path.write_text(header)

ir = ir_path.read_text()
old_verify = r'''    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION:
        return instruction_result_is_valid(function, instruction) &&
               minic_type_is_integer(instruction->type) &&
               instruction->value.operand < function->value_count &&
               available_values[instruction->value.operand] &&
               minic_type_is_integer(function->values[instruction->value.operand].type);
'''
new_verify = r'''    case MINIC_CORE_INSTRUCTION_INTEGER_EQUAL:
        if (!instruction_result_is_valid(function, instruction) ||
            !minic_type_equal(instruction->type, minic_type_int()) ||
            instruction->value.binary.left >= function->value_count ||
            instruction->value.binary.right >= function->value_count ||
            !available_values[instruction->value.binary.left] ||
            !available_values[instruction->value.binary.right]) {
            return false;
        }
        left = &function->values[instruction->value.binary.left];
        right = &function->values[instruction->value.binary.right];
        return minic_type_is_integer(left->type) && minic_type_equal(left->type, right->type);
    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION:
        return instruction_result_is_valid(function, instruction) &&
               minic_type_is_integer(instruction->type) &&
               instruction->value.operand < function->value_count &&
               available_values[instruction->value.operand] &&
               minic_type_is_integer(function->values[instruction->value.operand].type);
'''
ir = replace_once(ir, old_verify, new_verify, "integer equality verifier")
old_dump = r'''    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION:
        return fprintf(output,
                       "  %%%" PRIu32 " = convert.int %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.operand) >= 0;
'''
new_dump = r'''    case MINIC_CORE_INSTRUCTION_INTEGER_EQUAL:
        return fprintf(output,
                       "  %%%" PRIu32 " = eq.int %%%" PRIu32 ", %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.binary.left,
                       instruction->value.binary.right) >= 0;
    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION:
        return fprintf(output,
                       "  %%%" PRIu32 " = convert.int %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.operand) >= 0;
'''
ir = replace_once(ir, old_dump, new_dump, "integer equality dump")
ir_path.write_text(ir)

lower = lower_path.read_text()
anchor = r'''    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        expression->value.binary.operator_kind == MINIC_BINARY_ADD) {
'''
equality = r'''    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        expression->value.binary.operator_kind == MINIC_BINARY_EQUAL) {
        MinicCoreValueId left;
        MinicCoreValueId right;
        MinicCoreLowerStatus status;

        if (!minic_type_equal(expression->type, minic_type_int())) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = lower_expression(context, expression->value.binary.left, &left);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = lower_expression(context, expression->value.binary.right, &right);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (left >= context->function->value_count || right >= context->function->value_count ||
            !minic_type_is_integer(context->function->values[left].type) ||
            !minic_type_equal(context->function->values[left].type,
                              context->function->values[right].type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_EQUAL;
        instruction.type = minic_type_int();
        instruction.value.binary.left = left;
        instruction.value.binary.right = right;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
'''
lower = replace_once(lower, anchor, equality + anchor, "lower same-type integer equality")
lower_path.write_text(lower)

codegen = codegen_path.read_text()
codegen = replace_once(
    codegen,
    "    case MINIC_CORE_INSTRUCTION_INTEGER_ADD:\n    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION:\n",
    "    case MINIC_CORE_INSTRUCTION_INTEGER_ADD:\n    case MINIC_CORE_INSTRUCTION_INTEGER_EQUAL:\n    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION:\n",
    "integer equality codegen support",
)
old_emit = r'''    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION:
        if (!load_core_value(file, frame, instruction->value.operand, "t0") ||
            !minic_riscv64_emit_integer_conversion_for_program(
                file, program, instruction->type, "t0")) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
'''
new_emit = r'''    case MINIC_CORE_INSTRUCTION_INTEGER_EQUAL:
        if (!load_core_value(file, frame, instruction->value.binary.left, "t0") ||
            !load_core_value(file, frame, instruction->value.binary.right, "t1") ||
            fprintf(file, "  xor t0, t0, t1\n  seqz t0, t0\n") < 0) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION:
        if (!load_core_value(file, frame, instruction->value.operand, "t0") ||
            !minic_riscv64_emit_integer_conversion_for_program(
                file, program, instruction->type, "t0")) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
'''
codegen = replace_once(codegen, old_emit, new_emit, "emit integer equality")
codegen_path.write_text(codegen)

source_path.write_text(
    r'''extern int core_m5b_global;

int core_m5b_equal(int expected) {
    return core_m5b_global == expected;
}

void core_m5b_set_if_equal(int value, int expected) {
    if (core_m5b_global == expected)
        core_m5b_global = value;
}
'''
)
runtime_path.write_text(
    r'''#include <stdio.h>

int core_m5b_global = 13;

int core_m5b_equal(int expected);
void core_m5b_set_if_equal(int value, int expected);

int main(void) {
    int before_equal;
    int before_unequal;

    before_equal = core_m5b_equal(13);
    before_unequal = core_m5b_equal(12);
    core_m5b_set_if_equal(21, 12);
    core_m5b_set_if_equal(29, 13);
    (void)printf("%d %d %d\n", before_equal, before_unequal, core_m5b_global);
    return 0;
}
'''
)
runner_path.write_text(
    r'''#!/bin/sh
set -eu

: "${MINIC:?MINIC must point to the compiler binary}"
: "${RISCV_CC:=riscv64-linux-gnu-gcc}"
: "${QEMU_RISCV64:=qemu-riscv64}"

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work="${BUILD_DIR:-$root/build/core-integer-equality}"
source_file="$root/tests/compiler/c0/core_integer_equality.c"
runtime_file="$root/tests/compiler/c0/core_integer_equality_runtime.c"
mkdir -p "$work"

cc -E -P -std=gnu11 "$source_file" -o "$work/core_integer_equality.i"
MINIC_CORE_IR=strict "$MINIC" -S "$work/core_integer_equality.i" \
    -o "$work/core_integer_equality-strict.s"
MINIC_CORE_CODEGEN=basic-v0 "$MINIC" -S "$work/core_integer_equality.i" \
    -o "$work/core_integer_equality-core.s"

grep -q '^core_m5b_equal:' "$work/core_integer_equality-core.s"
grep -q '^core_m5b_set_if_equal:' "$work/core_integer_equality-core.s"
grep -q 'xor t0, t0, t1' "$work/core_integer_equality-core.s"
grep -q 'seqz t0, t0' "$work/core_integer_equality-core.s"
grep -q 'la t0, core_m5b_global' "$work/core_integer_equality-core.s"

"$RISCV_CC" -static -O2 "$source_file" "$runtime_file" -o "$work/reference-rv64"
"$RISCV_CC" -static -O2 "$runtime_file" "$work/core_integer_equality-core.s" \
    -o "$work/minic-rv64"
"$QEMU_RISCV64" "$work/reference-rv64" >"$work/reference.out"
"$QEMU_RISCV64" "$work/minic-rv64" >"$work/minic.out"
cmp "$work/reference.out" "$work/minic.out"
printf '%s\n' 'PASS compiler/c0/core-integer-equality'
'''
)

gate = gate_path.read_text()
anchor_gate = r'''core_global_scalar_memory_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \
    BUILD_DIR="$root/build/ci-core-global-scalar-memory" \
    RISCV_CC=riscv64-linux-gnu-gcc \
    QEMU_RISCV64=qemu-riscv64 \
        sh tests/compiler/c0/run-core-global-scalar-memory.sh
}

'''
gate = replace_once(
    gate,
    anchor_gate,
    anchor_gate
    + r'''core_integer_equality_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \
    BUILD_DIR="$root/build/ci-core-integer-equality" \
    RISCV_CC=riscv64-linux-gnu-gcc \
    QEMU_RISCV64=qemu-riscv64 \
        sh tests/compiler/c0/run-core-integer-equality.sh
}

''',
    "gate helper",
)
gate = replace_once(
    gate,
    "start_gate core-global-scalar-memory-focused core_global_scalar_memory_focused\n",
    "start_gate core-global-scalar-memory-focused core_global_scalar_memory_focused\n"
    "start_gate core-integer-equality-focused core_integer_equality_focused\n",
    "gate invocation",
)
gate_path.write_text(gate)
