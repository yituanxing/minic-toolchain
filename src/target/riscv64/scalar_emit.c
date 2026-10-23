#include "target/riscv64/scalar_emit.h"
#include "target/riscv64/fp_emit.h"
#include "target/riscv64/overflow_emit.h"
#include "target/riscv64/codegen_internal.h"
#include "target/riscv64/legalize.h"
#include "target/common/direct_instruction_semantics.h"
#include "target/riscv64/frame_access.h"
#include "target/riscv64/runtime_libcall.h"
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

/* Six RV64 integer operations share the same SSA transfer and width fixup.
 * Only the ISA opcode varies. Preserve Core's per-instruction legalization. */
static bool emit_simple_integer_binary(
    FILE *file, const MinicC0Program *program, const MinicRiscv64CoreFrame *frame,
    const MinicCoreInstruction *instruction, const char *opcode) {
    if (file == NULL || frame == NULL ||
        instruction == NULL || opcode == NULL ||
        !minic_riscv64_frame_load_value(file, frame, instruction->value.binary.left, "t0") ||
        !minic_riscv64_frame_load_value(file, frame, instruction->value.binary.right, "t1") ||
        fprintf(file, "  %s t0, t0, t1\n", opcode) < 0 ||
        !minic_riscv64_emit_integer_conversion_for_program(
            file, program, instruction->type, "t0")) {
        return false;
    }
    return minic_riscv64_frame_store_value(file, frame, instruction->result, "t0");
}

