#include "target/riscv64/scalar_emit.h"
#include "target/riscv/instruction_select.h"
#include "target/riscv/word_operations.h"
#include "target/riscv64/fp_emit.h"
#include "target/riscv64/overflow_emit.h"
#include "target/riscv64/codegen_internal.h"
#include "target/riscv64/legalize.h"
#include "target/common/core_instruction_family.h"
#include "target/common/core_numeric_shape.h"
#include "target/riscv64/frame_access.h"
#include "target/riscv64/runtime_libcall.h"
#include "target/riscv64/core_type_query.h"
#include "target/common/lp64_scalar.h"
#include "target/common/core_scalar_conversion.h"
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

static const MinicCoreType *rv64_scalar_integer(
    const MinicCoreFunction *function, MinicCoreTypeId id) {
    const MinicCoreType *type = minic_riscv64_core_base_type(function, id);
    return type != NULL && type->kind == MINIC_CORE_TYPE_INTEGER ? type : NULL;
}

static bool rv64_scalar_unsigned(
    const MinicCoreFunction *function, MinicCoreTypeId id) {
    const MinicCoreType *type = rv64_scalar_integer(function, id);
    return type != NULL && !type->value.integer.is_signed;
}

static bool rv64_scalar_same_type(
    const MinicCoreFunction *function, MinicCoreTypeId left,
    MinicCoreTypeId right) {
    return function != NULL && function->type_context != NULL &&
           minic_lp64_core_equal_unqualified(function->type_context, left, right);
}

/* Six RV64 integer operations share the same SSA transfer and width fixup.
 * Only the ISA opcode varies. Preserve Core's per-instruction legalization. */
static bool emit_simple_integer_binary(
    FILE *file, const MinicCoreFunction *function, const MinicRiscv64CoreFrame *frame,
    const MinicCoreInstruction *instruction, const char *opcode) {
    if (file == NULL || frame == NULL ||
        instruction == NULL || opcode == NULL ||
        !minic_riscv64_frame_load_value(file, frame, instruction->value.binary.left, "t0") ||
        !minic_riscv64_frame_load_value(file, frame, instruction->value.binary.right, "t1") ||
        fprintf(file, "  %s t0, t0, t1\n", opcode) < 0 ||
        !minic_riscv64_emit_core_integer_conversion(
            file, function->type_context, instruction->core_type_id, "t0")) {
        return false;
    }
    return minic_riscv64_frame_store_value(file, frame, instruction->result, "t0");
}

/* RV64 LP64 passes TI integers as low/high XLEN register pairs.
 * Source operands are frame-owned, so incoming a0..a3 can be loaded
 * without destructive aliasing. It is the Base-I fallback even when
 * an optional M extension exists but has no two-word compare opcode.
 * The frame planner must save ra for these runtime calls. */
static bool emit_int128_runtime_call(
    FILE *file, const MinicRiscv64CoreFrame *frame,
    const MinicCoreFunction *function, const MinicCoreInstruction *instruction,
    const char *symbol, bool two_word_result, bool equal_result) {
    MinicCoreValueId left;
    MinicCoreValueId right;
    if (file == NULL || frame == NULL || function == NULL ||
        instruction == NULL || symbol == NULL || !frame->saves_return_address)
        return false;
    left = instruction->value.binary.left;
    right = instruction->value.binary.right;
    if (left >= function->value_count || right >= function->value_count ||
        !minic_riscv64_core_is_i128(function, function->values[left].core_type_id) ||
        !minic_riscv64_core_is_i128(function, function->values[right].core_type_id) ||
        !minic_riscv64_frame_load_int128(file, frame, left, "a0", "a1") ||
        !minic_riscv64_frame_load_int128(file, frame, right, "a2", "a3") ||
        fprintf(file, "  call %s\n", symbol) < 0)
        return false;
    if (two_word_result)
        return minic_riscv64_frame_store_int128(
            file, frame, instruction->result, "a0", "a1");
    /* GCC/libgcc TI compare ABI: 0 less, 1 equal, 2 greater. */
    if (fprintf(file, equal_result
                ? "  addi t0, a0, -1\n  seqz t0, t0\n"
                : "  seqz t0, a0\n") < 0)
        return false;
    return minic_riscv64_frame_store_value(
        file, frame, instruction->result, "t0");
}


