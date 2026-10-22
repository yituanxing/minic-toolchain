#include "target/common/core_call_view.h"

#include <string.h>

bool minic_core_call_view_build(const MinicCoreFunction *function,
                                const MinicCoreInstruction *instruction,
                                MinicCoreCallView *result) {
    MinicCoreCallView view;
    size_t begin, count;
    if (function == NULL || instruction == NULL || result == NULL)
        return false;
    (void)memset(&view, 0, sizeof(view));
    view.function_pointer = MINIC_CORE_VALUE_INVALID;
    if (instruction->kind == MINIC_CORE_INSTRUCTION_CALL) {
        const size_t id = instruction->value.call.callee_id;
        if (id >= function->callee_count || function->callees == NULL)
            return false;
        view.callee = &function->callees[id];
        view.return_type = view.callee->return_type;
        view.parameter_types = view.callee->parameter_types;
        view.parameter_count = view.callee->parameter_count;
        view.is_variadic = view.callee->is_variadic;
        begin = instruction->value.call.argument_begin;
        count = instruction->value.call.argument_count;
    } else if (instruction->kind == MINIC_CORE_INSTRUCTION_INDIRECT_CALL) {
        const size_t id = instruction->value.indirect_call.signature_id;
        const MinicCoreValueId pointer = instruction->value.indirect_call.callee;
        MinicType function_type;
        if (id >= function->call_signature_count ||
            function->call_signatures == NULL ||
            pointer >= function->value_count || function->values == NULL ||
            !minic_type_pointee(function->values[pointer].type, &function_type) ||
            !minic_type_is_function(function_type))
            return false;
        view.signature = &function->call_signatures[id];
        if (function_type.function_type_id != view.signature->function_type_id)
            return false;
        view.function_pointer = pointer;
        view.return_type = view.signature->return_type;
        view.parameter_types = view.signature->parameter_types;
        view.parameter_count = view.signature->parameter_count;
        view.is_variadic = view.signature->is_variadic;
        begin = instruction->value.indirect_call.argument_begin;
        count = instruction->value.indirect_call.argument_count;
    } else {
        return false;
    }
    if (begin > function->call_argument_count ||
        count > function->call_argument_count - begin ||
        (count != 0U && function->call_arguments == NULL) ||
        (view.parameter_count != 0U && view.parameter_types == NULL) ||
        view.parameter_count > count ||
        (!view.is_variadic && view.parameter_count != count))
        return false;
    view.argument_begin = begin;
    view.argument_count = count;
    if (count != 0U)
        view.arguments = function->call_arguments + begin;
    *result = view;
    return true;
}

bool minic_core_call_argument_actual_type(const MinicCoreFunction *function,
                                           const MinicCoreCallArgument *argument,
                                           MinicType *type) {
    if (function == NULL || argument == NULL || type == NULL)
        return false;
    if (argument->kind == MINIC_CORE_CALL_ARGUMENT_VALUE) {
        const MinicCoreValueId id = argument->value.value_id;
        if (id >= function->value_count || function->values == NULL)
            return false;
        *type = function->values[id].type;
        return !minic_type_is_record(*type);
    }
    if (argument->kind == MINIC_CORE_CALL_ARGUMENT_OBJECT) {
        const MinicCoreObjectId id = argument->value.object_id;
        if (id >= function->object_count || function->objects == NULL)
            return false;
        *type = function->objects[id].type;
        return minic_type_is_record(*type);
    }
    return false;
}
