#include "core/core_ir_internal.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Core IR verification owns instruction admission, block/value invariants
 * and the whole-function fail-closed acceptance contract. */
static bool storage_shape_is_valid(const void *data, size_t count, size_t capacity) {
    return count <= capacity && (count == 0U || data != NULL);
}

/* Source descriptors coexist temporarily with the fully resolved Core
 * snapshots. These checks are independent of C integer rank/enum tables and
 * deliberately fail closed when a TypeId belongs to another (or no) arena. */
static const MinicCoreType *core_snapshot_base_type(const MinicCoreFunction *function,
                                                    MinicCoreTypeId type_id) {
    MinicCoreTypeId base;
    if (function == NULL || function->type_context == NULL) return NULL;
    base = minic_core_type_unqualified(function->type_context, type_id);
    return minic_core_type_get(function->type_context, base);
}

static bool core_snapshot_exists(const MinicCoreFunction *function,
                                 MinicCoreTypeId type_id) {
    return minic_core_function_type_get(function, type_id) != NULL;
}

static bool core_snapshot_parameter_list_valid(
    const MinicCoreFunction *function, const MinicCoreTypeId *types,
    size_t count) {
    size_t i;
    if (count != 0U && types == NULL) return false;
    for (i = 0U; i < count; ++i)
        if (!core_snapshot_exists(function, types[i])) return false;
    return true;
}

/* The function-pointer's pointee and the indirect call's signature must be
 * the SAME Core function identity. Merely comparing a return register class
 * would silently widen the call ABI contract. */
static bool core_snapshot_indirect_callee_valid(
    const MinicCoreFunction *function, const MinicCoreInstruction *instruction) {
    const MinicCoreCallSignature *signature;
    const MinicCoreType *callee_pointer;
    MinicCoreValueId callee_value;
    callee_value = instruction->value.indirect_call.callee;
    if (instruction->value.indirect_call.signature_id >= function->call_signature_count ||
        callee_value >= function->value_count) return false;
    signature = &function->call_signatures[instruction->value.indirect_call.signature_id];
    callee_pointer = core_snapshot_base_type(
        function, function->values[callee_value].core_type_id);
    if (callee_pointer == NULL || callee_pointer->kind != MINIC_CORE_TYPE_POINTER)
        return false;
    return minic_core_type_unqualified(function->type_context,
                                      callee_pointer->value.pointer.pointee) ==
           minic_core_type_unqualified(function->type_context,
                                       signature->core_function_type_id) &&
           instruction->core_type_id == signature->core_return_type_id;
}

static bool core_snapshot_fixed_record_arguments_valid(
    const MinicCoreFunction *function, size_t argument_begin,
    size_t argument_count, const MinicCoreTypeId *parameters,
    size_t parameter_count) {
    size_t i;
    if (parameter_count > argument_count ||
        argument_begin > function->call_argument_count ||
        argument_count > function->call_argument_count - argument_begin)
        return false;
    for (i = 0U; i < parameter_count; ++i) {
        const MinicCoreType *param = core_snapshot_base_type(function, parameters[i]);
        const MinicCoreCallArgument *argument =
            &function->call_arguments[argument_begin + i];
        if (param == NULL) return false;
        if (param->kind == MINIC_CORE_TYPE_RECORD) {
            if (argument->kind != MINIC_CORE_CALL_ARGUMENT_OBJECT ||
                argument->value.object_id >= function->object_count ||
                minic_core_type_unqualified(function->type_context, parameters[i]) !=
                    minic_core_type_unqualified(
                        function->type_context,
                        function->objects[argument->value.object_id].core_type_id))
                return false;
        } else if (argument->kind != MINIC_CORE_CALL_ARGUMENT_VALUE ||
                   argument->value.value_id >= function->value_count) {
            return false;
        }
    }
    return true;
}

bool minic_core_function_verify_core_type_snapshot(const MinicCoreFunction *function) {
    const MinicCoreType *type;
    size_t i, j;
    if (function == NULL || function->type_context == NULL ||
        !storage_shape_is_valid(function->fixed_register_bindings,
                                function->fixed_register_binding_count,
                                function->fixed_register_binding_capacity) ||
        !storage_shape_is_valid(function->objects, function->object_count,
                                function->object_capacity) ||
        !storage_shape_is_valid(function->globals, function->global_count,
                                function->global_capacity) ||
        !storage_shape_is_valid(function->callees, function->callee_count,
                                function->callee_capacity) ||
        !storage_shape_is_valid(function->call_signatures,
                                function->call_signature_count,
                                function->call_signature_capacity) ||
        !storage_shape_is_valid(function->values, function->value_count,
                                function->value_capacity) ||
        !storage_shape_is_valid(function->instructions, function->instruction_count,
                                function->instruction_capacity) ||
        !storage_shape_is_valid(function->call_arguments,
                                function->call_argument_count,
                                function->call_argument_capacity) ||
        !core_snapshot_exists(function, function->core_return_type_id) ||
        !core_snapshot_parameter_list_valid(function,
                                            function->core_parameter_type_ids,
                                            function->parameter_count)) return false;
    for (i = 0U; i < function->fixed_register_binding_count; ++i)
        if (!core_snapshot_exists(function,
                                  function->fixed_register_bindings[i].core_type_id))
            return false;
    for (i = 0U; i < function->object_count; ++i) {
        size_t size, alignment;
        if (!minic_core_function_object_layout(function, (MinicCoreObjectId)i,
                                               &size, &alignment) || alignment == 0U)
            return false;
        type = core_snapshot_base_type(function, function->objects[i].core_type_id);
        if (type == NULL || type->kind == MINIC_CORE_TYPE_VOID ||
            type->kind == MINIC_CORE_TYPE_FUNCTION ||
            type->kind == MINIC_CORE_TYPE_OPAQUE) return false;
        (void)size;
    }
    for (i = 0U; i < function->global_count; ++i)
        if (!core_snapshot_exists(function, function->globals[i].core_type_id))
            return false;
    for (i = 0U; i < function->callee_count; ++i) {
        const MinicCoreCallee *callee = &function->callees[i];
        if (!core_snapshot_exists(function, callee->core_return_type_id) ||
            !core_snapshot_parameter_list_valid(function,
                                                callee->core_parameter_type_ids,
                                                callee->parameter_count)) return false;
    }
    for (i = 0U; i < function->call_signature_count; ++i) {
        const MinicCoreCallSignature *signature = &function->call_signatures[i];
        const MinicCoreType *function_type =
            core_snapshot_base_type(function, signature->core_function_type_id);
        if (function_type == NULL || function_type->kind != MINIC_CORE_TYPE_FUNCTION ||
            !core_snapshot_exists(function, signature->core_return_type_id) ||
            !core_snapshot_parameter_list_valid(function,
                                                signature->core_parameter_type_ids,
                                                signature->parameter_count) ||
            function_type->value.function.return_type != signature->core_return_type_id ||
            function_type->value.function.parameter_count != signature->parameter_count ||
            function_type->value.function.is_variadic != signature->is_variadic ||
            function_type->value.function.calling_convention != signature->calling_convention ||
            (signature->parameter_count != 0U &&
             function_type->value.function.parameters == NULL)) return false;
        for (j = 0U; j < signature->parameter_count; ++j)
            if (function_type->value.function.parameters[j] !=
                signature->core_parameter_type_ids[j]) return false;
    }
    for (i = 0U; i < function->value_count; ++i)
        if (!core_snapshot_exists(function, function->values[i].core_type_id)) return false;
    for (i = 0U; i < function->instruction_count; ++i) {
        const MinicCoreInstruction *instruction = &function->instructions[i];
        if (!core_snapshot_exists(function, instruction->core_type_id)) return false;
        if (instruction->result != MINIC_CORE_VALUE_INVALID) {
            if (instruction->result >= function->value_count ||
                instruction->core_type_id !=
                    function->values[instruction->result].core_type_id) return false;
        }
        if (instruction->kind == MINIC_CORE_INSTRUCTION_FIELD_ADDRESS) {
            const MinicCoreType *record = core_snapshot_base_type(
                function, instruction->value.field_address.core_record_type_id);
            const MinicCoreType *pointer;
            size_t base_id = instruction->value.field_address.base;
            if (record == NULL || record->kind != MINIC_CORE_TYPE_RECORD ||
                !record->value.record.is_complete ||
                instruction->value.field_address.field_index >= record->value.record.field_count ||
                record->value.record.fields == NULL ||
                record->value.record.fields[instruction->value.field_address.field_index]
                    .byte_offset != instruction->value.field_address.byte_offset ||
                base_id >= function->value_count) return false;
            pointer = core_snapshot_base_type(function,
                                              function->values[base_id].core_type_id);
            if (pointer == NULL || pointer->kind != MINIC_CORE_TYPE_POINTER ||
                minic_core_type_unqualified(function->type_context,
                                            pointer->value.pointer.pointee) !=
                    minic_core_type_unqualified(function->type_context,
                        instruction->value.field_address.core_record_type_id)) return false;
        } else if (instruction->kind == MINIC_CORE_INSTRUCTION_CALL) {
            const MinicCoreCallee *callee;
            if (instruction->value.call.callee_id >= function->callee_count) return false;
            callee = &function->callees[instruction->value.call.callee_id];
            if (instruction->core_type_id != callee->core_return_type_id ||
                !core_snapshot_fixed_record_arguments_valid(
                    function, instruction->value.call.argument_begin,
                    instruction->value.call.argument_count, callee->core_parameter_type_ids,
                    callee->parameter_count)) return false;
        } else if (instruction->kind == MINIC_CORE_INSTRUCTION_INDIRECT_CALL) {
            const MinicCoreCallSignature *signature;
            if (!core_snapshot_indirect_callee_valid(function, instruction)) return false;
            signature = &function->call_signatures[
                instruction->value.indirect_call.signature_id];
            if (!core_snapshot_fixed_record_arguments_valid(
                function, instruction->value.indirect_call.argument_begin,
                instruction->value.indirect_call.argument_count,
                signature->core_parameter_type_ids, signature->parameter_count)) return false;
        }
    }
    return true;
}

/* The verifier consumes frozen, context-local CoreType identities. Source
 * enum ranks, C tags, and MinicType descriptors are not verifier inputs. */