/* RV64 integer-pair Core lowering: shared 128-bit language semantics, with
 * only base-I add/sub/logic/shift machine instructions. Low word is first
 * in the frame and in the LP64 ABI. Never truncate an i128 to t0. */
static bool emit_int128_pair_binary(
    FILE *file, const MinicRiscv64CoreFrame *frame,
    const MinicCoreFunction *function, const MinicCoreInstruction *instruction) {
    MinicCoreValueId left = instruction->value.binary.left;
    MinicCoreValueId right = instruction->value.binary.right;
    if (left >= function->value_count || right >= function->value_count ||
        !minic_riscv64_core_is_i128(function, function->values[left].core_type_id) ||
        !minic_riscv64_core_is_i128(function, function->values[right].core_type_id) ||
        !minic_riscv64_frame_load_int128(file, frame, left, "t0", "t1") ||
        !minic_riscv64_frame_load_int128(file, frame, right, "t2", "t3") ||
        !minic_riscv_emit_word_pair_binary(file, instruction->kind))
        return false;
    return minic_riscv64_frame_store_int128(
        file, frame, instruction->result, "t0", "t1");
}

static bool emit_int128_pair_unary(
    FILE *file, const MinicRiscv64CoreFrame *frame,
    const MinicCoreFunction *function, const MinicCoreInstruction *instruction) {
    const MinicCoreValueId operand = instruction->value.operand;
    if (operand >= function->value_count ||
        !minic_riscv64_core_is_i128(function, function->values[operand].core_type_id) ||
        !minic_riscv64_frame_load_int128(file, frame, operand, "t0", "t1") ||
        !minic_riscv_emit_word_pair_unary(file, instruction->kind))
        return false;
    return minic_riscv64_frame_store_int128(
        file, frame, instruction->result, "t0", "t1");
}

static bool emit_int128_pair_shift_left(
    FILE *file, const MinicRiscv64CoreFrame *frame,
    const MinicCoreFunction *function, const MinicCoreInstruction *instruction,
    const char *symbol_name) {
    const MinicCoreValueId left = instruction->value.binary.left;
    const MinicCoreValueId right = instruction->value.binary.right;
    if (left >= function->value_count || right >= function->value_count ||
        !minic_riscv64_core_is_i128(function, function->values[left].core_type_id) ||
        rv64_scalar_integer(function, function->values[right].core_type_id) == NULL ||
        minic_riscv64_core_is_i128(function, function->values[right].core_type_id) ||
        !minic_riscv64_frame_load_int128(file, frame, left, "t0", "t1") ||
        !minic_riscv64_frame_load_value(file, frame, right, "t2") ||
        !minic_riscv_emit_word_pair_shift(
            file, instruction->kind, false,
            symbol_name, "core_i128_shl", instruction->result, 64U, false))
        return false;
    return minic_riscv64_frame_store_int128(
        file, frame, instruction->result, "t0", "t1");
}

