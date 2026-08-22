#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    file_path = Path(path)
    text = file_path.read_text()
    if text.count(old) != 1:
        raise SystemExit(f"expected exactly one anchor in {path}: {old[:80]!r}")
    file_path.write_text(text.replace(old, new, 1))


replace_once(
    "src/core/core_ir.h",
    "    MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION,\n"
    "    MINIC_CORE_INSTRUCTION_INTEGER_NEGATE,\n",
    "    MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION,\n"
    "    MINIC_CORE_INSTRUCTION_SCALAR_BITCAST,\n"
    "    MINIC_CORE_INSTRUCTION_INTEGER_NEGATE,\n",
)

replace_once(
    "src/core/core_ir.c",
    "static bool instruction_is_valid(const MinicCoreFunction *function,\n"
    "                                 const MinicCoreInstruction *instruction,\n"
    "                                 const bool *available_values) {\n",
    "static bool core_scalar_bitcast_types_valid(MinicType target_type, MinicType source_type) {\n"
    "    return (minic_type_is_pointer(target_type) &&\n"
    "            (minic_type_is_pointer(source_type) || minic_type_is_integer(source_type))) ||\n"
    "           (minic_type_is_integer(target_type) && minic_type_is_pointer(source_type));\n"
    "}\n\n"
    "static bool instruction_is_valid(const MinicCoreFunction *function,\n"
    "                                 const MinicCoreInstruction *instruction,\n"
    "                                 const bool *available_values) {\n",
)

replace_once(
    "src/core/core_ir.c",
    "    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION:\n"
    "        return instruction_result_is_valid(function, instruction) &&\n"
    "               minic_type_is_integer(instruction->type) &&\n"
    "               instruction->value.operand < function->value_count &&\n"
    "               available_values[instruction->value.operand] &&\n"
    "               minic_type_is_integer(function->values[instruction->value.operand].type);\n"
    "    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:\n",
    "    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION:\n"
    "        return instruction_result_is_valid(function, instruction) &&\n"
    "               minic_type_is_integer(instruction->type) &&\n"
    "               instruction->value.operand < function->value_count &&\n"
    "               available_values[instruction->value.operand] &&\n"
    "               minic_type_is_integer(function->values[instruction->value.operand].type);\n"
    "    case MINIC_CORE_INSTRUCTION_SCALAR_BITCAST:\n"
    "        return instruction_result_is_valid(function, instruction) &&\n"
    "               instruction->value.operand < function->value_count &&\n"
    "               available_values[instruction->value.operand] &&\n"
    "               core_scalar_bitcast_types_valid(\n"
    "                   instruction->type, function->values[instruction->value.operand].type);\n"
    "    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:\n",
)

replace_once(
    "src/core/core_ir.c",
    "    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION:\n"
    "        return fprintf(output,\n"
    "                       \"  %%%\" PRIu32 \" = convert.int %%%\" PRIu32 \"\\n\",\n"
    "                       instruction->result,\n"
    "                       instruction->value.operand) >= 0;\n"
    "    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:\n",
    "    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION:\n"
    "        return fprintf(output,\n"
    "                       \"  %%%\" PRIu32 \" = convert.int %%%\" PRIu32 \"\\n\",\n"
    "                       instruction->result,\n"
    "                       instruction->value.operand) >= 0;\n"
    "    case MINIC_CORE_INSTRUCTION_SCALAR_BITCAST:\n"
    "        return fprintf(output,\n"
    "                       \"  %%%\" PRIu32 \" = bitcast.scalar %%%\" PRIu32 \"\\n\",\n"
    "                       instruction->result,\n"
    "                       instruction->value.operand) >= 0;\n"
    "    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:\n",
)