static MinicCoreTypeId core_unqualified(const MinicCoreFunction *function,
                                       MinicCoreTypeId id) {
    return minic_core_type_unqualified(function->type_context, id);
}

static const MinicCoreType *core_type(const MinicCoreFunction *function,
                                      MinicCoreTypeId id) {
    return core_snapshot_base_type(function, id);
}

static bool core_is(const MinicCoreFunction *function, MinicCoreTypeId id,
                    MinicCoreTypeKind kind) {
    const MinicCoreType *type = core_type(function, id);
    return type != NULL && type->kind == kind;
}

static bool core_is_float_format(const MinicCoreFunction *function,
                                 MinicCoreTypeId id, MinicCoreFloatFormat format) {
    const MinicCoreType *type = core_type(function, id);
    return type != NULL && type->kind == MINIC_CORE_TYPE_FLOAT &&
           type->value.floating.format == format;
}

static bool core_is_integer(const MinicCoreFunction *function, MinicCoreTypeId id) {
    return core_is(function, id, MINIC_CORE_TYPE_INTEGER);
}

static bool core_is_pointer(const MinicCoreFunction *function, MinicCoreTypeId id) {
    return core_is(function, id, MINIC_CORE_TYPE_POINTER);
}

static bool core_is_record(const MinicCoreFunction *function, MinicCoreTypeId id) {
    return core_is(function, id, MINIC_CORE_TYPE_RECORD);
}

static bool core_is_void(const MinicCoreFunction *function, MinicCoreTypeId id) {
    return core_is(function, id, MINIC_CORE_TYPE_VOID);
}

static bool core_is_function(const MinicCoreFunction *function, MinicCoreTypeId id) {
    return core_is(function, id, MINIC_CORE_TYPE_FUNCTION);
}

static bool core_is_vector(const MinicCoreFunction *function, MinicCoreTypeId id) {
    const MinicCoreType *type = core_type(function, id);
    return type != NULL && type->kind == MINIC_CORE_TYPE_VECTOR && type->has_layout &&
           type->size == 16U;
}

static bool core_is_unsigned_integer(const MinicCoreFunction *function,
                                     MinicCoreTypeId id) {
    const MinicCoreType *type = core_type(function, id);
    return type != NULL && type->kind == MINIC_CORE_TYPE_INTEGER &&
           !type->value.integer.is_signed;
}

static bool core_is_writable(const MinicCoreFunction *function, MinicCoreTypeId id) {
    const MinicCoreType *type = minic_core_function_type_get(function, id);
    return type != NULL && !type->is_read_only;
}

static bool core_is_volatile(const MinicCoreFunction *function, MinicCoreTypeId id) {
    const MinicCoreType *type = minic_core_function_type_get(function, id);
    return type != NULL && type->is_volatile;
}

static bool core_equal(MinicCoreTypeId a, MinicCoreTypeId b) { return a == b; }

static bool core_equal_unqualified(const MinicCoreFunction *function,
                                   MinicCoreTypeId a, MinicCoreTypeId b) {
    return core_unqualified(function, a) == core_unqualified(function, b);
}

static bool core_pointee(const MinicCoreFunction *function, MinicCoreTypeId id,
                         MinicCoreTypeId *out) {
    const MinicCoreType *type = core_type(function, id);
    if (type == NULL || type->kind != MINIC_CORE_TYPE_POINTER || out == NULL) return false;
    *out = type->value.pointer.pointee;
    return core_snapshot_exists(function, *out);
}

static bool core_is_scalar(const MinicCoreFunction *function, MinicCoreTypeId id) {
    const MinicCoreType *type = core_type(function, id);
    return type != NULL && (type->kind == MINIC_CORE_TYPE_INTEGER ||
                            type->kind == MINIC_CORE_TYPE_POINTER ||
                            (type->kind == MINIC_CORE_TYPE_FLOAT &&
                             (type->value.floating.format == MINIC_CORE_FLOAT_BINARY32 ||
                              type->value.floating.format == MINIC_CORE_FLOAT_BINARY64 ||
                              type->value.floating.format == MINIC_CORE_FLOAT_EXTENDED)));
}

static bool core_call_type(const MinicCoreFunction *function, MinicCoreTypeId id) {
    return core_is_scalar(function, id) || core_is_record(function, id);
}

static bool core_call_return_type(const MinicCoreFunction *function, MinicCoreTypeId id) {
    return core_is_void(function, id) || core_call_type(function, id);
}

static bool instruction_result_is_valid(const MinicCoreFunction *function,
                                        const MinicCoreInstruction *instruction) {
    return instruction->result < function->value_count &&
           core_equal(function->values[instruction->result].core_type_id,
                      instruction->core_type_id);
}

static bool available_pointer_pointee(const MinicCoreFunction *function,
                                      const bool *available_values,
                                      MinicCoreValueId address,
                                      MinicCoreTypeId *pointee) {
    return address < function->value_count && available_values[address] &&
           core_pointee(function, function->values[address].core_type_id, pointee);
}

/* The Core vector has a final 16-byte layout and a typed element. Source
 * ACLE memory forms may address a whole vector or an 8-bit storage carrier,
 * except uint64x2 which admits a full 64-bit unsigned scalar carrier.
 * UINT8X16 and POLY8X16 have identical Core execution layout; their source
 * signed-char admission difference must be settled by typed Source/Semantic
 * before this source-independent verifier runs. */
static bool vector128_memory_pointee(const MinicCoreFunction *function,
                                     MinicCoreTypeId vector, MinicCoreTypeId pointee) {
    const MinicCoreType *vec = core_type(function, vector);
    const MinicCoreType *element;
    const MinicCoreType *mem;
    if (!core_is_vector(function, vector)) return false;
    if (core_equal_unqualified(function, vector, pointee)) return true;
    element = core_type(function, vec->value.vector.element);
    mem = core_type(function, pointee);
    if (element == NULL || mem == NULL || mem->kind != MINIC_CORE_TYPE_INTEGER) return false;
    if (element->kind == MINIC_CORE_TYPE_INTEGER &&
        element->value.integer.bit_width == 64U &&
        !element->value.integer.is_signed && vec->value.vector.element_count == 2U)
        return mem->value.integer.bit_width == 64U && !mem->value.integer.is_signed;
    return mem->value.integer.bit_width == 8U;
}

static bool core_unqualified_out(const MinicCoreFunction *function, MinicCoreTypeId id,
                                 MinicCoreTypeId *out) {
    if (out == NULL) return false;
    *out = core_unqualified(function, id);
    return *out != MINIC_CORE_TYPE_INVALID;
}

static bool core_is_bool(const MinicCoreFunction *function, MinicCoreTypeId id) {
    const MinicCoreType *type = core_type(function, id);
    return type != NULL && type->kind == MINIC_CORE_TYPE_INTEGER &&
           type->value.integer.is_bool;
}

static bool core_is_builtin_vector(const MinicCoreFunction *function, MinicCoreTypeId id) {
    const MinicCoreType *type = core_type(function, id);
    return core_is_vector(function, id) && type->value.vector.is_builtin;
}

static bool core_is_gnu_vector(const MinicCoreFunction *function, MinicCoreTypeId id) {
    const MinicCoreType *type = core_type(function, id);
    return core_is_vector(function, id) && !type->value.vector.is_builtin;
}

static bool core_is_int(const MinicCoreFunction *function, MinicCoreTypeId id) {
    const MinicCoreType *type = core_type(function, id);
    return type != NULL && type->kind == MINIC_CORE_TYPE_INTEGER &&
           type->value.integer.is_signed && type->value.integer.bit_width == 32U &&
           !type->value.integer.is_bool;
}

static bool core_is_unsigned_long_for_pointer(const MinicCoreFunction *function,
                                              MinicCoreTypeId integer,
                                              MinicCoreTypeId pointer) {
    const MinicCoreType *int_type = core_type(function, integer);
    const MinicCoreType *pointer_type = core_type(function, pointer);
    return int_type != NULL && pointer_type != NULL &&
           int_type->kind == MINIC_CORE_TYPE_INTEGER &&
           pointer_type->kind == MINIC_CORE_TYPE_POINTER &&
           !int_type->value.integer.is_signed && !int_type->value.integer.is_bool &&
           int_type->value.integer.bit_width == pointer_type->size * 8U;
}

static bool core_bitcast_types_valid(const MinicCoreFunction *function,
                                     MinicCoreTypeId target, MinicCoreTypeId source) {
    return (core_is_pointer(function, target) &&
            (core_is_pointer(function, source) || core_is_integer(function, source))) ||
           (core_is_integer(function, target) && core_is_pointer(function, source));
}

/* Typed accessors deliberately require a `function` in scope. They keep the
 * exhaustive opcode checker readable while every predicate refers exclusively
 * to the sealed Core type arena, never to source-language descriptors. */
#define ct_is_integer(id) core_is_integer(function, (id))
#define ct_is_unsigned_integer(id) core_is_unsigned_integer(function, (id))
#define ct_is_bool_integer(id) core_is_bool(function, (id))
#define ct_is_pointer(id) core_is_pointer(function, (id))
#define ct_is_record(id) core_is_record(function, (id))
#define ct_is_void(id) core_is_void(function, (id))
#define ct_is_function(id) core_is_function(function, (id))
#define ct_is_float(id) core_is_float_format(function, (id), MINIC_CORE_FLOAT_BINARY32)
#define ct_is_double(id) core_is_float_format(function, (id), MINIC_CORE_FLOAT_BINARY64)
#define ct_is_long_double(id) core_is_float_format(function, (id), MINIC_CORE_FLOAT_EXTENDED)
#define ct_is_aarch64_fixed_vector128(id) core_is_builtin_vector(function, (id))
#define ct_is_gnu_fixed_vector(id) core_is_gnu_vector(function, (id))
#define ct_is_const(id) (!core_is_writable(function, (id)))
#define ct_is_volatile(id) core_is_volatile(function, (id))
#define ct_equal(a, b) core_equal((a), (b))
#define ct_unqualified(id, out) core_unqualified_out(function, (id), (out))
#define ct_pointee(id, out) core_pointee(function, (id), (out))
#define ct_call_scalar_type(id) core_is_scalar(function, (id))
#define ct_call_parameter_type(id) core_call_type(function, (id))
#define ct_direct_call_return_type(id) core_call_return_type(function, (id))

