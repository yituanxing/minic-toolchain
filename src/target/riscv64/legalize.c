#include "target/riscv64/legalize.h"
#include "target/common/core_call_view.h"
#include "target/riscv64/abi.h"
#include "target/riscv64/core_type_query.h"
#include "target/common/lp64_scalar.h"
#include "target/common/core_scalar_conversion.h"

/* Existing production RV64 instruction and calling-convention legal gates.
 * Moved from core_codegen.c. Executable type admission is Core-only.
 * ISA emission and actual register/stack placement remain target-owned. */
bool minic_riscv64_core_integer_type_is_signed(const MinicCoreFunction *function,
                                        MinicCoreTypeId type_id,
                                        bool *is_signed) {
    const MinicCoreType *type = minic_riscv64_core_base_type(function, type_id);
    if (is_signed == NULL || type == NULL ||
        type->kind != MINIC_CORE_TYPE_INTEGER) return false;
    *is_signed = type->value.integer.is_signed;
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

bool minic_riscv64_core_record_load_supported(const MinicCoreFunction *function,
                                       const MinicCoreInstruction *instruction,
                                       size_t *record_size) {
    const MinicCoreTypeContext *types;
    const MinicCoreType *record, *pointee;
    MinicCoreTypeId source_pointee_id;
    if (function == NULL || function->type_context == NULL || instruction == NULL ||
        instruction->kind != MINIC_CORE_INSTRUCTION_RECORD_LOAD ||
        instruction->result != MINIC_CORE_VALUE_INVALID ||
        instruction->value.record_load.destination_object >= function->object_count ||
        instruction->value.record_load.source_address >= function->value_count)
        return false;
    types = function->type_context;
    if (!minic_lp64_core_kind(types, instruction->core_type_id,
                             MINIC_CORE_TYPE_RECORD) ||
        !minic_lp64_core_equal_unqualified(
            types, function->objects[
                instruction->value.record_load.destination_object].core_type_id,
            instruction->core_type_id) ||
        !minic_lp64_core_pointer_pointee(
            types, function->values[
                instruction->value.record_load.source_address].core_type_id,
            &source_pointee_id) ||
        !minic_lp64_core_equal_unqualified(
            types, source_pointee_id, instruction->core_type_id)) return false;
    record = minic_riscv64_core_base_type(function, instruction->core_type_id);
    pointee = minic_core_type_get(types, source_pointee_id);
    if (record == NULL || pointee == NULL || !record->has_layout ||
        record->size == 0U || record->alignment == 0U ||
        instruction->value.record_load.is_volatile != pointee->is_volatile)
        return false;
    if (record_size != NULL) *record_size = record->size;
    return true;
}

bool minic_riscv64_core_record_copy_supported(const MinicCoreFunction *function,
                                       const MinicCoreInstruction *instruction) {
    const MinicCoreTypeContext *types;
    const MinicCoreType *record;
    MinicCoreTypeId destination_pointee_id, source_pointee_id;
    if (function == NULL || function->type_context == NULL || instruction == NULL ||
        instruction->kind != MINIC_CORE_INSTRUCTION_RECORD_COPY ||
        instruction->result != MINIC_CORE_VALUE_INVALID ||
        instruction->value.record_copy.destination_address >= function->value_count ||
        instruction->value.record_copy.source_address >= function->value_count)
        return false;
    types = function->type_context;
    if (!minic_lp64_core_kind(types, instruction->core_type_id,
                             MINIC_CORE_TYPE_RECORD) ||
        !minic_lp64_core_pointer_pointee(
            types, function->values[
                instruction->value.record_copy.destination_address].core_type_id,
            &destination_pointee_id) ||
        !minic_lp64_core_pointer_pointee(
            types, function->values[
                instruction->value.record_copy.source_address].core_type_id,
            &source_pointee_id) ||
        !minic_lp64_core_equal_unqualified(
            types, destination_pointee_id, instruction->core_type_id) ||
        !minic_lp64_core_equal_unqualified(
            types, source_pointee_id, instruction->core_type_id)) return false;
    record = minic_riscv64_core_base_type(function, instruction->core_type_id);
    /* M167_ZERO_RECORD_COPY: GNU empty records are addressable semantic
       objects. RECORD_COPY has already evaluated both address operands; with
       zero storage bytes the target action is an intentional no-op. */
    return record != NULL && record->has_layout && record->alignment != 0U;
}

bool minic_riscv64_core_call_frame_address_supported(
    const MinicCoreFunction *function, const MinicCoreInstruction *instruction) {
    if (instruction == NULL ||
        instruction->kind != MINIC_CORE_INSTRUCTION_CALL_FRAME_ADDRESS ||
        (instruction->value.call_frame_address.kind != MINIC_CORE_CALL_FRAME_ADDRESS_RETURN &&
         instruction->value.call_frame_address.kind != MINIC_CORE_CALL_FRAME_ADDRESS_FRAME) ||
        instruction->value.call_frame_address.level != 0U ||
        function == NULL || function->type_context == NULL) return false;
    {
        MinicCoreTypeId core_pointee;
        return minic_lp64_core_pointer_pointee(
                   function->type_context, instruction->core_type_id, &core_pointee) &&
               minic_lp64_core_kind(function->type_context, core_pointee,
                                    MINIC_CORE_TYPE_VOID);
    }
}

bool minic_riscv64_core_scalar_bitcast_supported(const MinicCoreFunction *function,
                                          const MinicCoreInstruction *instruction) {
    MinicCoreScalarBitcastShape cast;
    return minic_core_scalar_bitcast_shape(function, instruction, &cast) &&
           cast.source_bytes <= 8U && cast.target_bytes <= 8U;
}

bool minic_riscv64_core_integer_type_range_fits(
    const MinicCoreFunction *function, MinicCoreTypeId source_type,
    MinicCoreTypeId result_type) {
    const MinicCoreType *source = minic_riscv64_core_base_type(function, source_type);
    const MinicCoreType *result = minic_riscv64_core_base_type(function, result_type);
    bool source_signed;
    bool result_signed;

    if (source == NULL || result == NULL ||
        source->kind != MINIC_CORE_TYPE_INTEGER ||
        result->kind != MINIC_CORE_TYPE_INTEGER ||
        !source->has_layout || !result->has_layout ||
        source->size == 0U || source->size > 8U ||
        result->size == 0U || result->size > 8U) {
        return false;
    }
    source_signed = source->value.integer.is_signed;
    result_signed = result->value.integer.is_signed;
    if (source_signed == result_signed) {
        return source->size <= result->size;
    }
    /* Every unsigned N-bit value fits a signed result only when the result has
       strictly more value bits. A signed range never wholly fits unsigned. */
    return !source_signed && result_signed && source->size < result->size;
}

bool minic_riscv64_core_integer_overflow_xlen_scratch_exact(
    const MinicCoreFunction *function, MinicCoreTypeId left_type,
    MinicCoreTypeId right_type, size_t result_size) {
    const MinicCoreType *left = minic_riscv64_core_base_type(function, left_type);
    const MinicCoreType *right = minic_riscv64_core_base_type(function, right_type);
    return result_size > 0U && result_size < 8U &&
           left != NULL && right != NULL &&
           left->kind == MINIC_CORE_TYPE_INTEGER &&
           right->kind == MINIC_CORE_TYPE_INTEGER &&
           left->has_layout && right->has_layout &&
           left->size > 0U && right->size > 0U &&
           left->size <= result_size && right->size <= result_size;
}

bool minic_riscv64_core_integer_overflow_supported(
    const MinicCoreFunction *function, const MinicCoreInstruction *instruction,
    MinicCoreTypeId *result_type, size_t *result_size, bool *is_unsigned) {
    const MinicCoreType *result, *pointee, *left, *right, *flag;
    MinicCoreTypeId pointee_id, left_id, right_id;
    size_t left_size, right_size;
    bool result_is_unsigned;

    if (function == NULL || function->type_context == NULL ||
        instruction == NULL || result_size == NULL ||
        instruction->kind != MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW ||
        (instruction->value.integer_overflow.operator_kind != MINIC_CORE_INTEGER_OVERFLOW_ADD &&
         instruction->value.integer_overflow.operator_kind !=
             MINIC_CORE_INTEGER_OVERFLOW_SUBTRACT &&
         instruction->value.integer_overflow.operator_kind !=
             MINIC_CORE_INTEGER_OVERFLOW_MULTIPLY) ||
        instruction->value.integer_overflow.left >= function->value_count ||
        instruction->value.integer_overflow.right >= function->value_count ||
        instruction->value.integer_overflow.result_address >= function->value_count ||
        !minic_lp64_core_pointer_pointee(
            function->type_context,
            function->values[instruction->value.integer_overflow.result_address].core_type_id,
            &pointee_id)) {
        return false;
    }
    flag = minic_riscv64_core_base_type(function, instruction->core_type_id);
    pointee = minic_core_type_get(function->type_context, pointee_id);
    result = minic_riscv64_core_base_type(function, pointee_id);
    left_id = function->values[instruction->value.integer_overflow.left].core_type_id;
    right_id = function->values[instruction->value.integer_overflow.right].core_type_id;
    left = minic_riscv64_core_base_type(function, left_id);
    right = minic_riscv64_core_base_type(function, right_id);
    if (flag == NULL || flag->kind != MINIC_CORE_TYPE_INTEGER ||
        !flag->value.integer.is_bool ||
        pointee == NULL || pointee->is_read_only || pointee->is_volatile ||
        result == NULL || result->kind != MINIC_CORE_TYPE_INTEGER ||
        result->value.integer.is_bool || !result->has_layout ||
        result->size == 0U || result->size > 8U ||
        left == NULL || right == NULL ||
        left->kind != MINIC_CORE_TYPE_INTEGER ||
        right->kind != MINIC_CORE_TYPE_INTEGER ||
        !left->has_layout || !right->has_layout ||
        left->size == 0U || left->size > 8U ||
        right->size == 0U || right->size > 8U) return false;
    *result_size = result->size;
    left_size = left->size;
    right_size = right->size;
    result_is_unsigned = !result->value.integer.is_signed;

    if ((!minic_lp64_core_equal_unqualified(function->type_context, left_id, pointee_id) ||
         !minic_lp64_core_equal_unqualified(function->type_context, right_id, pointee_id)) &&
        !(minic_riscv64_core_integer_type_range_fits(function, left_id, pointee_id) &&
          minic_riscv64_core_integer_type_range_fits(function, right_id, pointee_id)) &&
        !minic_riscv64_core_integer_overflow_xlen_scratch_exact(
            function, left_id, right_id, *result_size)) {
        bool left_is_result;
        bool right_is_result;
        const MinicCoreType *other;

        /* Preserve the established narrow signed-result extension for the
           one mixed case whose full unsigned operand range does not fit. */
        left_is_result = minic_lp64_core_equal_unqualified(
            function->type_context, left_id, pointee_id);
        right_is_result = minic_lp64_core_equal_unqualified(
            function->type_context, right_id, pointee_id);
        if ((instruction->value.integer_overflow.operator_kind !=
                 MINIC_CORE_INTEGER_OVERFLOW_ADD &&
             instruction->value.integer_overflow.operator_kind !=
                 MINIC_CORE_INTEGER_OVERFLOW_MULTIPLY) ||
            result_is_unsigned || *result_size >= 8U || left_is_result == right_is_result ||
            !result->value.integer.is_signed) {
            return false;
        }
        other = left_is_result ? right : left;
        if (other->value.integer.is_signed) {
            return false;
        }
        if (instruction->value.integer_overflow.operator_kind ==
                MINIC_CORE_INTEGER_OVERFLOW_MULTIPLY &&
            left_size > 8U - right_size) {
            return false;
        }
    }
    if (result_type != NULL) {
        *result_type = pointee_id;
    }
    if (is_unsigned != NULL) {
        *is_unsigned = result_is_unsigned;
    }
    return true;
}


static bool rv64_core_abi_scalar(const MinicCoreType *type) {
    return type != NULL &&
        (type->kind == MINIC_CORE_TYPE_INTEGER ||
         type->kind == MINIC_CORE_TYPE_POINTER ||
         (type->kind == MINIC_CORE_TYPE_FLOAT &&
          (type->value.floating.format == MINIC_CORE_FLOAT_BINARY32 ||
           type->value.floating.format == MINIC_CORE_FLOAT_BINARY64 ||
           type->value.floating.format == MINIC_CORE_FLOAT_EXTENDED)));
}

static bool rv64_core_abi_wide(const MinicCoreType *type) {
    return type != NULL &&
        ((type->kind == MINIC_CORE_TYPE_INTEGER &&
          type->value.integer.bit_width == 128U) ||
         (type->kind == MINIC_CORE_TYPE_FLOAT &&
          type->value.floating.format == MINIC_CORE_FLOAT_EXTENDED));
}

/* Only resolved type IDs drive production ABI admission. The prepared
 * locations came from the same Core call view, so machine placement is
 * checked for structural validity without reopening a C function table. */
static bool rv64_core_call_arguments_resolved_supported(
    const MinicCoreFunction *function, const MinicCoreCallView *call,
    const MinicRiscv64PreparedCall *prepared, bool direct_call) {
    size_t index;
    if (function == NULL || function->type_context == NULL || call == NULL ||
        prepared == NULL || prepared->argument_count != call->argument_count ||
        call->argument_count > function->call_argument_count ||
        call->argument_begin > function->call_argument_count ||
        call->argument_count > function->call_argument_count - call->argument_begin ||
        (call->argument_count != 0U && call->arguments == NULL) ||
        call->parameter_count > call->argument_count ||
        (call->parameter_count != 0U && call->core_parameter_type_ids == NULL))
        return false;
    for (index = 0U; index < call->argument_count; ++index) {
        const MinicCoreCallArgument *argument = &call->arguments[index];
        const MinicRiscv64AbiArgumentLocation location = prepared->arguments[index];
        const bool fixed = index < call->parameter_count;
        const MinicCoreType *type;
        MinicCoreTypeId actual_type;
        MinicCoreTypeId type_id;
        if (!minic_core_call_view_argument_core_type(
                function, call, index, &actual_type)) return false;
        type_id = fixed ? call->core_parameter_type_ids[index] : actual_type;
        type = minic_riscv64_core_base_type(function, type_id);
        if (type == NULL || (!fixed &&
            (!call->is_variadic ||
             (!rv64_core_abi_scalar(type) &&
              (!direct_call || type->kind != MINIC_CORE_TYPE_RECORD)))))
            return false;
        if (rv64_core_abi_scalar(type)) {
            bool is_fp = type->kind == MINIC_CORE_TYPE_FLOAT &&
                (type->value.floating.format == MINIC_CORE_FLOAT_BINARY32 ||
                 type->value.floating.format == MINIC_CORE_FLOAT_BINARY64);
            bool is_double = is_fp &&
                type->value.floating.format == MINIC_CORE_FLOAT_BINARY64;
            if (argument->kind != MINIC_CORE_CALL_ARGUMENT_VALUE ||
                argument->value.value_id >= function->value_count) return false;
            if (location.floating_register_count != 0U) {
                if (!fixed || !is_fp ||
                    location.value.kind != MINIC_RISCV64_ABI_VALUE_FLOAT ||
                    location.floating_register_count != 1U ||
                    location.floating_register_begin >= 8U ||
                    location.integer_register_count != 0U ||
                    location.stack_slot_count != 0U) return false;
            } else if (location.value.kind == MINIC_RISCV64_ABI_VALUE_WIDE_SCALAR) {
                if (!rv64_core_abi_wide(type) ||
                    location.value.storage_size != 16U ||
                    location.value.slot_count != 2U ||
                    location.integer_register_count + location.stack_slot_count != 2U ||
                    location.integer_register_begin > 8U ||
                    location.integer_register_count >
                        8U - location.integer_register_begin) return false;
            } else if (location.floating_register_count != 0U ||
                       (location.value.kind != MINIC_RISCV64_ABI_VALUE_INTEGER &&
                        !(location.value.kind == MINIC_RISCV64_ABI_VALUE_FLOAT &&
                          (fixed || is_double) && is_fp)) ||
                       !((location.integer_register_count == 1U &&
                          location.integer_register_begin < 8U &&
                          location.stack_slot_count == 0U) ||
                         (location.integer_register_count == 0U &&
                          location.stack_slot_count == 1U))) return false;
            continue;
        }
        if (type->kind != MINIC_CORE_TYPE_RECORD ||
            argument->kind != MINIC_CORE_CALL_ARGUMENT_OBJECT ||
            (!direct_call && !fixed) ||
            argument->value.object_id >= function->object_count ||
            minic_core_type_unqualified(function->type_context,
                function->objects[argument->value.object_id].core_type_id) !=
            minic_core_type_unqualified(function->type_context, type_id))
            return false;
        switch (location.value.kind) {
        case MINIC_RISCV64_ABI_VALUE_IGNORE:
            if (location.value.slot_count != 0U ||
                location.integer_register_count != 0U ||
                location.stack_slot_count != 0U) return false;
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
                   location.stack_slot_count == 1U))) return false;
            break;
        default: return false;
        }
    }
    return true;
}

