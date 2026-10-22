#include "target/common/direct_instruction_semantics.h"
#include "core/core_ir.h"
#include "target/common/lp64_scalar.h"
#include "target/data_layout.h"

#include <stdint.h>

bool minic_multi_direct_record_copy_extent(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    size_t *bytes) {
    MinicCoreValueId dst, src;
    MinicType dst_type, src_type, dst_unqualified, src_unqualified;
    size_t alignment;
    if (program == NULL || target == NULL || fn == NULL || ins == NULL ||
        bytes == NULL)
        return false;
    dst = ins->value.record_copy.destination_address;
    src = ins->value.record_copy.source_address;
    if (ins->kind != MINIC_CORE_INSTRUCTION_RECORD_COPY ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        !minic_type_is_record(ins->type) ||
        dst >= fn->value_count || src >= fn->value_count ||
        !minic_type_pointee(fn->values[dst].type, &dst_type) ||
        !minic_type_pointee(fn->values[src].type, &src_type) ||
        !minic_type_unqualified(dst_type, &dst_unqualified) ||
        !minic_type_unqualified(src_type, &src_unqualified) ||
        !minic_type_equal(dst_unqualified, ins->type) ||
        !minic_type_equal(src_unqualified, ins->type) ||
        !minic_data_layout_type(target->data_layout, program, ins->type,
                                bytes, &alignment) ||
        alignment == 0U || *bytes > 2048U)
        return false;
    return true;
}

bool minic_multi_direct_parameter_object_location(
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    const MinicLP64ArgumentPlan *incoming_plan,
    const MinicLP64FramePlan *frame_plan,
    const MinicLP64ArgumentLocation **location, size_t *object_offset) {
    size_t position;
    MinicCoreObjectId obj_id;
    const MinicLP64ArgumentLocation *loc;
    if (fn == NULL || ins == NULL || incoming_plan == NULL ||
        frame_plan == NULL || location == NULL || object_offset == NULL)
        return false;
    position = ins->value.parameter_object.parameter_index;
    obj_id = ins->value.parameter_object.object_id;
    if (ins->kind != MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT ||
        position >= incoming_plan->argument_count ||
        position >= fn->parameter_count)
        return false;
    loc = &incoming_plan->arguments[position];
    if (ins->result != MINIC_CORE_VALUE_INVALID ||
        obj_id >= fn->object_count ||
        !minic_type_equal(fn->objects[obj_id].type, fn->parameter_types[position]) ||
        fn->objects[obj_id].element_count != 1U ||
        (loc->kind != MINIC_LP64_ARGUMENT_RECORD &&
         loc->kind != MINIC_LP64_ARGUMENT_INDIRECT_RECORD) ||
        !minic_lp64_frame_plan_object(frame_plan, obj_id, object_offset))
        return false;
    *location = loc;
    return true;
}


/* A compiler barrier is an ordering constraint in the Core stream, not a
 * machine fence and not a produced SSA value. */
bool minic_multi_direct_compiler_barrier_shape(const MinicCoreInstruction *ins) {
    return ins != NULL &&
           ins->kind == MINIC_CORE_INSTRUCTION_COMPILER_BARRIER &&
           ins->result == MINIC_CORE_VALUE_INVALID &&
           minic_type_is_void(ins->type);
}

/* Both direct LP64 emitters materialize int128 and long double in a 16-byte
 * Core frame slot, but select fundamentally different ISA load/store forms.
 * Validate the common source address and slot identity only once. */
bool minic_multi_direct_wide_store_shape(
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    const MinicLP64FramePlan *frame_plan, size_t *value_offset) {
    MinicCoreValueId address, value;
    if (fn == NULL || ins == NULL || frame_plan == NULL ||
        value_offset == NULL || ins->kind != MINIC_CORE_INSTRUCTION_STORE)
        return false;
    address = ins->value.store.address;
    value = ins->value.store.stored_value;
    return address < fn->value_count && value < fn->value_count &&
           minic_type_is_pointer(fn->values[address].type) &&
           (minic_type_is_int128_integer(fn->values[value].type) ||
            minic_type_is_long_double(fn->values[value].type)) &&
           minic_lp64_frame_plan_wide_slot(frame_plan, value, value_offset);
}

