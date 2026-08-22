#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly one match, found {count}")
    target.write_text(text.replace(old, new, 1))


# M6: a target-neutral checked integer multiply. This is not a GNU-builtin
# opcode: it records the execution fact that two integer values are multiplied,
# the wrapped result is stored through an address, and the expression result is
# the overflow flag. The frontend has already resolved GNU C operand/result
# typing before this boundary.
header_path = Path("src/core/core_ir.h")
header = header_path.read_text()
header = replace_once.__wrapped__(header_path, "", "", "") if False else header
header_anchor = "    MINIC_CORE_INSTRUCTION_INTEGER_EQUAL,\n    MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION,\n"
header_replacement = "    MINIC_CORE_INSTRUCTION_INTEGER_EQUAL,\n    MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY_OVERFLOW,\n    MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION,\n"
if header.count(header_anchor) != 1:
    raise SystemExit(f"checked multiply opcode: expected one anchor, found {header.count(header_anchor)}")
header = header.replace(header_anchor, header_replacement, 1)
union_anchor = r'''        struct {
            MinicCoreValueId left;
            MinicCoreValueId right;
        } binary;
'''
union_replacement = union_anchor + r'''        struct {
            MinicCoreValueId left;
            MinicCoreValueId right;
            MinicCoreValueId result_address;
        } multiply_overflow;
'''
if header.count(union_anchor) != 1:
    raise SystemExit(f"checked multiply payload: expected one binary payload, found {header.count(union_anchor)}")
header_path.write_text(header.replace(union_anchor, union_replacement, 1))

# Core verifier and deterministic dump. The result address pointee is the
# operation type. The instruction itself returns _Bool and also has a memory
# effect, like CALL may both produce a value and have effects.
ir_path = Path("src/core/core_ir.c")
ir = ir_path.read_text()
verify_anchor = r'''    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION:
        return instruction_result_is_valid(function, instruction) &&
               minic_type_is_integer(instruction->type) &&
               instruction->value.operand < function->value_count &&
               available_values[instruction->value.operand] &&
               minic_type_is_integer(function->values[instruction->value.operand].type);
'''
verify_replacement = r'''    case MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY_OVERFLOW: {
        MinicType result_type;

        if (!instruction_result_is_valid(function, instruction) ||
            !minic_type_equal(instruction->type, minic_type_bool()) ||
            instruction->value.multiply_overflow.left >= function->value_count ||
            instruction->value.multiply_overflow.right >= function->value_count ||
            instruction->value.multiply_overflow.result_address >= function->value_count ||
            !available_values[instruction->value.multiply_overflow.left] ||
            !available_values[instruction->value.multiply_overflow.right] ||
            !available_values[instruction->value.multiply_overflow.result_address] ||
            !minic_type_pointee(
                function->values[instruction->value.multiply_overflow.result_address].type,
                &result_type) ||
            !minic_type_is_integer(result_type) || minic_type_is_bool_integer(result_type) ||
            minic_type_is_const(result_type) || minic_type_is_volatile(result_type)) {
            return false;
        }
        left = &function->values[instruction->value.multiply_overflow.left];
        right = &function->values[instruction->value.multiply_overflow.right];
        return minic_type_equal(left->type, result_type) && minic_type_equal(right->type, result_type);
    }
    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION:
        return instruction_result_is_valid(function, instruction) &&
               minic_type_is_integer(instruction->type) &&
               instruction->value.operand < function->value_count &&
               available_values[instruction->value.operand] &&
               minic_type_is_integer(function->values[instruction->value.operand].type);
'''
if ir.count(verify_anchor) != 1:
    raise SystemExit(f"checked multiply verifier: expected one anchor, found {ir.count(verify_anchor)}")