bool minic_riscv64_emit_scalar_instruction(
    FILE *file, const MinicC0Program *program,
    const MinicCoreFunction *function, const MinicRiscv64CoreFrame *frame,
    const char *symbol_name, const MinicCoreInstruction *instruction,
    const MinicRiscvIsa *isa) {
    if (file == NULL || function == NULL ||
        frame == NULL || symbol_name == NULL || instruction == NULL)
        return false;
    /* The four ISA backends agree on the semantic FP family.
     * RV64 keeps its own fmv/fcvt and floating register sequences. */
    if (minic_multi_direct_fp_instruction_kind(instruction->kind))
        return minic_riscv64_emit_fp_instruction(
            file, program, function, frame, instruction, isa);
    if (instruction->kind == MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW)
        return minic_riscv64_emit_overflow_instruction(
            file, program, function, frame, instruction, isa);
    switch (instruction->kind) {
    case MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT:
        if (fprintf(file, "  li t0, %" PRId64 "\n", instruction->value.integer_value) < 0 ||
            !minic_riscv64_emit_integer_conversion_for_program(
                file, program, instruction->type, "t0")) {
            return false;
        }
        return minic_riscv64_frame_store_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_INTEGER_ADD:
        return emit_simple_integer_binary(file, program, frame, instruction, "add");
    case MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT:
        return emit_simple_integer_binary(file, program, frame, instruction, "sub");
    case MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY:
        if (minic_type_is_int128_integer(instruction->type)) {
            MinicCoreValueId left = instruction->value.binary.left;
            MinicCoreValueId right = instruction->value.binary.right;

            if (left >= function->value_count || right >= function->value_count ||
                !minic_type_is_int128_integer(function->values[left].type) ||
                !minic_type_is_int128_integer(function->values[right].type) ||
                !minic_riscv64_frame_load_int128(file, frame, left, "t0", "t1") ||
                !minic_riscv64_frame_load_int128(file, frame, right, "t2", "t3") ||
                fprintf(file,
                        "  mulhu t4, t0, t2\n"
                        "  mul t5, t1, t2\n"
                        "  add t4, t4, t5\n"
                        "  mul t5, t0, t3\n"
                        "  add t4, t4, t5\n"
                        "  mul t0, t0, t2\n") < 0) {
                return false;
            }
            return minic_riscv64_frame_store_int128(file, frame, instruction->result, "t0", "t4");
        }
        if (!minic_riscv64_frame_load_value(file, frame, instruction->value.binary.left, "t0") ||
            !minic_riscv64_frame_load_value(file, frame, instruction->value.binary.right, "t1") ||
            !minic_riscv64_emit_arithmetic_libcall(
                file, isa, MINIC_RISCV64_LIBCALL_MUL, "t0", "t0", "t1") ||
            !minic_riscv64_emit_integer_conversion_for_program(
                file, program, instruction->type, "t0")) {
            return false;
        }
        return minic_riscv64_frame_store_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE:
    case MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER: {
        MinicType effective_type;
        if (!minic_core_function_effective_integer_type(
                function, instruction->type, &effective_type)) {
            return false;
        }
        if (!minic_riscv64_frame_load_value(file, frame, instruction->value.binary.left, "t0") ||
            !minic_riscv64_frame_load_value(file, frame, instruction->value.binary.right, "t1") ||
            /* LP64 libcalls consume full XLEN inputs. Canonicalize 32-bit
             * signed/unsigned operands before entering libgcc/compiler-rt.
             * Leave the historical M hardware path byte-for-byte intact. */
            (minic_riscv64_uses_arithmetic_libcall(isa) &&
             (!minic_riscv64_emit_integer_conversion_for_program(
                  file, program, effective_type, "t0") ||
              !minic_riscv64_emit_integer_conversion_for_program(
                  file, program, effective_type, "t1"))) ||
            !minic_riscv64_emit_arithmetic_libcall(
                file, isa,
                instruction->kind == MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER
                  ? (minic_type_is_unsigned_integer(effective_type)
                       ? MINIC_RISCV64_LIBCALL_UMOD : MINIC_RISCV64_LIBCALL_SMOD)
                  : (minic_type_is_unsigned_integer(effective_type)
                       ? MINIC_RISCV64_LIBCALL_UDIV : MINIC_RISCV64_LIBCALL_SDIV),
                "t0", "t0", "t1") ||
            !minic_riscv64_emit_integer_conversion_for_program(
                file, program, instruction->type, "t0")) {
            return false;
        }
        return minic_riscv64_frame_store_value(file, frame, instruction->result, "t0");
    }
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:
        return emit_simple_integer_binary(file, program, frame, instruction, "and");
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR:
        return emit_simple_integer_binary(file, program, frame, instruction, "xor");
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR:
        return emit_simple_integer_binary(file, program, frame, instruction, "or");
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT:
        return emit_simple_integer_binary(file, program, frame, instruction, "sll");
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT: {
        MinicType effective_type;
        const char *opcode;

        if (!minic_core_function_effective_integer_type(function, instruction->type, &effective_type)) {
            return false;
        }
        if (minic_type_is_int128_integer(instruction->type)) {
            bool is_signed;
            MinicCoreValueId left = instruction->value.binary.left;
            MinicCoreValueId right = instruction->value.binary.right;

            if (left >= function->value_count || right >= function->value_count ||
                !minic_type_is_int128_integer(function->values[left].type) ||
                !minic_type_is_integer(function->values[right].type) ||
                minic_type_is_int128_integer(function->values[right].type) ||
                !minic_riscv64_core_integer_type_is_signed(function, instruction->type, &is_signed) ||
                !minic_riscv64_frame_load_int128(file, frame, left, "t0", "t1") ||
                !minic_riscv64_frame_load_value(file, frame, right, "t2") ||
                fprintf(file,
                        "  beqz t2, .L%s_core_i128_shr_done_%" PRIu32 "\n"
                        "  li t3, 64\n"
                        "  bgeu t2, t3, .L%s_core_i128_shr_ge64_%" PRIu32 "\n"
                        "  neg t3, t2\n"
                        "  sll t4, t1, t3\n"
                        "  srl t0, t0, t2\n"
                        "  or t0, t0, t4\n"
                        "  %s t1, t1, t2\n"
                        "  j .L%s_core_i128_shr_done_%" PRIu32 "\n"
                        ".L%s_core_i128_shr_ge64_%" PRIu32 ":\n"
                        "  addi t2, t2, -64\n"
                        "  %s t0, t1, t2\n"
                        "  %s\n"
                        ".L%s_core_i128_shr_done_%" PRIu32 ":\n",
                        symbol_name,
                        instruction->result,
                        symbol_name,
                        instruction->result,
                        is_signed ? "sra" : "srl",
                        symbol_name,
                        instruction->result,
                        symbol_name,
                        instruction->result,
                        is_signed ? "sra" : "srl",
                        is_signed ? "  srai t1, t1, 63" : "  li t1, 0",
                        symbol_name,
                        instruction->result) < 0) {
                return false;
            }
            return minic_riscv64_frame_store_int128(file, frame, instruction->result, "t0", "t1");
        }
        opcode = minic_type_is_unsigned_integer(effective_type) ? "srl" : "sra";
        if (!minic_riscv64_frame_load_value(file, frame, instruction->value.binary.left, "t0") ||
            !minic_riscv64_frame_load_value(file, frame, instruction->value.binary.right, "t1") ||
            fprintf(file, "  %s t0, t0, t1\n", opcode) < 0 ||
            !minic_riscv64_emit_integer_conversion_for_program(
                file, program, instruction->type, "t0")) {
            return false;
        }
        return minic_riscv64_frame_store_value(file, frame, instruction->result, "t0");
    }
    case MINIC_CORE_INSTRUCTION_INTEGER_LESS: {
        MinicType effective_type;
        MinicType operand_type;
        const char *opcode;

        if (instruction->value.binary.left >= function->value_count ||
            instruction->value.binary.right >= function->value_count) {
            return false;
        }
        operand_type = function->values[instruction->value.binary.left].type;
        if (!minic_type_is_integer(operand_type) ||
            !minic_type_equal(operand_type,
                              function->values[instruction->value.binary.right].type) ||
            !minic_core_function_effective_integer_type(function, operand_type, &effective_type)) {
            return false;
        }
        opcode = minic_type_is_unsigned_integer(effective_type) ? "sltu" : "slt";
        if (!minic_riscv64_frame_load_value(file, frame, instruction->value.binary.left, "t0") ||
            !minic_riscv64_frame_load_value(file, frame, instruction->value.binary.right, "t1") ||
            fprintf(file, "  %s t0, t0, t1\n", opcode) < 0) {
            return false;
        }
        return minic_riscv64_frame_store_value(file, frame, instruction->result, "t0");
    }
    case MINIC_CORE_INSTRUCTION_POINTER_LESS:
        if (instruction->value.binary.left >= function->value_count ||
            instruction->value.binary.right >= function->value_count ||
            !minic_type_is_pointer(function->values[instruction->value.binary.left].type) ||
            !minic_type_equal(function->values[instruction->value.binary.left].type,
                              function->values[instruction->value.binary.right].type) ||
            !minic_riscv64_frame_load_value(file, frame, instruction->value.binary.left, "t0") ||
            !minic_riscv64_frame_load_value(file, frame, instruction->value.binary.right, "t1") ||
            fprintf(file, "  sltu t0, t0, t1\n") < 0) {
            return false;
        }
        return minic_riscv64_frame_store_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:
        if (!minic_riscv64_frame_load_value(file, frame, instruction->value.binary.left, "t0") ||
            !minic_riscv64_frame_load_value(file, frame, instruction->value.binary.right, "t1") ||
            fprintf(file, "  xor t0, t0, t1\n  seqz t0, t0\n") < 0) {
            return false;
        }
        return minic_riscv64_frame_store_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION: {
        MinicCoreValueId operand = instruction->value.operand;
        MinicType source_type;

        if (operand >= function->value_count) {
            return false;
        }
        source_type = function->values[operand].type;
        if (minic_type_is_int128_integer(instruction->type)) {
            if (minic_type_is_int128_integer(source_type)) {
                if (!minic_riscv64_frame_load_int128(file, frame, operand, "t0", "t1")) {
                    return false;
                }
            } else {
                bool source_signed;

                if (!minic_type_is_integer(source_type) ||
                    !minic_riscv64_frame_load_value(file, frame, operand, "t0") ||
                    !minic_riscv64_core_integer_type_is_signed(function, source_type, &source_signed) ||
                    fprintf(file,
                            source_signed ? "  srai t1, t0, 63\n" : "  li t1, 0\n") < 0) {
                    return false;
                }
            }
            return minic_riscv64_frame_store_int128(file, frame, instruction->result, "t0", "t1");
        }
        if (minic_type_is_int128_integer(source_type)) {
            if (!minic_riscv64_frame_load_int128(file, frame, operand, "t0", "t1") ||
                !minic_riscv64_emit_integer_conversion_for_program(
                    file, program, instruction->type, "t0")) {
                return false;
            }
            return minic_riscv64_frame_store_value(file, frame, instruction->result, "t0");
        }
        if (!minic_riscv64_frame_load_value(file, frame, operand, "t0") ||
            !minic_riscv64_emit_integer_conversion_for_program(
                file, program, instruction->type, "t0")) {
            return false;
        }
        return minic_riscv64_frame_store_value(file, frame, instruction->result, "t0");
    }
    case MINIC_CORE_INSTRUCTION_SCALAR_BITCAST:
        if (!minic_riscv64_frame_load_value(file, frame, instruction->value.operand, "t0")) {
            return false;
        }
        if (minic_type_is_integer(instruction->type) &&
            !minic_riscv64_emit_integer_conversion_for_program(
                file, program, instruction->type, "t0")) {
            return false;
        }
        return minic_riscv64_frame_store_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:
        if (!minic_riscv64_frame_load_value(file, frame, instruction->value.operand, "t0") ||
            fprintf(file, "  neg t0, t0\n") < 0 ||
            !minic_riscv64_emit_integer_conversion_for_program(
                file, program, instruction->type, "t0")) {
            return false;
        }
        return minic_riscv64_frame_store_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT:
        if (!minic_riscv64_frame_load_value(file, frame, instruction->value.operand, "t0") ||
            fprintf(file, "  xori t0, t0, -1\n") < 0 ||
            !minic_riscv64_emit_integer_conversion_for_program(
                file, program, instruction->type, "t0")) {
            return false;
        }
        return minic_riscv64_frame_store_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_INTEGER_CLZ: {
        unsigned int width;

        if (!minic_multi_direct_integer_bitscan_shape(
                program, minic_target_info_for_architecture(MINIC_TARGET_ARCH_RISCV64),
                function, instruction, &width) ||
            !minic_riscv64_frame_load_value(file, frame, instruction->value.operand, "t0") ||
            fprintf(file,
                    "  li t1, 0\n"
                    "  li t2, 1\n"
                    "  slli t2, t2, %u\n"
                    ".L%s_core_clz_loop_%" PRIu32 ":\n"
                    "  and t3, t0, t2\n"
                    "  bnez t3, .L%s_core_clz_done_%" PRIu32 "\n"
                    "  addi t1, t1, 1\n"
                    "  srli t2, t2, 1\n"
                    "  bnez t2, .L%s_core_clz_loop_%" PRIu32 "\n"
                    ".L%s_core_clz_done_%" PRIu32 ":\n",
                    width - 1U,
                    symbol_name, instruction->result,
                    symbol_name, instruction->result,
                    symbol_name, instruction->result,
                    symbol_name, instruction->result) < 0) {
            return false;
        }
        return minic_riscv64_frame_store_value(file, frame, instruction->result, "t1");
    }
    case MINIC_CORE_INSTRUCTION_INTEGER_CTZ: {
        unsigned int width;

        if (!minic_multi_direct_integer_bitscan_shape(
                program, minic_target_info_for_architecture(MINIC_TARGET_ARCH_RISCV64),
                function, instruction, &width) ||
            !minic_riscv64_frame_load_value(file, frame, instruction->value.operand, "t0") ||
            fprintf(file,
                    "  beqz t0, .L%s_core_ctz_zero_%" PRIu32 "\n"
                    "  li t1, 0\n"
                    ".L%s_core_ctz_loop_%" PRIu32 ":\n"
                    "  andi t2, t0, 1\n"
                    "  bnez t2, .L%s_core_ctz_done_%" PRIu32 "\n"
                    "  addi t1, t1, 1\n"
                    "  srli t0, t0, 1\n"
                    "  j .L%s_core_ctz_loop_%" PRIu32 "\n"
                    ".L%s_core_ctz_zero_%" PRIu32 ":\n"
                    "  li t1, %u\n"
                    ".L%s_core_ctz_done_%" PRIu32 ":\n",
                    symbol_name, instruction->result,
                    symbol_name, instruction->result,
                    symbol_name, instruction->result,
                    symbol_name, instruction->result,
                    symbol_name, instruction->result,
                    width,
                    symbol_name, instruction->result) < 0) {
            return false;
        }
        return minic_riscv64_frame_store_value(file, frame, instruction->result, "t1");
    }
    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO:
        if (!minic_riscv64_frame_load_value(file, frame, instruction->value.operand, "t0") ||
            fprintf(file, "  seqz t0, t0\n") < 0) {
            return false;
        }
        return minic_riscv64_frame_store_value(file, frame, instruction->result, "t0");
    default:
        return false;
    }
}
