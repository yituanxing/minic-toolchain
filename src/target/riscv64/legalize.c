#include "target/riscv64/legalize.h"
#include "target/common/core_call_view.h"
#include "target/riscv64/abi.h"
#include "target/data_layout.h"
#include <stdint.h>
#include <limits.h>

/* Original target scalar gate remains shared by RV64 object analysis
 * and instruction-admission queries. */
bool minic_riscv64_scalar_type(MinicType type) {
    return minic_type_is_integer(type) || minic_type_is_pointer(type) ||
           minic_type_is_float(type) || minic_type_is_double(type) ||
           minic_type_is_long_double(type);
}

/* Existing production RV64 instruction and calling-convention legal gates.
 * Moved from core_codegen.c without changing any comparisons or fallbacks.
 * ISA emission and actual register/stack placement remain target-owned. */
bool minic_riscv64_core_integer_type_is_signed(const MinicCoreFunction *function,
                                        MinicType type,
                                        bool *is_signed) {
    MinicType effective_type;

    if (is_signed == NULL ||
        !minic_core_function_effective_integer_type(function, type, &effective_type)) {
        return false;
    }
    *is_signed = minic_type_is_signed_integer(effective_type);
    return true;
}

bool minic_riscv64_core_field_address_supported(const MinicCoreInstruction *instruction,
                                         size_t *field_offset) {
    if (instruction == NULL || instruction->kind != MINIC_CORE_INSTRUCTION_FIELD_ADDRESS) {
        return false;
    }
    if (field_offset != NULL) {
        *field_offset = instruction->value.field_address.byte_offset;
    }
    return true;
}

bool minic_riscv64_core_record_load_supported(const MinicC0Program *program,
                                       const MinicCoreFunction *function,
                                       const MinicCoreInstruction *instruction,
                                       size_t *record_size) {
    MinicCoreObjectId destination_object;
    MinicCoreValueId source_address;
    MinicType record_type;
    MinicType source_pointee;
    MinicType source_type;
    size_t alignment;
    size_t size;

    if (program == NULL || function == NULL || instruction == NULL ||
        instruction->kind != MINIC_CORE_INSTRUCTION_RECORD_LOAD ||
        instruction->result != MINIC_CORE_VALUE_INVALID ||
        !minic_type_is_record(instruction->type) ||
        !minic_type_unqualified(instruction->type, &record_type) ||
        !minic_type_equal(record_type, instruction->type)) {
        return false;
    }
    destination_object = instruction->value.record_load.destination_object;
    source_address = instruction->value.record_load.source_address;
    if (destination_object >= function->object_count || source_address >= function->value_count ||
        !minic_type_equal(function->objects[destination_object].type, instruction->type) ||
        !minic_type_pointee(function->values[source_address].type, &source_pointee) ||
        !minic_type_unqualified(source_pointee, &source_type) ||
        !minic_type_equal(source_type, instruction->type) ||
        instruction->value.record_load.is_volatile != minic_type_is_volatile(source_pointee) ||
        !minic_data_layout_type(
            minic_default_data_layout(), program, instruction->type, &size, &alignment) ||
        size == 0U) {
        return false;
    }
    (void)alignment;
    if (record_size != NULL) {
        *record_size = size;
    }
    return true;
}

bool minic_riscv64_core_record_copy_supported(const MinicC0Program *program,
                                       const MinicCoreFunction *function,
                                       const MinicCoreInstruction *instruction) {
    MinicType destination_pointee;
    MinicType destination_type;
    MinicType source_pointee;
    MinicType source_type;
    size_t alignment;
    size_t size;

    if (program == NULL || function == NULL || instruction == NULL ||
        instruction->kind != MINIC_CORE_INSTRUCTION_RECORD_COPY ||
        instruction->result != MINIC_CORE_VALUE_INVALID || !minic_type_is_record(instruction->type) ||
        instruction->value.record_copy.destination_address >= function->value_count ||
        instruction->value.record_copy.source_address >= function->value_count ||
        !minic_type_pointee(
            function->values[instruction->value.record_copy.destination_address].type,
            &destination_pointee) ||
        !minic_type_pointee(function->values[instruction->value.record_copy.source_address].type,
                            &source_pointee) ||
        !minic_type_unqualified(destination_pointee, &destination_type) ||
        !minic_type_unqualified(source_pointee, &source_type) ||
        !minic_type_equal(destination_type, instruction->type) ||
        !minic_type_equal(source_type, instruction->type) ||
        !minic_data_layout_type(
            minic_default_data_layout(), program, instruction->type, &size, &alignment)) {
        return false;
    }
    /* M167_ZERO_RECORD_COPY: GNU empty records are addressable semantic
       objects. RECORD_COPY has already evaluated both address operands; with
       zero storage bytes the target action is an intentional no-op. */
    return alignment != 0U;
}