/* ABI legality is independent of whether the callee address is a symbol or
 * an SSA pointer. One checked CoreCallView, one immutable RV64 prepared
 * placement and one transfer shape checker cover both. We must not rerun a
 * default LP64D cursor here: the frame/caller may target soft-float LP64.
 * Only the intentionally unsupported *indirect*
 * unnamed-record path remains a policy difference: its machine emitter does
 * not yet transport those aggregates. Never widen admission ahead of emit. */
static bool rv64_core_call_arguments_supported(
    const MinicCoreFunction *function,
    const MinicCoreCallView *call, const MinicRiscv64PreparedCall *prepared,
    bool direct_call) {
    return rv64_core_call_arguments_resolved_supported(
        function, call, prepared, direct_call);
}

/* The verified call view owns the result representation for both direct
 * symbol calls and SSA-pointer calls. Return ABI legality must therefore be
 * checked by one target policy, not two subtly different call-site copies. */
static bool rv64_core_call_return_supported(
    const MinicCoreFunction *function, const MinicCoreCallView *call,
    const MinicRiscv64AbiValue *value) {
    const MinicCoreType *core;
    if (function == NULL || function->type_context == NULL ||
        call == NULL || value == NULL)
        return false;
    core = minic_riscv64_core_base_type(function, call->core_return_type_id);
    if (core == NULL) return false;
    switch (value->kind) {
    case MINIC_RISCV64_ABI_VALUE_VOID:
        return call->return_form == MINIC_CORE_CALL_RETURN_VOID;
    case MINIC_RISCV64_ABI_VALUE_INTEGER:
        return call->return_form == MINIC_CORE_CALL_RETURN_VALUE;
    case MINIC_RISCV64_ABI_VALUE_FLOAT:
        return call->return_form == MINIC_CORE_CALL_RETURN_VALUE &&
               core->kind == MINIC_CORE_TYPE_FLOAT &&
               (core->value.floating.format == MINIC_CORE_FLOAT_BINARY32 ||
                core->value.floating.format == MINIC_CORE_FLOAT_BINARY64);
    case MINIC_RISCV64_ABI_VALUE_WIDE_SCALAR:
        return call->return_form == MINIC_CORE_CALL_RETURN_VALUE &&
               rv64_core_abi_wide(core) &&
               value->storage_size == 16U && value->slot_count == 2U;
    case MINIC_RISCV64_ABI_VALUE_AGGREGATE:
        return call->return_form == MINIC_CORE_CALL_RETURN_OBJECT &&
               core->kind == MINIC_CORE_TYPE_RECORD &&
               value->slot_count >= 1U && value->slot_count <= 2U;
    case MINIC_RISCV64_ABI_VALUE_INDIRECT:
        return call->return_form == MINIC_CORE_CALL_RETURN_OBJECT &&
               core->kind == MINIC_CORE_TYPE_RECORD &&
               value->storage_size > 16U &&
               value->slot_count == 1U;
    default: return false;
    }
}