/* One machine-independent result-shape gate for direct LP64 codegen.
 * AArch64 admits binary64 VA_ARG; LoongArch64 does not yet, so callers
 * select that documented ISA capability explicitly. */
bool minic_multi_direct_result_slot_offset(
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    bool allow_variadic_double_result, size_t *offset) {
    if (fn == NULL || ins == NULL || offset == NULL)
        return false;
    if ((ins->kind == MINIC_CORE_INSTRUCTION_CALL ||
         ins->kind == MINIC_CORE_INSTRUCTION_INDIRECT_CALL) &&
        (minic_type_is_void(ins->type) ||
         (ins->kind == MINIC_CORE_INSTRUCTION_CALL &&
          minic_type_is_record(ins->type) &&
          ins->value.call.result_object != MINIC_CORE_OBJECT_INVALID))) {
        /* Effect-only call or record-result object: no scalar Core slot. */
        *offset = 0U;
        return true;
    }
    if (ins->result == MINIC_CORE_VALUE_INVALID ||
        ins->result >= fn->value_count ||
        ((ins->kind == MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS ||
          ins->kind == MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS)
            ? !minic_type_is_pointer(ins->type)
            : !minic_type_is_integer(ins->type) &&
              !minic_type_is_pointer(ins->type) &&
              !(minic_type_is_float(ins->type) &&
                (ins->kind == MINIC_CORE_INSTRUCTION_LOAD ||
                 ins->kind == MINIC_CORE_INSTRUCTION_PARAMETER ||
                 ins->kind == MINIC_CORE_INSTRUCTION_CALL ||
                 ins->kind == MINIC_CORE_INSTRUCTION_INDIRECT_CALL ||
                 ins->kind == MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT ||
                 ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_TO_FLOAT ||
                 ins->kind == MINIC_CORE_INSTRUCTION_FLOAT_ADD ||
                 ins->kind == MINIC_CORE_INSTRUCTION_FLOAT_SUBTRACT ||
                 ins->kind == MINIC_CORE_INSTRUCTION_FLOAT_MULTIPLY ||
                 ins->kind == MINIC_CORE_INSTRUCTION_FLOAT_DIVIDE)) &&
              !(minic_type_is_long_double(ins->type) &&
                (ins->kind == MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT ||
                 ins->kind == MINIC_CORE_INSTRUCTION_LOAD ||
                 ins->kind == MINIC_CORE_INSTRUCTION_PARAMETER ||
                 ins->kind == MINIC_CORE_INSTRUCTION_CALL ||
                 ins->kind == MINIC_CORE_INSTRUCTION_INDIRECT_CALL)) &&
              !(minic_type_is_double(ins->type) &&
                (ins->kind == MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT ||
                 ins->kind == MINIC_CORE_INSTRUCTION_LOAD ||
                 ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_ADD ||
                 ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_NEGATE ||
                 ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_DIVIDE ||
                 ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_SUBTRACT ||
                 ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_MULTIPLY ||
                 ins->kind == MINIC_CORE_INSTRUCTION_PARAMETER ||
                 ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_TO_DOUBLE ||
                 ins->kind == MINIC_CORE_INSTRUCTION_FLOAT_TO_DOUBLE ||
                 (allow_variadic_double_result &&
                  ins->kind == MINIC_CORE_INSTRUCTION_VARIADIC_NEXT_SCALAR) ||
                 ins->kind == MINIC_CORE_INSTRUCTION_CALL ||
                 ins->kind == MINIC_CORE_INSTRUCTION_INDIRECT_CALL))))
        return false;
    *offset = (size_t)ins->result * 8U;
    return true;
}