ir = ir.replace(verify_anchor, verify_replacement, 1)
dump_anchor = r'''    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION:
        return fprintf(output,
                       "  %%%" PRIu32 " = convert.int %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.operand) >= 0;
'''
dump_replacement = r'''    case MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY_OVERFLOW:
        return fprintf(output,
                       "  %%%" PRIu32 " = mul.overflow.int %%%" PRIu32 ", %%%" PRIu32
                       ", %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.multiply_overflow.left,
                       instruction->value.multiply_overflow.right,
                       instruction->value.multiply_overflow.result_address) >= 0;
    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION:
        return fprintf(output,
                       "  %%%" PRIu32 " = convert.int %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.operand) >= 0;
'''
if ir.count(dump_anchor) != 1:
    raise SystemExit(f"checked multiply dump: expected one anchor, found {ir.count(dump_anchor)}")
ir_path.write_text(ir.replace(dump_anchor, dump_replacement, 1))

# AST -> Core lowering. Only multiplication is moved in this bounded slice;
# ordinary multiply and add/sub overflow remain separate future execution
# capabilities. Operand conversion is performed here because the frontend has
# already resolved the builtin's effective result pointee type.
lower_path = Path("src/core/core_lower.c")
lower = lower_path.read_text()
lower_anchor = r'''    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        expression->value.binary.operator_kind == MINIC_BINARY_EQUAL) {
'''
lower_case = r'''    if (expression->kind == MINIC_EXPRESSION_BUILTIN_OVERFLOW &&
        expression->value.overflow.operator_kind == MINIC_OVERFLOW_MULTIPLY) {
        const MinicExpression *left_expression;
        const MinicExpression *result_pointer_expression;
        const MinicExpression *right_expression;
        MinicCoreValueId left;
        MinicCoreValueId left_source;
        MinicCoreValueId result_address;
        MinicCoreValueId right;
        MinicCoreValueId right_source;
        MinicCoreLowerStatus status;
        MinicType result_type;

        if (!minic_type_equal(expression->type, minic_type_bool())) {
            return MINIC_CORE_LOWER_ERROR;
        }
        left_expression =
            minic_c0_program_expression(context->body->program, expression->value.overflow.left);
        right_expression =
            minic_c0_program_expression(context->body->program, expression->value.overflow.right);
        result_pointer_expression = minic_c0_program_expression(
            context->body->program, expression->value.overflow.result_pointer);
        if (left_expression == NULL || right_expression == NULL || result_pointer_expression == NULL ||
            !minic_type_pointee(result_pointer_expression->type, &result_type) ||
            !minic_type_is_integer(result_type) || minic_type_is_bool_integer(result_type) ||
            minic_type_is_const(result_type) || minic_type_is_volatile(result_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_expression(context, expression->value.overflow.left, &left_source);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = append_integer_conversion(
            context, left_expression->span, result_type, left_source, &left);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = lower_expression(context, expression->value.overflow.right, &right_source);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = append_integer_conversion(
            context, right_expression->span, result_type, right_source, &right);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status =
            lower_expression(context, expression->value.overflow.result_pointer, &result_address);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (left >= context->function->value_count || right >= context->function->value_count ||
            result_address >= context->function->value_count ||
            !minic_type_equal(context->function->values[left].type, result_type) ||
            !minic_type_equal(context->function->values[right].type, result_type) ||
            !minic_type_equal(context->function->values[result_address].type,
                              result_pointer_expression->type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY_OVERFLOW;
        instruction.type = minic_type_bool();
        instruction.value.multiply_overflow.left = left;
        instruction.value.multiply_overflow.right = right;
        instruction.value.multiply_overflow.result_address = result_address;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
'''
if lower.count(lower_anchor) != 1:
    raise SystemExit(f"checked multiply lower: expected one anchor, found {lower.count(lower_anchor)}")
lower_path.write_text(lower.replace(lower_anchor, lower_case + lower_anchor, 1))