/* Standalone (no ABI cursor) Core admission has deliberately narrower
 * capability than the production prepared-call path: at most eight scalar
 * VALUE arguments. Once a type snapshot is attached the proof must use its
 * Core IDs, not the C signatures retained for legacy fixture producers. */
static bool rv64_core_preparedless_call_supported(
    const MinicCoreFunction *function, const MinicCoreCallView *call) {
    const MinicCoreType *return_type;
    size_t argument_index;
    if (function == NULL || function->type_context == NULL || call == NULL ||
        call->argument_count > 8U ||
        (call->parameter_count != 0U && call->core_parameter_type_ids == NULL))
        return false;
    return_type = minic_riscv64_core_base_type(function, call->core_return_type_id);
    if (return_type == NULL ||
        (return_type->kind != MINIC_CORE_TYPE_VOID &&
         !rv64_core_abi_scalar(return_type))) return false;
    for (argument_index = 0U; argument_index < call->argument_count; ++argument_index) {
        const MinicCoreCallArgument *argument = &call->arguments[argument_index];
        MinicCoreTypeId type_id;
        if (argument->kind != MINIC_CORE_CALL_ARGUMENT_VALUE ||
            argument->value.value_id >= function->value_count ||
            !minic_core_call_view_argument_core_type(
                function, call, argument_index, &type_id)) return false;
        if (argument_index < call->parameter_count)
            type_id = call->core_parameter_type_ids[argument_index];
        else if (!call->is_variadic) return false;
        if (!rv64_core_abi_scalar(minic_riscv64_core_base_type(function, type_id)))
            return false;
    }
    return true;
}