/* Instruction kinds and Core operand/result types are not properties of
 * an ISA encoder. Keep the shared IEEE-754 validation here, but leave the
 * actual AArch64/LoongArch64 floating opcodes and register transfers local. */
/* This is the complete numeric machine family implemented by both
 * direct LP64 emitters. SSA/result types remain validated by the separate
 * minic_multi_direct_floating_shape gate, never by opcode alone. */
/* Complete Core integer, pointer-comparison and bit-operation machine
 * family shared by AAPCS64 and LP64D; instruction encodings remain ISA-owned. */
/* One checked overflow operator/value/pointee contract for GNU overflow
 * builtins. These are shared Core semantics rather than native flags:
 * AArch64 uses NZCV, LoongArch64 constructs carry/overflow with integer ops. */
bool minic_multi_direct_integer_overflow_shape(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    size_t *width, bool *is_signed) {
    MinicCoreValueId left, right, address;
    MinicType value_type;
    int op;
    if (program == NULL || target == NULL || fn == NULL || ins == NULL ||
        width == NULL || is_signed == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW)
        return false;
    left = ins->value.integer_overflow.left;
    right = ins->value.integer_overflow.right;
    address = ins->value.integer_overflow.result_address;
    op = ins->value.integer_overflow.operator_kind;
    return left < fn->value_count && right < fn->value_count &&
           address < fn->value_count &&
           (op == MINIC_CORE_INTEGER_OVERFLOW_ADD ||
            op == MINIC_CORE_INTEGER_OVERFLOW_SUBTRACT ||
            op == MINIC_CORE_INTEGER_OVERFLOW_MULTIPLY) &&
           minic_type_pointee(fn->values[address].type, &value_type) &&
           minic_type_is_integer(value_type) &&
           !minic_type_is_bool_integer(value_type) &&
           !minic_type_is_const(value_type) &&
           !minic_type_is_volatile(value_type) &&
           minic_type_equal(value_type, fn->values[left].type) &&
           minic_type_equal(value_type, fn->values[right].type) &&
           minic_lp64_scalar_integer_width(
               program, target, value_type, width, is_signed) &&
           (*width == 4U || *width == 8U);
}

/* CLZ/CTZ are defined here for unsigned 8/16/32/64 Core operands.
 * The ISA owns masking or bitfield extraction and exact machine opcodes. */
bool minic_multi_direct_integer_bitscan_shape(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    unsigned int *width) {
    MinicCoreValueId operand;
    if (program == NULL || target == NULL || fn == NULL ||
        ins == NULL || width == NULL ||
        (ins->kind != MINIC_CORE_INSTRUCTION_INTEGER_CLZ &&
         ins->kind != MINIC_CORE_INSTRUCTION_INTEGER_CTZ))
        return false;
    operand = ins->value.operand;
    return operand < fn->value_count &&
           minic_type_is_integer(ins->type) &&
           minic_type_is_unsigned_integer(fn->values[operand].type) &&
           minic_target_info_integer_width(
               target, program, fn->values[operand].type, width) &&
           (*width == 8U || *width == 16U ||
            *width == 32U || *width == 64U);
}

/* The two ISA implementations consume the same Core call/parameter ABI
 * transfer family. Planning remains target-ABI-specific at the call site. */
bool minic_multi_direct_abi_transfer_kind(MinicCoreInstructionKind kind) {
    return kind == MINIC_CORE_INSTRUCTION_CALL ||
           kind == MINIC_CORE_INSTRUCTION_INDIRECT_CALL ||
           kind == MINIC_CORE_INSTRUCTION_PARAMETER;
}

/* Bounds and scalar result typing are a Core contract, not a per-ISA
 * register assignment rule. The ABI-specific plan already owns whether
 * a named parameter occupies FP/GP registers or caller stack. */
