#include "target/x86_64/machine_internal.h"
#include "target/x86_64/abi.h"
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

/* The complete x86 SSE/x87 scalar Core instruction family. No ELF/TU work. */
bool minic_x86_64_emit_fp_instruction(
    FILE *out, const MinicC0Program *program,
    const MinicTargetInfo *target, const MinicCoreFunction *fn,
    const MinicX86Frame *frame, const MinicCoreInstruction *ins) {
    if (out == NULL || program == NULL || target == NULL ||
        fn == NULL || frame == NULL || ins == NULL) return false;
    switch (ins->kind) {
    case MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT:
        if (minic_type_is_long_double(ins->type)) {
            const uint64_t hi = ins->value.floating128_bits.high;
            const uint64_t lo = ins->value.floating128_bits.low;
            const uint64_t fraction_hi = hi & UINT64_C(0x0000ffffffffffff);
            const unsigned int biased_exponent = (unsigned int)((hi >> 48U) & 0x7fffU);
            const unsigned int sign_exponent = (unsigned int)(hi >> 48U);
            const uint64_t significand = biased_exponent == 0U
                ? 0U : (UINT64_C(1) << 63U) |
                    (fraction_hi << 15U) | (lo >> 49U);
            size_t off;
            /* Core literals are currently constructed by converting a
             * parsed binary64 to binary128. The x87 exponent has the
             * same bias as binary128, and its explicit 64-bit significand
             * represents every binary64 value exactly. Reject any wider
             * binary128 payload rather than silently truncating it. */
            if (ins->result >= fn->value_count || frame->value_offsets == NULL ||
                (lo & UINT64_C(0x0fffffffffffffff)) != 0U ||
                (biased_exponent == 0U && (fraction_hi != 0U || lo != 0U)))
                return false;
            off = frame->value_offsets[ins->result];
            if (off < 16U) return false;
            return fprintf(out,
                "  movabsq $0x%016" PRIx64 ", %%rax\n"
                "  movq %%rax, -%zu(%%rbp)\n"
                "  movq $%u, -%zu(%%rbp)\n",
                significand, off, sign_exponent, off - 8U) >= 0;
        }
        if (!minic_type_is_float(ins->type) && !minic_type_is_double(ins->type))
            return false;
        return fprintf(out, "  movabsq $%" PRIu64 ", %%rax\n",
                       ins->value.floating_bits) >= 0 &&
               x86_value_store(out, frame, ins->result);
    case MINIC_CORE_INSTRUCTION_DOUBLE_ADD:
    case MINIC_CORE_INSTRUCTION_DOUBLE_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_DOUBLE_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_DOUBLE_DIVIDE: {
        const char *opcode =
            ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_ADD ? "addsd" :
            ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_SUBTRACT ? "subsd" :
            ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_MULTIPLY ? "mulsd" : "divsd";
        if (!minic_type_is_double(ins->type) ||
            ins->value.binary.left >= fn->value_count ||
            ins->value.binary.right >= fn->value_count ||
            !x86_value_load(out, frame, ins->value.binary.left, "%rax") ||
            !x86_value_load(out, frame, ins->value.binary.right, "%r10") ||
            fprintf(out,
                    "  movq %%rax, %%xmm0\n"
                    "  movq %%r10, %%xmm1\n"
                    "  %s %%xmm1, %%xmm0\n"
                    "  movq %%xmm0, %%rax\n", opcode) < 0)
            return false;
        return x86_value_store(out, frame, ins->result);
    }
    case MINIC_CORE_INSTRUCTION_INTEGER_TO_DOUBLE: {
        const MinicCoreValueId operand = ins->value.operand;
        size_t source_bytes;
        bool source_signed;
        if (operand >= fn->value_count || !minic_type_is_double(ins->type) ||
            !minic_x86_64_scalar_width(program, target, fn->values[operand].type,
                              &source_bytes, &source_signed) ||
            !x86_value_load(out, frame, operand, "%rax"))
            return false;
        if (fprintf(out, "  cvtsi2sdq %%rax, %%xmm0\n") < 0) return false;
        /* Signed cvtsi2sdq is also valid for positive unsigned values.
         * A negative interpretation of an unsigned 64-bit value requires
         * adding exactly 2^64 (encoded as a binary64 constant). */
        if (!source_signed && source_bytes == 8U &&
            fprintf(out,
                    "  testq %%rax, %%rax\n"
                    "  jns 1f\n"
                    "  movabsq $0x43f0000000000000, %%r10\n"
                    "  movq %%r10, %%xmm1\n"
                    "  addsd %%xmm1, %%xmm0\n"
                    "1:\n") < 0)
            return false;
        return fprintf(out, "  movq %%xmm0, %%rax\n") >= 0 &&
               x86_value_store(out, frame, ins->result);
    }
    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_INTEGER: {
        const MinicCoreValueId operand = ins->value.operand;
        size_t dst_bytes;
        bool dst_signed;
        if (operand >= fn->value_count ||
            !minic_type_is_double(fn->values[operand].type) ||
            !minic_type_is_integer(ins->type) ||
            !minic_x86_64_scalar_width(program, target, ins->type, &dst_bytes, &dst_signed) ||
            !x86_value_load(out, frame, operand, "%rax") ||
            fprintf(out, "  movq %%rax, %%xmm0\n") < 0)
            return false;
        if (!dst_signed && dst_bytes == 8U) {
            /* For C-defined uint64 results, subtract 2^63 before converting
             * the high half, then restore the high bit. Out-of-range and NaN
             * float-to-int conversions have undefined C behavior. */
            if (fprintf(out,
                        "  movabsq $0x43e0000000000000, %%r10\n"
                        "  movq %%r10, %%xmm1\n"
                        "  ucomisd %%xmm1, %%xmm0\n"
                        "  jb 1f\n"
                        "  subsd %%xmm1, %%xmm0\n"
                        "  cvttsd2siq %%xmm0, %%rax\n"
                        "  btsq $63, %%rax\n"
                        "  jmp 2f\n"
                        "1:\n"
                        "  cvttsd2siq %%xmm0, %%rax\n"
                        "2:\n") < 0)
                return false;
        } else if (fprintf(out, "  cvttsd2siq %%xmm0, %%rax\n") < 0) {
            return false;
        }
        return x86_normalize(out, program, target, ins->type) &&
               x86_value_store(out, frame, ins->result);
    }
    case MINIC_CORE_INSTRUCTION_FLOAT_TO_DOUBLE:
    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_FLOAT: {
        const MinicCoreValueId operand = ins->value.operand;
        const bool widening = ins->kind == MINIC_CORE_INSTRUCTION_FLOAT_TO_DOUBLE;
        if (operand >= fn->value_count ||
            (widening && (!minic_type_is_float(fn->values[operand].type) ||
                          !minic_type_is_double(ins->type))) ||
            (!widening && (!minic_type_is_double(fn->values[operand].type) ||
                           !minic_type_is_float(ins->type))) ||
            !x86_value_load(out, frame, operand, "%rax"))
            return false;
        if (fprintf(out,
                    widening ? "  movd %%eax, %%xmm0\n"
                               "  cvtss2sd %%xmm0, %%xmm0\n"
                               "  movq %%xmm0, %%rax\n"
                             : "  movq %%rax, %%xmm0\n"
                               "  cvtsd2ss %%xmm0, %%xmm0\n"
                               "  movd %%xmm0, %%eax\n") < 0)
            return false;
        return x86_value_store(out, frame, ins->result);
    }
    case MINIC_CORE_INSTRUCTION_FLOAT_ADD:
    case MINIC_CORE_INSTRUCTION_FLOAT_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_FLOAT_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_FLOAT_DIVIDE: {
        const char *opcode =
            ins->kind == MINIC_CORE_INSTRUCTION_FLOAT_ADD ? "addss" :
            ins->kind == MINIC_CORE_INSTRUCTION_FLOAT_SUBTRACT ? "subss" :
            ins->kind == MINIC_CORE_INSTRUCTION_FLOAT_MULTIPLY ? "mulss" : "divss";
        if (!minic_type_is_float(ins->type) ||
            ins->value.binary.left >= fn->value_count ||
            ins->value.binary.right >= fn->value_count ||
            !x86_value_load(out, frame, ins->value.binary.left, "%rax") ||
            !x86_value_load(out, frame, ins->value.binary.right, "%r10") ||
            fprintf(out, "  movd %%eax, %%xmm0\n"
                         "  movd %%r10d, %%xmm1\n"
                         "  %s %%xmm1, %%xmm0\n"
                         "  movd %%xmm0, %%eax\n", opcode) < 0)
            return false;
        return x86_value_store(out, frame, ins->result);
    }
    case MINIC_CORE_INSTRUCTION_DOUBLE_NEGATE:
        if (!minic_type_is_double(ins->type) ||
            !x86_value_load(out, frame, ins->value.operand, "%rax") ||
            fprintf(out, "  movabsq $9223372036854775808, %%r10\n"
                         "  xorq %%r10, %%rax\n") < 0) return false;
        return x86_value_store(out, frame, ins->result);
    case MINIC_CORE_INSTRUCTION_DOUBLE_EQUAL:
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS:
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS_EQUAL: {
        const char *test =
            ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_EQUAL ? "sete" :
            ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_LESS ? "setb" : "setbe";
        if (ins->value.binary.left >= fn->value_count ||
            ins->value.binary.right >= fn->value_count ||
            !minic_type_is_double(fn->values[ins->value.binary.left].type) ||
            !minic_type_is_double(fn->values[ins->value.binary.right].type) ||
            !x86_value_load(out, frame, ins->value.binary.left, "%rax") ||
            !x86_value_load(out, frame, ins->value.binary.right, "%r10") ||
            fprintf(out, "  movq %%rax, %%xmm0\n"
                         "  movq %%r10, %%xmm1\n"
                         "  ucomisd %%xmm1, %%xmm0\n"
                         "  %s %%al\n"
                         "  setnp %%r10b\n"
                         "  andb %%r10b, %%al\n"
                         "  movzbq %%al, %%rax\n", test) < 0)
            return false;
        return x86_value_store(out, frame, ins->result);
    }
    default:
        return false;
    }
}