bool minic_riscv64_core_call_frame_address_supported(
    const MinicCoreInstruction *instruction) {
    MinicType pointee;

    return instruction != NULL &&
           instruction->kind == MINIC_CORE_INSTRUCTION_CALL_FRAME_ADDRESS &&
           (instruction->value.call_frame_address.kind == MINIC_CORE_CALL_FRAME_ADDRESS_RETURN ||
            instruction->value.call_frame_address.kind == MINIC_CORE_CALL_FRAME_ADDRESS_FRAME) &&
           instruction->value.call_frame_address.level == 0U &&
           minic_type_pointee(instruction->type, &pointee) && minic_type_is_void(pointee);
}

bool minic_riscv64_core_scalar_bitcast_supported(const MinicC0Program *program,
                                          const MinicCoreFunction *function,
                                          const MinicCoreInstruction *instruction) {
    const MinicCoreValue *source;
    size_t source_size;
    size_t source_alignment;
    size_t target_size;
    size_t target_alignment;
    if (program == NULL || function == NULL || instruction == NULL ||
        instruction->kind != MINIC_CORE_INSTRUCTION_SCALAR_BITCAST ||
        instruction->value.operand >= function->value_count) {
        return false;
    }
    source = &function->values[instruction->value.operand];
    if (!minic_core_scalar_bitcast_types_valid(instruction->type, source->type) ||
        !minic_data_layout_type(
            minic_default_data_layout(), program, source->type, &source_size, &source_alignment) ||
        !minic_data_layout_type(minic_default_data_layout(),
                                program,
                                instruction->type,
                                &target_size,
                                &target_alignment)) {
        return false;
    }
    (void)source_alignment;
    (void)target_alignment;
    return source_size != 0U && source_size <= 8U && target_size != 0U && target_size <= 8U;
}

bool minic_riscv64_core_integer_type_range_fits(const MinicC0Program *program,
                                         const MinicCoreFunction *function,
                                         MinicType source_type,
                                         MinicType result_type) {
    MinicType effective_source;
    MinicType effective_result;
    size_t source_alignment;
    size_t source_size;
    size_t result_alignment;
    size_t result_size;
    bool source_signed;
    bool result_signed;

    if (program == NULL || function == NULL ||
        !minic_type_is_integer(source_type) || !minic_type_is_integer(result_type) ||
        !minic_core_function_effective_integer_type(function, source_type, &effective_source) ||
        !minic_core_function_effective_integer_type(function, result_type, &effective_result) ||
        !minic_data_layout_type(
            minic_default_data_layout(), program, source_type, &source_size, &source_alignment) ||
        !minic_data_layout_type(
            minic_default_data_layout(), program, result_type, &result_size, &result_alignment) ||
        source_size == 0U || source_size > 8U || result_size == 0U || result_size > 8U) {
        return false;
    }
    (void)source_alignment;
    (void)result_alignment;
    source_signed = minic_type_is_signed_integer(effective_source);
    result_signed = minic_type_is_signed_integer(effective_result);
    if ((!source_signed && !minic_type_is_unsigned_integer(effective_source)) ||
        (!result_signed && !minic_type_is_unsigned_integer(effective_result))) {
        return false;
    }
    if (source_signed == result_signed) {
        return source_size <= result_size;
    }
    /* Every unsigned N-bit value fits a signed result only when the result has
       strictly more value bits. A signed range never wholly fits unsigned. */
    return !source_signed && result_signed && source_size < result_size;
}