bool minic_multi_direct_scalar_parameter_location(
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    const MinicLP64ArgumentPlan *incoming,
    const MinicLP64ArgumentLocation **location) {
    size_t ordinal;
    if (fn == NULL || ins == NULL || incoming == NULL ||
        location == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_PARAMETER ||
        fn->parameter_types == NULL)
        return false;
    ordinal = (size_t)ins->value.parameter_index;
    if (ordinal >= fn->parameter_count ||
        ordinal >= incoming->argument_count ||
        !minic_type_equal(ins->type, fn->parameter_types[ordinal]) ||
        incoming->arguments[ordinal].kind == MINIC_LP64_ARGUMENT_RECORD ||
        incoming->arguments[ordinal].kind == MINIC_LP64_ARGUMENT_INDIRECT_RECORD)
        return false;
    *location = &incoming->arguments[ordinal];
    return true;
}

bool minic_multi_direct_integer_machine_kind(MinicCoreInstructionKind kind) {
    switch (kind) {
    case MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW:
    case MINIC_CORE_INSTRUCTION_INTEGER_CLZ:
    case MINIC_CORE_INSTRUCTION_INTEGER_CTZ:
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT:
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT:
    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:
    case MINIC_CORE_INSTRUCTION_INTEGER_ADD:
    case MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR:
    case MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE:
    case MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER:
    case MINIC_CORE_INSTRUCTION_POINTER_LESS:
    case MINIC_CORE_INSTRUCTION_INTEGER_LESS:
    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:
    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO:
        return true;
    default:
        return false;
    }
}

bool minic_multi_direct_fp_instruction_kind(MinicCoreInstructionKind kind) {
    switch (kind) {
    case MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT:
    case MINIC_CORE_INSTRUCTION_DOUBLE_EQUAL:
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS:
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS_EQUAL:
    case MINIC_CORE_INSTRUCTION_DOUBLE_ADD:
    case MINIC_CORE_INSTRUCTION_DOUBLE_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_DOUBLE_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_DOUBLE_DIVIDE:
    case MINIC_CORE_INSTRUCTION_DOUBLE_NEGATE:
    case MINIC_CORE_INSTRUCTION_FLOAT_ADD:
    case MINIC_CORE_INSTRUCTION_FLOAT_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_FLOAT_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_FLOAT_DIVIDE:
    case MINIC_CORE_INSTRUCTION_FLOAT_TO_DOUBLE:
    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_FLOAT:
    case MINIC_CORE_INSTRUCTION_INTEGER_TO_DOUBLE:
    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_INTEGER:
        return true;
    default:
        return false;
    }
}

