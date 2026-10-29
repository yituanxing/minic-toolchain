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
        view.calling_convention = view.callee->calling_convention;
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
        view.calling_convention = view.signature->calling_convention;
        begin = instruction->value.indirect_call.argument_begin;
        count = instruction->value.indirect_call.argument_count;
    } else {
        return false;
    }
    if (!minic_function_calling_convention_valid(view.calling_convention) ||
        begin > function->call_argument_count ||
        count > function->call_argument_count - begin ||
        (count != 0U && function->call_arguments == NULL) ||
        (view.parameter_count != 0U && view.parameter_types == NULL) ||
        view.parameter_count > count ||
        (!view.is_variadic && view.parameter_count != count))
        return false;
    if (!minic_type_equal(view.return_type, instruction->type))
        return false;
    if (minic_type_is_void(instruction->type)) {
        if (instruction->result != MINIC_CORE_VALUE_INVALID)
            return false;
        view.return_form = MINIC_CORE_CALL_RETURN_VOID;
    } else if (minic_type_is_record(instruction->type)) {
        if (instruction->kind != MINIC_CORE_INSTRUCTION_CALL ||
            instruction->value.call.result_object >= function->object_count ||
            function->objects == NULL ||
            !minic_type_equal(
                function->objects[instruction->value.call.result_object].type,
                instruction->type) ||
            instruction->result != MINIC_CORE_VALUE_INVALID)
            return false;
        view.return_form = MINIC_CORE_CALL_RETURN_OBJECT;
    } else {
        if (instruction->result >= function->value_count ||
            function->values == NULL ||
            !minic_type_equal(function->values[instruction->result].type,
                              instruction->type))
            return false;
        view.return_form = MINIC_CORE_CALL_RETURN_VALUE;
    }
    if (instruction->kind == MINIC_CORE_INSTRUCTION_CALL &&
        view.return_form != MINIC_CORE_CALL_RETURN_OBJECT &&
        instruction->value.call.result_object != MINIC_CORE_OBJECT_INVALID)
        return false;
    view.argument_begin = begin;
    view.argument_count = count;
    if (count != 0U)
        view.arguments = function->call_arguments + begin;
    /* All backends reject malformed Core argument storage before machine
     * placement; no machine ISA or psABI choice occurs during this check. */
    {
        size_t i;
        MinicType actual;
        for (i = 0U; i < count; ++i)
            if (!minic_core_call_view_argument_type(function, &view, i, &actual))
                return false;
    }
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

bool minic_core_call_view_argument_type(const MinicCoreFunction *function,
                                        const MinicCoreCallView *view,
                                        size_t index, MinicType *actual) {
    const MinicCoreCallArgument *arg;
    if (function == NULL || view == NULL || actual == NULL ||
        index >= view->argument_count || view->arguments == NULL ||
        view->parameter_count > view->argument_count ||
        (view->parameter_count != 0U && view->parameter_types == NULL))
        return false;
    arg = &view->arguments[index];
    if (!minic_core_call_argument_actual_type(function, arg, actual))
        return false;
    if (index < view->parameter_count) {
        const MinicType expected = view->parameter_types[index];
        /* Aggregate identity is a Core invariant. Scalar promotions and
         * physical register/stack placement remain target-owned. */
        if (minic_type_is_record(expected) != minic_type_is_record(*actual))
            return false;
        if (minic_type_is_record(expected) &&
            !minic_type_equal(expected, *actual))
            return false;
    }
    return true;
}