# RV64 Core emission. The arithmetic is the same proven execution rule used by
# the legacy RV64 emitter, but the source-language builtin no longer leaks into
# this layer. The operation is bounded to integer objects no wider than XLEN.
codegen_path = Path("src/target/riscv64/core_codegen.c")
codegen = codegen_path.read_text()
support_anchor = r'''static bool core_instruction_supported(const MinicC0Program *program,
                                       const MinicCoreFunction *function,
                                       const MinicCoreInstruction *instruction) {
'''
support_helper = r'''static bool core_integer_multiply_overflow_supported(
    const MinicC0Program *program,
    const MinicCoreFunction *function,
    const MinicCoreInstruction *instruction,
    MinicType *result_type,
    size_t *result_size,
    bool *is_unsigned) {
    MinicType effective_result_type;
    MinicType pointee;
    size_t alignment;

    if (program == NULL || function == NULL || instruction == NULL ||
        instruction->kind != MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY_OVERFLOW ||
        !minic_type_equal(instruction->type, minic_type_bool()) ||
        instruction->value.multiply_overflow.left >= function->value_count ||
        instruction->value.multiply_overflow.right >= function->value_count ||
        instruction->value.multiply_overflow.result_address >= function->value_count ||
        !minic_type_pointee(
            function->values[instruction->value.multiply_overflow.result_address].type, &pointee) ||
        !minic_type_is_integer(pointee) || minic_type_is_bool_integer(pointee) ||
        minic_type_is_const(pointee) || minic_type_is_volatile(pointee) ||
        !minic_type_equal(function->values[instruction->value.multiply_overflow.left].type, pointee) ||
        !minic_type_equal(function->values[instruction->value.multiply_overflow.right].type, pointee) ||
        !minic_data_layout_type(
            minic_default_data_layout(), program, pointee, result_size, &alignment) ||
        *result_size == 0U || *result_size > 8U ||
        !minic_c0_type_effective_integer_type(program, pointee, &effective_result_type)) {
        return false;
    }
    (void)alignment;
    if (result_type != NULL) {
        *result_type = pointee;
    }
    if (is_unsigned != NULL) {
        *is_unsigned = minic_type_is_unsigned_integer(effective_result_type);
    }
    return true;
}

'''
if codegen.count(support_anchor) != 1:
    raise SystemExit(f"checked multiply support helper: expected one anchor, found {codegen.count(support_anchor)}")
codegen = codegen.replace(support_anchor, support_helper + support_anchor, 1)
switch_anchor = r'''    case MINIC_CORE_INSTRUCTION_INTEGER_EQUAL:
    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION:
'''
switch_replacement = r'''    case MINIC_CORE_INSTRUCTION_INTEGER_EQUAL:
    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION:
'''
# Keep ordinary scalar op grouping unchanged and give checked multiply a
# program-sensitive support predicate below.
if codegen.count(switch_anchor) != 1:
    raise SystemExit(f"checked multiply support switch: expected one scalar anchor, found {codegen.count(switch_anchor)}")
call_anchor = r'''    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS:
        return instruction->value.global_id < function->global_count &&
               function->globals[instruction->value.global_id].name != NULL &&
               function->globals[instruction->value.global_id].name_length != 0U;
'''
call_replacement = call_anchor + r'''    case MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY_OVERFLOW: {
        MinicType result_type;
        size_t result_size;
        bool is_unsigned;

        return core_integer_multiply_overflow_supported(
            program, function, instruction, &result_type, &result_size, &is_unsigned);
    }
'''
if codegen.count(call_anchor) != 1:
    raise SystemExit(f"checked multiply support case: expected one global anchor, found {codegen.count(call_anchor)}")
codegen = codegen.replace(call_anchor, call_replacement, 1)