bool minic_multi_direct_floating_shape(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    size_t *integer_width, bool *integer_signed) {
    MinicCoreValueId left, right, operand;
    if (fn == NULL || ins == NULL)
        return false;
    switch (ins->kind) {
    case MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT:
        return minic_type_is_float(ins->type) ||
               minic_type_is_double(ins->type) ||
               minic_type_is_long_double(ins->type);
    case MINIC_CORE_INSTRUCTION_DOUBLE_EQUAL:
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS:
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS_EQUAL:
    case MINIC_CORE_INSTRUCTION_DOUBLE_ADD:
    case MINIC_CORE_INSTRUCTION_DOUBLE_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_DOUBLE_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_DOUBLE_DIVIDE:
        left = ins->value.binary.left;
        right = ins->value.binary.right;
        return (ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_EQUAL ||
                ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_LESS ||
                ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_LESS_EQUAL
                    ? minic_type_equal(ins->type, minic_type_int())
                    : minic_type_is_double(ins->type)) &&
               left < fn->value_count && right < fn->value_count &&
               minic_type_is_double(fn->values[left].type) &&
               minic_type_is_double(fn->values[right].type);
    case MINIC_CORE_INSTRUCTION_FLOAT_ADD:
    case MINIC_CORE_INSTRUCTION_FLOAT_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_FLOAT_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_FLOAT_DIVIDE:
        left = ins->value.binary.left;
        right = ins->value.binary.right;
        return minic_type_is_float(ins->type) &&
               left < fn->value_count && right < fn->value_count &&
               minic_type_is_float(fn->values[left].type) &&
               minic_type_is_float(fn->values[right].type);
    case MINIC_CORE_INSTRUCTION_DOUBLE_NEGATE:
        operand = ins->value.operand;
        return minic_type_is_double(ins->type) &&
               operand < fn->value_count &&
               minic_type_is_double(fn->values[operand].type);
    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_FLOAT:
        operand = ins->value.operand;
        return minic_type_is_float(ins->type) &&
               operand < fn->value_count &&
               minic_type_is_double(fn->values[operand].type);
    case MINIC_CORE_INSTRUCTION_FLOAT_TO_DOUBLE:
        operand = ins->value.operand;
        return minic_type_is_double(ins->type) &&
               operand < fn->value_count &&
               minic_type_is_float(fn->values[operand].type);
    case MINIC_CORE_INSTRUCTION_INTEGER_TO_DOUBLE:
        operand = ins->value.operand;
        return program != NULL && target != NULL &&
               integer_width != NULL && integer_signed != NULL &&
               minic_type_is_double(ins->type) &&
               operand < fn->value_count &&
               minic_lp64_scalar_integer_width(
                   program, target, fn->values[operand].type,
                   integer_width, integer_signed);
    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_INTEGER:
        operand = ins->value.operand;
        if (program == NULL || target == NULL ||
            integer_width == NULL || integer_signed == NULL ||
            operand >= fn->value_count ||
            !minic_type_is_double(fn->values[operand].type) ||
            !minic_lp64_scalar_integer_width(
                program, target, ins->type, integer_width, integer_signed))
            return false;
        return *integer_width == 1U || *integer_width == 2U ||
               *integer_width == 4U || *integer_width == 8U;
    default:
        return false;
    }
}

/* Scalar bitcast shape for direct LP64 lowering. Pointer->integer must
 * preserve all 64 address bits; integer->pointer keeps narrow source width
 * for the ISA's existing sign/zero-extension sequence. */
bool minic_multi_direct_scalar_bitcast_shape(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    bool *pointer_to_integer, size_t *source_width,
    bool *source_signed, bool *source_pointer) {
    MinicCoreValueId source;
    MinicType input;
    size_t output_width;
    bool output_signed;
    if (program == NULL || target == NULL || fn == NULL || ins == NULL ||
        pointer_to_integer == NULL || source_width == NULL ||
        source_signed == NULL || source_pointer == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_SCALAR_BITCAST)
        return false;
    source = ins->value.operand;
    if (source >= fn->value_count)
        return false;
    input = fn->values[source].type;
    if (!minic_core_scalar_bitcast_types_valid(ins->type, input))
        return false;
    *source_pointer = minic_type_is_pointer(input);
    *pointer_to_integer = *source_pointer && minic_type_is_integer(ins->type);
    *source_width = 0U;
    *source_signed = false;
    if (*pointer_to_integer)
        return minic_lp64_scalar_integer_width(program, target, ins->type,
                                                &output_width, &output_signed) &&
               output_width == 8U;
    if (!minic_type_is_pointer(ins->type))
        return false;
    return *source_pointer ||
           minic_lp64_scalar_integer_width(program, target, input,
                                          source_width, source_signed);
}

/* POINTER_LESS compares two identically typed pointer Core values and
 * produces C int. The unsigned machine comparison is ISA-owned. */
bool minic_multi_direct_pointer_less_shape(
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins) {
    MinicCoreValueId lhs, rhs;
    if (fn == NULL || ins == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_POINTER_LESS ||
        !minic_type_equal(ins->type, minic_type_int()))
        return false;
    lhs = ins->value.binary.left;
    rhs = ins->value.binary.right;
    return lhs < fn->value_count && rhs < fn->value_count &&
           minic_type_is_pointer(fn->values[lhs].type) &&
           minic_type_equal(fn->values[lhs].type, fn->values[rhs].type);
}

