#include "target/common/direct_instruction_semantics.h"
#include "core/core_ir.h"
#include "target/common/core_instruction_family.h"
#include "target/common/core_scalar_conversion.h"
#include "target/common/core_floating_shape.h"
#include "target/common/lp64_scalar.h"
#include <stdint.h>

/* Every Direct LP64 machine consumer sees sealed Core execution types.
 * Missing Core provenance is a rejection, not permission to reopen C types. */
static const MinicCoreType *direct_core_type(const MinicCoreFunction *fn,
                                             MinicCoreTypeId id) {
    if (fn == NULL || fn->type_context == NULL) return NULL;
    return minic_core_type_get(fn->type_context,
                               minic_core_type_unqualified(fn->type_context, id));
}

static bool direct_core_kind(const MinicCoreFunction *fn,
                             MinicCoreTypeId id, MinicCoreTypeKind kind) {
    return fn != NULL && fn->type_context != NULL &&
           minic_lp64_core_kind(fn->type_context, id, kind);
}

static bool direct_core_float(const MinicCoreFunction *fn,
                              MinicCoreTypeId id, MinicCoreFloatFormat format) {
    const MinicCoreType *type = direct_core_type(fn, id);
    return type != NULL && type->kind == MINIC_CORE_TYPE_FLOAT &&
           type->value.floating.format == format;
}

static bool direct_core_bool(const MinicCoreFunction *fn, MinicCoreTypeId id) {
    const MinicCoreType *type = direct_core_type(fn, id);
    return type != NULL && type->kind == MINIC_CORE_TYPE_INTEGER &&
           type->value.integer.is_bool;
}

static bool direct_core_int_width(const MinicCoreFunction *fn,
                                  MinicCoreTypeId id, size_t *width,
                                  bool *sign) {
    return fn != NULL && fn->type_context != NULL &&
           minic_lp64_core_integer_width(fn->type_context, id, width, sign);
}

static bool direct_core_pointer_pointee(const MinicCoreFunction *fn,
                                        MinicCoreTypeId pointer,
                                        MinicCoreTypeId *pointee) {
    return fn != NULL && fn->type_context != NULL &&
           minic_lp64_core_pointer_pointee(fn->type_context, pointer, pointee);
}

static bool direct_core_equal_unqualified(const MinicCoreFunction *fn,
                                          MinicCoreTypeId left,
                                          MinicCoreTypeId right) {
    return fn != NULL && fn->type_context != NULL &&
           minic_lp64_core_equal_unqualified(fn->type_context, left, right);
}

static bool direct_core_wide(const MinicCoreFunction *fn, MinicCoreTypeId id) {
    const MinicCoreType *type = direct_core_type(fn, id);
    return type != NULL && type->has_layout && type->size == 16U &&
           ((type->kind == MINIC_CORE_TYPE_INTEGER &&
             type->value.integer.bit_width == 128U) ||
            (type->kind == MINIC_CORE_TYPE_FLOAT &&
             type->value.floating.format == MINIC_CORE_FLOAT_BINARY128));
}

static bool direct_core_register64(const MinicCoreFunction *fn,
                                   MinicCoreTypeId id) {
    size_t width;
    bool sign;
    return direct_core_kind(fn, id, MINIC_CORE_TYPE_POINTER) ||
           (direct_core_int_width(fn, id, &width, &sign) && width == 8U);
}

static bool direct_core_i32(const MinicCoreFunction *fn,
                            MinicCoreTypeId id) {
    const MinicCoreType *type = direct_core_type(fn, id);
    return type != NULL && type->kind == MINIC_CORE_TYPE_INTEGER &&
           type->has_layout && type->size == 4U &&
           type->value.integer.bit_width == 32U &&
           type->value.integer.is_signed && !type->value.integer.is_bool;
}

static bool direct_core_int128(const MinicCoreFunction *fn,
                               MinicCoreTypeId id) {
    const MinicCoreType *type = direct_core_type(fn, id);
    return type != NULL && type->kind == MINIC_CORE_TYPE_INTEGER &&
           type->value.integer.bit_width == 128U && type->has_layout;
}

/* Equal exact execution IDs, intentionally distinct from unqualified
 * pointee/record equivalence used by aggregate transport. */
static bool direct_core_equal_exact(const MinicCoreFunction *fn,
                                    MinicCoreTypeId lhs_id,
                                    MinicCoreTypeId rhs_id) {
    return fn != NULL && fn->type_context != NULL &&
           lhs_id != MINIC_CORE_TYPE_INVALID &&
           minic_core_type_get(fn->type_context, lhs_id) != NULL &&
           lhs_id == rhs_id;
}