bool minic_riscv64_core_integer_overflow_xlen_scratch_exact(
    const MinicC0Program *program,
    MinicType left_type,
    MinicType right_type,
    size_t result_size) {
    size_t left_alignment;
    size_t left_size;
    size_t right_alignment;
    size_t right_size;

    if (program == NULL || result_size == 0U || result_size >= 8U ||
        !minic_data_layout_type(
            minic_default_data_layout(), program, left_type, &left_size, &left_alignment) ||
        !minic_data_layout_type(
            minic_default_data_layout(), program, right_type, &right_size, &right_alignment)) {
        return false;
    }
    (void)left_alignment;
    (void)right_alignment;
    return left_size != 0U && right_size != 0U &&
           left_size <= result_size && right_size <= result_size;
}

bool minic_riscv64_core_integer_overflow_supported(const MinicC0Program *program,
                                            const MinicCoreFunction *function,
                                            const MinicCoreInstruction *instruction,
                                            MinicType *result_type,
                                            size_t *result_size,
                                            bool *is_unsigned) {
    MinicType effective_left_type;
    MinicType effective_result_type;
    MinicType effective_right_type;
    MinicType left_type;
    MinicType pointee;
    MinicType right_type;
    size_t alignment;
    size_t left_alignment;
    size_t left_size;
    size_t right_alignment;
    size_t right_size;
    bool result_is_unsigned;

    if (program == NULL || function == NULL || instruction == NULL ||
        instruction->kind != MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW ||
        !minic_type_equal(instruction->type, minic_type_bool()) ||
        (instruction->value.integer_overflow.operator_kind != MINIC_CORE_INTEGER_OVERFLOW_ADD &&
         instruction->value.integer_overflow.operator_kind !=
             MINIC_CORE_INTEGER_OVERFLOW_SUBTRACT &&
         instruction->value.integer_overflow.operator_kind !=
             MINIC_CORE_INTEGER_OVERFLOW_MULTIPLY) ||
        instruction->value.integer_overflow.left >= function->value_count ||
        instruction->value.integer_overflow.right >= function->value_count ||
        instruction->value.integer_overflow.result_address >= function->value_count ||
        !minic_type_pointee(
            function->values[instruction->value.integer_overflow.result_address].type, &pointee) ||
        !minic_type_is_integer(pointee) || minic_type_is_bool_integer(pointee) ||
        minic_type_is_const(pointee) || minic_type_is_volatile(pointee) ||
        !minic_data_layout_type(
            minic_default_data_layout(), program, pointee, result_size, &alignment) ||
        *result_size == 0U || *result_size > 8U ||
        !minic_core_function_effective_integer_type(function, pointee, &effective_result_type)) {
        return false;
    }
    (void)alignment;
    left_type = function->values[instruction->value.integer_overflow.left].type;
    right_type = function->values[instruction->value.integer_overflow.right].type;
    if (!minic_type_is_integer(left_type) || !minic_type_is_integer(right_type) ||
        !minic_core_function_effective_integer_type(function, left_type, &effective_left_type) ||
        !minic_core_function_effective_integer_type(function, right_type, &effective_right_type) ||
        !minic_data_layout_type(
            minic_default_data_layout(), program, left_type, &left_size, &left_alignment) ||
        !minic_data_layout_type(
            minic_default_data_layout(), program, right_type, &right_size, &right_alignment) ||
        left_size == 0U || left_size > 8U || right_size == 0U || right_size > 8U) {
        return false;
    }
    (void)left_alignment;
    (void)right_alignment;
    result_is_unsigned = minic_type_is_unsigned_integer(effective_result_type);

    if ((!minic_type_equal(left_type, pointee) || !minic_type_equal(right_type, pointee)) &&
        !(minic_riscv64_core_integer_type_range_fits(program, function, left_type, pointee) &&
          minic_riscv64_core_integer_type_range_fits(program, function, right_type, pointee)) &&
        !minic_riscv64_core_integer_overflow_xlen_scratch_exact(
            program, left_type, right_type, *result_size)) {
        bool left_is_result;
        bool right_is_result;
        const MinicType *other_effective_type;

        /* Preserve the established narrow signed-result extension for the
           one mixed case whose full unsigned operand range does not fit. */
        left_is_result = minic_type_equal(left_type, pointee);
        right_is_result = minic_type_equal(right_type, pointee);
        if ((instruction->value.integer_overflow.operator_kind !=
                 MINIC_CORE_INTEGER_OVERFLOW_ADD &&
             instruction->value.integer_overflow.operator_kind !=
                 MINIC_CORE_INTEGER_OVERFLOW_MULTIPLY) ||
            result_is_unsigned || *result_size >= 8U || left_is_result == right_is_result ||
            !minic_type_is_signed_integer(effective_result_type)) {
            return false;
        }
        other_effective_type =
            left_is_result ? &effective_right_type : &effective_left_type;
        if (!minic_type_is_unsigned_integer(*other_effective_type)) {
            return false;
        }
        if (instruction->value.integer_overflow.operator_kind ==
                MINIC_CORE_INTEGER_OVERFLOW_MULTIPLY &&
            left_size > 8U - right_size) {
            return false;
        }
    }
    if (result_type != NULL) {
        *result_type = pointee;
    }
    if (is_unsigned != NULL) {
        *is_unsigned = result_is_unsigned;
    }
    return true;
}