/* Core unary/shift/compare type admission is shared across direct LP64
 * targets. Native extensions and opcode selection remain ISA-owned. */
bool minic_multi_direct_integer_unary_shape(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    size_t *width, bool *is_signed) {
    MinicCoreValueId operand;
    if (program == NULL || target == NULL || fn == NULL || ins == NULL ||
        width == NULL || is_signed == NULL ||
        (ins->kind != MINIC_CORE_INSTRUCTION_INTEGER_NEGATE &&
         ins->kind != MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT))
        return false;
    operand = ins->value.operand;
    return operand < fn->value_count &&
           minic_type_equal(fn->values[operand].type, ins->type) &&
           minic_lp64_scalar_integer_width(program, target, ins->type,
                                          width, is_signed) &&
           !minic_type_is_bool_integer(ins->type);
}

/* File-private Core operand decoders. The public family probes below
 * retain their independent type, boolean, width and ABI admission rules. */
static bool core_direct_integer_pair(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    size_t *lw, size_t *rw, bool *ls, bool *rs) {
    MinicCoreValueId lhs, rhs;
    if (program == NULL || target == NULL || fn == NULL || ins == NULL ||
        lw == NULL || rw == NULL || ls == NULL || rs == NULL)
        return false;
    lhs = ins->value.binary.left;
    rhs = ins->value.binary.right;
    return lhs < fn->value_count && rhs < fn->value_count &&
           minic_lp64_scalar_integer_width(program, target, fn->values[lhs].type,
                                          lw, ls) &&
           minic_lp64_scalar_integer_width(program, target, fn->values[rhs].type,
                                          rw, rs);
}

/* This remains a relaxed-only atomic preflight, not a C11 memory model.
 * LOAD and STORE apply separate payload/bit-width tests afterward. */
static bool core_direct_relaxed_atomic_pointee_width(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, MinicCoreValueId address,
    unsigned int order, size_t *width) {
    MinicType pointee;
    size_t align;
    if (program == NULL || target == NULL || fn == NULL || width == NULL ||
        order != 0U || address >= fn->value_count ||
        !minic_type_pointee(fn->values[address].type, &pointee) ||
        !(minic_type_is_integer(pointee) || minic_type_is_pointer(pointee)) ||
        !minic_data_layout_type(target->data_layout, program, pointee,
                                width, &align))
        return false;
    (void)align;
    return true;
}

bool minic_multi_direct_integer_shift_shape(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    size_t *left_width, size_t *right_width,
    bool *left_signed, bool *right_signed) {
    MinicCoreValueId lhs;
    if (program == NULL || target == NULL || fn == NULL || ins == NULL ||
        left_width == NULL || right_width == NULL ||
        left_signed == NULL || right_signed == NULL ||
        (ins->kind != MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT &&
         ins->kind != MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT))
        return false;
    lhs = ins->value.binary.left;
    return lhs < fn->value_count &&
           minic_type_equal(fn->values[lhs].type, ins->type) &&
           core_direct_integer_pair(program, target, fn, ins,
                                    left_width, right_width,
                                    left_signed, right_signed) &&
           (*left_width == 1U || *left_width == 2U ||
            *left_width == 4U || *left_width == 8U) &&
           !minic_type_is_bool_integer(ins->type);
}

bool minic_multi_direct_integer_compare_shape(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    size_t *left_width, size_t *right_width,
    bool *left_signed, bool *right_signed) {
    if (program == NULL || target == NULL || fn == NULL || ins == NULL ||
        left_width == NULL || right_width == NULL ||
        left_signed == NULL || right_signed == NULL ||
        (ins->kind != MINIC_CORE_INSTRUCTION_INTEGER_LESS &&
         ins->kind != MINIC_CORE_INSTRUCTION_SCALAR_EQUAL))
        return false;
    return core_direct_integer_pair(program, target, fn, ins,
                                    left_width, right_width,
                                    left_signed, right_signed) &&
           *left_width == *right_width && *left_signed == *right_signed &&
           (*left_width == 4U || *left_width == 8U);
}