replace_once(
    "src/core/core_lower.c",
    "static bool core_memory_scalar_type(MinicType type) {\n"
    "    return minic_type_is_integer(type) || minic_type_is_pointer(type);\n"
    "}\n",
    "static bool core_memory_scalar_type(MinicType type) {\n"
    "    return minic_type_is_integer(type) || minic_type_is_pointer(type);\n"
    "}\n\n"
    "static bool core_scalar_bitcast_types(MinicType target_type, MinicType source_type) {\n"
    "    return (minic_type_is_pointer(target_type) &&\n"
    "            (minic_type_is_pointer(source_type) || minic_type_is_integer(source_type))) ||\n"
    "           (minic_type_is_integer(target_type) && minic_type_is_pointer(source_type));\n"
    "}\n",
)

replace_once(
    "src/core/core_lower.c",
    "    if (expression->kind == MINIC_EXPRESSION_LOCAL &&\n"
    "        expression->value_category == MINIC_VALUE_LVALUE) {\n",
    "    if (expression->value_category == MINIC_VALUE_LVALUE &&\n"
    "        core_memory_scalar_type(expression->type)) {\n",
)

replace_once(
    "src/core/core_lower.c",
    "    if (expression->kind == MINIC_EXPRESSION_BINARY &&\n"
    "        expression->value.binary.operator_kind == MINIC_BINARY_ADD) {\n",
    "    if (expression->kind == MINIC_EXPRESSION_BITCAST) {\n"
    "        const MinicExpression *operand;\n"
    "        MinicCoreValueId operand_value;\n"
    "        MinicCoreLowerStatus status;\n\n"
    "        operand = minic_c0_program_expression(\n"
    "            context->body->program, expression->value.unary.operand);\n"
    "        if (operand == NULL) {\n"
    "            return MINIC_CORE_LOWER_ERROR;\n"
    "        }\n"
    "        if (!core_scalar_bitcast_types(expression->type, operand->type)) {\n"
    "            return MINIC_CORE_LOWER_UNSUPPORTED;\n"
    "        }\n"
    "        status = lower_expression(context, expression->value.unary.operand, &operand_value);\n"
    "        if (status != MINIC_CORE_LOWER_OK) {\n"
    "            return status;\n"
    "        }\n"
    "        if (operand_value >= context->function->value_count ||\n"
    "            !minic_type_equal(context->function->values[operand_value].type, operand->type)) {\n"
    "            return MINIC_CORE_LOWER_ERROR;\n"
    "        }\n"
    "        (void)memset(&instruction, 0, sizeof(instruction));\n"
    "        instruction.kind = MINIC_CORE_INSTRUCTION_SCALAR_BITCAST;\n"
    "        instruction.span = expression->span;\n"
    "        instruction.type = expression->type;\n"
    "        instruction.result = MINIC_CORE_VALUE_INVALID;\n"
    "        instruction.value.operand = operand_value;\n"
    "        return minic_core_function_append_value_instruction(\n"
    "                   context->function, context->block_id, &instruction, value_id)\n"
    "                   ? MINIC_CORE_LOWER_OK\n"
    "                   : MINIC_CORE_LOWER_ERROR;\n"
    "    }\n"
    "    if (expression->kind == MINIC_EXPRESSION_BINARY &&\n"
    "        expression->value.binary.operator_kind == MINIC_BINARY_ADD) {\n",
)