static bool instruction_is_valid(const MinicCoreFunction *function,
                                 const MinicCoreInstruction *instruction,
                                 const bool *available_values) {
    const MinicCoreValue *left;
    const MinicCoreValue *right;

    if (function == NULL || instruction == NULL) {
        return false;
    }
    switch (instruction->kind) {
    case MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT:
        return instruction_result_is_valid(function, instruction) &&
               ct_is_integer(instruction->core_type_id);
    case MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT:
        return instruction_result_is_valid(function, instruction) &&
               (ct_is_float(instruction->core_type_id) ||
                ct_is_double(instruction->core_type_id) ||
                ct_is_long_double(instruction->core_type_id));
    case MINIC_CORE_INSTRUCTION_FLOAT_ADD:
    case MINIC_CORE_INSTRUCTION_FLOAT_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_FLOAT_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_FLOAT_DIVIDE:
        if (!instruction_result_is_valid(function, instruction) ||
            !ct_is_float(instruction->core_type_id) ||
            instruction->value.binary.left >= function->value_count ||
            instruction->value.binary.right >= function->value_count ||
            !available_values[instruction->value.binary.left] ||
            !available_values[instruction->value.binary.right]) {
            return false;
        }
        left = &function->values[instruction->value.binary.left];
        right = &function->values[instruction->value.binary.right];
        return ct_is_float(left->core_type_id) &&
               ct_equal(left->core_type_id, instruction->core_type_id) &&
               ct_equal(right->core_type_id, instruction->core_type_id);
    case MINIC_CORE_INSTRUCTION_DOUBLE_ADD:
    case MINIC_CORE_INSTRUCTION_DOUBLE_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_DOUBLE_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_DOUBLE_DIVIDE:
        if (!instruction_result_is_valid(function, instruction) ||
            !ct_is_double(instruction->core_type_id) ||
            instruction->value.binary.left >= function->value_count ||
            instruction->value.binary.right >= function->value_count ||
            !available_values[instruction->value.binary.left] ||
            !available_values[instruction->value.binary.right]) {
            return false;
        }
        left = &function->values[instruction->value.binary.left];
        right = &function->values[instruction->value.binary.right];
        return ct_is_double(left->core_type_id) &&
               ct_equal(left->core_type_id, instruction->core_type_id) &&
               ct_equal(right->core_type_id, instruction->core_type_id);
    case MINIC_CORE_INSTRUCTION_DOUBLE_EQUAL:
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS:
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS_EQUAL:
        if (!instruction_result_is_valid(function, instruction) ||
            !core_is_int(function, instruction->core_type_id) ||
            instruction->value.binary.left >= function->value_count ||
            instruction->value.binary.right >= function->value_count ||
            !available_values[instruction->value.binary.left] ||
            !available_values[instruction->value.binary.right]) {
            return false;
        }
        left = &function->values[instruction->value.binary.left];
        right = &function->values[instruction->value.binary.right];
        return ct_is_double(left->core_type_id) &&
               ct_equal(left->core_type_id, right->core_type_id);
    case MINIC_CORE_INSTRUCTION_INTEGER_ADD:
    case MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE:
    case MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR:
        if (!instruction_result_is_valid(function, instruction) ||
            !ct_is_integer(instruction->core_type_id) ||
            instruction->value.binary.left >= function->value_count ||
            instruction->value.binary.right >= function->value_count ||
            !available_values[instruction->value.binary.left] ||
            !available_values[instruction->value.binary.right]) {
            return false;
        }
        left = &function->values[instruction->value.binary.left];
        right = &function->values[instruction->value.binary.right];
        return ct_equal(left->core_type_id, instruction->core_type_id) &&
               ct_equal(right->core_type_id, instruction->core_type_id);
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT:
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT:
        if (!instruction_result_is_valid(function, instruction) ||
            !ct_is_integer(instruction->core_type_id) ||
            instruction->value.binary.left >= function->value_count ||
            instruction->value.binary.right >= function->value_count ||
            !available_values[instruction->value.binary.left] ||
            !available_values[instruction->value.binary.right]) {
            return false;
        }
        left = &function->values[instruction->value.binary.left];
        right = &function->values[instruction->value.binary.right];
        return ct_equal(left->core_type_id, instruction->core_type_id) &&
               ct_is_integer(right->core_type_id);
    case MINIC_CORE_INSTRUCTION_INTEGER_LESS:
        if (!instruction_result_is_valid(function, instruction) ||
            !core_is_int(function, instruction->core_type_id) ||
            instruction->value.binary.left >= function->value_count ||
            instruction->value.binary.right >= function->value_count ||
            !available_values[instruction->value.binary.left] ||
            !available_values[instruction->value.binary.right]) {
            return false;
        }
        left = &function->values[instruction->value.binary.left];
        right = &function->values[instruction->value.binary.right];
        return ct_is_integer(left->core_type_id) && ct_equal(left->core_type_id, right->core_type_id);
    case MINIC_CORE_INSTRUCTION_POINTER_LESS:
        if (!instruction_result_is_valid(function, instruction) ||
            !core_is_int(function, instruction->core_type_id) ||
            instruction->value.binary.left >= function->value_count ||
            instruction->value.binary.right >= function->value_count ||
            !available_values[instruction->value.binary.left] ||
            !available_values[instruction->value.binary.right]) {
            return false;
        }
        left = &function->values[instruction->value.binary.left];
        right = &function->values[instruction->value.binary.right];
        return ct_is_pointer(left->core_type_id) && ct_equal(left->core_type_id, right->core_type_id);
    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:
        if (!instruction_result_is_valid(function, instruction) ||
            !core_is_int(function, instruction->core_type_id) ||
            instruction->value.binary.left >= function->value_count ||
            instruction->value.binary.right >= function->value_count ||
            !available_values[instruction->value.binary.left] ||
            !available_values[instruction->value.binary.right]) {
            return false;
        }
        left = &function->values[instruction->value.binary.left];
        right = &function->values[instruction->value.binary.right];
        return (ct_is_integer(left->core_type_id) || ct_is_pointer(left->core_type_id)) &&
               ct_equal(left->core_type_id, right->core_type_id);
    case MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW: {
        MinicCoreTypeId result_type;

        if (!instruction_result_is_valid(function, instruction) ||
            !core_is_bool(function, instruction->core_type_id) ||
            (instruction->value.integer_overflow.operator_kind != MINIC_CORE_INTEGER_OVERFLOW_ADD &&
             instruction->value.integer_overflow.operator_kind !=
                 MINIC_CORE_INTEGER_OVERFLOW_SUBTRACT &&
             instruction->value.integer_overflow.operator_kind !=
                 MINIC_CORE_INTEGER_OVERFLOW_MULTIPLY) ||
            instruction->value.integer_overflow.left >= function->value_count ||
            instruction->value.integer_overflow.right >= function->value_count ||
            instruction->value.integer_overflow.result_address >= function->value_count ||
            !available_values[instruction->value.integer_overflow.left] ||
            !available_values[instruction->value.integer_overflow.right] ||
            !available_values[instruction->value.integer_overflow.result_address] ||
            !ct_pointee(
                function->values[instruction->value.integer_overflow.result_address].core_type_id,
                &result_type) ||
            !ct_is_integer(result_type) || ct_is_bool_integer(result_type) ||
            ct_is_const(result_type) || ct_is_volatile(result_type)) {
            return false;
        }
        left = &function->values[instruction->value.integer_overflow.left];
        right = &function->values[instruction->value.integer_overflow.right];
        return ct_is_integer(left->core_type_id) && ct_is_integer(right->core_type_id);
    }
    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION:
        return instruction_result_is_valid(function, instruction) &&
               ct_is_integer(instruction->core_type_id) &&
               instruction->value.operand < function->value_count &&
               available_values[instruction->value.operand] &&
               ct_is_integer(function->values[instruction->value.operand].core_type_id);
    case MINIC_CORE_INSTRUCTION_INTEGER_TO_DOUBLE:
        return instruction_result_is_valid(function, instruction) &&
               ct_is_double(instruction->core_type_id) &&
               instruction->value.operand < function->value_count &&
               available_values[instruction->value.operand] &&
               ct_is_integer(function->values[instruction->value.operand].core_type_id);
    case MINIC_CORE_INSTRUCTION_FLOAT_TO_DOUBLE:
        return instruction_result_is_valid(function, instruction) &&
               ct_is_double(instruction->core_type_id) &&
               instruction->value.operand < function->value_count &&
               available_values[instruction->value.operand] &&
               ct_is_float(function->values[instruction->value.operand].core_type_id);
    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_FLOAT:
        return instruction_result_is_valid(function, instruction) &&
               ct_is_float(instruction->core_type_id) &&
               instruction->value.operand < function->value_count &&
               available_values[instruction->value.operand] &&
               ct_is_double(function->values[instruction->value.operand].core_type_id);
    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_INTEGER:
        return instruction_result_is_valid(function, instruction) &&
               ct_is_integer(instruction->core_type_id) &&
               instruction->value.operand < function->value_count &&
               available_values[instruction->value.operand] &&
               ct_is_double(function->values[instruction->value.operand].core_type_id);
    case MINIC_CORE_INSTRUCTION_SCALAR_BITCAST:
        return instruction_result_is_valid(function, instruction) &&
               instruction->value.operand < function->value_count &&
               available_values[instruction->value.operand] &&
               core_bitcast_types_valid(function,
                   instruction->core_type_id, function->values[instruction->value.operand].core_type_id);
    case MINIC_CORE_INSTRUCTION_DOUBLE_NEGATE:
        return instruction_result_is_valid(function, instruction) &&
               ct_is_double(instruction->core_type_id) &&
               instruction->value.operand < function->value_count &&
               available_values[instruction->value.operand] &&
               ct_equal(function->values[instruction->value.operand].core_type_id,
                                instruction->core_type_id);
    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT:
        return instruction_result_is_valid(function, instruction) &&
               ct_is_integer(instruction->core_type_id) &&
               instruction->value.operand < function->value_count &&
               available_values[instruction->value.operand] &&
               ct_equal(function->values[instruction->value.operand].core_type_id,
                                instruction->core_type_id);
    case MINIC_CORE_INSTRUCTION_INTEGER_CLZ:
    case MINIC_CORE_INSTRUCTION_INTEGER_CTZ:
        return instruction_result_is_valid(function, instruction) &&
               core_is_int(function, instruction->core_type_id) &&
               instruction->value.operand < function->value_count &&
               available_values[instruction->value.operand] &&
               ct_is_unsigned_integer(
                   function->values[instruction->value.operand].core_type_id);
    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO:
        return instruction_result_is_valid(function, instruction) &&
               core_is_int(function, instruction->core_type_id) &&
               instruction->value.operand < function->value_count &&
               available_values[instruction->value.operand] &&
               (ct_is_integer(function->values[instruction->value.operand].core_type_id) ||
                ct_is_pointer(function->values[instruction->value.operand].core_type_id));
    /* M79_CALL_FRAME_RETURN_ADDRESS: Core validates the semantic shape only.
       Backend support for a particular kind/level pair is a target concern. */
    case MINIC_CORE_INSTRUCTION_CALL_FRAME_ADDRESS: {
        MinicCoreTypeId pointee;

        return instruction_result_is_valid(function, instruction) &&
               (instruction->value.call_frame_address.kind ==
                    MINIC_CORE_CALL_FRAME_ADDRESS_RETURN ||
                instruction->value.call_frame_address.kind ==
                    MINIC_CORE_CALL_FRAME_ADDRESS_FRAME) &&
               instruction->value.call_frame_address.level <= 1U &&
               (instruction->value.call_frame_address.level == 0U ||
                instruction->value.call_frame_address.kind ==
                    MINIC_CORE_CALL_FRAME_ADDRESS_FRAME) &&
               ct_pointee(instruction->core_type_id, &pointee) && ct_is_void(pointee);
    }
    /* M123_VARIADIC_ARGUMENT_ADDRESS: Core validates only that the semantic
       cursor is represented as a pointer value. Whether a target ABI can
       materialize it is a backend capability question. */
    case MINIC_CORE_INSTRUCTION_VARIADIC_ARGUMENT_ADDRESS:
        return instruction_result_is_valid(function, instruction) &&
               ct_is_pointer(instruction->core_type_id);
    case MINIC_CORE_INSTRUCTION_VARIADIC_COPY: {
        const MinicCoreValueId dst = instruction->value.variadic_copy.destination;
        const MinicCoreValueId src = instruction->value.variadic_copy.source;
        return instruction->result == MINIC_CORE_VALUE_INVALID &&
               ct_is_void(instruction->core_type_id) &&
               dst < function->value_count && src < function->value_count &&
               available_values[dst] && available_values[src] &&
               ct_is_pointer(function->values[dst].core_type_id) &&
               ct_equal(function->values[dst].core_type_id,
                                function->values[src].core_type_id);
    }
    case MINIC_CORE_INSTRUCTION_VARIADIC_NEXT_SCALAR: {
        const MinicCoreValueId list = instruction->value.operand;
        return instruction_result_is_valid(function, instruction) &&
               (ct_is_integer(instruction->core_type_id) ||
                ct_is_pointer(instruction->core_type_id) ||
                ct_is_double(instruction->core_type_id)) &&
               list < function->value_count && available_values[list] &&
               ct_is_pointer(function->values[list].core_type_id);
    }
    case MINIC_CORE_INSTRUCTION_DYNAMIC_STACK_ALLOC: {
        MinicCoreValueId size_value = instruction->value.operand;
        return instruction_result_is_valid(function, instruction) &&
               ct_is_pointer(instruction->core_type_id) &&
               size_value < function->value_count && available_values[size_value] &&
               core_is_unsigned_long_for_pointer(
                   function, function->values[size_value].core_type_id,
                   instruction->core_type_id);
    }
    case MINIC_CORE_INSTRUCTION_PARAMETER:
        return instruction_result_is_valid(function, instruction) &&
               instruction->value.parameter_index < function->parameter_count &&
               ct_equal(function->core_parameter_type_ids[instruction->value.parameter_index],
                                instruction->core_type_id);
    case MINIC_CORE_INSTRUCTION_FIXED_REGISTER_READ: {
        size_t binding_id = instruction->value.fixed_register_binding_id;

        return instruction_result_is_valid(function, instruction) &&
               binding_id < function->fixed_register_binding_count &&
               function->fixed_register_bindings[binding_id].register_name != NULL &&
               function->fixed_register_bindings[binding_id].register_name_length != 0U &&
               ct_equal(function->fixed_register_bindings[binding_id].core_type_id,
                                instruction->core_type_id);
    }
    case MINIC_CORE_INSTRUCTION_FIXED_REGISTER_WRITE: {
        const size_t id = instruction->value.fixed_register_write.binding_id;
        const MinicCoreValueId source = instruction->value.fixed_register_write.source;
        const MinicCoreFixedRegisterBinding *binding;
        if (instruction->result != MINIC_CORE_VALUE_INVALID ||
            !ct_is_void(instruction->core_type_id) ||
            id >= function->fixed_register_binding_count ||
            source >= function->value_count || !available_values[source])
            return false;
        binding = &function->fixed_register_bindings[id];
        return !binding->is_local && binding->register_name != NULL &&
               binding->register_name_length != 0U &&
               ct_equal(binding->core_type_id, function->values[source].core_type_id) &&
               (ct_is_integer(binding->core_type_id) ||
                ct_is_pointer(binding->core_type_id));
    }
    case MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT: {
        MinicCoreObjectId object_id;
        MinicCoreTypeId object_value_type;
        size_t parameter_index;

        object_id = instruction->value.parameter_object.object_id;
        parameter_index = instruction->value.parameter_object.parameter_index;
        return instruction->result == MINIC_CORE_VALUE_INVALID &&
               ct_is_void(instruction->core_type_id) &&
               parameter_index < function->parameter_count && object_id < function->object_count &&
               ct_is_record(function->core_parameter_type_ids[parameter_index]) &&
               ct_unqualified(function->objects[object_id].core_type_id, &object_value_type) &&
               ct_equal(function->core_parameter_type_ids[parameter_index], object_value_type);
    }
    case MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS: {
        MinicCoreTypeId pointee;

        if (!instruction_result_is_valid(function, instruction) ||
            instruction->value.object_id >= function->object_count ||
            !ct_pointee(instruction->core_type_id, &pointee)) {
            return false;
        }
        return ct_equal(pointee,
                        function->objects[instruction->value.object_id].core_type_id);
    }
    /* M64_LOCAL_LABEL_BLOCK_ADDRESS: block addresses are first-class pointer values. */
    case MINIC_CORE_INSTRUCTION_BLOCK_ADDRESS:
        return instruction_result_is_valid(function, instruction) &&
               ct_is_pointer(instruction->core_type_id) &&
               instruction->value.block_id < function->block_count;
    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS: {
        MinicCoreTypeId pointee;

        if (!instruction_result_is_valid(function, instruction) ||
            instruction->value.global_id >= function->global_count ||
            !ct_pointee(instruction->core_type_id, &pointee)) {
            return false;
        }
        return ct_equal(pointee,
                        function->globals[instruction->value.global_id].core_type_id);
    }
    case MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS: {
        MinicCoreTypeId function_type;
        MinicCoreFunctionSymbolId symbol_id;

        symbol_id = instruction->value.function_symbol_id;
        return instruction_result_is_valid(function, instruction) &&
               symbol_id < function->function_symbol_count &&
               function->function_symbols[symbol_id].name != NULL &&
               function->function_symbols[symbol_id].name_length != 0U &&
               ct_pointee(instruction->core_type_id, &function_type) &&
               ct_is_function(function_type);
    }
    case MINIC_CORE_INSTRUCTION_FIELD_ADDRESS: {
        MinicCoreValueId base;
        MinicCoreTypeId base_pointee;
        MinicCoreTypeId field_type;
        const MinicCoreType *record;
        const MinicCoreRecordField *field;
        const MinicCoreType *field_record_type;
        const MinicCoreType *storage_type;

        base = instruction->value.field_address.base;
        if (!instruction_result_is_valid(function, instruction) || base >= function->value_count ||
            !available_values[base] ||
            !ct_pointee(function->values[base].core_type_id, &base_pointee) ||
            !ct_is_record(base_pointee) ||
            !core_equal_unqualified(function, base_pointee,
                                    instruction->value.field_address.core_record_type_id) ||
            !ct_pointee(instruction->core_type_id, &field_type) ||
            ct_is_void(field_type) || ct_is_function(field_type)) {
            return false;
        }
        record = core_type(function, base_pointee);
        if (record == NULL || !record->value.record.is_complete ||
            instruction->value.field_address.field_index >= record->value.record.field_count ||
            record->value.record.fields == NULL) return false;
        field = &record->value.record.fields[instruction->value.field_address.field_index];
        if (field->byte_offset != instruction->value.field_address.byte_offset) return false;
        switch (instruction->value.field_address.access_form) {
        case MINIC_CORE_FIELD_ACCESS_EXACT:
            return !field->is_bit_field &&
                   core_equal_unqualified(function, field_type, field->type_id);
        case MINIC_CORE_FIELD_ACCESS_ARRAY_ELEMENT:
            if (field->is_bit_field) return false;
            field_record_type = core_type(function, field->type_id);
            return field_record_type != NULL &&
                   field_record_type->kind == MINIC_CORE_TYPE_ARRAY &&
                   core_equal_unqualified(function, field_type,
                                          field_record_type->value.array.element);
        case MINIC_CORE_FIELD_ACCESS_BITFIELD_STORAGE:
            storage_type = core_type(function, field_type);
            return field->is_bit_field && field->bit_width > 0U &&
                   field->bit_width <= 64U && field->bit_offset < 8U &&
                   field->byte_offset < record->size && storage_type != NULL &&
                   storage_type->kind == MINIC_CORE_TYPE_INTEGER &&
                   !storage_type->value.integer.is_signed &&
                   !storage_type->value.integer.is_bool &&
                   (storage_type->value.integer.bit_width == 8U ||
                    storage_type->value.integer.bit_width == 16U ||
                    storage_type->value.integer.bit_width == 32U ||
                    storage_type->value.integer.bit_width == 64U) &&
                   storage_type->has_layout &&
                   storage_type->size == storage_type->value.integer.bit_width / 8U;
        }
        return false;
    }
    case MINIC_CORE_INSTRUCTION_POINTER_OFFSET: {
        MinicCoreValueId base;
        MinicCoreValueId index;

        base = instruction->value.pointer_offset.base;
        index = instruction->value.pointer_offset.index;
        return instruction_result_is_valid(function, instruction) &&
               ct_is_pointer(instruction->core_type_id) && base < function->value_count &&
               index < function->value_count && available_values[base] && available_values[index] &&
               ct_equal(function->values[base].core_type_id, instruction->core_type_id) &&
               /* M131_ZERO_STRIDE_POINTER_OFFSET: zero is a valid GNU object
                  stride. Keeping POINTER_OFFSET rather than folding it in the
                  producer preserves index evaluation and gives all pointer
                  arithmetic producers one Core semantic owner. */
               ct_is_integer(function->values[index].core_type_id);
    }
    case MINIC_CORE_INSTRUCTION_LOAD: {
        MinicCoreTypeId pointee;
        MinicCoreTypeId value_type;

        if (!instruction_result_is_valid(function, instruction) ||
            !available_pointer_pointee(
                function, available_values, instruction->value.load.address, &pointee) ||
            !ct_unqualified(pointee, &value_type)) {
            return false;
        }
        return ct_equal(value_type, instruction->core_type_id) &&
               instruction->value.load.is_volatile == ct_is_volatile(pointee);
    }
    case MINIC_CORE_INSTRUCTION_ATOMIC_LOAD: {
        MinicCoreTypeId pointee, value_type;
        if (!instruction_result_is_valid(function, instruction) ||
            instruction->value.atomic_load.memory_order != 0U ||
            !available_pointer_pointee(
                function, available_values, instruction->value.atomic_load.address, &pointee) ||
            !ct_unqualified(pointee, &value_type) ||
            !(ct_is_integer(value_type) || ct_is_pointer(value_type))) return false;
        return ct_equal(value_type, instruction->core_type_id);
    }
    case MINIC_CORE_INSTRUCTION_ATOMIC_STORE: {
        MinicCoreTypeId pointee, stored_type;
        MinicCoreValueId value = instruction->value.atomic_store.stored_value;
        if (instruction->result != MINIC_CORE_VALUE_INVALID ||
            !ct_is_void(instruction->core_type_id) ||
            instruction->value.atomic_store.memory_order != 0U ||
            value >= function->value_count || !available_values[value] ||
            !available_pointer_pointee(
                function, available_values, instruction->value.atomic_store.address, &pointee) ||
            !ct_unqualified(pointee, &stored_type) ||
            !(ct_is_integer(stored_type) || ct_is_pointer(stored_type))) return false;
        return ct_equal(stored_type, function->values[value].core_type_id);
    }
    case MINIC_CORE_INSTRUCTION_STORE: {
        MinicCoreTypeId pointee;
        MinicCoreTypeId value_type;
        MinicCoreValueId stored_value;

        stored_value = instruction->value.store.stored_value;
        if (instruction->result != MINIC_CORE_VALUE_INVALID ||
            !ct_is_void(instruction->core_type_id) || stored_value >= function->value_count ||
            !available_values[stored_value] ||
            !available_pointer_pointee(
                function, available_values, instruction->value.store.address, &pointee) ||
            !ct_unqualified(pointee, &value_type)) {
            return false;
        }
        return ct_equal(value_type, function->values[stored_value].core_type_id) &&
               instruction->value.store.is_volatile == ct_is_volatile(pointee);
    }
    /* BATCH_M_RECORD_LOAD: source qualification is semantic metadata.  The
       destination is an unqualified private snapshot object. */
    case MINIC_CORE_INSTRUCTION_RECORD_LOAD: {
        MinicCoreObjectId destination_object;
        MinicCoreTypeId record_type;
        MinicCoreTypeId source_pointee;
        MinicCoreTypeId source_type;

        destination_object = instruction->value.record_load.destination_object;
        return instruction->result == MINIC_CORE_VALUE_INVALID &&
               ct_is_record(instruction->core_type_id) &&
               ct_unqualified(instruction->core_type_id, &record_type) &&
               ct_equal(record_type, instruction->core_type_id) &&
               destination_object < function->object_count &&
               ct_equal(function->objects[destination_object].core_type_id,
                                instruction->core_type_id) &&
               available_pointer_pointee(function,
                                         available_values,
                                         instruction->value.record_load.source_address,
                                         &source_pointee) &&
               ct_unqualified(source_pointee, &source_type) &&
               ct_equal(source_type, instruction->core_type_id) &&
               instruction->value.record_load.is_volatile ==
                   ct_is_volatile(source_pointee);
    }
    /* M80_ADDRESS_BACKED_RECORD_COPY: both SSA operands are addresses to the
       same unqualified record type; legality of writing a const-qualified
       destination is already established by the frontend initializer/copy node. */
    case MINIC_CORE_INSTRUCTION_RECORD_COPY: {
        MinicCoreTypeId destination_pointee;
        MinicCoreTypeId destination_type;
        MinicCoreTypeId source_pointee;
        MinicCoreTypeId source_type;
        MinicCoreTypeId record_type;

        return instruction->result == MINIC_CORE_VALUE_INVALID &&
               ct_is_record(instruction->core_type_id) &&
               ct_unqualified(instruction->core_type_id, &record_type) &&
               ct_equal(record_type, instruction->core_type_id) &&
               available_pointer_pointee(function,
                                         available_values,
                                         instruction->value.record_copy.destination_address,
                                         &destination_pointee) &&
               available_pointer_pointee(function,
                                         available_values,
                                         instruction->value.record_copy.source_address,
                                         &source_pointee) &&
               ct_unqualified(destination_pointee, &destination_type) &&
               ct_unqualified(source_pointee, &source_type) &&
               ct_equal(destination_type, instruction->core_type_id) &&
               ct_equal(source_type, instruction->core_type_id);
    }
    case MINIC_CORE_INSTRUCTION_GNU_FIXED_VECTOR_STORAGE_COPY: {
        MinicCoreTypeId destination_pointee, source_pointee;
        MinicCoreTypeId destination_type, source_type, unqualified_type;

        return instruction->result == MINIC_CORE_VALUE_INVALID &&
               ct_is_gnu_fixed_vector(instruction->core_type_id) &&
               ct_unqualified(instruction->core_type_id, &unqualified_type) &&
               ct_equal(unqualified_type, instruction->core_type_id) &&
               available_pointer_pointee(
                   function, available_values,
                   instruction->value.gnu_fixed_vector_storage_copy.destination_address,
                   &destination_pointee) &&
               available_pointer_pointee(
                   function, available_values,
                   instruction->value.gnu_fixed_vector_storage_copy.source_address,
                   &source_pointee) &&
               !ct_is_const(destination_pointee) &&
               !ct_is_volatile(destination_pointee) &&
               !ct_is_volatile(source_pointee) &&
               ct_unqualified(destination_pointee, &destination_type) &&
               ct_unqualified(source_pointee, &source_type) &&
               ct_equal(destination_type, instruction->core_type_id) &&
               ct_equal(source_type, instruction->core_type_id);
    }
    case MINIC_CORE_INSTRUCTION_FIXED_VECTOR128_LOAD: {
        MinicCoreTypeId pointee, unqualified;
        return instruction_result_is_valid(function, instruction) &&
               ct_is_aarch64_fixed_vector128(instruction->core_type_id) &&
               ct_unqualified(instruction->core_type_id, &unqualified) &&
               ct_equal(instruction->core_type_id, unqualified) &&
               available_pointer_pointee(function, available_values,
                   instruction->value.load.address, &pointee) &&
               vector128_memory_pointee(function, instruction->core_type_id, pointee) &&
               instruction->value.load.is_volatile == ct_is_volatile(pointee);
    }
    case MINIC_CORE_INSTRUCTION_FIXED_VECTOR128_STORE: {
        MinicCoreTypeId pointee;
        const MinicCoreValueId value = instruction->value.store.stored_value;
        return instruction->result == MINIC_CORE_VALUE_INVALID &&
               ct_is_void(instruction->core_type_id) &&
               value < function->value_count && available_values[value] &&
               ct_is_aarch64_fixed_vector128(function->values[value].core_type_id) &&
               available_pointer_pointee(function, available_values,
                   instruction->value.store.address, &pointee) &&
               !ct_is_const(pointee) &&
               vector128_memory_pointee(function, function->values[value].core_type_id,
                                        pointee) &&
               instruction->value.store.is_volatile == ct_is_volatile(pointee);
    }
    case MINIC_CORE_INSTRUCTION_FIXED_VECTOR128_XOR:
        if (!instruction_result_is_valid(function, instruction) ||
            !ct_is_aarch64_fixed_vector128(instruction->core_type_id) ||
            instruction->value.binary.left >= function->value_count ||
            instruction->value.binary.right >= function->value_count ||
            !available_values[instruction->value.binary.left] ||
            !available_values[instruction->value.binary.right]) return false;
        return ct_equal(
                   function->values[instruction->value.binary.left].core_type_id,
                   instruction->core_type_id) &&
               ct_equal(
                   function->values[instruction->value.binary.right].core_type_id,
                   instruction->core_type_id);
    case MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM: {
        const MinicCoreInlineAsm *inline_asm;

        if (instruction->result != MINIC_CORE_VALUE_INVALID ||
            !ct_is_void(instruction->core_type_id) ||
            instruction->value.inline_asm_id >= function->inline_asm_count) {
            return false;
        }
        inline_asm = &function->inline_asms[instruction->value.inline_asm_id];
        if (!inline_asm->template_text || !inline_asm->is_volatile)
            return false;
        /* Link-time symbolic immediates in qualified asm-goto are owned by
         * Core, regardless of whether the machine template has one or two
         * C labels. Every recorded slot must be a real, nonempty string;
         * partial materialization is not a valid target contract. */
        if (inline_asm->resolved_immediate_count != 0U) {
            size_t i;
            if (!inline_asm->is_goto ||
                inline_asm->goto_target >= function->block_count ||
                inline_asm->resolved_immediate_count > 3U)
                return false;
            for (i = 0U; i < inline_asm->resolved_immediate_count; ++i)
                if (!inline_asm->resolved_immediates[i] ||
                    inline_asm->resolved_immediates[i][0] == '\0')
                    return false;
        }
        if (inline_asm->goto_second_target != MINIC_CORE_BLOCK_INVALID) {
            if (!inline_asm->is_goto ||
                inline_asm->goto_target >= function->block_count ||
                inline_asm->goto_second_target >= function->block_count ||
                inline_asm->goto_target == inline_asm->goto_second_target ||
                inline_asm->resolved_immediate_count != 3U)
                return false;
        }
        return true;
    }
    case MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INLINE_ASM: {
        const MinicCoreInlineAsm *inline_asm;

        if (!instruction_result_is_valid(function, instruction) ||
            (!ct_is_integer(instruction->core_type_id) &&
             !ct_is_pointer(instruction->core_type_id)) ||
            instruction->value.inline_asm_id >= function->inline_asm_count) {
            return false;
        }
        inline_asm = &function->inline_asms[instruction->value.inline_asm_id];
        return inline_asm->template_text != NULL && inline_asm->template_length != 0U &&
               inline_asm->is_volatile;
    }
    case MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INPUT_INLINE_ASM: {
        const MinicCoreInlineAsm *inline_asm;
        MinicCoreValueId operand;

        operand = instruction->value.register_output_input_inline_asm.operand;
        if (!instruction_result_is_valid(function, instruction) ||
            (!ct_is_integer(instruction->core_type_id) &&
             !ct_is_pointer(instruction->core_type_id)) ||
            operand >= function->value_count || !available_values[operand] ||
            (!ct_is_integer(function->values[operand].core_type_id) &&
             !ct_is_pointer(function->values[operand].core_type_id)) ||
            instruction->value.register_output_input_inline_asm.inline_asm_id >=
                function->inline_asm_count) {
            return false;
        }
        inline_asm = &function->inline_asms[
            instruction->value.register_output_input_inline_asm.inline_asm_id];
        return inline_asm->template_text != NULL && inline_asm->template_length != 0U &&
               inline_asm->is_volatile;
    }
    case MINIC_CORE_INSTRUCTION_MEMORY_READWRITE_SCALAR_INPUT_INLINE_ASM: {
        const MinicCoreInlineAsm *inline_asm;
        MinicCoreValueId memory_address;
        MinicCoreValueId operand;
        MinicCoreTypeId pointee;
        size_t memory_index;
        size_t register_index;
        size_t scalar_index;
        bool has_register_output;

        memory_address = instruction->value.memory_readwrite_scalar_input_inline_asm.memory_address;
        operand = instruction->value.memory_readwrite_scalar_input_inline_asm.operand;
        memory_index = instruction->value.memory_readwrite_scalar_input_inline_asm.memory_operand_index;
        register_index =
            instruction->value.memory_readwrite_scalar_input_inline_asm.register_output_operand_index;
        scalar_index =
            instruction->value.memory_readwrite_scalar_input_inline_asm.scalar_input_operand_index;
        has_register_output = register_index != SIZE_MAX;
        if (memory_address >= function->value_count || operand >= function->value_count ||
            !available_values[memory_address] || !available_values[operand] ||
            !ct_pointee(function->values[memory_address].core_type_id, &pointee) ||
            (!ct_is_integer(pointee) && !ct_is_pointer(pointee)) ||
            (!ct_is_integer(function->values[operand].core_type_id) &&
             !ct_is_pointer(function->values[operand].core_type_id)) ||
            memory_index > 9U || scalar_index > 9U || memory_index == scalar_index ||
            (has_register_output &&
             (register_index > 9U || register_index == memory_index || register_index == scalar_index)) ||
            instruction->value.memory_readwrite_scalar_input_inline_asm.inline_asm_id >=
                function->inline_asm_count) {
            return false;
        }
        if (has_register_output) {
            if (!instruction_result_is_valid(function, instruction) ||
                (!ct_is_integer(instruction->core_type_id) &&
                 !ct_is_pointer(instruction->core_type_id))) {
                return false;
            }
        } else if (instruction->result != MINIC_CORE_VALUE_INVALID ||
                   !ct_is_void(instruction->core_type_id)) {
            return false;
        }
        inline_asm = &function->inline_asms[
            instruction->value.memory_readwrite_scalar_input_inline_asm.inline_asm_id];
        return inline_asm->template_text != NULL && inline_asm->template_length != 0U &&
               inline_asm->is_volatile && inline_asm->has_memory_clobber;
    }
    case MINIC_CORE_INSTRUCTION_SCALAR_INPUT_INLINE_ASM: {
        const MinicCoreInlineAsm *inline_asm;
        MinicCoreValueId operand;

        operand = instruction->value.scalar_input_inline_asm.operand;
        if (instruction->result != MINIC_CORE_VALUE_INVALID ||
            !ct_is_void(instruction->core_type_id) || operand >= function->value_count ||
            !available_values[operand] ||
            (!ct_is_integer(function->values[operand].core_type_id) &&
             !ct_is_pointer(function->values[operand].core_type_id)) ||
            instruction->value.scalar_input_inline_asm.inline_asm_id >=
                function->inline_asm_count) {
            return false;
        }
        inline_asm = &function->inline_asms[
            instruction->value.scalar_input_inline_asm.inline_asm_id];
        return inline_asm->template_text != NULL && inline_asm->template_length != 0U &&
               inline_asm->is_volatile;
    }
    /* M67_STRUCTURED_MULTI_OPERAND_INLINE_ASM: Core records operand roles and
       semantic values/addresses; target register assignment stays in the backend. */
    case MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM: {
        const MinicCoreInlineAsm *inline_asm;
        bool used_indices[MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT] = {false};
        bool has_memory_readwrite = false;
        size_t operand_index;

        if (instruction->result != MINIC_CORE_VALUE_INVALID ||
            !ct_is_void(instruction->core_type_id) ||
            instruction->value.structured_inline_asm.inline_asm_id >= function->inline_asm_count ||
            instruction->value.structured_inline_asm.operand_count >
                MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT) {
            return false;
        }
        inline_asm = &function->inline_asms[
            instruction->value.structured_inline_asm.inline_asm_id];
        if (inline_asm->template_text == NULL ||
            inline_asm->template_length == 0U || !inline_asm->is_volatile) {
            return false;
        }
        if (inline_asm->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_NONE &&
            (inline_asm->operand_plan_owner !=
                 MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED &&
             inline_asm->operand_plan_owner !=
                 MINIC_ASM_OPERAND_PLAN_GENERIC_STRUCTURED)) return false;
        if ((inline_asm->target_machine_form_id != 0U &&
             inline_asm->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED) ||
            (inline_asm->target_machine_form_id == 0U &&
             inline_asm->target_machine_form_variant != 0U)) return false;
        if (inline_asm->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_NONE &&
            (inline_asm->source_output_count >
                 instruction->value.structured_inline_asm.operand_count ||
             ((inline_asm->explicit_memory_clobber ||
               inline_asm->implicit_memory_effect) &&
              !inline_asm->has_memory_clobber))) return false;
        /* The C label is a real CFG destination even when the asm also
         * produces typed GNU register outputs. The same Core block IDs
         * are used by opaque and structured goto machine emitters. */
        if (inline_asm->is_goto &&
            (inline_asm->goto_target >= function->block_count ||
             inline_asm->goto_second_target != MINIC_CORE_BLOCK_INVALID))
            return false;
        /* Core only validates the ASM operand/effect shape. A target may
         * qualify a zero-operand instruction which clobbers exactly one
         * machine condition-code resource (x86 "cc"); its ISA emitter
         * revalidates that clobber and literal instruction. Other targets
         * retain their stricter target-only contracts. */
        if (instruction->value.structured_inline_asm.operand_count == 0U)
            return inline_asm->register_clobber_count <= 1U;
        for (operand_index = 0U;
             operand_index < instruction->value.structured_inline_asm.operand_count;
             ++operand_index) {
            const MinicCoreStructuredInlineAsmOperand *binding;
            MinicCoreTypeId pointee;
            MinicCoreTypeId value_type;

            binding = &instruction->value.structured_inline_asm.operands[operand_index];
            if (binding->operand_index >= MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT ||
                used_indices[binding->operand_index] ||
                binding->value >= function->value_count || !available_values[binding->value] ||
                (binding->has_fixed_register_binding &&
                 binding->fixed_register_binding_id >=
                     function->fixed_register_binding_count) ||
                (binding->early_clobber &&
                 binding->kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT &&
                 binding->kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE) ||
                (binding->gnu_early_clobber &&
                 binding->kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT &&
                 binding->kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE &&
                 binding->kind != MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT &&
                 binding->kind != MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE) ||
                (inline_asm->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_NONE &&
                 ((binding->gnu_early_clobber || binding->gnu_readwrite) &&
                  operand_index >= inline_asm->source_output_count)) ||
                (binding->has_tied_output &&
                 (inline_asm->operand_plan_owner == MINIC_ASM_OPERAND_PLAN_NONE ||
                  operand_index < inline_asm->source_output_count ||
                  binding->tied_output_index >= inline_asm->source_output_count))) {
                return false;
            }
            used_indices[binding->operand_index] = true;
            switch (binding->kind) {
            case MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:
            case MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE:
            case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT:
            case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT:
            case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE:
                if (!available_pointer_pointee(
                        function, available_values, binding->value, &pointee) ||
                    (binding->kind != MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT &&
                     ct_is_const(pointee)) ||
                    !ct_unqualified(pointee, &value_type) ||
                    /* A GNU memory operand denotes an addressable storage
                     * region, not necessarily a scalar value. Linux's
                     * __get_user/__put_user uses an intentionally opaque
                     * struct __large_struct memory carrier. Register roles
                     * still require a machine scalar; target admission
                     * determines whether the aggregate memory shape is
                     * legal for a particular instruction family. */
                    ((binding->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT ||
                      binding->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE) &&
                     !ct_is_integer(value_type) &&
                     !ct_is_pointer(value_type))) {
                    return false;
                }
                if (binding->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE) {
                    has_memory_readwrite = true;
                }
                break;
            case MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT:
                if (!ct_is_integer(function->values[binding->value].core_type_id) &&
                    !ct_is_pointer(function->values[binding->value].core_type_id)) {
                    return false;
                }
                break;
            default:
                return false;
            }
        }
        return !has_memory_readwrite || inline_asm->has_memory_clobber;
    }
    case MINIC_CORE_INSTRUCTION_COMPILER_BARRIER:
        return instruction->result == MINIC_CORE_VALUE_INVALID &&
               ct_is_void(instruction->core_type_id);
    case MINIC_CORE_INSTRUCTION_CALL: {
        const MinicCoreCallee *callee;
        size_t argument_index;
        size_t argument_end;
        bool returns_void;

        if (instruction->value.call.callee_id >= function->callee_count ||
            instruction->value.call.argument_begin > function->call_argument_count ||
            instruction->value.call.argument_count >
                function->call_argument_count - instruction->value.call.argument_begin) {
            return false;
        }
        callee = &function->callees[instruction->value.call.callee_id];
        if ((!callee->is_variadic &&
             instruction->value.call.argument_count != callee->parameter_count) ||
            (callee->is_variadic &&
             instruction->value.call.argument_count < callee->parameter_count) ||
            !ct_equal(instruction->core_type_id, callee->core_return_type_id)) {
            return false;
        }
        returns_void = ct_is_void(callee->core_return_type_id);
        if (returns_void) {
            if (instruction->result != MINIC_CORE_VALUE_INVALID ||
                instruction->value.call.result_object != MINIC_CORE_OBJECT_INVALID) {
                return false;
            }
        } else if (ct_is_record(callee->core_return_type_id)) {
            if (instruction->result != MINIC_CORE_VALUE_INVALID ||
                instruction->value.call.result_object >= function->object_count ||
                !ct_equal(
                    function->objects[instruction->value.call.result_object].core_type_id,
                    callee->core_return_type_id)) {
                return false;
            }
        } else if (!ct_call_scalar_type(callee->core_return_type_id) ||
                   instruction->value.call.result_object != MINIC_CORE_OBJECT_INVALID ||
                   !instruction_result_is_valid(function, instruction)) {
            return false;
        }
        argument_end =
            instruction->value.call.argument_begin + instruction->value.call.argument_count;
        for (argument_index = instruction->value.call.argument_begin; argument_index < argument_end;
             ++argument_index) {
            const MinicCoreCallArgument *argument;
            size_t parameter_index;

            argument = &function->call_arguments[argument_index];
            parameter_index = argument_index - instruction->value.call.argument_begin;
            if (parameter_index >= callee->parameter_count) {
                if (!callee->is_variadic) {
                    return false;
                }
                if (argument->kind == MINIC_CORE_CALL_ARGUMENT_VALUE) {
                    MinicCoreValueId value_id = argument->value.value_id;

                    if (value_id >= function->value_count || !available_values[value_id] ||
                        !ct_call_scalar_type(function->values[value_id].core_type_id)) {
                        return false;
                    }
                    continue;
                }
                if (argument->kind == MINIC_CORE_CALL_ARGUMENT_OBJECT) {
                    MinicCoreObjectId object_id = argument->value.object_id;

                    if (object_id >= function->object_count ||
                        !ct_is_record(function->objects[object_id].core_type_id)) {
                        return false;
                    }
                    continue;
                }
                return false;
            }
            {
                MinicCoreTypeId parameter_type = callee->core_parameter_type_ids[parameter_index];

                if (ct_call_scalar_type(parameter_type)) {
                    MinicCoreValueId value_id;

                    if (argument->kind != MINIC_CORE_CALL_ARGUMENT_VALUE) {
                        return false;
                    }
                    value_id = argument->value.value_id;
                    if (value_id >= function->value_count || !available_values[value_id] ||
                        !ct_equal(function->values[value_id].core_type_id, parameter_type)) {
                        return false;
                    }
                } else if (ct_is_record(parameter_type)) {
                    MinicCoreObjectId object_id;

                    if (argument->kind != MINIC_CORE_CALL_ARGUMENT_OBJECT) {
                        return false;
                    }
                    object_id = argument->value.object_id;
                    if (object_id >= function->object_count ||
                        !ct_equal(function->objects[object_id].core_type_id, parameter_type)) {
                        return false;
                    }
                } else {
                    return false;
                }
            }
        }
        return true;
    }
    case MINIC_CORE_INSTRUCTION_INDIRECT_CALL: {
        const MinicCoreCallSignature *signature;
        MinicCoreValueId callee_value;
        MinicCoreTypeId function_type;
        size_t argument_index;
        size_t argument_end;
        bool returns_void;

        callee_value = instruction->value.indirect_call.callee;
        if (instruction->value.indirect_call.signature_id >= function->call_signature_count ||
            callee_value >= function->value_count || !available_values[callee_value] ||
            !ct_pointee(function->values[callee_value].core_type_id, &function_type) ||
            !ct_is_function(function_type) ||
            instruction->value.indirect_call.argument_begin > function->call_argument_count ||
            instruction->value.indirect_call.argument_count >
                function->call_argument_count - instruction->value.indirect_call.argument_begin) {
            return false;
        }
        signature =
            &function->call_signatures[instruction->value.indirect_call.signature_id];
        if (!core_equal_unqualified(function, function_type,
                                    signature->core_function_type_id) ||
            (!signature->is_variadic &&
             instruction->value.indirect_call.argument_count != signature->parameter_count) ||
            (signature->is_variadic &&
             instruction->value.indirect_call.argument_count < signature->parameter_count) ||
            !ct_equal(instruction->core_type_id, signature->core_return_type_id)) {
            return false;
        }
        returns_void = ct_is_void(signature->core_return_type_id);
        if (returns_void) {
            if (instruction->result != MINIC_CORE_VALUE_INVALID ||
                instruction->value.indirect_call.result_object != MINIC_CORE_OBJECT_INVALID)
                return false;
        } else if (ct_is_record(signature->core_return_type_id)) {
            if (instruction->result != MINIC_CORE_VALUE_INVALID ||
                instruction->value.indirect_call.result_object >= function->object_count ||
                !ct_equal(
                    function->objects[instruction->value.indirect_call.result_object].core_type_id,
                    signature->core_return_type_id))
                return false;
        } else if (!ct_call_scalar_type(signature->core_return_type_id) ||
                   instruction->value.indirect_call.result_object != MINIC_CORE_OBJECT_INVALID ||
                   !instruction_result_is_valid(function, instruction)) {
            return false;
        }
        argument_end = instruction->value.indirect_call.argument_begin +
                       instruction->value.indirect_call.argument_count;
        for (argument_index = instruction->value.indirect_call.argument_begin;
             argument_index < argument_end;
             ++argument_index) {
            const MinicCoreCallArgument *argument;
            size_t parameter_index;

            argument = &function->call_arguments[argument_index];
            parameter_index =
                argument_index - instruction->value.indirect_call.argument_begin;
            if (parameter_index >= signature->parameter_count) {
                MinicCoreValueId value_id;

                if (!signature->is_variadic ||
                    argument->kind != MINIC_CORE_CALL_ARGUMENT_VALUE) {
                    return false;
                }
                value_id = argument->value.value_id;
                if (value_id >= function->value_count || !available_values[value_id] ||
                    !ct_call_scalar_type(function->values[value_id].core_type_id)) {
                    return false;
                }
                continue;
            }
            {
                MinicCoreTypeId parameter_type = signature->core_parameter_type_ids[parameter_index];

                if (ct_call_scalar_type(parameter_type)) {
                    MinicCoreValueId value_id;

                    if (argument->kind != MINIC_CORE_CALL_ARGUMENT_VALUE) {
                        return false;
                    }
                    value_id = argument->value.value_id;
                    if (value_id >= function->value_count || !available_values[value_id] ||
                        !ct_equal(function->values[value_id].core_type_id, parameter_type)) {
                        return false;
                    }
                } else if (ct_is_record(parameter_type)) {
                    MinicCoreObjectId object_id;

                    if (argument->kind != MINIC_CORE_CALL_ARGUMENT_OBJECT) {
                        return false;
                    }
                    object_id = argument->value.object_id;
                    if (object_id >= function->object_count ||
                        !ct_equal(function->objects[object_id].core_type_id, parameter_type)) {
                        return false;
                    }
                } else {
                    return false;
                }
            }
        }
        return true;
    }
    }
    return false;
}