bool minic_multi_direct_scalar_zero_shape(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    size_t *width, bool *is_signed, bool *is_pointer) {
    MinicCoreValueId operand;
    MinicType type;
    if (program == NULL || target == NULL || fn == NULL || ins == NULL ||
        width == NULL || is_signed == NULL || is_pointer == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO)
        return false;
    operand = ins->value.operand;
    if (operand >= fn->value_count) return false;
    type = fn->values[operand].type;
    *is_pointer = minic_type_is_pointer(type);
    *width = 0U;
    *is_signed = false;
    return *is_pointer || minic_type_equal(type, minic_type_bool()) ||
           minic_lp64_scalar_integer_width(program, target, type,
                                          width, is_signed);
}

/* AArch64's current direct emitter admits a 16-bit element-stride literal;
 * LoongArch64 materializes full-width strides. Keep the different limits. */
bool minic_multi_direct_pointer_offset_shape(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    size_t maximum_stride, size_t *index_width, bool *index_signed) {
    MinicCoreValueId base, index;
    if (program == NULL || target == NULL || fn == NULL || ins == NULL ||
        index_width == NULL || index_signed == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_POINTER_OFFSET)
        return false;
    base = ins->value.pointer_offset.base;
    index = ins->value.pointer_offset.index;
    return base < fn->value_count && index < fn->value_count &&
           minic_type_is_pointer(fn->values[base].type) &&
           minic_lp64_scalar_integer_width(program, target,
                                          fn->values[index].type,
                                          index_width, index_signed) &&
           ins->value.pointer_offset.element_size <= maximum_stride;
}

/* Arithmetic, bitwise and division/remainder retain distinct result-type
 * and 8/16/32/64-bit restrictions above the common pair decoder. */
bool minic_multi_direct_integer_binary_shape(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    size_t *left_width, size_t *right_width,
    bool *left_signed, bool *right_signed) {
    MinicCoreValueId lhs;
    bool arithmetic, bitwise, division;
    if (program == NULL || target == NULL || fn == NULL || ins == NULL ||
        left_width == NULL || right_width == NULL ||
        left_signed == NULL || right_signed == NULL)
        return false;
    arithmetic = ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_ADD ||
                 ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT ||
                 ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY;
    bitwise = ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND ||
              ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR ||
              ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR;
    division = ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE ||
               ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER;
    if (!arithmetic && !bitwise && !division)
        return false;
    lhs = ins->value.binary.left;
    if (!core_direct_integer_pair(program, target, fn, ins,
                                  left_width, right_width,
                                  left_signed, right_signed) ||
        *left_width != *right_width || *left_signed != *right_signed)
        return false;
    if (arithmetic)
        return minic_type_equal(fn->values[lhs].type, ins->type) &&
               !minic_type_is_bool_integer(ins->type);
    if (bitwise)
        return (*left_width == 1U || *left_width == 2U ||
                *left_width == 4U || *left_width == 8U) &&
               !minic_type_is_bool_integer(ins->type);
    return (*left_width == 4U || *left_width == 8U) &&
           minic_type_equal(fn->values[lhs].type, ins->type);
}

/* The direct LP64 backends admit relaxed-only atomic scalar loads.
 * This remains strictly a source/Core value-shape gate. */
