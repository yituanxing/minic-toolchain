from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    file = Path(path)
    text = file.read_text()
    if text.count(old) != 1:
        raise SystemExit(f"expected exactly one anchor in {path}, found {text.count(old)}")
    file.write_text(text.replace(old, new, 1))


replace_once(
    "src/core/core_ir.h",
    "    MINIC_CORE_INSTRUCTION_INTEGER_NEGATE,\n    MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO,",
    "    MINIC_CORE_INSTRUCTION_INTEGER_NEGATE,\n"
    "    MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT,\n"
    "    MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO,",
)

replace_once(
    "src/core/core_ir.c",
    "    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:\n"
    "        return instruction_result_is_valid(function, instruction) &&\n"
    "               minic_type_is_integer(instruction->type) &&\n"
    "               instruction->value.operand < function->value_count &&\n"
    "               available_values[instruction->value.operand] &&\n"
    "               minic_type_equal(function->values[instruction->value.operand].type,\n"
    "                                instruction->type);\n"
    "    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO:",
    "    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:\n"
    "    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT:\n"
    "        return instruction_result_is_valid(function, instruction) &&\n"
    "               minic_type_is_integer(instruction->type) &&\n"
    "               instruction->value.operand < function->value_count &&\n"
    "               available_values[instruction->value.operand] &&\n"
    "               minic_type_equal(function->values[instruction->value.operand].type,\n"
    "                                instruction->type);\n"
    "    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO:",
)

replace_once(
    "src/core/core_ir.c",
    "    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:\n"
    "        return fprintf(output,\n"
    "                       \"  %%%\" PRIu32 \" = ineg %%%\" PRIu32 \"\\n\",\n"
    "                       instruction->result,\n"
    "                       instruction->value.operand) >= 0;\n"
    "    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO:",
    "    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:\n"
    "        return fprintf(output,\n"
    "                       \"  %%%\" PRIu32 \" = ineg %%%\" PRIu32 \"\\n\",\n"
    "                       instruction->result,\n"
    "                       instruction->value.operand) >= 0;\n"
    "    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT:\n"
    "        return fprintf(output,\n"
    "                       \"  %%%\" PRIu32 \" = inot %%%\" PRIu32 \"\\n\",\n"
    "                       instruction->result,\n"
    "                       instruction->value.operand) >= 0;\n"
    "    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO:",
)

negate_block = """    if (expression->kind == MINIC_EXPRESSION_UNARY &&
        expression->value.unary.operator_kind == MINIC_UNARY_NEGATE) {
        MinicCoreValueId operand_value;
        MinicCoreLowerStatus status;

        if (!minic_type_is_integer(expression->type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_expression(context, expression->value.unary.operand, &operand_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (operand_value >= context->function->value_count ||
            !minic_type_equal(context->function->values[operand_value].type, expression->type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_NEGATE;
        instruction.span = expression->span;
        instruction.type = expression->type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.operand = operand_value;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
"""
bitwise_block = negate_block + """    if (expression->kind == MINIC_EXPRESSION_UNARY &&
        expression->value.unary.operator_kind == MINIC_UNARY_BITWISE_NOT) {
        MinicCoreValueId operand_value;
        MinicCoreLowerStatus status;

        if (!minic_type_is_integer(expression->type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_expression(context, expression->value.unary.operand, &operand_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (operand_value >= context->function->value_count ||
            !minic_type_equal(context->function->values[operand_value].type, expression->type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT;
        instruction.span = expression->span;
        instruction.type = expression->type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.operand = operand_value;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
"""
replace_once("src/core/core_lower.c", negate_block, bitwise_block)

replace_once(
    "src/target/riscv64/core_codegen.c",
    "    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:\n"
    "    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO:",
    "    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:\n"
    "    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT:\n"
    "    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO:",
)

replace_once(
    "src/target/riscv64/core_codegen.c",
    "    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:\n"
    "        if (!load_core_value(file, frame, instruction->value.operand, \"t0\") ||\n"
    "            fprintf(file, \"  neg t0, t0\\n\") < 0 ||\n"
    "            !minic_riscv64_emit_integer_conversion_for_program(\n"
    "                file, program, instruction->type, \"t0\")) {\n"
    "            return false;\n"
    "        }\n"
    "        return store_core_value(file, frame, instruction->result, \"t0\");\n"
    "    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO:",
    "    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:\n"
    "        if (!load_core_value(file, frame, instruction->value.operand, \"t0\") ||\n"
    "            fprintf(file, \"  neg t0, t0\\n\") < 0 ||\n"
    "            !minic_riscv64_emit_integer_conversion_for_program(\n"
    "                file, program, instruction->type, \"t0\")) {\n"
    "            return false;\n"
    "        }\n"
    "        return store_core_value(file, frame, instruction->result, \"t0\");\n"
    "    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT:\n"
    "        if (!load_core_value(file, frame, instruction->value.operand, \"t0\") ||\n"
    "            fprintf(file, \"  xori t0, t0, -1\\n\") < 0 ||\n"
    "            !minic_riscv64_emit_integer_conversion_for_program(\n"
    "                file, program, instruction->type, \"t0\")) {\n"
    "            return false;\n"
    "        }\n"
    "        return store_core_value(file, frame, instruction->result, \"t0\");\n"
    "    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO:",
)