static bool terminator_is_valid(const MinicCoreFunction *function,
                                const MinicCoreTerminator *terminator,
                                const bool *available_values) {
    if (function == NULL || terminator == NULL) {
        return false;
    }
    switch (terminator->kind) {
    case MINIC_CORE_TERMINATOR_RETURN:
        if (ct_is_void(function->core_return_type_id)) {
            return terminator->return_value == MINIC_CORE_VALUE_INVALID;
        }
        if (ct_is_record(function->core_return_type_id)) {
            MinicCoreTypeId result_type;
            /* Returning a const-qualified LOCAL record copies its value.
             * C discards top-level qualifiers on the returned rvalue, not
             * nested member or pointee qualifiers. The Core return object
             * can still be the original const local; require exact record
             * identity after removing only that top-level qualifier. */
            return terminator->return_value == MINIC_CORE_VALUE_INVALID &&
                   terminator->return_object < function->object_count &&
                   ct_unqualified(
                       function->objects[terminator->return_object].core_type_id,
                       &result_type) &&
                   ct_equal(result_type, function->core_return_type_id);
        }
        return terminator->return_value < function->value_count &&
               available_values[terminator->return_value] &&
               ct_equal(function->values[terminator->return_value].core_type_id,
                                function->core_return_type_id);
    case MINIC_CORE_TERMINATOR_UNREACHABLE:
        return terminator->return_value == MINIC_CORE_VALUE_INVALID &&
               terminator->return_object == MINIC_CORE_OBJECT_INVALID;
    case MINIC_CORE_TERMINATOR_BRANCH:
        return terminator->branch_target < function->block_count;
    /* M158_FINAL_STRICT_TAIL_INDIRECT_BRANCH_VERIFY: the source
       expression was accepted by GNU goto semantics; Core owns only
       the pointer-valued dynamic control-flow edge. */
    case MINIC_CORE_TERMINATOR_INDIRECT_BRANCH:
        return terminator->indirect_target < function->value_count &&
               available_values[terminator->indirect_target] &&
               ct_is_pointer(function->values[terminator->indirect_target].core_type_id);
    case MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH:
        return terminator->conditional.condition < function->value_count &&
               available_values[terminator->conditional.condition] &&
               ct_is_integer(function->values[terminator->conditional.condition].core_type_id) &&
               terminator->conditional.when_true < function->block_count &&
               terminator->conditional.when_false < function->block_count;
    }
    return false;
}