bool minic_multi_direct_scalar_load_shape(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    size_t *width, bool *is_signed, bool *is_pointer) {
    bool atomic;
    MinicCoreValueId address;
    size_t pointee_width = 0U;
    if (program == NULL || target == NULL || fn == NULL || ins == NULL ||
        width == NULL || is_signed == NULL || is_pointer == NULL)
        return false;
    atomic = ins->kind == MINIC_CORE_INSTRUCTION_ATOMIC_LOAD;
    if (ins->kind != MINIC_CORE_INSTRUCTION_LOAD && !atomic)
        return false;
    address = atomic ? ins->value.atomic_load.address : ins->value.load.address;
    *width = minic_type_is_double(ins->type) ? 8U :
             minic_type_is_float(ins->type) ? 4U : 0U;
    *is_signed = false;
    *is_pointer = minic_type_is_pointer(ins->type);
    if (atomic &&
        (!core_direct_relaxed_atomic_pointee_width(
             program, target, fn, address,
             ins->value.atomic_load.memory_order, &pointee_width) ||
         (*is_pointer ? pointee_width != 8U :
            !minic_lp64_scalar_integer_width(program, target, ins->type,
                                             width, is_signed) ||
            (*width != 1U && *width != 2U && *width != 4U &&
             *width != 8U) ||
            pointee_width != *width)))
        return false;
    if (address >= fn->value_count ||
        !minic_type_is_pointer(fn->values[address].type) ||
        (!*is_pointer && !minic_type_is_double(ins->type) &&
         !minic_type_is_float(ins->type) &&
         !minic_lp64_scalar_integer_width(program, target, ins->type,
                                          width, is_signed)))
        return false;
    return true;
}

/* One frame/source admissibility check for the two 128-bit load paths.
 * Keep actual 128-bit loads and ISA stack address encodings private. */
bool minic_multi_direct_wide_scalar_load_location(
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    const MinicLP64FramePlan *frame_plan, MinicCoreValueId *address,
    size_t *destination_offset) {
    if (fn == NULL || ins == NULL || frame_plan == NULL ||
        address == NULL || destination_offset == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_LOAD ||
        (!minic_type_is_int128_integer(ins->type) &&
         !minic_type_is_long_double(ins->type)))
        return false;
    *address = ins->value.load.address;
    return *address < fn->value_count &&
           minic_type_is_pointer(fn->values[*address].type) &&
           minic_lp64_frame_plan_wide_slot(frame_plan, ins->result,
                                            destination_offset);
}

bool minic_multi_direct_scalar_store_shape(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    size_t *width, bool *is_signed, bool *is_pointer) {
    bool atomic;
    MinicCoreValueId addr, value;
    size_t pointee_width = 0U;
    if (program == NULL || target == NULL || fn == NULL || ins == NULL ||
        width == NULL || is_signed == NULL || is_pointer == NULL)
        return false;
    atomic = ins->kind == MINIC_CORE_INSTRUCTION_ATOMIC_STORE;
    addr = atomic ? ins->value.atomic_store.address
                  : ins->value.store.address;
    value = atomic ? ins->value.atomic_store.stored_value
                   : ins->value.store.stored_value;
    if ((ins->kind != MINIC_CORE_INSTRUCTION_STORE && !atomic) ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        addr >= fn->value_count || value >= fn->value_count ||
        !minic_type_is_pointer(fn->values[addr].type))
        return false;
    *width = 0U;
    *is_signed = false;
    *is_pointer = minic_type_is_pointer(fn->values[value].type);
    /* These are the existing *relaxed-only*, scalar, naturally-aligned
     * admission rules. This is not a general atomic memory model. */
    if (atomic &&
        !core_direct_relaxed_atomic_pointee_width(
             program, target, fn, addr,
             ins->value.atomic_store.memory_order, &pointee_width))
        return false;
    if (!*is_pointer && !minic_type_is_double(fn->values[value].type) &&
        !minic_type_is_float(fn->values[value].type) &&
        !minic_lp64_scalar_integer_width(program, target, fn->values[value].type,
                                         width, is_signed))
        return false;
    if (minic_type_is_double(fn->values[value].type))
        *width = 8U;
    if (minic_type_is_float(fn->values[value].type))
        *width = 4U;
    if (atomic &&
        (minic_type_is_double(fn->values[value].type) ||
         minic_type_is_float(fn->values[value].type) ||
         (*is_pointer ? pointee_width != 8U :
          (*width != 1U && *width != 2U &&
           *width != 4U && *width != 8U) ||
          *width != pointee_width)))
        return false;
    return true;
}
