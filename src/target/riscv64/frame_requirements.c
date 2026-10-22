#include "target/riscv64/frame_requirements.h"
#include "target/riscv64/abi.h"
#include <stdint.h>

/* Preserve ABI-specific requirement queries, not a simplistic common frame. */
/* M79_CALL_FRAME_RETURN_ADDRESS: a return-address query needs the entry
   value of ra even in a function that has no ordinary Core CALL yet. Save it
   in the prologue whenever either a call or this semantic instruction exists. */
bool minic_riscv64_core_variadic_fixed_prefix(const MinicC0Program *program,
                                       const MinicCoreFunction *function,
                                       size_t *integer_parameter_count,
                                       size_t *fixed_stack_slots) {
    MinicRiscv64AbiCursor cursor;
    MinicRiscv64AbiValue return_value;
    size_t parameter_index;

    if (program == NULL || function == NULL || integer_parameter_count == NULL ||
        fixed_stack_slots == NULL ||
        !minic_riscv64_abi_cursor_initialize_for_return(
            program, function->return_type, &cursor, &return_value)) {
        return false;
    }
    (void)return_value;
    for (parameter_index = 0U; parameter_index < function->parameter_count; ++parameter_index) {
        MinicRiscv64AbiArgumentLocation location;

        if (!minic_riscv64_abi_place_argument(
                program, function->parameter_types[parameter_index], true, &cursor, &location)) {
            return false;
        }
    }
    /* RV64 varargs are contiguous after the named prefix. If integer argument
       registers remain, preserve them in the callee frame as before. Once the
       named prefix has exhausted a0-a7, the first unnamed argument starts
       directly after the named incoming stack slots. Keep mixed register/stack
       prefixes fail-closed unless all integer argument registers are consumed. */
    return minic_riscv64_abi_variadic_prefix_from_cursor(
        &cursor, integer_parameter_count, fixed_stack_slots);
}

/* M168_RV64_INDIRECT_CALL_STACK: reserve one fixed outgoing stack area
   shared by direct and indirect calls.  The ABI cursor remains the single owner
   of register/stack placement; Core keeps only VALUE/OBJECT arguments. */
/* One semantic argument-type resolver feeds RV64 psABI sizing and both
 * machine call forms. A variadic OBJECT argument must really be a record;
 * Core never silently classifies an arbitrary object as a scalar. */
bool minic_riscv64_core_call_argument_type(
    const MinicCoreFunction *function,
    const MinicCoreCallArgument *argument,
    const MinicType *parameter_types, size_t parameter_count,
    bool is_variadic, size_t argument_index,
    MinicType *argument_type, bool *is_fixed_parameter) {
    if (function == NULL || argument == NULL || argument_type == NULL ||
        is_fixed_parameter == NULL)
        return false;
    *is_fixed_parameter = argument_index < parameter_count;
    if (*is_fixed_parameter) {
        if (parameter_types == NULL)
            return false;
        *argument_type = parameter_types[argument_index];
        return true;
    }
    if (!is_variadic)
        return false;
    if (argument->kind == MINIC_CORE_CALL_ARGUMENT_VALUE) {
        MinicCoreValueId id = argument->value.value_id;
        if (id >= function->value_count || function->values == NULL)
            return false;
        *argument_type = function->values[id].type;
        return true;
    }
    if (argument->kind == MINIC_CORE_CALL_ARGUMENT_OBJECT) {
        MinicCoreObjectId id = argument->value.object_id;
        if (id >= function->object_count || function->objects == NULL ||
            !minic_type_is_record(function->objects[id].type))
            return false;
        *argument_type = function->objects[id].type;
        return true;
    }
    return false;
}

bool minic_riscv64_core_call_outgoing_stack_size(const MinicC0Program *program,
                                          const MinicCoreFunction *function,
                                          size_t *result) {
    size_t instruction_index;
    size_t maximum_stack_slots;

    if (program == NULL || function == NULL || result == NULL) {
        return false;
    }
    maximum_stack_slots = 0U;
    for (instruction_index = 0U; instruction_index < function->instruction_count;
         ++instruction_index) {
        const MinicCoreInstruction *instruction = &function->instructions[instruction_index];
        const MinicType *parameter_types;
        size_t parameter_count;
        bool is_variadic;
        size_t argument_begin;
        size_t argument_count;
        MinicType return_type;
        MinicRiscv64AbiCursor cursor;
        MinicRiscv64AbiValue return_value;
        size_t argument_index;

        if (instruction->kind == MINIC_CORE_INSTRUCTION_CALL) {
            const MinicCoreCallee *callee;
            if (instruction->value.call.callee_id >= function->callee_count ||
                instruction->value.call.argument_begin > function->call_argument_count ||
                instruction->value.call.argument_count >
                    function->call_argument_count - instruction->value.call.argument_begin) {
                return false;
            }
            callee = &function->callees[instruction->value.call.callee_id];
            parameter_types = callee->parameter_types;
            parameter_count = callee->parameter_count;
            is_variadic = callee->is_variadic;
            argument_begin = instruction->value.call.argument_begin;
            argument_count = instruction->value.call.argument_count;
            return_type = callee->return_type;
        } else if (instruction->kind == MINIC_CORE_INSTRUCTION_INDIRECT_CALL) {
            const MinicCoreCallSignature *signature;
            if (instruction->value.indirect_call.signature_id >= function->call_signature_count ||
                instruction->value.indirect_call.argument_begin > function->call_argument_count ||
                instruction->value.indirect_call.argument_count >
                    function->call_argument_count - instruction->value.indirect_call.argument_begin) {
                return false;
            }
            signature = &function->call_signatures[instruction->value.indirect_call.signature_id];
            parameter_types = signature->parameter_types;
            parameter_count = signature->parameter_count;
            is_variadic = signature->is_variadic;
            argument_begin = instruction->value.indirect_call.argument_begin;
            argument_count = instruction->value.indirect_call.argument_count;
            return_type = signature->return_type;
        } else {
            continue;
        }

        if (!minic_riscv64_abi_cursor_initialize_for_return(
                program, return_type, &cursor, &return_value)) {
            return false;
        }
        (void)return_value;
        for (argument_index = 0U; argument_index < argument_count; ++argument_index) {
            const MinicCoreCallArgument *argument =
                &function->call_arguments[argument_begin + argument_index];
            MinicRiscv64AbiArgumentLocation location;
            MinicType argument_type;
            bool is_fixed_parameter;
            if (!minic_riscv64_core_call_argument_type(
                    function, argument, parameter_types, parameter_count,
                    is_variadic, argument_index, &argument_type,
                    &is_fixed_parameter))
                return false;
            if (!minic_riscv64_abi_place_argument(
                    program, argument_type, is_fixed_parameter, &cursor, &location)) {
                return false;
            }
        }
        if (cursor.stack_slot_count > maximum_stack_slots) {
            maximum_stack_slots = cursor.stack_slot_count;
        }
    }
    if (maximum_stack_slots > SIZE_MAX / 8U) {
        return false;
    }
    *result = maximum_stack_slots * 8U;
    return true;
}