bool minic_multi_direct_call_frame_level0_shape(
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    MinicCoreCallFrameAddressKind kind) {
    MinicCoreTypeId pointee;
    return fn != NULL && ins != NULL && fn->type_context != NULL &&
           (kind == MINIC_CORE_CALL_FRAME_ADDRESS_RETURN ||
            kind == MINIC_CORE_CALL_FRAME_ADDRESS_FRAME) &&
           ins->kind == MINIC_CORE_INSTRUCTION_CALL_FRAME_ADDRESS &&
           ins->result < fn->value_count && fn->values != NULL &&
           ins->value.call_frame_address.level == 0U &&
           ins->value.call_frame_address.kind == kind &&
           direct_core_pointer_pointee(fn, ins->core_type_id, &pointee) &&
           direct_core_kind(fn, pointee, MINIC_CORE_TYPE_VOID) &&
           fn->values[ins->result].core_type_id == ins->core_type_id;
}

bool minic_multi_direct_call_frame_return_level0_shape(
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins) {
    return minic_multi_direct_call_frame_level0_shape(
        fn, ins, MINIC_CORE_CALL_FRAME_ADDRESS_RETURN);
}

bool minic_multi_direct_fixed_register_write_shape(
    const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    const MinicCoreFixedRegisterBinding **binding_out) {
    const MinicCoreFixedRegisterBinding *binding;
    const size_t index = ins != NULL
        ? ins->value.fixed_register_write.binding_id : SIZE_MAX;
    const MinicCoreValueId source = ins != NULL
        ? ins->value.fixed_register_write.source : MINIC_CORE_VALUE_INVALID;
    if (target == NULL || fn == NULL ||
        fn->type_context == NULL || ins == NULL ||
        binding_out == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_FIXED_REGISTER_WRITE ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        !direct_core_kind(fn, ins->core_type_id, MINIC_CORE_TYPE_VOID) ||
        index >= fn->fixed_register_binding_count ||
        fn->fixed_register_bindings == NULL || source >= fn->value_count)
        return false;
    binding = &fn->fixed_register_bindings[index];
    if (binding->is_local || binding->register_name == NULL ||
        binding->register_name_length == 0U ||
        !minic_target_info_fixed_register_write_supported(
            target, binding->register_name, binding->register_name_length) ||
        binding->core_type_id != fn->values[source].core_type_id ||
        !direct_core_register64(fn, binding->core_type_id))
        return false;
    *binding_out = binding;
    return true;
}

bool minic_multi_direct_fixed_register_read_shape(
    const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    const MinicCoreFixedRegisterBinding **binding_out) {
    const MinicCoreFixedRegisterBinding *binding;
    const size_t index = ins != NULL ? ins->value.fixed_register_binding_id : 0U;
    if (!target || !fn || fn->type_context == NULL ||
        !ins || !binding_out ||
        ins->kind != MINIC_CORE_INSTRUCTION_FIXED_REGISTER_READ ||
        ins->result >= fn->value_count ||
        index >= fn->fixed_register_binding_count ||
        fn->fixed_register_bindings == NULL)
        return false;
    binding = &fn->fixed_register_bindings[index];
    if (binding->is_local || !binding->register_name ||
        binding->register_name_length == 0U ||
        !minic_target_info_fixed_register_supported(
            target, binding->register_name, binding->register_name_length) ||
        binding->core_type_id != ins->core_type_id ||
        fn->values[ins->result].core_type_id != ins->core_type_id ||
        !direct_core_register64(fn, ins->core_type_id))
        return false;
    *binding_out = binding;
    return true;
}

bool minic_multi_direct_record_copy_extent(
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    size_t *bytes) {
    MinicCoreValueId dst, src;
    MinicCoreTypeId dst_pointee, src_pointee;
    const MinicCoreType *record;
    if (fn == NULL ||
        fn->type_context == NULL || ins == NULL ||
        bytes == NULL)
        return false;
    dst = ins->value.record_copy.destination_address;
    src = ins->value.record_copy.source_address;
    record = direct_core_type(fn, ins->core_type_id);
    if (ins->kind != MINIC_CORE_INSTRUCTION_RECORD_COPY ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        record == NULL || record->kind != MINIC_CORE_TYPE_RECORD ||
        !record->has_layout || record->alignment == 0U ||
        dst >= fn->value_count || src >= fn->value_count ||
        fn->values == NULL ||
        !direct_core_pointer_pointee(fn, fn->values[dst].core_type_id,
                                     &dst_pointee) ||
        !direct_core_pointer_pointee(fn, fn->values[src].core_type_id,
                                     &src_pointee) ||
        !direct_core_equal_unqualified(fn, dst_pointee, ins->core_type_id) ||
        !direct_core_equal_unqualified(fn, src_pointee, ins->core_type_id) ||
        /* Kernel task_struct and other compound records are often many
         * KiB. Native emitters use counted byte loops for large transfers;
         * this cap bounds the requested runtime copy without imposing the
         * former 2-KiB single-instruction-source expansion limit. */
        record->size > (16U * 1024U * 1024U))
        return false;
    *bytes = record->size;
    return true;
}