static bool verify_block(const MinicCoreFunction *function,
                         MinicCoreBlockId block_id,
                         bool *instruction_seen,
                         bool *value_seen,
                         bool *available_values) {
    const MinicCoreBlock *block;
    size_t index;

    block = &function->blocks[block_id];
    if (!storage_shape_is_valid(
            block->instructions, block->instruction_count, block->instruction_capacity) ||
        !block->has_terminator) {
        if (getenv("MINIC_CORE_VERIFY_TRACE") != NULL)
            (void)fprintf(stderr,
                "CORE_IR_VERIFY_FAIL reason=block-shape function=%s block=%" PRIu32
                " terminator=%d instructions=%zu capacity=%zu\n",
                function->name, block_id, block->has_terminator,
                block->instruction_count, block->instruction_capacity);
        return false;
    }
    if (function->value_count != 0U) {
        /* ValueIds are function-wide identities but uses must be available
         * within this block, in instruction order. Do not carry availability
         * from the preceding block: value_seen separately checks uniqueness
         * of definitions across the whole function. */
        (void)memset(available_values, 0, function->value_count * sizeof(*available_values));
    }
    for (index = 0U; index < block->instruction_count; ++index) {
        MinicCoreInstructionId instruction_id;
        const MinicCoreInstruction *instruction;

        instruction_id = block->instructions[index];
        if (instruction_id >= function->instruction_count || instruction_seen[instruction_id]) {
            if (getenv("MINIC_CORE_VERIFY_TRACE") != NULL)
                (void)fprintf(stderr,
                    "CORE_IR_VERIFY_FAIL reason=instruction-ownership function=%s "
                    "block=%" PRIu32 " instruction=%" PRIu32 "\n",
                    function->name, block_id, instruction_id);
            return false;
        }
        instruction = &function->instructions[instruction_id];
        if (!instruction_is_valid(function, instruction, available_values)) {
            /* Opt-in verifier diagnostics are emitted only on a rejected
             * instruction. Preserve the ordinary verifier's strict contract. */
            if (getenv("MINIC_CORE_VERIFY_TRACE") != NULL) {
                (void)fprintf(stderr,
                    "CORE_IR_VERIFY_FAIL reason=instruction function=%s "
                    "block=%" PRIu32 " id=%" PRIu32 " kind=%d "
                    "result=%" PRIu32 " span=%zu:%zu\n",
                    function->name, block_id, instruction_id,
                    (int)instruction->kind, instruction->result,
                    instruction->span.begin.line,
                    instruction->span.begin.column);
            }
            return false;
        }
        if (instruction->result != MINIC_CORE_VALUE_INVALID) {
            const MinicCoreValue *result;

            result = &function->values[instruction->result];
            if (value_seen[instruction->result] || result->definition != instruction_id) {
                return false;
            }
            available_values[instruction->result] = true;
            value_seen[instruction->result] = true;
        }
        instruction_seen[instruction_id] = true;
    }
    if (!terminator_is_valid(function, &block->terminator, available_values)) {
        if (getenv("MINIC_CORE_VERIFY_TRACE") != NULL)
            (void)fprintf(stderr,
                "CORE_IR_VERIFY_FAIL reason=terminator function=%s "
                "block=%" PRIu32 " kind=%d\n",
                function->name, block_id, (int)block->terminator.kind);
        return false;
    }
    return true;
}