/* The ABI cursor owns the placement; CALL admission checks the *same*
 * two-XLEN machine shape for direct and indirect signed/unsigned int128
 * and IEEE binary128 long double. Keep it in one location so widening
 * indirect CALL support cannot silently diverge from direct CALLs. */
static bool rv64_wide_call_argument_supported(
    MinicType type, const MinicRiscv64AbiArgumentLocation *location) {
    return location != NULL &&
        (minic_type_is_long_double(type) ||
         minic_type_is_int128_integer(type)) &&
        location->value.kind == MINIC_RISCV64_ABI_VALUE_WIDE_SCALAR &&
        location->value.storage_size == 16U &&
        location->value.slot_count == 2U &&
        location->floating_register_count == 0U &&
        location->integer_register_count + location->stack_slot_count == 2U &&
        location->integer_register_begin <= 8U &&
        location->integer_register_count <=
            8U - location->integer_register_begin;
}

/* The LP64D named FP stream falls back to GARs/stack once fa0..fa7 are
 * exhausted. This is also the regular path for LP64 and unnamed double. */
static bool rv64_scalar_gpr_call_argument_supported(
    MinicType type, bool fixed,
    const MinicRiscv64AbiArgumentLocation *location) {
    if (location == NULL || location->floating_register_count != 0U ||
        (location->value.kind != MINIC_RISCV64_ABI_VALUE_INTEGER &&
         !(location->value.kind == MINIC_RISCV64_ABI_VALUE_FLOAT &&
           (fixed || minic_type_is_double(type)) &&
           (minic_type_is_float(type) || minic_type_is_double(type)))))
        return false;
    return (location->integer_register_count == 1U &&
            location->integer_register_begin < 8U &&
            location->stack_slot_count == 0U) ||
           (location->integer_register_count == 0U &&
            location->stack_slot_count == 1U);
}

/* ABI legality is independent of whether the callee address is a symbol or
 * an SSA pointer. One checked CoreCallView, one RV64 cursor, and one transfer
 * shape checker cover both. Only the intentionally unsupported *indirect*
 * unnamed-record path remains a policy difference: its machine emitter does
 * not yet transport those aggregates. Never widen admission ahead of emit. */