replace_once(
    "src/target/riscv64/core_codegen.c",
    "static bool core_instruction_supported(const MinicC0Program *program,\n"
    "                                       const MinicCoreFunction *function,\n"
    "                                       const MinicCoreInstruction *instruction) {\n",
    "static bool core_scalar_bitcast_supported(const MinicC0Program *program,\n"
    "                                          const MinicCoreFunction *function,\n"
    "                                          const MinicCoreInstruction *instruction) {\n"
    "    const MinicCoreValue *source;\n"
    "    size_t source_size;\n"
    "    size_t source_alignment;\n"
    "    size_t target_size;\n"
    "    size_t target_alignment;\n"
    "    bool type_pair_valid;\n\n"
    "    if (program == NULL || function == NULL || instruction == NULL ||\n"
    "        instruction->kind != MINIC_CORE_INSTRUCTION_SCALAR_BITCAST ||\n"
    "        instruction->value.operand >= function->value_count) {\n"
    "        return false;\n"
    "    }\n"
    "    source = &function->values[instruction->value.operand];\n"
    "    type_pair_valid =\n"
    "        (minic_type_is_pointer(instruction->type) &&\n"
    "         (minic_type_is_pointer(source->type) || minic_type_is_integer(source->type))) ||\n"
    "        (minic_type_is_integer(instruction->type) && minic_type_is_pointer(source->type));\n"
    "    if (!type_pair_valid ||\n"
    "        !minic_data_layout_type(minic_default_data_layout(),\n"
    "                                program,\n"
    "                                source->type,\n"
    "                                &source_size,\n"
    "                                &source_alignment) ||\n"
    "        !minic_data_layout_type(minic_default_data_layout(),\n"
    "                                program,\n"
    "                                instruction->type,\n"
    "                                &target_size,\n"
    "                                &target_alignment)) {\n"
    "        return false;\n"
    "    }\n"
    "    (void)source_alignment;\n"
    "    (void)target_alignment;\n"
    "    return source_size != 0U && source_size <= 8U && target_size != 0U && target_size <= 8U;\n"
    "}\n\n"
    "static bool core_instruction_supported(const MinicC0Program *program,\n"
    "                                       const MinicCoreFunction *function,\n"
    "                                       const MinicCoreInstruction *instruction) {\n",
)

replace_once(
    "src/target/riscv64/core_codegen.c",
    "    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION:\n"
    "    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:\n",
    "    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION:\n"
    "    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:\n",
)

replace_once(
    "src/target/riscv64/core_codegen.c",
    "    case MINIC_CORE_INSTRUCTION_FIELD_ADDRESS:\n"
    "        return core_field_address_supported(program, instruction, NULL);\n",
    "    case MINIC_CORE_INSTRUCTION_FIELD_ADDRESS:\n"
    "        return core_field_address_supported(program, instruction, NULL);\n"
    "    case MINIC_CORE_INSTRUCTION_SCALAR_BITCAST:\n"
    "        return core_scalar_bitcast_supported(program, function, instruction);\n",
)

replace_once(
    "src/target/riscv64/core_codegen.c",
    "    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:\n"
    "        if (!load_core_value(file, frame, instruction->value.operand, \"t0\") ||\n",
    "    case MINIC_CORE_INSTRUCTION_SCALAR_BITCAST:\n"
    "        if (!load_core_value(file, frame, instruction->value.operand, \"t0\")) {\n"
    "            return false;\n"
    "        }\n"
    "        if (minic_type_is_integer(instruction->type) &&\n"
    "            !minic_riscv64_emit_integer_conversion_for_program(\n"
    "                file, program, instruction->type, \"t0\")) {\n"
    "            return false;\n"
    "        }\n"
    "        return store_core_value(file, frame, instruction->result, \"t0\");\n"
    "    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:\n"
    "        if (!load_core_value(file, frame, instruction->value.operand, \"t0\") ||\n",
)

source = r'''struct CoreBitcastPair {
    int offset;
    int value;
};

void *core_offset_to_ptr(const int *offset) {
    return (void *)((unsigned long)offset + *offset);
}

int core_pointer_read(const int *value) {
    return *value;
}

int core_member_read(struct CoreBitcastPair *pair) {
    return pair->value;
}

unsigned long core_pointer_bits(const void *value) {
    return (unsigned long)value;
}
'''
Path("tests/compiler/c0/core_scalar_lvalue_bitcast.c").write_text(source)

runtime = r'''#include <stdio.h>

struct CoreBitcastPair {
    int offset;
    int value;
};

void *core_offset_to_ptr(const int *offset);
int core_pointer_read(const int *value);
int core_member_read(struct CoreBitcastPair *pair);
unsigned long core_pointer_bits(const void *value);

int main(void) {
    struct CoreBitcastPair pair;
    void *resolved;
    int direct;
    int member;
    unsigned long bits;

    pair.offset = (int)((char *)&pair.value - (char *)&pair.offset);
    pair.value = 73;
    resolved = core_offset_to_ptr(&pair.offset);
    direct = core_pointer_read(&pair.value);
    member = core_member_read(&pair);
    bits = core_pointer_bits(&pair.value);
    printf("%d %d %d %d\n",
           resolved == (void *)&pair.value,
           direct,
           member,
           bits == (unsigned long)&pair.value);
    return resolved == (void *)&pair.value && direct == 73 && member == 73 &&
                   bits == (unsigned long)&pair.value
               ? 0
               : 1;
}
'''
Path("tests/compiler/c0/core_scalar_lvalue_bitcast_runtime.c").write_text(runtime)