bool minic_core_function_verify(const MinicCoreFunction *function) {
    bool *available_values;
    bool *instruction_seen;
    bool *value_seen;
    size_t block_index;
    size_t index;
    bool valid;

    if (function == NULL || function->name == NULL || function->name_length == 0U ||
        !minic_function_calling_convention_valid(function->calling_convention) ||
        (function->parameter_count != 0U && function->core_parameter_type_ids == NULL) ||
        !storage_shape_is_valid(function->fixed_register_bindings,
                                function->fixed_register_binding_count,
                                function->fixed_register_binding_capacity) ||
        !storage_shape_is_valid(function->function_symbols,
                                function->function_symbol_count,
                                function->function_symbol_capacity) ||
        !storage_shape_is_valid(
            function->globals, function->global_count, function->global_capacity) ||
        !storage_shape_is_valid(
            function->callees, function->callee_count, function->callee_capacity) ||
        !storage_shape_is_valid(function->call_signatures,
                                function->call_signature_count,
                                function->call_signature_capacity) ||
        !storage_shape_is_valid(
            function->inline_asms, function->inline_asm_count, function->inline_asm_capacity) ||
        !storage_shape_is_valid(function->call_arguments,
                                function->call_argument_count,
                                function->call_argument_capacity) ||
        !storage_shape_is_valid(
            function->objects, function->object_count, function->object_capacity) ||
        !storage_shape_is_valid(
            function->values, function->value_count, function->value_capacity) ||
        !storage_shape_is_valid(
            function->instructions, function->instruction_count, function->instruction_capacity) ||
        !storage_shape_is_valid(
            function->blocks, function->block_count, function->block_capacity) ||
        function->block_count == 0U || function->entry_block != 0U ||
        function->value_count > function->instruction_count) {
        return false;
    }
    /* No source-only fallback: the ingress first materializes its complete
     * CoreType snapshot, then validates this sealed function. */
    if (!minic_core_function_verify_core_type_snapshot(function) ||
        !core_call_return_type(function, function->core_return_type_id)) return false;
    for (index = 0U; index < function->parameter_count; ++index)
        if (!core_call_type(function, function->core_parameter_type_ids[index])) return false;
    for (index = 0U; index < function->fixed_register_binding_count; ++index) {
        const MinicCoreFixedRegisterBinding *binding = &function->fixed_register_bindings[index];
        if (binding->register_name == NULL || binding->register_name_length == 0U ||
            (!ct_is_integer(binding->core_type_id) &&
             !ct_is_pointer(binding->core_type_id) &&
             !ct_is_double(binding->core_type_id))) return false;
    }
    for (index = 0U; index < function->function_symbol_count; ++index) {
        const MinicCoreFunctionSymbol *symbol = &function->function_symbols[index];
        if (symbol->name == NULL || symbol->name_length == 0U) return false;
    }
    for (index = 0U; index < function->object_count; ++index) {
        const MinicCoreObject *object = &function->objects[index];
        if (object->element_count == 0U || ct_is_void(object->core_type_id) ||
            ct_is_function(object->core_type_id) ||
            core_is(function, object->core_type_id, MINIC_CORE_TYPE_OPAQUE)) {
            return false;
        }
    }
    for (index = 0U; index < function->global_count; ++index) {
        const MinicCoreGlobal *global;

        global = &function->globals[index];
        if (global->name == NULL || global->name_length == 0U ||
            ct_is_function(global->core_type_id) ||
            core_is(function, global->core_type_id, MINIC_CORE_TYPE_OPAQUE)) {
            return false;
        }
    }
    for (index = 0U; index < function->callee_count; ++index) {
        const MinicCoreCallee *callee;
        size_t parameter_index;

        callee = &function->callees[index];
        if (callee->name == NULL || callee->name_length == 0U ||
            !minic_function_calling_convention_valid(callee->calling_convention) ||
            !ct_direct_call_return_type(callee->core_return_type_id) ||
            (callee->parameter_count != 0U && callee->core_parameter_type_ids == NULL)) {
            return false;
        }
        /* Direct and indirect signatures share one scalar-or-record parameter
         * contract. The Core CALL result_object controls aggregate ownership. */
        for (parameter_index = 0U; parameter_index < callee->parameter_count; ++parameter_index) {
            if (!ct_call_parameter_type(callee->core_parameter_type_ids[parameter_index])) {
                return false;
            }
        }
    }
    for (index = 0U; index < function->call_signature_count; ++index) {
        const MinicCoreCallSignature *signature;
        size_t parameter_index;

        signature = &function->call_signatures[index];
        if (!ct_is_function(signature->core_function_type_id) ||
            !minic_function_calling_convention_valid(signature->calling_convention) ||
            (!ct_direct_call_return_type(signature->core_return_type_id)) ||
            (signature->parameter_count != 0U && signature->core_parameter_type_ids == NULL)) {
            return false;
        }
        for (parameter_index = 0U; parameter_index < signature->parameter_count;
             ++parameter_index) {
            if (!ct_call_parameter_type(signature->core_parameter_type_ids[parameter_index])) {
                return false;
            }
        }
    }
    instruction_seen = function->instruction_count == 0U
                           ? NULL
                           : (bool *)calloc(function->instruction_count, sizeof(*instruction_seen));
    value_seen = function->value_count == 0U
                     ? NULL
                     : (bool *)calloc(function->value_count, sizeof(*value_seen));
    available_values = function->value_count == 0U
                           ? NULL
                           : (bool *)calloc(function->value_count, sizeof(*available_values));
    if ((function->instruction_count != 0U && instruction_seen == NULL) ||
        (function->value_count != 0U && (value_seen == NULL || available_values == NULL))) {
        free(instruction_seen);
        free(value_seen);
        free(available_values);
        return false;
    }
    valid = true;
    for (block_index = 0U; valid && block_index < function->block_count; ++block_index) {
        valid = verify_block(function,
                             (MinicCoreBlockId)block_index,
                             instruction_seen,
                             value_seen,
                             available_values);
    }
    for (index = 0U; valid && index < function->instruction_count; ++index) {
        valid = instruction_seen[index];
    }
    for (index = 0U; valid && index < function->value_count; ++index) {
        valid = value_seen[index];
    }
    free(instruction_seen);
    free(value_seen);
    free(available_values);
    return valid;
}