static bool rv64_core_call_arguments_supported(
    const MinicC0Program *program, const MinicCoreFunction *function,
    const MinicCoreCallView *call, MinicRiscv64AbiCursor *cursor,
    bool direct_call) {
    size_t index;
    if (program == NULL || function == NULL || call == NULL || cursor == NULL ||
        call->argument_count > function->call_argument_count ||
        call->argument_begin > function->call_argument_count ||
        call->argument_count > function->call_argument_count - call->argument_begin ||
        (call->argument_count != 0U && call->arguments == NULL) ||
        call->parameter_count > call->argument_count ||
        (call->parameter_count != 0U && call->parameter_types == NULL))
        return false;
    for (index = 0U; index < call->argument_count; ++index) {
        const MinicCoreCallArgument *argument = &call->arguments[index];
        MinicRiscv64AbiArgumentLocation location;
        MinicType actual_type, type;
        const bool fixed = index < call->parameter_count;
        if (!minic_core_call_view_argument_type(function, call, index, &actual_type))
            return false;
        type = fixed ? call->parameter_types[index] : actual_type;
        if (!fixed &&
            (!call->is_variadic ||
             (!minic_riscv64_scalar_type(type) &&
              (!direct_call || !minic_type_is_record(type)))))
            return false;
        if (!minic_riscv64_abi_place_argument(program, type, fixed, cursor, &location))
            return false;
        if (minic_riscv64_scalar_type(type)) {
            if (argument->kind != MINIC_CORE_CALL_ARGUMENT_VALUE ||
                argument->value.value_id >= function->value_count)
                return false;
            if (location.floating_register_count != 0U) {
                if (!fixed || location.value.kind != MINIC_RISCV64_ABI_VALUE_FLOAT ||
                    (!minic_type_is_float(type) && !minic_type_is_double(type)) ||
                    location.floating_register_count != 1U ||
                    location.floating_register_begin >= 8U ||
                    location.integer_register_count != 0U ||
                    location.stack_slot_count != 0U)
                    return false;
            } else if (location.value.kind == MINIC_RISCV64_ABI_VALUE_WIDE_SCALAR) {
                if (!rv64_wide_call_argument_supported(type, &location))
                    return false;
            } else if (!rv64_scalar_gpr_call_argument_supported(type, fixed, &location)) {
                return false;
            }
            continue;
        }
        if (!minic_type_is_record(type) ||
            argument->kind != MINIC_CORE_CALL_ARGUMENT_OBJECT ||
            (!direct_call && !fixed) ||
            argument->value.object_id >= function->object_count ||
            !minic_type_equal(
                function->objects[argument->value.object_id].type, type))
            return false;
        switch (location.value.kind) {
        case MINIC_RISCV64_ABI_VALUE_IGNORE:
            if (location.value.slot_count != 0U ||
                location.integer_register_count != 0U ||
                location.stack_slot_count != 0U)
                return false;
            break;
        case MINIC_RISCV64_ABI_VALUE_AGGREGATE:
            if (location.value.slot_count == 0U || location.value.slot_count > 2U ||
                location.value.slot_count !=
                    location.integer_register_count + location.stack_slot_count ||
                location.integer_register_begin + location.integer_register_count > 8U)
                return false;
            break;
        case MINIC_RISCV64_ABI_VALUE_INDIRECT:
            if (location.value.slot_count != 1U ||
                !((location.integer_register_count == 1U &&
                   location.integer_register_begin < 8U &&
                   location.stack_slot_count == 0U) ||
                  (location.integer_register_count == 0U &&
                   location.stack_slot_count == 1U)))
                return false;
            break;
        default: return false;
        }
    }
    return true;
}

/* The verified call view owns the result representation for both direct
 * symbol calls and SSA-pointer calls. Return ABI legality must therefore be
 * checked by one target policy, not two subtly different call-site copies. */
static bool rv64_core_call_return_supported(
    const MinicCoreCallView *call, const MinicRiscv64AbiValue *value) {
    MinicType type;
    if (call == NULL || value == NULL)
        return false;
    type = call->return_type;
    switch (value->kind) {
    case MINIC_RISCV64_ABI_VALUE_VOID:
        return call->return_form == MINIC_CORE_CALL_RETURN_VOID;
    case MINIC_RISCV64_ABI_VALUE_INTEGER:
        return call->return_form == MINIC_CORE_CALL_RETURN_VALUE;
    case MINIC_RISCV64_ABI_VALUE_FLOAT:
        return call->return_form == MINIC_CORE_CALL_RETURN_VALUE &&
               (minic_type_is_float(type) || minic_type_is_double(type));
    case MINIC_RISCV64_ABI_VALUE_WIDE_SCALAR:
        return call->return_form == MINIC_CORE_CALL_RETURN_VALUE &&
               (minic_type_is_long_double(type) || minic_type_is_int128_integer(type)) &&
               value->storage_size == 16U && value->slot_count == 2U;
    case MINIC_RISCV64_ABI_VALUE_AGGREGATE:
        return call->return_form == MINIC_CORE_CALL_RETURN_OBJECT &&
               minic_type_is_record(type) &&
               value->slot_count >= 1U && value->slot_count <= 2U;
    case MINIC_RISCV64_ABI_VALUE_INDIRECT:
        return call->return_form == MINIC_CORE_CALL_RETURN_OBJECT &&
               minic_type_is_record(type) && value->storage_size > 16U &&
               value->slot_count == 1U;
    default: return false;
    }
}