bool minic_riscv64_emit_scalar_instruction(
    FILE *file, const MinicCoreFunction *function, const MinicRiscv64CoreFrame *frame,
    const char *symbol_name, const MinicCoreInstruction *instruction,
    const MinicRiscvIsa *isa) {
    if (file == NULL || function == NULL || function->type_context == NULL ||
        frame == NULL || symbol_name == NULL || instruction == NULL)
        return false;
    /* The four ISA backends agree on the semantic FP family.
     * RV64 keeps its own fmv/fcvt and floating register sequences. */
    if (minic_core_instruction_is_floating_scalar(instruction->kind))
        return minic_riscv64_emit_fp_instruction(
            file, function, frame, instruction, isa);
    if (instruction->kind == MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW)
        return minic_riscv64_emit_overflow_instruction(
            file, function, frame, instruction, isa);
    /* The ordinary scalar emitters consume only one XLEN word.
     * The pair emitter is the unique selection seam for wide I operations. */
    if (minic_riscv64_core_is_i128(function, instruction->core_type_id)) {
        switch (instruction->kind) {
        case MINIC_CORE_INSTRUCTION_INTEGER_ADD:
        case MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT:
        case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:
        case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR:
        case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR:
            return emit_int128_pair_binary(file, frame, function, instruction);
        case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:
        case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT:
            return emit_int128_pair_unary(file, frame, function, instruction);
        case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT:
            return emit_int128_pair_shift_left(
                file, frame, function, instruction, symbol_name);
        case MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE:
        case MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER:
            if (isa != NULL && isa->strict_isa) {
                const bool is_unsigned =
                    rv64_scalar_unsigned(function, instruction->core_type_id);
                const char *sym =
                    instruction->kind == MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE
                        ? (is_unsigned ? "__udivti3" : "__divti3")
                        : (is_unsigned ? "__umodti3" : "__modti3");
                return emit_int128_runtime_call(
                    file, frame, function, instruction, sym, true, false);
            }
            return false;
        default:
            break;
        }
    }
    switch (instruction->kind) {
    case MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT:
        if (minic_riscv64_core_is_i128(function, instruction->core_type_id)) {
            /* Core immediate storage is signed int64. Sign-extend this
             * mathematical integer into the LP64 low/high register pair.
             * Wider literals must be built from separate Core operations. */
            if (fprintf(file, "  li t0, %" PRId64 "\n"
                              "  li t1, %d\n",
                        instruction->value.integer_value,
                        instruction->value.integer_value < 0 ? -1 : 0) < 0)
                return false;
            return minic_riscv64_frame_store_int128(
                file, frame, instruction->result, "t0", "t1");
        }
        if (fprintf(file, "  li t0, %" PRId64 "\n", instruction->value.integer_value) < 0 ||
            !minic_riscv64_emit_core_integer_conversion(
                file, function->type_context, instruction->core_type_id, "t0")) {
            return false;
        }
        return minic_riscv64_frame_store_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_INTEGER_ADD:
    case MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR:
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT:
        return emit_simple_integer_binary(
            file, function, frame, instruction,
            minic_riscv_scalar_binary_opcode(instruction->kind, false));
    case MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY:
        if (minic_riscv64_core_is_i128(function, instruction->core_type_id)) {
            if (minic_riscv64_uses_arithmetic_libcall(isa))
                return emit_int128_runtime_call(
                    file, frame, function, instruction, "__multi3", true, false);
            MinicCoreValueId left = instruction->value.binary.left;
            MinicCoreValueId right = instruction->value.binary.right;

            if (left >= function->value_count || right >= function->value_count ||
                !minic_riscv64_core_is_i128(function, function->values[left].core_type_id) ||
                !minic_riscv64_core_is_i128(function, function->values[right].core_type_id) ||
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
            !minic_riscv64_emit_core_integer_conversion(
                file, function->type_context, instruction->core_type_id, "t0")) {
            return false;
        }
        return minic_riscv64_frame_store_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE:
    case MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER: {
        const MinicCoreType *effective_type =
            rv64_scalar_integer(function, instruction->core_type_id);
        if (effective_type == NULL) return false;
        if (!minic_riscv64_frame_load_value(file, frame, instruction->value.binary.left, "t0") ||
            !minic_riscv64_frame_load_value(file, frame, instruction->value.binary.right, "t1") ||
            /* LP64 libcalls consume full XLEN inputs. Canonicalize 32-bit
             * signed/unsigned operands before entering libgcc/compiler-rt.
             * Leave the historical M hardware path byte-for-byte intact. */
            (minic_riscv64_uses_arithmetic_libcall(isa) &&
             (!minic_riscv64_emit_core_integer_conversion(
                  file, function->type_context, instruction->core_type_id, "t0") ||
              !minic_riscv64_emit_core_integer_conversion(
                  file, function->type_context, instruction->core_type_id, "t1"))) ||
            !minic_riscv64_emit_arithmetic_libcall(
                file, isa,
                instruction->kind == MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER
                  ? (!effective_type->value.integer.is_signed
                       ? MINIC_RISCV64_LIBCALL_UMOD : MINIC_RISCV64_LIBCALL_SMOD)
                  : (!effective_type->value.integer.is_signed
                       ? MINIC_RISCV64_LIBCALL_UDIV : MINIC_RISCV64_LIBCALL_SDIV),
                "t0", "t0", "t1") ||
            !minic_riscv64_emit_core_integer_conversion(
                file, function->type_context, instruction->core_type_id, "t0")) {
            return false;
        }
        return minic_riscv64_frame_store_value(file, frame, instruction->result, "t0");
    }
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT: {
        const MinicCoreType *effective_type;
        const char *opcode;

        effective_type = rv64_scalar_integer(function, instruction->core_type_id);
        if (effective_type == NULL) return false;
        if (minic_riscv64_core_is_i128(function, instruction->core_type_id)) {
            bool is_signed;
            MinicCoreValueId left = instruction->value.binary.left;
            MinicCoreValueId right = instruction->value.binary.right;

            if (left >= function->value_count || right >= function->value_count ||
                !minic_riscv64_core_is_i128(function, function->values[left].core_type_id) ||
                rv64_scalar_integer(function, function->values[right].core_type_id) == NULL ||
                minic_riscv64_core_is_i128(function, function->values[right].core_type_id) ||
                !minic_riscv64_core_integer_type_is_signed(
                    function, instruction->core_type_id, &is_signed) ||
                !minic_riscv64_frame_load_int128(file, frame, left, "t0", "t1") ||
                !minic_riscv64_frame_load_value(file, frame, right, "t2") ||
                !minic_riscv_emit_word_pair_shift(
                    file, instruction->kind, is_signed,
                    symbol_name, "core_i128_shr",
                    instruction->result, 64U, false))
                return false;
            return minic_riscv64_frame_store_int128(file, frame, instruction->result, "t0", "t1");
        }
        opcode = minic_riscv_scalar_binary_opcode(
            instruction->kind, !effective_type->value.integer.is_signed);
        if (!minic_riscv64_frame_load_value(file, frame, instruction->value.binary.left, "t0") ||
            !minic_riscv64_frame_load_value(file, frame, instruction->value.binary.right, "t1") ||
            fprintf(file, "  %s t0, t0, t1\n", opcode) < 0 ||
            !minic_riscv64_emit_core_integer_conversion(
                file, function->type_context, instruction->core_type_id, "t0")) {
            return false;
        }
        return minic_riscv64_frame_store_value(file, frame, instruction->result, "t0");
    }
    case MINIC_CORE_INSTRUCTION_INTEGER_LESS: {
        if (isa != NULL && isa->strict_isa &&
            instruction->value.binary.left < function->value_count &&
            minic_riscv64_core_is_i128(
                function, function->values[instruction->value.binary.left].core_type_id)) {
            const MinicCoreTypeId wide_type =
                function->values[instruction->value.binary.left].core_type_id;
            return emit_int128_runtime_call(
                file, frame, function, instruction,
                rv64_scalar_unsigned(function, wide_type)
                    ? "__ucmpti2" : "__cmpti2", false, false);
        }
        const MinicCoreType *effective_type;
        MinicCoreTypeId operand_type;
        const char *opcode;

        if (instruction->value.binary.left >= function->value_count ||
            instruction->value.binary.right >= function->value_count) {
            return false;
        }
        operand_type = function->values[instruction->value.binary.left].core_type_id;
        effective_type = rv64_scalar_integer(function, operand_type);
        if (effective_type == NULL || !rv64_scalar_same_type(
                function, operand_type,
                function->values[instruction->value.binary.right].core_type_id)) return false;
        opcode = minic_riscv_scalar_binary_opcode(
            instruction->kind, !effective_type->value.integer.is_signed);
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
            minic_riscv64_core_base_type(
                function, function->values[instruction->value.binary.left].core_type_id) == NULL ||
            minic_riscv64_core_base_type(
                function, function->values[instruction->value.binary.left].core_type_id)->kind !=
                MINIC_CORE_TYPE_POINTER ||
            !rv64_scalar_same_type(
                function, function->values[instruction->value.binary.left].core_type_id,
                function->values[instruction->value.binary.right].core_type_id) ||
            !minic_riscv64_frame_load_value(file, frame, instruction->value.binary.left, "t0") ||
            !minic_riscv64_frame_load_value(file, frame, instruction->value.binary.right, "t1") ||
            fprintf(file, "  sltu t0, t0, t1\n") < 0) {
            return false;
        }
        return minic_riscv64_frame_store_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:
        if (isa != NULL && isa->strict_isa &&
            instruction->value.binary.left < function->value_count &&
            minic_riscv64_core_is_i128(
                function, function->values[instruction->value.binary.left].core_type_id))
            return emit_int128_runtime_call(
                file, frame, function, instruction, "__ucmpti2", false, true);
        if (!minic_riscv64_frame_load_value(file, frame, instruction->value.binary.left, "t0") ||
            !minic_riscv64_frame_load_value(file, frame, instruction->value.binary.right, "t1") ||
            fprintf(file, "  xor t0, t0, t1\n  seqz t0, t0\n") < 0) {
            return false;
        }
        return minic_riscv64_frame_store_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION: {
        MinicCoreValueId operand = instruction->value.operand;
        MinicCoreIntegerConversionShape cast;
        if (!minic_core_integer_conversion_shape(function, instruction, &cast))
            return false;
        const MinicCoreTypeId source_type = cast.source_id;
        if (minic_riscv64_core_is_i128(function, instruction->core_type_id)) {
            if (minic_riscv64_core_is_i128(function, source_type)) {
                if (!minic_riscv64_frame_load_int128(file, frame, operand, "t0", "t1")) {
                    return false;
                }
            } else {
                bool source_signed;

                if (rv64_scalar_integer(function, source_type) == NULL ||
                    !minic_riscv64_frame_load_value(file, frame, operand, "t0") ||
                    !minic_riscv64_core_integer_type_is_signed(function, source_type, &source_signed) ||
                    fprintf(file,
                            source_signed ? "  srai t1, t0, 63\n" : "  li t1, 0\n") < 0) {
                    return false;
                }
            }
            return minic_riscv64_frame_store_int128(file, frame, instruction->result, "t0", "t1");
        }
        if (minic_riscv64_core_is_i128(function, source_type)) {
            if (!minic_riscv64_frame_load_int128(file, frame, operand, "t0", "t1") ||
                !minic_riscv64_emit_core_integer_conversion(
                    file, function->type_context, instruction->core_type_id, "t0")) {
                return false;
            }
            return minic_riscv64_frame_store_value(file, frame, instruction->result, "t0");
        }
        if (!minic_riscv64_frame_load_value(file, frame, operand, "t0") ||
            !minic_riscv64_emit_core_integer_conversion(
                file, function->type_context, instruction->core_type_id, "t0")) {
            return false;
        }
        return minic_riscv64_frame_store_value(file, frame, instruction->result, "t0");
    }
    case MINIC_CORE_INSTRUCTION_SCALAR_BITCAST:
        if (!minic_riscv64_frame_load_value(file, frame, instruction->value.operand, "t0")) {
            return false;
        }
        if (rv64_scalar_integer(function, instruction->core_type_id) != NULL &&
            !minic_riscv64_emit_core_integer_conversion(
                file, function->type_context, instruction->core_type_id, "t0")) {
            return false;
        }
        return minic_riscv64_frame_store_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:
        if (!minic_riscv64_frame_load_value(file, frame, instruction->value.operand, "t0") ||
            fprintf(file, "  neg t0, t0\n") < 0 ||
            !minic_riscv64_emit_core_integer_conversion(
                file, function->type_context, instruction->core_type_id, "t0")) {
            return false;
        }
        return minic_riscv64_frame_store_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT:
        if (!minic_riscv64_frame_load_value(file, frame, instruction->value.operand, "t0") ||
            fprintf(file, "  xori t0, t0, -1\n") < 0 ||
            !minic_riscv64_emit_core_integer_conversion(
                file, function->type_context, instruction->core_type_id, "t0")) {
            return false;
        }
        return minic_riscv64_frame_store_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_INTEGER_CLZ:
    case MINIC_CORE_INSTRUCTION_INTEGER_CTZ: {
        unsigned int width;
        if (!minic_core_integer_bitscan_shape(
                function, instruction, &width) ||
            !minic_riscv64_frame_load_value(file, frame, instruction->value.operand, "t0") ||
            !minic_riscv_emit_word_bitscan(
                file, instruction->kind, symbol_name, "core",
                instruction->result, width, 64U))
            return false;
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