emit_anchor = r'''    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION:
        if (!load_core_value(file, frame, instruction->value.operand, "t0") ||
'''
emit_case = r'''    case MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY_OVERFLOW: {
        MinicType result_type;
        size_t result_size;
        bool is_unsigned;

        if (!core_integer_multiply_overflow_supported(
                program, function, instruction, &result_type, &result_size, &is_unsigned) ||
            !load_core_value(file, frame, instruction->value.multiply_overflow.left, "t0") ||
            !load_core_value(file, frame, instruction->value.multiply_overflow.right, "t1") ||
            !load_core_value(file, frame, instruction->value.multiply_overflow.result_address, "t3")) {
            return false;
        }
        if (result_size < 8U) {
            if (fprintf(file, "  mul t2, t0, t1\n  mv t4, t2\n") < 0 ||
                !minic_riscv64_emit_integer_conversion_for_program(
                    file, program, result_type, "t2") ||
                fprintf(file, "  xor t4, t4, t2\n  snez t4, t4\n") < 0) {
                return false;
            }
        } else if (is_unsigned) {
            if (fprintf(file,
                        "  mul t2, t0, t1\n"
                        "  mulhu t4, t0, t1\n"
                        "  snez t4, t4\n") < 0) {
                return false;
            }
        } else if (fprintf(file,
                           "  mul t2, t0, t1\n"
                           "  mulh t4, t0, t1\n"
                           "  srai t5, t2, 63\n"
                           "  xor t4, t4, t5\n"
                           "  snez t4, t4\n") < 0) {
            return false;
        }
        if (!minic_riscv64_emit_scalar_store_for_program(
                file, program, result_type, "t2", "t3")) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t4");
    }
'''
if codegen.count(emit_anchor) != 1:
    raise SystemExit(f"checked multiply emitter: expected one anchor, found {codegen.count(emit_anchor)}")
codegen_path.write_text(codegen.replace(emit_anchor, emit_case + emit_anchor, 1))

# Focused source is intentionally Linux-shaped without copying Linux-specific
# names into Core. It also freezes sub-XLEN, signed XLEN and unsigned XLEN
# behavior and verifies both store and overflow results at runtime.
source = r'''static _Bool core_m6_must_check(_Bool overflow) {
    return overflow;
}

int core_m6_checked_int(int left, int right, int *result) {
    return __builtin_mul_overflow(left, right, result);
}

long core_m6_checked_long(long left, long right, long *result) {
    return __builtin_mul_overflow(left, right, result);
}

unsigned long core_m6_checked_ulong(unsigned long left,
                                    unsigned long right,
                                    unsigned long *result) {
    return __builtin_mul_overflow(left, right, result);
}

unsigned long core_m6_size_mul(unsigned long left, unsigned long right) {
    unsigned long bytes;

    if (core_m6_must_check(__builtin_mul_overflow(left, right, &bytes)))
        return 99UL;
    return bytes;
}
'''
Path("tests/compiler/c0/core_integer_multiply_overflow.c").write_text(source)

runtime = r'''#include <limits.h>
#include <stdio.h>

int core_m6_checked_int(int left, int right, int *result);
long core_m6_checked_long(long left, long right, long *result);
unsigned long core_m6_checked_ulong(unsigned long left,
                                    unsigned long right,
                                    unsigned long *result);
unsigned long core_m6_size_mul(unsigned long left, unsigned long right);

int main(void) {
    int int_result;
    long long_result;
    unsigned long ulong_result;
    int int_overflow;
    long long_overflow;
    unsigned long ulong_overflow;
    unsigned long size_ok;
    unsigned long size_overflow;

    int_result = 0;
    long_result = 0;
    ulong_result = 0;
    int_overflow = core_m6_checked_int(50000, 50000, &int_result);
    long_overflow = core_m6_checked_long(LONG_MAX, 2, &long_result);
    ulong_overflow = core_m6_checked_ulong(ULONG_MAX, 2UL, &ulong_result);
    size_ok = core_m6_size_mul(7UL, 9UL);
    size_overflow = core_m6_size_mul(ULONG_MAX, 2UL);

    printf("%d %d %ld %ld %lu %lu %lu %lu\n",
           int_overflow,
           int_result,
           long_overflow,
           long_result,
           ulong_overflow,
           ulong_result,
           size_ok,
           size_overflow);
    return 0;
}
'''
Path("tests/compiler/c0/core_integer_multiply_overflow_runtime.c").write_text(runtime)