bool minic_riscv64_core_direct_call_supported(const MinicC0Program *program,
                                       const MinicCoreFunction *function,
                                       const MinicCoreInstruction *instruction,
                                       const MinicCoreCallView *semantic_view) {
    MinicCoreCallView call;
    const MinicCoreCallee *callee;
    MinicRiscv64AbiCursor cursor;
    MinicRiscv64AbiValue return_value;
    size_t argument_index;

    /* The already-admitted Common FunctionPlan supplies an immutable
     * semantic view. A standalone caller still builds a checked view once. */
    if (!(semantic_view != NULL ? (call = *semantic_view, true) :
          minic_core_call_view_build(function, instruction, &call)) ||
        call.callee == NULL)
        return false;
    callee = call.callee;
    if (callee->name == NULL || callee->name_length == 0U)
        return false;
    if (program == NULL) {
        if ((!minic_type_is_void(callee->return_type) &&
             !minic_riscv64_scalar_type(callee->return_type)) ||
            instruction->value.call.argument_count > 8U) {
            return false;
        }
        for (argument_index = 0U; argument_index < instruction->value.call.argument_count;
             ++argument_index) {
            const MinicCoreCallArgument *argument = &function->call_arguments[
                instruction->value.call.argument_begin + argument_index];
            if (argument->kind != MINIC_CORE_CALL_ARGUMENT_VALUE) {
                return false;
            }
            if (argument_index < callee->parameter_count) {
                if (!minic_riscv64_scalar_type(callee->parameter_types[argument_index])) {
                    return false;
                }
            } else if (argument->value.value_id >= function->value_count ||
                       !minic_riscv64_scalar_type(function->values[argument->value.value_id].type)) {
                return false;
            }
        }
        return true;
    }
    if (!minic_riscv64_abi_cursor_initialize_for_return(
            program, call.return_type, &cursor, &return_value) ||
        !rv64_core_call_return_supported(&call, &return_value)) {
        return false;
    }
    return rv64_core_call_arguments_supported(program, function, &call, &cursor, true);
}

/* M151_INDIRECT_CALL_BATCH_OWNER: validate indirect arguments through the
   same register-only RV64 ABI classifier used by direct calls. */
bool minic_riscv64_core_indirect_call_supported(const MinicC0Program *program,
                                         const MinicCoreFunction *function,
                                         const MinicCoreInstruction *instruction,
                                         const MinicCoreCallView *semantic_view) {
    MinicCoreCallView call;
    const MinicCoreCallSignature *signature;
    MinicRiscv64AbiCursor cursor;
    MinicRiscv64AbiValue return_value;
    size_t argument_index;

    if (!(semantic_view != NULL ? (call = *semantic_view, true) :
          minic_core_call_view_build(function, instruction, &call)) ||
        call.signature == NULL)
        return false;
    signature = call.signature;
    if (program == NULL) {
        if ((!minic_type_is_void(signature->return_type) &&
             !minic_riscv64_scalar_type(signature->return_type)) ||
            instruction->value.indirect_call.argument_count > 8U) {
            return false;
        }
        for (argument_index = 0U;
             argument_index < instruction->value.indirect_call.argument_count;
             ++argument_index) {
            const MinicCoreCallArgument *argument = &function->call_arguments[
                instruction->value.indirect_call.argument_begin + argument_index];
            if (argument->kind != MINIC_CORE_CALL_ARGUMENT_VALUE ||
                argument->value.value_id >= function->value_count) {
                return false;
            }
            if (argument_index < signature->parameter_count) {
                if (!minic_riscv64_scalar_type(signature->parameter_types[argument_index])) {
                    return false;
                }
            } else if (!signature->is_variadic ||
                       !minic_riscv64_scalar_type(function->values[argument->value.value_id].type)) {
                return false;
            }
        }
        return true;
    }
    if (!minic_riscv64_abi_cursor_initialize_for_return(
            program, call.return_type, &cursor, &return_value) ||
        !rv64_core_call_return_supported(&call, &return_value)) {
        return false;
    }
    return rv64_core_call_arguments_supported(program, function, &call, &cursor, false);
}