source = """int core_m7_not_int(int value) {
    return ~value;
}

unsigned int core_m7_not_uint(unsigned int value) {
    return ~value;
}

long core_m7_not_long(long value) {
    return ~value;
}

unsigned long core_m7_not_ulong(unsigned long value) {
    return ~value;
}

unsigned long core_m7_size_max(void) {
    return ~0UL;
}
"""
Path("tests/compiler/c0/core_integer_bitwise_not.c").write_text(source)

runtime = """#include <stdio.h>

int core_m7_not_int(int value);
unsigned int core_m7_not_uint(unsigned int value);
long core_m7_not_long(long value);
unsigned long core_m7_not_ulong(unsigned long value);
unsigned long core_m7_size_max(void);

int main(void) {
    printf("%d %u %ld %lu %lu\\n",
           core_m7_not_int(0x12345678),
           core_m7_not_uint(0x89abcdefU),
           core_m7_not_long(0x123456789L),
           core_m7_not_ulong(0x123456789abcdef0UL),
           core_m7_size_max());
    return 0;
}
"""
Path("tests/compiler/c0/core_integer_bitwise_not_runtime.c").write_text(runtime)

runner = """#!/bin/sh
set -eu

: "${MINIC:?MINIC must point to the compiler binary}"
: "${RISCV_CC:=riscv64-linux-gnu-gcc}"
: "${QEMU_RISCV64:=qemu-riscv64}"

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work="${BUILD_DIR:-$root/build/core-integer-bitwise-not}"
source_file="$root/tests/compiler/c0/core_integer_bitwise_not.c"
runtime_file="$root/tests/compiler/c0/core_integer_bitwise_not_runtime.c"
mkdir -p "$work"

cc -E -P -std=gnu11 "$source_file" -o "$work/core_integer_bitwise_not.i"
MINIC_CORE_IR=strict "$MINIC" -S "$work/core_integer_bitwise_not.i" \
    -o "$work/core_integer_bitwise_not-strict.s"
MINIC_CORE_CODEGEN=basic-v0 "$MINIC" -S "$work/core_integer_bitwise_not.i" \
    -o "$work/core_integer_bitwise_not-core.s"

grep -q '^core_m7_not_int:' "$work/core_integer_bitwise_not-core.s"
grep -q '^core_m7_not_uint:' "$work/core_integer_bitwise_not-core.s"
grep -q '^core_m7_not_long:' "$work/core_integer_bitwise_not-core.s"
grep -q '^core_m7_not_ulong:' "$work/core_integer_bitwise_not-core.s"
grep -q '^core_m7_size_max:' "$work/core_integer_bitwise_not-core.s"
grep -q 'xori t0, t0, -1' "$work/core_integer_bitwise_not-core.s"

"$RISCV_CC" -static -O2 "$source_file" "$runtime_file" -o "$work/reference-rv64"
"$RISCV_CC" -static -O2 "$runtime_file" "$work/core_integer_bitwise_not-core.s" \
    -o "$work/minic-rv64"
"$QEMU_RISCV64" "$work/reference-rv64" >"$work/reference.out"
"$QEMU_RISCV64" "$work/minic-rv64" >"$work/minic.out"
cmp "$work/reference.out" "$work/minic.out"
printf '%s\\n' 'PASS compiler/c0/core-integer-bitwise-not'
"""
Path("tests/compiler/c0/run-core-integer-bitwise-not.sh").write_text(runner)

replace_once(
    ".github/scripts/compiler-c0-full-gate.sh",
    "core_integer_multiply_overflow_focused() {\n"
    "    MINIC=\"$root/build/ci-debug/bin/minic\" \\\n"
    "    BUILD_DIR=\"$root/build/ci-core-integer-multiply-overflow\" \\\n"
    "    RISCV_CC=riscv64-linux-gnu-gcc \\\n"
    "    QEMU_RISCV64=qemu-riscv64 \\\n"
    "        sh tests/compiler/c0/run-core-integer-multiply-overflow.sh\n"
    "}\n\n"
    "runtime_record_fam_prefix_focused() {",
    "core_integer_multiply_overflow_focused() {\n"
    "    MINIC=\"$root/build/ci-debug/bin/minic\" \\\n"
    "    BUILD_DIR=\"$root/build/ci-core-integer-multiply-overflow\" \\\n"
    "    RISCV_CC=riscv64-linux-gnu-gcc \\\n"
    "    QEMU_RISCV64=qemu-riscv64 \\\n"
    "        sh tests/compiler/c0/run-core-integer-multiply-overflow.sh\n"
    "}\n\n"
    "core_integer_bitwise_not_focused() {\n"
    "    MINIC=\"$root/build/ci-debug/bin/minic\" \\\n"
    "    BUILD_DIR=\"$root/build/ci-core-integer-bitwise-not\" \\\n"
    "    RISCV_CC=riscv64-linux-gnu-gcc \\\n"
    "    QEMU_RISCV64=qemu-riscv64 \\\n"
    "        sh tests/compiler/c0/run-core-integer-bitwise-not.sh\n"
    "}\n\n"
    "runtime_record_fam_prefix_focused() {",
)

replace_once(
    ".github/scripts/compiler-c0-full-gate.sh",
    "start_gate core-integer-multiply-overflow-focused core_integer_multiply_overflow_focused\n"
    "start_gate record-fam-prefix-focused runtime_record_fam_prefix_focused",
    "start_gate core-integer-multiply-overflow-focused core_integer_multiply_overflow_focused\n"
    "start_gate core-integer-bitwise-not-focused core_integer_bitwise_not_focused\n"
    "start_gate record-fam-prefix-focused runtime_record_fam_prefix_focused",
)