runner = r'''#!/bin/sh
set -eu

: "${MINIC:?MINIC must point to the compiler binary}"
: "${RISCV_CC:=riscv64-linux-gnu-gcc}"
: "${QEMU_RISCV64:=qemu-riscv64}"

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work="${BUILD_DIR:-$root/build/core-integer-multiply-overflow}"
source_file="$root/tests/compiler/c0/core_integer_multiply_overflow.c"
runtime_file="$root/tests/compiler/c0/core_integer_multiply_overflow_runtime.c"
mkdir -p "$work"

cc -E -P -std=gnu11 "$source_file" -o "$work/core_integer_multiply_overflow.i"
MINIC_CORE_IR=strict "$MINIC" -S "$work/core_integer_multiply_overflow.i" \
    -o "$work/core_integer_multiply_overflow-strict.s"
MINIC_CORE_CODEGEN=basic-v0 "$MINIC" -S "$work/core_integer_multiply_overflow.i" \
    -o "$work/core_integer_multiply_overflow-core.s"

grep -q '^core_m6_checked_int:' "$work/core_integer_multiply_overflow-core.s"
grep -q '^core_m6_checked_long:' "$work/core_integer_multiply_overflow-core.s"
grep -q '^core_m6_checked_ulong:' "$work/core_integer_multiply_overflow-core.s"
grep -q '^core_m6_size_mul:' "$work/core_integer_multiply_overflow-core.s"
grep -q 'mulhu t4, t0, t1' "$work/core_integer_multiply_overflow-core.s"
grep -q 'mulh t4, t0, t1' "$work/core_integer_multiply_overflow-core.s"

"$RISCV_CC" -static -O2 "$source_file" "$runtime_file" -o "$work/reference-rv64"
"$RISCV_CC" -static -O2 "$runtime_file" "$work/core_integer_multiply_overflow-core.s" \
    -o "$work/minic-rv64"
"$QEMU_RISCV64" "$work/reference-rv64" >"$work/reference.out"
"$QEMU_RISCV64" "$work/minic-rv64" >"$work/minic.out"
cmp "$work/reference.out" "$work/minic.out"
printf '%s\n' 'PASS compiler/c0/core-integer-multiply-overflow'
'''
Path("tests/compiler/c0/run-core-integer-multiply-overflow.sh").write_text(runner)

# Wire the focused gate into the permanent compiler gate.
gate_path = Path(".github/scripts/compiler-c0-full-gate.sh")
gate = gate_path.read_text()
gate_function_anchor = r'''core_integer_equality_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \
    BUILD_DIR="$root/build/ci-core-integer-equality" \
    RISCV_CC=riscv64-linux-gnu-gcc \
    QEMU_RISCV64=qemu-riscv64 \
        sh tests/compiler/c0/run-core-integer-equality.sh
}
'''
gate_function_replacement = gate_function_anchor + r'''
core_integer_multiply_overflow_focused() {
    MINIC="$root/build/ci-debug/bin/minic" \
    BUILD_DIR="$root/build/ci-core-integer-multiply-overflow" \
    RISCV_CC=riscv64-linux-gnu-gcc \
    QEMU_RISCV64=qemu-riscv64 \
        sh tests/compiler/c0/run-core-integer-multiply-overflow.sh
}
'''
if gate.count(gate_function_anchor) != 1:
    raise SystemExit(f"checked multiply gate function: expected one anchor, found {gate.count(gate_function_anchor)}")
gate = gate.replace(gate_function_anchor, gate_function_replacement, 1)
gate_start_anchor = "start_gate core-integer-equality-focused core_integer_equality_focused\n"
gate_start_replacement = gate_start_anchor + \
    "start_gate core-integer-multiply-overflow-focused core_integer_multiply_overflow_focused\n"
if gate.count(gate_start_anchor) != 1:
    raise SystemExit(f"checked multiply gate registration: expected one anchor, found {gate.count(gate_start_anchor)}")
gate_path.write_text(gate.replace(gate_start_anchor, gate_start_replacement, 1))