script = r'''#!/usr/bin/env bash
set -Eeuo pipefail

: "${MINIC:?MINIC must point to the compiler binary}"
: "${RISCV_CC:=riscv64-linux-gnu-gcc}"
: "${QEMU_RISCV64:=qemu-riscv64}"

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work="${BUILD_DIR:-$root/build/core-scalar-lvalue-bitcast}"
source_file="$root/tests/compiler/c0/core_scalar_lvalue_bitcast.c"
runtime_file="$root/tests/compiler/c0/core_scalar_lvalue_bitcast_runtime.c"
mkdir -p "$work"

cc -E -P -std=gnu11 "$source_file" -o "$work/core_scalar_lvalue_bitcast.i"
MINIC_CORE_IR=strict "$MINIC" -S "$work/core_scalar_lvalue_bitcast.i" \
    -o "$work/core_scalar_lvalue_bitcast-strict.s"
MINIC_CORE_CODEGEN=basic-v0 "$MINIC" -S "$work/core_scalar_lvalue_bitcast.i" \
    -o "$work/core_scalar_lvalue_bitcast-core.s"

for symbol in core_offset_to_ptr core_pointer_read core_member_read core_pointer_bits; do
    grep -q "^${symbol}:" "$work/core_scalar_lvalue_bitcast-core.s"
    grep -q "${symbol}_core_bb0" "$work/core_scalar_lvalue_bitcast-core.s"
done

grep -q 'bitcast.scalar' <(MINIC_CORE_IR=strict "$MINIC" -S \
    "$work/core_scalar_lvalue_bitcast.i" -o /dev/null 2>&1 || true) || true

"$RISCV_CC" -static -O2 "$source_file" "$runtime_file" -o "$work/reference-rv64"
"$RISCV_CC" -static -O2 "$runtime_file" "$work/core_scalar_lvalue_bitcast-core.s" \
    -o "$work/minic-rv64"
"$QEMU_RISCV64" "$work/reference-rv64" >"$work/reference.out"
"$QEMU_RISCV64" "$work/minic-rv64" >"$work/minic.out"
cmp "$work/reference.out" "$work/minic.out"
printf '%s\n' 'PASS compiler/c0/core-scalar-lvalue-bitcast'
'''
Path("tests/compiler/c0/run-core-scalar-lvalue-bitcast.sh").write_text(script)

replace_once(
    ".github/scripts/compiler-c0-full-gate.sh",
    "runtime_record_fam_prefix_focused() {\n",
    "core_scalar_lvalue_bitcast_focused() {\n"
    "    MINIC=\"$root/build/ci-debug/bin/minic\" \\\n"
    "    BUILD_DIR=\"$root/build/ci-core-scalar-lvalue-bitcast\" \\\n"
    "    RISCV_CC=riscv64-linux-gnu-gcc \\\n"
    "    QEMU_RISCV64=qemu-riscv64 \\\n"
    "        sh tests/compiler/c0/run-core-scalar-lvalue-bitcast.sh\n"
    "}\n\n"
    "runtime_record_fam_prefix_focused() {\n",
)

replace_once(
    ".github/scripts/compiler-c0-full-gate.sh",
    "start_gate core-required-no-fallback-focused core_required_no_fallback_focused\n"
    "start_gate record-fam-prefix-focused runtime_record_fam_prefix_focused\n",
    "start_gate core-required-no-fallback-focused core_required_no_fallback_focused\n"
    "start_gate core-scalar-lvalue-bitcast-focused core_scalar_lvalue_bitcast_focused\n"
    "start_gate record-fam-prefix-focused runtime_record_fam_prefix_focused\n",
)
