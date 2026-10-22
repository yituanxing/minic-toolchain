#include "target/riscv64/scalar_emit.h"
#include "target/riscv64/codegen_internal.h"
#include "target/riscv64/legalize.h"
#include "target/riscv64/frame_access.h"
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

/* Six RV64 integer operations share the same SSA transfer and width fixup.
 * Only the ISA opcode varies. Preserve Core's per-instruction legalization. */
static bool emit_simple_integer_binary(
    FILE *file, const MinicC0Program *program, const MinicRiscv64CoreFrame *frame,
    const MinicCoreInstruction *instruction, const char *opcode) {
    if (file == NULL || program == NULL || frame == NULL ||
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
    const char *symbol_name, const MinicCoreInstruction *instruction) {
    if (file == NULL || program == NULL || function == NULL ||
        frame == NULL || symbol_name == NULL || instruction == NULL)
        return false;
    switch (instruction->kind) {
    case MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT:
        if (fprintf(file, "  li t0, %" PRId64 "\n", instruction->value.integer_value) < 0 ||
            !minic_riscv64_emit_integer_conversion_for_program(
                file, program, instruction->type, "t0")) {
            return false;
        }
        return minic_riscv64_frame_store_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT:
        if (minic_type_is_long_double(instruction->type)) {
            if (fprintf(file,
                        "  li t0, 0x%016" PRIx64 "\n"
                        "  li t1, 0x%016" PRIx64 "\n",
                        instruction->value.floating128_bits.low,
                        instruction->value.floating128_bits.high) < 0) {
                return false;
            }
            return minic_riscv64_frame_store_int128(file, frame, instruction->result, "t0", "t1");
        }
        if (minic_type_is_double(instruction->type)) {
            if (fprintf(file,
                        "  li t0, 0x%016" PRIx64 "\n",
                        instruction->value.floating_bits) < 0) {
                return false;
            }
        } else if (minic_type_is_float(instruction->type)) {
            if (fprintf(file,
                        "  li t0, 0x%08" PRIx64 "\n",
                        instruction->value.floating_bits & UINT64_C(0xffffffff)) < 0) {
                return false;
            }
        } else {
            return false;
        }
        return minic_riscv64_frame_store_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_FLOAT_ADD:
    case MINIC_CORE_INSTRUCTION_FLOAT_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_FLOAT_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_FLOAT_DIVIDE: {
        const char *opcode;

        if (!minic_type_is_float(instruction->type)) {
            return false;
        }
        switch (instruction->kind) {
        case MINIC_CORE_INSTRUCTION_FLOAT_ADD:
            opcode = "fadd.s";
            break;
        case MINIC_CORE_INSTRUCTION_FLOAT_SUBTRACT:
            opcode = "fsub.s";
            break;
        case MINIC_CORE_INSTRUCTION_FLOAT_MULTIPLY:
            opcode = "fmul.s";
            break;
        case MINIC_CORE_INSTRUCTION_FLOAT_DIVIDE:
            opcode = "fdiv.s";
            break;
        default:
            return false;
        }
        if (!minic_riscv64_frame_load_value(file, frame, instruction->value.binary.left, "t0") ||
            !minic_riscv64_frame_load_value(file, frame, instruction->value.binary.right, "t1") ||
            fprintf(file,
                    "  fmv.w.x ft0, t0\n"
                    "  fmv.w.x ft1, t1\n"
                    "  %s ft0, ft0, ft1\n"
                    "  fmv.x.w t0, ft0\n"
                    "  slli t0, t0, 32\n"
                    "  srli t0, t0, 32\n",
                    opcode) < 0) {
            return false;
        }
        return minic_riscv64_frame_store_value(file, frame, instruction->result, "t0");
    }
    case MINIC_CORE_INSTRUCTION_DOUBLE_ADD:
    case MINIC_CORE_INSTRUCTION_DOUBLE_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_DOUBLE_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_DOUBLE_DIVIDE: {
        const char *opcode;

        if (!minic_type_is_double(instruction->type)) {
            return false;
        }
        switch (instruction->kind) {
        case MINIC_CORE_INSTRUCTION_DOUBLE_ADD:
            opcode = "fadd.d";
            break;
        case MINIC_CORE_INSTRUCTION_DOUBLE_SUBTRACT:
            opcode = "fsub.d";
            break;
        case MINIC_CORE_INSTRUCTION_DOUBLE_MULTIPLY:
            opcode = "fmul.d";
            break;
        case MINIC_CORE_INSTRUCTION_DOUBLE_DIVIDE:
            opcode = "fdiv.d";
            break;
        default:
            return false;
        }
        if (!minic_riscv64_frame_load_value(file, frame, instruction->value.binary.left, "t0") ||
            !minic_riscv64_frame_load_value(file, frame, instruction->value.binary.right, "t1") ||
            fprintf(file,
                    "  fmv.d.x ft0, t0\n"
                    "  fmv.d.x ft1, t1\n"
                    "  %s ft0, ft0, ft1\n"
                    "  fmv.x.d t0, ft0\n",
                    opcode) < 0) {
            return false;
        }
        return minic_riscv64_frame_store_value(file, frame, instruction->result, "t0");
    }
    case MINIC_CORE_INSTRUCTION_DOUBLE_EQUAL:
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS:
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS_EQUAL: {
        const char *opcode;

        if (instruction->value.binary.left >= function->value_count ||
            instruction->value.binary.right >= function->value_count ||
            !minic_type_is_double(function->values[instruction->value.binary.left].type) ||
            !minic_type_equal(function->values[instruction->value.binary.left].type,
                              function->values[instruction->value.binary.right].type)) {
            return false;
        }
        opcode = instruction->kind == MINIC_CORE_INSTRUCTION_DOUBLE_EQUAL
                     ? "feq.d"
                 : instruction->kind == MINIC_CORE_INSTRUCTION_DOUBLE_LESS
                     ? "flt.d"
                     : "fle.d";
        if (!minic_riscv64_frame_load_value(file, frame, instruction->value.binary.left, "t0") ||
            !minic_riscv64_frame_load_value(file, frame, instruction->value.binary.right, "t1") ||
            fprintf(file,
                    "  fmv.d.x ft0, t0\n"
                    "  fmv.d.x ft1, t1\n"
                    "  %s t0, ft0, ft1\n",
                    opcode) < 0) {
            return false;
        }
        return minic_riscv64_frame_store_value(file, frame, instruction->result, "t0");
    }
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
            fprintf(file, "  mul t0, t0, t1\n") < 0 ||
            !minic_riscv64_emit_integer_conversion_for_program(
                file, program, instruction->type, "t0")) {
            return false;
        }
        return minic_riscv64_frame_store_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE:
    case MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER: {
        MinicType effective_type;
        const char *opcode;

        if (!minic_core_function_effective_integer_type(function, instruction->type, &effective_type)) {
            return false;
        }
        if (instruction->kind == MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE) {
            opcode = minic_type_is_unsigned_integer(effective_type) ? "divu" : "div";
        } else {
            opcode = minic_type_is_unsigned_integer(effective_type) ? "remu" : "rem";
        }
        if (!minic_riscv64_frame_load_value(file, frame, instruction->value.binary.left, "t0") ||
            !minic_riscv64_frame_load_value(file, frame, instruction->value.binary.right, "t1") ||
            fprintf(file, "  %s t0, t0, t1\n", opcode) < 0 ||
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
    case MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW: {
        MinicType left_type;
        MinicType result_type;
        MinicType right_type;
        size_t result_size;
        bool is_unsigned;

        if (!minic_riscv64_core_integer_overflow_supported(
                program, function, instruction, &result_type, &result_size, &is_unsigned) ||
            !minic_riscv64_frame_load_value(file, frame, instruction->value.integer_overflow.left, "t0") ||
            !minic_riscv64_frame_load_value(file, frame, instruction->value.integer_overflow.right, "t1") ||
            !minic_riscv64_frame_load_value(
                file, frame, instruction->value.integer_overflow.result_address, "t3")) {
            return false;
        }
        left_type = function->values[instruction->value.integer_overflow.left].type;
        right_type = function->values[instruction->value.integer_overflow.right].type;
        if ((!minic_type_equal(left_type, result_type) ||
             !minic_type_equal(right_type, result_type)) &&
            !(minic_riscv64_core_integer_type_range_fits(program, function, left_type, result_type) &&
              minic_riscv64_core_integer_type_range_fits(program, function, right_type, result_type)) &&
            !minic_riscv64_core_integer_overflow_xlen_scratch_exact(
                program, left_type, right_type, result_size)) {
            const char *signed_register;
            const char *unsigned_register;
            uint64_t maximum;

            if (is_unsigned || result_size >= 8U) {
                return false;
            }
            if (instruction->value.integer_overflow.operator_kind ==
                MINIC_CORE_INTEGER_OVERFLOW_MULTIPLY) {
                if (fprintf(file, "  mul t2, t0, t1\n  mv t4, t2\n") < 0 ||
                    !minic_riscv64_emit_integer_conversion_for_program(
                        file, program, result_type, "t2") ||
                    fprintf(file, "  xor t4, t4, t2\n  snez t4, t4\n") < 0 ||
                    !minic_riscv64_emit_scalar_store_for_program(
                        file, program, result_type, "t2", "t3")) {
                    return false;
                }
                return minic_riscv64_frame_store_value(file, frame, instruction->result, "t4");
            }
            if (instruction->value.integer_overflow.operator_kind !=
                MINIC_CORE_INTEGER_OVERFLOW_ADD) {
                return false;
            }
            signed_register = minic_type_equal(left_type, result_type) ? "t0" : "t1";
            unsigned_register = minic_type_equal(left_type, result_type) ? "t1" : "t0";
            maximum = (UINT64_C(1) << (result_size * 8U - 1U)) - UINT64_C(1);
            if (fprintf(file,
                        "  li t5, %" PRIu64 "\n"
                        "  sub t5, t5, %s\n"
                        "  sltu t4, t5, %s\n"
                        "  add t2, t0, t1\n",
                        maximum,
                        signed_register,
                        unsigned_register) < 0 ||
                !minic_riscv64_emit_integer_conversion_for_program(
                    file, program, result_type, "t2") ||
                !minic_riscv64_emit_scalar_store_for_program(
                    file, program, result_type, "t2", "t3")) {
                return false;
            }
            return minic_riscv64_frame_store_value(file, frame, instruction->result, "t4");
        }

        if (instruction->value.integer_overflow.operator_kind == MINIC_CORE_INTEGER_OVERFLOW_ADD) {
            if (result_size < 8U) {
                if (fprintf(file, "  add t2, t0, t1\n  mv t4, t2\n") < 0 ||
                    !minic_riscv64_emit_integer_conversion_for_program(
                        file, program, result_type, "t2") ||
                    fprintf(file, "  xor t4, t4, t2\n  snez t4, t4\n") < 0) {
                    return false;
                }
            } else if (is_unsigned) {
                if (fprintf(file,
                            "  add t2, t0, t1\n"
                            "  sltu t4, t2, t0\n") < 0) {
                    return false;
                }
            } else if (fprintf(file,
                               "  add t2, t0, t1\n"
                               "  xor t4, t0, t1\n"
                               "  xori t4, t4, -1\n"
                               "  xor t5, t0, t2\n"
                               "  and t4, t4, t5\n"
                               "  srli t4, t4, 63\n") < 0) {
                return false;
            }
        } else if (instruction->value.integer_overflow.operator_kind ==
                   MINIC_CORE_INTEGER_OVERFLOW_SUBTRACT) {
            if (result_size < 8U) {
                if (fprintf(file, "  sub t2, t0, t1\n  mv t4, t2\n") < 0 ||
                    !minic_riscv64_emit_integer_conversion_for_program(
                        file, program, result_type, "t2") ||
                    fprintf(file, "  xor t4, t4, t2\n  snez t4, t4\n") < 0) {
                    return false;
                }
            } else if (is_unsigned) {
                if (fprintf(file,
                            "  sub t2, t0, t1\n"
                            "  sltu t4, t0, t1\n") < 0) {
                    return false;
                }
            } else if (fprintf(file,
                               "  sub t2, t0, t1\n"
                               "  xor t4, t0, t1\n"
                               "  xor t5, t0, t2\n"
                               "  and t4, t4, t5\n"
                               "  srli t4, t4, 63\n") < 0) {
                return false;
            }
        } else if (result_size < 8U) {
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
        if (!minic_riscv64_emit_scalar_store_for_program(file, program, result_type, "t2", "t3")) {
            return false;
        }
        return minic_riscv64_frame_store_value(file, frame, instruction->result, "t4");
    }
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
    case MINIC_CORE_INSTRUCTION_INTEGER_TO_DOUBLE: {
        MinicCoreValueId operand = instruction->value.operand;
        MinicType source_type;
        const char *opcode;

        if (operand >= function->value_count ||
            !minic_core_function_effective_integer_type(function, function->values[operand].type, &source_type) ||
            minic_type_is_int128_integer(source_type) ||
            !minic_type_is_double(instruction->type)) {
            return false;
        }
        if (minic_type_is_long_integer(source_type)) {
            opcode = minic_type_is_unsigned_integer(source_type) ? "fcvt.d.lu" : "fcvt.d.l";
        } else {
            opcode = minic_type_is_unsigned_integer(source_type) ? "fcvt.d.wu" : "fcvt.d.w";
        }
        if (!minic_riscv64_frame_load_value(file, frame, operand, "t0") ||
            fprintf(file,
                    "  %s ft0, t0\n"
                    "  fmv.x.d t1, ft0\n",
                    opcode) < 0) {
            return false;
        }
        return minic_riscv64_frame_store_value(file, frame, instruction->result, "t1");
    }
    case MINIC_CORE_INSTRUCTION_FLOAT_TO_DOUBLE: {
        MinicCoreValueId operand = instruction->value.operand;

        if (operand >= function->value_count ||
            !minic_type_is_float(function->values[operand].type) ||
            !minic_type_is_double(instruction->type) ||
            !minic_riscv64_frame_load_value(file, frame, operand, "t0") ||
            fprintf(file,
                    "  fmv.w.x ft0, t0\n"
                    "  fcvt.d.s ft1, ft0\n"
                    "  fmv.x.d t1, ft1\n") < 0) {
            return false;
        }
        return minic_riscv64_frame_store_value(file, frame, instruction->result, "t1");
    }
    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_FLOAT: {
        MinicCoreValueId operand = instruction->value.operand;

        if (operand >= function->value_count ||
            !minic_type_is_double(function->values[operand].type) ||
            !minic_type_is_float(instruction->type) ||
            !minic_riscv64_frame_load_value(file, frame, operand, "t0") ||
            fprintf(file,
                    "  fmv.d.x ft0, t0\n"
                    "  fcvt.s.d ft1, ft0\n"
                    "  fmv.x.w t1, ft1\n"
                    "  slli t1, t1, 32\n"
                    "  srli t1, t1, 32\n") < 0) {
            return false;
        }
        return minic_riscv64_frame_store_value(file, frame, instruction->result, "t1");
    }
    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_INTEGER: {
        MinicCoreValueId operand = instruction->value.operand;
        MinicType target_type;
        const char *opcode;

        if (operand >= function->value_count ||
            !minic_type_is_double(function->values[operand].type) ||
            !minic_core_function_effective_integer_type(function, instruction->type, &target_type) ||
            minic_type_is_int128_integer(target_type)) {
            return false;
        }
        if (minic_type_is_long_integer(target_type)) {
            opcode = minic_type_is_unsigned_integer(target_type) ? "fcvt.lu.d" : "fcvt.l.d";
        } else {
            opcode = minic_type_is_unsigned_integer(target_type) ? "fcvt.wu.d" : "fcvt.w.d";
        }
        if (!minic_riscv64_frame_load_value(file, frame, operand, "t0") ||
            fprintf(file,
                    "  fmv.d.x ft0, t0\n"
                    "  %s t1, ft0, rtz\n",
                    opcode) < 0 ||
            !minic_riscv64_emit_integer_conversion_for_program(
                file, program, instruction->type, "t1")) {
            return false;
        }
        return minic_riscv64_frame_store_value(file, frame, instruction->result, "t1");
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
    case MINIC_CORE_INSTRUCTION_DOUBLE_NEGATE:
        if (!minic_riscv64_frame_load_value(file, frame, instruction->value.operand, "t0") ||
            fprintf(file,
                    "  fmv.d.x ft0, t0\n"
                    "  fsgnjn.d ft0, ft0, ft0\n"
                    "  fmv.x.d t0, ft0\n") < 0) {
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

        if (!minic_riscv64_core_unsigned_integer_width(
                program, function, instruction->value.operand, &width) ||
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

        if (!minic_riscv64_core_unsigned_integer_width(
                program, function, instruction->value.operand, &width) ||
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