/* GNU C record value snapshots are private bytewise copies of an
 * address-backed lvalue. Core owns type/volatility and DataLayout bounds;
 * native emitters alone own the actual ISA byte operations. */
bool minic_multi_direct_record_load_extent(
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    const MinicLP64FramePlan *frame_plan,
    MinicCoreValueId *source, size_t *object_offset, size_t *bytes) {
    MinicCoreObjectId dst;
    MinicCoreTypeId pointee_id;
    const MinicCoreType *record;
    if (!fn || fn->type_context == NULL ||
        !ins || !frame_plan ||
        !source || !object_offset || !bytes ||
        ins->kind != MINIC_CORE_INSTRUCTION_RECORD_LOAD ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        !direct_core_kind(fn, ins->core_type_id, MINIC_CORE_TYPE_RECORD)) return false;
    dst = ins->value.record_load.destination_object;
    *source = ins->value.record_load.source_address;
    if (dst >= fn->object_count || *source >= fn->value_count ||
        fn->objects == NULL || fn->values == NULL ||
        !direct_core_equal_unqualified(fn, fn->objects[dst].core_type_id,
                                       ins->core_type_id) ||
        !direct_core_pointer_pointee(fn, fn->values[*source].core_type_id,
                                     &pointee_id) ||
        !direct_core_equal_unqualified(fn, pointee_id, ins->core_type_id) ||
        !minic_lp64_frame_plan_object(frame_plan, dst, object_offset) ||
        (record = direct_core_type(fn, ins->core_type_id)) == NULL ||
        record->kind != MINIC_CORE_TYPE_RECORD || !record->has_layout ||
        record->size == 0U || record->alignment == 0U ||
        record->size > (16U * 1024U * 1024U))
        return false;
    *bytes = record->size;
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
    if (fn == NULL || fn->type_context == NULL ||
        ins == NULL || incoming_plan == NULL ||
        frame_plan == NULL || location == NULL || object_offset == NULL)
        return false;
    position = ins->value.parameter_object.parameter_index;
    obj_id = ins->value.parameter_object.object_id;
    if (ins->kind != MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT ||
        position >= incoming_plan->argument_count ||
        position >= minic_core_function_parameter_count(fn))
        return false;
    loc = &incoming_plan->arguments[position];
    if (ins->result != MINIC_CORE_VALUE_INVALID ||
        obj_id >= fn->object_count ||
        fn->objects == NULL ||
        minic_core_function_parameter_types(fn) == NULL ||
        /* GNU top-level const on a by-value parameter has no ABI effect.
         * Only remove top-level qualification, preserving nested pointee
         * and member qualifiers and the exact record identity. */
        !direct_core_equal_unqualified(fn, fn->objects[obj_id].core_type_id,
                                       minic_core_function_parameter_types(fn)[position]) ||
        (loc->kind != MINIC_LP64_ARGUMENT_RECORD &&
         loc->kind != MINIC_LP64_ARGUMENT_INDIRECT_RECORD &&
         loc->kind != MINIC_LP64_ARGUMENT_EMPTY_RECORD) ||
        !minic_lp64_frame_plan_object(frame_plan, obj_id, object_offset))
        return false;
    *location = loc;
    return true;
}


/* A compiler barrier is an ordering constraint in the Core stream, not a
 * machine fence and not a produced SSA value. */
static bool minic_multi_direct_compiler_barrier_shape(
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins) {
    return ins != NULL &&
           ins->kind == MINIC_CORE_INSTRUCTION_COMPILER_BARRIER &&
           ins->result == MINIC_CORE_VALUE_INVALID &&
           direct_core_kind(fn, ins->core_type_id, MINIC_CORE_TYPE_VOID);
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
           direct_core_kind(fn, fn->values[address].core_type_id,
                            MINIC_CORE_TYPE_POINTER) &&
           (direct_core_int128(fn, fn->values[value].core_type_id) ||
            direct_core_float(fn, fn->values[value].core_type_id,
                              MINIC_CORE_FLOAT_BINARY128)) &&
           minic_lp64_frame_plan_wide_slot(frame_plan, value, value_offset);
}

/* One machine-independent result-shape gate for direct LP64 codegen.
 * AArch64 admits binary64 VA_ARG; LoongArch64 does not yet, so callers
 * select that documented ISA capability explicitly. */
