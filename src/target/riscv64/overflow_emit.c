#include "target/riscv64/overflow_emit.h"
#include "target/riscv64/codegen_internal.h"
#include "target/riscv64/legalize.h"
#include "target/riscv64/frame_access.h"
#include "target/riscv64/runtime_libcall.h"
#include "target/riscv/typed_memory.h"
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

/* Strict RV64I checked multiplication uses upstream compiler-runtime calls,
 * not a second private software multiplier. For full-XLEN results, divide the
 * low product by one nonzero operand to detect high-bit loss, with an explicit
 * signed INT64_MIN * -1 check (RISC-V division overflow also returns MIN).
 * The native-M lowering below is intentionally unchanged. */
static bool emit_strict_i_checked_multiply(
    FILE *file, const MinicC0Program *program,
    const MinicCoreFunction *function,
    const MinicRiscv64CoreFrame *frame,
    const MinicCoreInstruction *instruction, const MinicRiscvIsa *isa,
    MinicType result_type, size_t result_size, bool is_unsigned) {
    MinicCoreValueId lhs = instruction->value.integer_overflow.left;
    MinicCoreValueId rhs = instruction->value.integer_overflow.right;
    MinicCoreValueId output = instruction->value.integer_overflow.result_address;
    const size_t proven_alignment = minic_riscv_address_alignment(
        program, minic_default_data_layout(), function, output);

    if (!minic_riscv64_frame_load_value(file, frame, lhs, "t0") ||
        !minic_riscv64_frame_load_value(file, frame, rhs, "t1") ||
        !minic_riscv64_emit_arithmetic_libcall(
            file, isa, MINIC_RISCV64_LIBCALL_MUL, "t2", "t0", "t1"))
        return false;

    if (result_size < 8U) {
        /* All admitted <=32-bit operand combinations fit in 64 bits.
         * Compare the full product to its narrow canonicalized value. */
        if (fprintf(file, "  mv t4, t2\n") < 0 ||
            !minic_riscv64_emit_integer_conversion_for_program(
                file, program, result_type, "t2") ||
            fprintf(file, "  xor t4, t4, t2\n  snez t4, t4\n") < 0)
            return false;
    } else {
        /* The result is stored before checking overflow as required by
         * __builtin_mul_overflow. Unlike using the reusable SSA result frame
         * slot as scratch, this cannot overwrite last-use input value slots.
         * Subsequent libcalls clobber all t-registers, so reread the source
         * values and the result pointer from their owned frame locations. */
        if (!minic_riscv64_frame_load_value(file, frame, output, "t3") ||
            !minic_riscv64_emit_safe_scalar_store(
                file, program, result_type, "t2", "t3", "t6",
                proven_alignment) ||
            !minic_riscv64_frame_load_value(file, frame, rhs, "t1") ||
            fprintf(file, "  beqz t1, 1f\n") < 0 ||
            !minic_riscv64_frame_load_value(file, frame, output, "t3") ||
            !minic_riscv64_emit_safe_scalar_load(
                file, program, result_type, "t0", "t3", "t6",
                proven_alignment) ||
            !minic_riscv64_emit_arithmetic_libcall(
                file, isa, is_unsigned ? MINIC_RISCV64_LIBCALL_UDIV :
                                         MINIC_RISCV64_LIBCALL_SDIV,
                "t4", "t0", "t1") ||
            !minic_riscv64_frame_load_value(file, frame, lhs, "t0") ||
            fprintf(file, "  xor t4, t4, t0\n  snez t4, t4\n") < 0)
            return false;
        if (!is_unsigned) {
            if (!minic_riscv64_frame_load_value(file, frame, lhs, "t0") ||
                !minic_riscv64_frame_load_value(file, frame, rhs, "t1") ||
                fprintf(file,
                        "  li t5, 1\n  slli t5, t5, 63\n"
                        "  xor t5, t5, t0\n  seqz t5, t5\n"
                        "  li t6, -1\n  xor t6, t6, t1\n"
                        "  seqz t6, t6\n  and t5, t5, t6\n"
                        "  or t4, t4, t5\n") < 0)
                return false;
        }
        if (fprintf(file, "  j 2f\n1:\n  li t4, 0\n2:\n") < 0)
            return false;
        return minic_riscv64_frame_store_value(file, frame, instruction->result, "t4");
    }

    /* A helper call invalidates t3; never reuse the pre-call output address. */
    if (!minic_riscv64_frame_load_value(file, frame, output, "t3") ||
        !minic_riscv64_emit_safe_scalar_store(
            file, program, result_type, "t2", "t3", "t6",
            proven_alignment))
        return false;
    return minic_riscv64_frame_store_value(file, frame, instruction->result, "t4");
}

/* Native RV64 overflow lowering is intentionally kept adjacent to its
 * mixed signedness/legalization rules rather than normal int ALU emission. */
bool minic_riscv64_emit_overflow_instruction(
    FILE *file, const MinicC0Program *program,
    const MinicCoreFunction *function, const MinicRiscv64CoreFrame *frame,
    const MinicCoreInstruction *instruction, const MinicRiscvIsa *isa) {
    if (file == NULL || function == NULL || frame == NULL ||
        instruction == NULL ||
        instruction->kind != MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW)
        return false;
    switch (instruction->kind) {
    case MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW: {
        MinicType left_type;
        MinicType result_type;
        MinicType right_type;
        size_t result_size;
        bool is_unsigned;
        size_t proven_alignment;

        if (!minic_riscv64_core_integer_overflow_supported(
                program, function, instruction, &result_type, &result_size, &is_unsigned))
            return false;
        proven_alignment = minic_riscv_address_alignment(
            program, minic_default_data_layout(), function,
            instruction->value.integer_overflow.result_address);
        if (minic_riscv64_uses_arithmetic_libcall(isa) &&
            instruction->value.integer_overflow.operator_kind ==
                MINIC_CORE_INTEGER_OVERFLOW_MULTIPLY)
            return emit_strict_i_checked_multiply(
                file, program, function, frame, instruction, isa, result_type, result_size,
                is_unsigned);
        if (!minic_riscv64_frame_load_value(
                file, frame, instruction->value.integer_overflow.left, "t0") ||
            !minic_riscv64_frame_load_value(
                file, frame, instruction->value.integer_overflow.right, "t1") ||
            !minic_riscv64_frame_load_value(
                file, frame, instruction->value.integer_overflow.result_address, "t3"))
            return false;
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
                    !minic_riscv64_emit_safe_scalar_store(
                        file, program, result_type, "t2", "t3", "t6",
                        proven_alignment)) {
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
                !minic_riscv64_emit_safe_scalar_store(
                    file, program, result_type, "t2", "t3", "t6",
                    proven_alignment)) {
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
        if (!minic_riscv64_emit_safe_scalar_store(
                file, program, result_type, "t2", "t3", "t6",
                proven_alignment)) {
            return false;
        }
        return minic_riscv64_frame_store_value(file, frame, instruction->result, "t4");
    }
    default:
        return false;
    }
}
