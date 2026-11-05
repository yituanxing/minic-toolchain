#include "target/riscv64/fp_emit.h"
#include "target/riscv64/codegen_internal.h"
#include "target/riscv64/frame_access.h"
#include "target/riscv64/runtime_libcall.h"
#include "target/common/core_instruction_family.h"
#include "target/common/core_floating_shape.h"
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

/* The whole RV64F/RV64D Core family, distinct from scalar integer code.
 * Width/format-independent opcode handling is implemented once. */
/* Both RV64F and RV64D operations use the same two-SSA-input transfer.
 * Only opcode suffix and fmv bit width differ. Keep the exact old RV64
 * register sequence and 32-bit result zero-extension on the RV64F path. */
static bool rv64_emit_fp_binary(
    FILE *file, const MinicRiscv64CoreFrame *frame,
    const MinicCoreInstruction *instruction, const MinicRiscvIsa *isa) {
    bool is_double;
    const char *op;
    switch (instruction->kind) {
    case MINIC_CORE_INSTRUCTION_FLOAT_ADD: op = "fadd"; is_double = false; break;
    case MINIC_CORE_INSTRUCTION_FLOAT_SUBTRACT: op = "fsub"; is_double = false; break;
    case MINIC_CORE_INSTRUCTION_FLOAT_MULTIPLY: op = "fmul"; is_double = false; break;
    case MINIC_CORE_INSTRUCTION_FLOAT_DIVIDE: op = "fdiv"; is_double = false; break;
    case MINIC_CORE_INSTRUCTION_DOUBLE_ADD: op = "fadd"; is_double = true; break;
    case MINIC_CORE_INSTRUCTION_DOUBLE_SUBTRACT: op = "fsub"; is_double = true; break;
    case MINIC_CORE_INSTRUCTION_DOUBLE_MULTIPLY: op = "fmul"; is_double = true; break;
    case MINIC_CORE_INSTRUCTION_DOUBLE_DIVIDE: op = "fdiv"; is_double = true; break;
    default: return false;
    }
    if (!minic_riscv64_frame_load_value(
            file, frame, instruction->value.binary.left, "t0") ||
        !minic_riscv64_frame_load_value(
            file, frame, instruction->value.binary.right, "t1"))
        return false;
    {
        const char *helper =
            minic_riscv64_softfloat_binary_symbol(isa, instruction->kind);
        if (helper != NULL) {
            /* No fp registers or hidden arithmetic implementation. The
             * existing frame plan preserved ra for this implicit call. */
            if (fprintf(file,
                        "  mv a0, t0\n"
                        "  mv a1, t1\n"
                        "  call %s\n"
                        "  mv t0, a0\n", helper) < 0)
                return false;
            if (!is_double &&
                fprintf(file, "  slli t0, t0, 32\n  srli t0, t0, 32\n") < 0)
                return false;
            return minic_riscv64_frame_store_value(
                file, frame, instruction->result, "t0");
        }
    }
    if (is_double) {
        if (fprintf(file,
                    "  fmv.d.x ft0, t0\n"
                    "  fmv.d.x ft1, t1\n"
                    "  %s.d ft0, ft0, ft1\n"
                    "  fmv.x.d t0, ft0\n", op) < 0)
            return false;
    } else if (fprintf(file,
                       "  fmv.w.x ft0, t0\n"
                       "  fmv.w.x ft1, t1\n"
                       "  %s.s ft0, ft0, ft1\n"
                       "  fmv.x.w t0, ft0\n"
                       "  slli t0, t0, 32\n"
                       "  srli t0, t0, 32\n", op) < 0) {
        return false;
    }
    return minic_riscv64_frame_store_value(
        file, frame, instruction->result, "t0");
}

bool minic_riscv64_emit_fp_instruction(
    FILE *file, const MinicCoreFunction *function,
    const MinicRiscv64CoreFrame *frame,
    const MinicCoreInstruction *instruction, const MinicRiscvIsa *isa) {
    MinicCoreFloatingShape shape;
    if (file == NULL || function == NULL || frame == NULL ||
        instruction == NULL ||
        !minic_core_instruction_is_floating_scalar(instruction->kind) ||
        !minic_core_floating_shape(function, instruction, &shape))
        return false;
    switch (instruction->kind) {
    case MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT:
        if (shape.result_format == MINIC_CORE_FLOAT_BINARY128) {
            if (fprintf(file,
                        "  li t0, 0x%016" PRIx64 "\n"
                        "  li t1, 0x%016" PRIx64 "\n",
                        instruction->value.floating128_bits.low,
                        instruction->value.floating128_bits.high) < 0) {
                return false;
            }
            return minic_riscv64_frame_store_int128(file, frame, instruction->result, "t0", "t1");
        }
        if (shape.result_format == MINIC_CORE_FLOAT_BINARY64) {
            if (fprintf(file,
                        "  li t0, 0x%016" PRIx64 "\n",
                        instruction->value.floating_bits) < 0) {
                return false;
            }
        } else if (shape.result_format == MINIC_CORE_FLOAT_BINARY32) {
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
    case MINIC_CORE_INSTRUCTION_FLOAT_DIVIDE:
    case MINIC_CORE_INSTRUCTION_DOUBLE_ADD:
    case MINIC_CORE_INSTRUCTION_DOUBLE_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_DOUBLE_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_DOUBLE_DIVIDE:
        return rv64_emit_fp_binary(file, frame, instruction, isa);
    case MINIC_CORE_INSTRUCTION_DOUBLE_EQUAL:
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS:
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS_EQUAL: {
        const char *opcode;

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
    case MINIC_CORE_INSTRUCTION_INTEGER_TO_DOUBLE: {
        MinicCoreValueId operand = instruction->value.operand;
        const char *opcode;
        if (shape.integer_bytes > 8U)
            return false;
        if (shape.integer_bytes == 8U) {
            opcode = shape.integer_signed ? "fcvt.d.l" : "fcvt.d.lu";
        } else {
            opcode = shape.integer_signed ? "fcvt.d.w" : "fcvt.d.wu";
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

        if (!minic_riscv64_frame_load_value(file, frame, operand, "t0") ||
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

        if (!minic_riscv64_frame_load_value(file, frame, operand, "t0") ||
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
        const char *opcode;
        if (shape.integer_bytes > 8U)
            return false;
        if (shape.integer_bytes == 8U) {
            opcode = shape.integer_signed ? "fcvt.l.d" : "fcvt.lu.d";
        } else {
            opcode = shape.integer_signed ? "fcvt.w.d" : "fcvt.wu.d";
        }
        if (!minic_riscv64_frame_load_value(file, frame, operand, "t0") ||
            fprintf(file,
                    "  fmv.d.x ft0, t0\n"
                    "  %s t1, ft0, rtz\n",
                    opcode) < 0 ||
            !minic_riscv64_emit_core_integer_conversion(
                file, function->type_context, shape.result_id, "t1")) {
            return false;
        }
        return minic_riscv64_frame_store_value(file, frame, instruction->result, "t1");
    }
    case MINIC_CORE_INSTRUCTION_DOUBLE_NEGATE:
        if (!minic_riscv64_frame_load_value(file, frame, instruction->value.operand, "t0") ||
            fprintf(file,
                    "  fmv.d.x ft0, t0\n"
                    "  fsgnjn.d ft0, ft0, ft0\n"
                    "  fmv.x.d t0, ft0\n") < 0) {
            return false;
        }
        return minic_riscv64_frame_store_value(file, frame, instruction->result, "t0");
    default:
        return false;
    }
}