bool minic_riscv64_core_direct_call_supported(bool require_prepared_abi,
                                       const MinicCoreFunction *function,
                                       const MinicCoreInstruction *instruction,
                                       const MinicCoreCallView *semantic_view,
                                       const MinicRiscv64PreparedCall *prepared) {
    MinicCoreCallView call;
    const MinicCoreCallee *callee;

    /* The already-admitted Common FunctionPlan supplies an immutable
     * semantic view. A standalone caller still builds a checked view once. */
    if (!(semantic_view != NULL ? (call = *semantic_view, true) :
          minic_core_call_view_build(function, instruction, &call)) ||
        call.callee == NULL)
        return false;
    callee = call.callee;
    if (callee->name == NULL || callee->name_length == 0U)
        return false;
    if (!require_prepared_abi)
        return function != NULL && function->type_context != NULL &&
               rv64_core_preparedless_call_supported(function, &call);
    if (prepared == NULL ||
        !rv64_core_call_return_supported(function, &call, &prepared->return_value)) {
        return false;
    }
    return rv64_core_call_arguments_supported(function, &call, prepared, true);
}

/* M151_INDIRECT_CALL_BATCH_OWNER: validate indirect arguments through the
   same register-only RV64 ABI classifier used by direct calls. */
bool minic_riscv64_core_indirect_call_supported(bool require_prepared_abi,
                                         const MinicCoreFunction *function,
                                         const MinicCoreInstruction *instruction,
                                         const MinicCoreCallView *semantic_view,
                                         const MinicRiscv64PreparedCall *prepared) {
    MinicCoreCallView call;

    if (!(semantic_view != NULL ? (call = *semantic_view, true) :
          minic_core_call_view_build(function, instruction, &call)) ||
        call.signature == NULL)
        return false;
    if (!require_prepared_abi)
        return function != NULL && function->type_context != NULL &&
               rv64_core_preparedless_call_supported(function, &call);
    if (prepared == NULL ||
        !rv64_core_call_return_supported(function, &call, &prepared->return_value)) {
        return false;
    }
    return rv64_core_call_arguments_supported(function, &call, prepared, false);
}