static bool minic_multi_direct_result_slot_offset(
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    bool allow_variadic_double_result, size_t *offset) {
    if (fn == NULL || ins == NULL || offset == NULL)
        return false;
    if ((ins->kind == MINIC_CORE_INSTRUCTION_CALL ||
         ins->kind == MINIC_CORE_INSTRUCTION_INDIRECT_CALL) &&
        (direct_core_kind(fn, ins->core_type_id, MINIC_CORE_TYPE_VOID) ||
         (direct_core_kind(fn, ins->core_type_id, MINIC_CORE_TYPE_RECORD) &&
          (ins->kind == MINIC_CORE_INSTRUCTION_CALL
              ? ins->value.call.result_object
              : ins->value.indirect_call.result_object) != MINIC_CORE_OBJECT_INVALID))) {
        /* Effect-only call or record-result object: no scalar Core slot. */
        *offset = 0U;
        return true;
    }
    if (ins->result == MINIC_CORE_VALUE_INVALID ||
        ins->result >= fn->value_count ||
        ((ins->kind == MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS ||
          ins->kind == MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS)
            ? !direct_core_kind(fn, ins->core_type_id, MINIC_CORE_TYPE_POINTER)
            : !direct_core_kind(fn, ins->core_type_id, MINIC_CORE_TYPE_INTEGER) &&
              !direct_core_kind(fn, ins->core_type_id, MINIC_CORE_TYPE_POINTER) &&
              !(direct_core_float(fn, ins->core_type_id, MINIC_CORE_FLOAT_BINARY32) &&
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
              !(direct_core_float(fn, ins->core_type_id, MINIC_CORE_FLOAT_BINARY128) &&
                (ins->kind == MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT ||
                 ins->kind == MINIC_CORE_INSTRUCTION_LOAD ||
                 ins->kind == MINIC_CORE_INSTRUCTION_PARAMETER ||
                 ins->kind == MINIC_CORE_INSTRUCTION_CALL ||
                 ins->kind == MINIC_CORE_INSTRUCTION_INDIRECT_CALL)) &&
              !(direct_core_float(fn, ins->core_type_id, MINIC_CORE_FLOAT_BINARY64) &&
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
    /* Slot placement is no longer encoded in the semantic shape. The
     * common machine dispatch resolves this validated Core result through
     * the shared LP64 FramePlan's last-use mapping. */
    *offset = 0U;
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
/* The two ISA implementations consume the same Core call/parameter ABI
 * transfer family. Planning remains target-ABI-specific at the call site. */
/* Common Core memory-effect family: ISA byte moves remain independent.
 * The relaxed-only atomic memory-order contract is checked separately. */
static bool minic_multi_direct_abi_transfer_kind(MinicCoreInstructionKind kind) {
    return minic_core_instruction_is_call(kind) ||
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
    if (fn == NULL || fn->type_context == NULL || ins == NULL ||
        incoming == NULL || location == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_PARAMETER ||
        minic_core_function_parameter_types(fn) == NULL)
        return false;
    ordinal = (size_t)ins->value.parameter_index;
    if (ordinal >= minic_core_function_parameter_count(fn) ||
        ordinal >= incoming->argument_count ||
        ins->core_type_id != minic_core_function_parameter_types(fn)[ordinal] ||
        incoming->arguments[ordinal].kind == MINIC_LP64_ARGUMENT_RECORD ||
        incoming->arguments[ordinal].kind == MINIC_LP64_ARGUMENT_INDIRECT_RECORD)
        return false;
    *location = &incoming->arguments[ordinal];
    return true;
}

static bool minic_multi_direct_integer_machine_kind(MinicCoreInstructionKind kind) {
    /* Constants and conversions use distinct ABI/residual lowering in
     * Direct LP64, unlike RV32/RV64 which use their scalar emitter. */
    return kind == MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW ||
           (kind != MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT &&
            kind != MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION &&
            minic_core_instruction_is_integer_scalar(kind));
}

bool minic_multi_direct_floating_shape(
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    size_t *integer_width, bool *integer_signed) {
    MinicCoreFloatingShape shape;
    if (!minic_core_floating_shape(fn, ins, &shape))
        return false;
    if (ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_TO_DOUBLE ||
        ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_TO_INTEGER) {
        if (integer_width == NULL || integer_signed == NULL)
            return false;
        *integer_width = shape.integer_bytes;
        *integer_signed = shape.integer_signed;
        if (ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_TO_INTEGER)
            return *integer_width <= 8U;
    }
    return true;
}

/* Scalar bitcast shape for direct LP64 lowering. Pointer->integer preserves
 * all 64 address bits at pointer width, truncates to 32 bits for C's narrow
 * integer casts, or produces canonical 0/1 for C pointer->bool.
 * source_width reports the integer result width for pointer->integer and
 * the integer input width for integer->pointer; source_signed describes the
 * integer result on pointer->integer and the input otherwise. */
bool minic_multi_direct_scalar_bitcast_shape(
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    bool *pointer_to_integer, size_t *source_width,
    bool *source_signed, bool *source_pointer) {
    MinicCoreScalarBitcastShape cast;
    if (fn == NULL ||
        pointer_to_integer == NULL || source_width == NULL ||
        source_signed == NULL || source_pointer == NULL ||
        !minic_core_scalar_bitcast_shape(fn, ins, &cast))
        return false;
    *source_pointer = cast.source_pointer;
    *pointer_to_integer = cast.source_pointer && !cast.target_pointer;
    if (*pointer_to_integer) {
        if (cast.target_bytes != 8U && cast.target_bytes != 4U &&
            !(cast.target_bytes == 1U && cast.target_bool))
            return false;
        *source_width = cast.target_bytes;
        *source_signed = cast.target_signed;
        return true;
    }
    *source_width = cast.source_pointer ? 0U : cast.source_bytes;
    *source_signed = cast.source_signed;
    return true;
}

/* POINTER_LESS compares two identically typed pointer Core values and
 * produces C int. The unsigned machine comparison is ISA-owned. */
bool minic_multi_direct_pointer_less_shape(
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins) {
    MinicCoreValueId lhs, rhs;
    if (fn == NULL || fn->type_context == NULL || ins == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_POINTER_LESS ||
        !direct_core_i32(fn, ins->core_type_id))
        return false;
    lhs = ins->value.binary.left;
    rhs = ins->value.binary.right;
    return lhs < fn->value_count && rhs < fn->value_count &&
           direct_core_kind(fn, fn->values[lhs].core_type_id,
                            MINIC_CORE_TYPE_POINTER) &&
           direct_core_equal_exact(fn, fn->values[lhs].core_type_id,
                                   fn->values[rhs].core_type_id);
}

/* Core unary/shift/compare type admission is shared across direct LP64
 * targets. Native extensions and opcode selection remain ISA-owned. */
bool minic_multi_direct_integer_unary_shape(
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    size_t *width, bool *is_signed) {
    MinicCoreValueId operand;
    if (fn == NULL || fn->type_context == NULL || fn->values == NULL ||
        ins == NULL ||
        width == NULL || is_signed == NULL ||
        (ins->kind != MINIC_CORE_INSTRUCTION_INTEGER_NEGATE &&
         ins->kind != MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT))
        return false;
    operand = ins->value.operand;
    return operand < fn->value_count &&
           direct_core_equal_exact(fn, fn->values[operand].core_type_id,
                                   ins->core_type_id) &&
           direct_core_int_width(fn, ins->core_type_id, width, is_signed) &&
           !direct_core_bool(fn, ins->core_type_id);
}

/* File-private Core operand decoders. The public family probes below
 * retain their independent type, boolean, width and ABI admission rules. */
static bool core_direct_integer_pair(
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    size_t *lw, size_t *rw, bool *ls, bool *rs) {
    MinicCoreValueId lhs, rhs;
    if (fn == NULL || fn->type_context == NULL || fn->values == NULL ||
        ins == NULL ||
        lw == NULL || rw == NULL || ls == NULL || rs == NULL)
        return false;
    lhs = ins->value.binary.left;
    rhs = ins->value.binary.right;
    return lhs < fn->value_count && rhs < fn->value_count &&
           direct_core_int_width(fn, fn->values[lhs].core_type_id, lw, ls) &&
           direct_core_int_width(fn, fn->values[rhs].core_type_id, rw, rs);
}

/* This remains a relaxed-only atomic preflight, not a C11 memory model.
 * LOAD and STORE apply separate payload/bit-width tests afterward. */
static bool core_direct_relaxed_atomic_pointee_width(
    const MinicCoreFunction *fn, MinicCoreValueId address,
    unsigned int order, size_t *width) {
    MinicCoreTypeId pointee_id;
    const MinicCoreType *type;
    if (fn == NULL || fn->type_context == NULL || fn->values == NULL ||
        width == NULL ||
        order != 0U || address >= fn->value_count) return false;
    if (!direct_core_pointer_pointee(fn, fn->values[address].core_type_id,
                                     &pointee_id)) return false;
    type = direct_core_type(fn, pointee_id);
    if (type == NULL || !type->has_layout || type->alignment == 0U ||
        (type->kind != MINIC_CORE_TYPE_INTEGER &&
         type->kind != MINIC_CORE_TYPE_POINTER)) return false;
    *width = type->size;
    return true;
}

bool minic_multi_direct_integer_shift_shape(
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    size_t *left_width, size_t *right_width,
    bool *left_signed, bool *right_signed) {
    MinicCoreValueId lhs;
    if (fn == NULL || fn->type_context == NULL || fn->values == NULL ||
        ins == NULL ||
        left_width == NULL || right_width == NULL ||
        left_signed == NULL || right_signed == NULL ||
        (ins->kind != MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT &&
         ins->kind != MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT))
        return false;
    lhs = ins->value.binary.left;
    return lhs < fn->value_count &&
           direct_core_equal_exact(fn, fn->values[lhs].core_type_id,
                                   ins->core_type_id) &&
           core_direct_integer_pair(fn, ins,
                                    left_width, right_width,
                                    left_signed, right_signed) &&
           (*left_width == 1U || *left_width == 2U ||
            *left_width == 4U || *left_width == 8U) &&
           !direct_core_bool(fn, ins->core_type_id);
}

bool minic_multi_direct_integer_compare_shape(
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    size_t *left_width, size_t *right_width,
    bool *left_signed, bool *right_signed) {
    if (fn == NULL || fn->type_context == NULL || ins == NULL ||
        left_width == NULL || right_width == NULL ||
        left_signed == NULL || right_signed == NULL ||
        (ins->kind != MINIC_CORE_INSTRUCTION_INTEGER_LESS &&
         ins->kind != MINIC_CORE_INSTRUCTION_SCALAR_EQUAL))
        return false;
    return core_direct_integer_pair(fn, ins,
                                    left_width, right_width,
                                    left_signed, right_signed) &&
           *left_width == *right_width && *left_signed == *right_signed &&
           (*left_width == 4U || *left_width == 8U);
}

bool minic_multi_direct_scalar_zero_shape(
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    size_t *width, bool *is_signed, bool *is_pointer) {
    MinicCoreValueId operand;
    MinicCoreTypeId type_id;
    if (fn == NULL || fn->type_context == NULL || fn->values == NULL ||
        ins == NULL ||
        width == NULL || is_signed == NULL || is_pointer == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO)
        return false;
    operand = ins->value.operand;
    if (operand >= fn->value_count) return false;
    type_id = fn->values[operand].core_type_id;
    *is_pointer = direct_core_kind(fn, type_id, MINIC_CORE_TYPE_POINTER);
    *width = 0U;
    *is_signed = false;
    return *is_pointer || direct_core_bool(fn, type_id) ||
           direct_core_int_width(fn, type_id, width, is_signed);
}

/* AArch64's current direct emitter admits a 16-bit element-stride literal;
 * LoongArch64 materializes full-width strides. Keep the different limits. */
bool minic_multi_direct_pointer_offset_shape(
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    size_t maximum_stride, size_t *index_width, bool *index_signed) {
    MinicCoreValueId base, index;
    if (fn == NULL || fn->type_context == NULL || fn->values == NULL ||
        ins == NULL ||
        index_width == NULL || index_signed == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_POINTER_OFFSET)
        return false;
    base = ins->value.pointer_offset.base;
    index = ins->value.pointer_offset.index;
    return base < fn->value_count && index < fn->value_count &&
           direct_core_kind(fn, fn->values[base].core_type_id,
                            MINIC_CORE_TYPE_POINTER) &&
           direct_core_int_width(fn, fn->values[index].core_type_id,
                                 index_width, index_signed) &&
           ins->value.pointer_offset.element_size <= maximum_stride;
}

/* Arithmetic, bitwise and division/remainder retain distinct result-type
 * and 8/16/32/64-bit restrictions above the common pair decoder. */
bool minic_multi_direct_integer_binary_shape(
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    size_t *left_width, size_t *right_width,
    bool *left_signed, bool *right_signed) {
    MinicCoreValueId lhs;
    bool arithmetic, bitwise, division;
    if (fn == NULL || fn->type_context == NULL || ins == NULL ||
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
    if (!core_direct_integer_pair(fn, ins,
                                  left_width, right_width,
                                  left_signed, right_signed) ||
        *left_width != *right_width || *left_signed != *right_signed)
        return false;
    if (arithmetic)
        return direct_core_equal_exact(fn, fn->values[lhs].core_type_id,
                                       ins->core_type_id) &&
               !direct_core_bool(fn, ins->core_type_id);
    if (bitwise)
        return (*left_width == 1U || *left_width == 2U ||
                *left_width == 4U || *left_width == 8U) &&
               !direct_core_bool(fn, ins->core_type_id);
    return (*left_width == 4U || *left_width == 8U) &&
           direct_core_equal_exact(fn, fn->values[lhs].core_type_id,
                                   ins->core_type_id);
}

/* The direct LP64 backends admit relaxed-only atomic scalar loads.
 * This remains strictly a source/Core value-shape gate. */
bool minic_multi_direct_scalar_load_shape(
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    size_t *width, bool *is_signed, bool *is_pointer) {
    bool atomic;
    MinicCoreValueId address;
    size_t pointee_width = 0U;
    if (fn == NULL || fn->type_context == NULL || fn->values == NULL ||
        ins == NULL ||
        width == NULL || is_signed == NULL || is_pointer == NULL)
        return false;
    atomic = ins->kind == MINIC_CORE_INSTRUCTION_ATOMIC_LOAD;
    if (ins->kind != MINIC_CORE_INSTRUCTION_LOAD && !atomic)
        return false;
    address = atomic ? ins->value.atomic_load.address : ins->value.load.address;
    *width = direct_core_float(fn, ins->core_type_id, MINIC_CORE_FLOAT_BINARY64) ? 8U :
             direct_core_float(fn, ins->core_type_id, MINIC_CORE_FLOAT_BINARY32) ? 4U : 0U;
    *is_signed = false;
    *is_pointer = direct_core_kind(fn, ins->core_type_id, MINIC_CORE_TYPE_POINTER);
    if (atomic &&
        (!core_direct_relaxed_atomic_pointee_width(
             fn, address, ins->value.atomic_load.memory_order, &pointee_width) ||
         (*is_pointer ? pointee_width != 8U :
            !direct_core_int_width(fn, ins->core_type_id, width, is_signed) ||
            (*width != 1U && *width != 2U && *width != 4U &&
             *width != 8U) ||
            pointee_width != *width)))
        return false;
    if (address >= fn->value_count ||
        !direct_core_kind(fn, fn->values[address].core_type_id,
                          MINIC_CORE_TYPE_POINTER) ||
        (!*is_pointer &&
         !direct_core_float(fn, ins->core_type_id, MINIC_CORE_FLOAT_BINARY64) &&
         !direct_core_float(fn, ins->core_type_id, MINIC_CORE_FLOAT_BINARY32) &&
         !direct_core_int_width(fn, ins->core_type_id, width, is_signed)))
        return false;
    return true;
}

/* One frame/source admissibility check for the two 128-bit load paths.
 * Keep actual 128-bit loads and ISA stack address encodings private. */
bool minic_multi_direct_wide_scalar_load_location(
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    const MinicLP64FramePlan *frame_plan, MinicCoreValueId *address,
    size_t *destination_offset) {
    if (fn == NULL || fn->type_context == NULL || ins == NULL || frame_plan == NULL ||
        address == NULL || destination_offset == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_LOAD ||
        !direct_core_wide(fn, ins->core_type_id))
        return false;
    *address = ins->value.load.address;
    return *address < fn->value_count &&
           direct_core_kind(fn, fn->values[*address].core_type_id,
                            MINIC_CORE_TYPE_POINTER) &&
           minic_lp64_frame_plan_wide_slot(frame_plan, ins->result,
                                            destination_offset);
}

bool minic_multi_direct_scalar_store_shape(
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    size_t *width, bool *is_signed, bool *is_pointer) {
    bool atomic;
    MinicCoreValueId addr, value;
    size_t pointee_width = 0U;
    if (fn == NULL || fn->type_context == NULL || fn->values == NULL ||
        ins == NULL ||
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
        !direct_core_kind(fn, fn->values[addr].core_type_id,
                          MINIC_CORE_TYPE_POINTER))
        return false;
    *width = 0U;
    *is_signed = false;
    *is_pointer = direct_core_kind(fn, fn->values[value].core_type_id,
                                   MINIC_CORE_TYPE_POINTER);
    /* These are the existing *relaxed-only*, scalar, naturally-aligned
     * admission rules. This is not a general atomic memory model. */
    if (atomic &&
        !core_direct_relaxed_atomic_pointee_width(
             fn, addr,
             ins->value.atomic_store.memory_order, &pointee_width))
        return false;
    if (!*is_pointer &&
        !direct_core_float(fn, fn->values[value].core_type_id,
                           MINIC_CORE_FLOAT_BINARY64) &&
        !direct_core_float(fn, fn->values[value].core_type_id,
                           MINIC_CORE_FLOAT_BINARY32) &&
        !direct_core_int_width(fn, fn->values[value].core_type_id,
                               width, is_signed))
        return false;
    if (direct_core_float(fn, fn->values[value].core_type_id,
                          MINIC_CORE_FLOAT_BINARY64))
        *width = 8U;
    if (direct_core_float(fn, fn->values[value].core_type_id,
                          MINIC_CORE_FLOAT_BINARY32))
        *width = 4U;
    if (atomic &&
        (direct_core_float(fn, fn->values[value].core_type_id,
                           MINIC_CORE_FLOAT_BINARY64) ||
         direct_core_float(fn, fn->values[value].core_type_id,
                           MINIC_CORE_FLOAT_BINARY32) ||
         (*is_pointer ? pointee_width != 8U :
          (*width != 1U && *width != 2U &&
           *width != 4U && *width != 8U) ||
          *width != pointee_width)))
        return false;
    return true;
}

/* One dispatch classifier and SSA-result guard for both LP64 direct
 * backends. Effect-only instructions deliberately have no scalar slots. */
bool minic_multi_direct_machine_instruction_shape(
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    bool allow_variadic_double_result,
    MinicDirectMachineInstructionShape *shape) {
    if (fn == NULL || fn->type_context == NULL || ins == NULL || shape == NULL)
        return false;
    shape->result_offset = 0U;
    if (ins->kind == MINIC_CORE_INSTRUCTION_COMPILER_BARRIER) {
        if (!minic_multi_direct_compiler_barrier_shape(fn, ins))
            return false;
        shape->family = MINIC_DIRECT_MACHINE_COMPILER_BARRIER;
        return true;
    }
    /* Structured GNU asm is already a Core effect, never an SSA value.
     * Native target emission remains independently qualified and fail-closed. */
    if (ins->kind == MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM) {
        if (ins->result != MINIC_CORE_VALUE_INVALID ||
            !direct_core_kind(fn, ins->core_type_id, MINIC_CORE_TYPE_VOID) ||
            ins->value.structured_inline_asm.inline_asm_id >= fn->inline_asm_count)
            return false;
        shape->family = MINIC_DIRECT_MACHINE_RESIDUAL;
        return true;
    }
    /* A zero-operand opaque hardware effect has no SSA result; the target
     * must independently requalify the full exact template at emission. */
    if (ins->kind == MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM) {
        if (ins->result != MINIC_CORE_VALUE_INVALID ||
            !direct_core_kind(fn, ins->core_type_id, MINIC_CORE_TYPE_VOID) ||
            ins->value.inline_asm_id >= fn->inline_asm_count)
            return false;
        shape->family = MINIC_DIRECT_MACHINE_RESIDUAL;
        return true;
    }
    if (ins->kind == MINIC_CORE_INSTRUCTION_FIXED_REGISTER_WRITE) {
        if (ins->result != MINIC_CORE_VALUE_INVALID ||
            !direct_core_kind(fn, ins->core_type_id, MINIC_CORE_TYPE_VOID) ||
            ins->value.fixed_register_write.binding_id >=
                fn->fixed_register_binding_count ||
            ins->value.fixed_register_write.source >= fn->value_count)
            return false;
        shape->family = MINIC_DIRECT_MACHINE_RESIDUAL;
        return true;
    }
    if (ins->kind == MINIC_CORE_INSTRUCTION_VARIADIC_COPY) {
        const MinicCoreValueId dst = ins->value.variadic_copy.destination;
        const MinicCoreValueId src = ins->value.variadic_copy.source;
        if (ins->result != MINIC_CORE_VALUE_INVALID ||
            !direct_core_kind(fn, ins->core_type_id, MINIC_CORE_TYPE_VOID) ||
            dst >= fn->value_count ||
            src >= fn->value_count ||
            !direct_core_kind(fn, fn->values[dst].core_type_id,
                              MINIC_CORE_TYPE_POINTER) ||
            !direct_core_equal_exact(fn, fn->values[dst].core_type_id,
                                     fn->values[src].core_type_id))
            return false;
        shape->family = MINIC_DIRECT_MACHINE_RESIDUAL;
        return true;
    }
    if (ins->kind == MINIC_CORE_INSTRUCTION_RECORD_LOAD) {
        shape->family = MINIC_DIRECT_MACHINE_RECORD_LOAD;
        return true;
    }
    if (ins->kind == MINIC_CORE_INSTRUCTION_RECORD_COPY) {
        shape->family = MINIC_DIRECT_MACHINE_RECORD_COPY;
        return true;
    }
    if (ins->kind == MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT) {
        shape->family = MINIC_DIRECT_MACHINE_PARAMETER_OBJECT;
        return true;
    }
    if (minic_core_instruction_semantic_family(ins->kind) ==
        MINIC_CORE_FAMILY_MEMORY) {
        shape->family = MINIC_DIRECT_MACHINE_MEMORY;
        return (ins->kind != MINIC_CORE_INSTRUCTION_LOAD &&
                ins->kind != MINIC_CORE_INSTRUCTION_ATOMIC_LOAD) ||
               minic_multi_direct_result_slot_offset(
                   fn, ins, allow_variadic_double_result,
                   &shape->result_offset);
    }
    if (!minic_multi_direct_result_slot_offset(
            fn, ins, allow_variadic_double_result, &shape->result_offset))
        return false;
    if (minic_core_instruction_semantic_family(ins->kind) ==
        MINIC_CORE_FAMILY_FLOATING_SCALAR)
        shape->family = MINIC_DIRECT_MACHINE_FP;
    else if (minic_multi_direct_integer_machine_kind(ins->kind))
        shape->family = MINIC_DIRECT_MACHINE_INTEGER;
    else if (minic_multi_direct_abi_transfer_kind(ins->kind))
        shape->family = MINIC_DIRECT_MACHINE_ABI;
    else
        shape->family = MINIC_DIRECT_MACHINE_RESIDUAL;
    return true;
}
