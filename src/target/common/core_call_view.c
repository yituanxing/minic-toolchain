#include "target/common/core_call_view.h"

#include <string.h>

/* Core type identities, not frontend typedef/record handles, own executable
 * call well-formedness after the C ingress has attached a Core type arena. */
static const MinicCoreType *minic_core_call_type(const MinicCoreFunction *function,
                                                 MinicCoreTypeId id) {
    return function != NULL && function->type_context != NULL &&
                   id != MINIC_CORE_TYPE_INVALID
               ? minic_core_type_get(function->type_context, id)
               : NULL;
}

static bool minic_core_call_is_kind(const MinicCoreFunction *function,
                                    MinicCoreTypeId id, MinicCoreTypeKind kind) {
    const MinicCoreType *type;
    if (function == NULL || function->type_context == NULL)
        return false;
    id = minic_core_type_unqualified(function->type_context, id);
    type = minic_core_call_type(function, id);
    return type != NULL && type->kind == kind;
}

/* The bound Core path never trusts a C function-type ID or derives a type
 * from the frontend program. Function types are nominal in Core, so distinct
 * source signature identities do not accidentally collapse. */
static bool minic_core_call_view_core_signature_valid(
    const MinicCoreFunction *function, const MinicCoreCallView *view,
    const MinicCoreInstruction *instruction) {
    size_t i;
    const MinicCoreType *return_type;

    if (function == NULL || function->type_context == NULL || view == NULL ||
        instruction == NULL ||
        view->core_return_type_id == MINIC_CORE_TYPE_INVALID ||
        instruction->core_type_id != view->core_return_type_id ||
        (view->parameter_count != 0U && view->core_parameter_type_ids == NULL))
        return false;
    return_type = minic_core_call_type(function, view->core_return_type_id);
    if (return_type == NULL)
        return false;
    for (i = 0U; i < view->parameter_count; ++i)
        if (minic_core_call_type(function, view->core_parameter_type_ids[i]) == NULL)
            return false;

    if (view->signature != NULL) {
        const MinicCoreType *signature = minic_core_call_type(
            function, minic_core_type_unqualified(function->type_context,
                                                   view->core_function_type_id));
        /* Only the FUNCTION snapshot owns these fields.  The view borrows
         * its immutable array rather than comparing a parallel Core copy. */
        if (signature == NULL || signature->kind != MINIC_CORE_TYPE_FUNCTION)
            return false;
    }
    return true;
}
bool minic_core_call_view_build(const MinicCoreFunction *function,
                                const MinicCoreInstruction *instruction,
                                MinicCoreCallView *result) {
    MinicCoreCallView view;
    size_t begin, count;
    /* Executable call semantics belong to the sealed Core type graph.
     * An unbound source descriptor is never an alternative admission path. */
    if (function == NULL || function->type_context == NULL ||
        instruction == NULL || result == NULL)
        return false;
    (void)memset(&view, 0, sizeof(view));
    view.function_pointer = MINIC_CORE_VALUE_INVALID;
    view.core_return_type_id = MINIC_CORE_TYPE_INVALID;
    view.core_function_type_id = MINIC_CORE_TYPE_INVALID;
    if (instruction->kind == MINIC_CORE_INSTRUCTION_CALL) {
        const size_t id = instruction->value.call.callee_id;
        const MinicCoreType *function_type;
        if (id >= function->callee_count || function->callees == NULL)
            return false;
        view.callee = &function->callees[id];
        view.core_function_type_id = view.callee->core_function_type_id;
        function_type = minic_core_call_type(function,
            minic_core_type_unqualified(function->type_context,
                                        view.core_function_type_id));
        if (function_type == NULL || function_type->kind != MINIC_CORE_TYPE_FUNCTION)
            return false;
        view.core_return_type_id = function_type->value.function.return_type;
        view.core_parameter_type_ids = function_type->value.function.parameters;
        view.parameter_count = function_type->value.function.parameter_count;
        view.is_variadic = function_type->value.function.is_variadic;
        view.calling_convention = function_type->value.function.calling_convention;
        begin = instruction->value.call.argument_begin;
        count = instruction->value.call.argument_count;
    } else if (instruction->kind == MINIC_CORE_INSTRUCTION_INDIRECT_CALL) {
        const size_t id = instruction->value.indirect_call.signature_id;
        const MinicCoreValueId pointer = instruction->value.indirect_call.callee;
        const MinicCoreType *function_type;
        if (id >= function->call_signature_count ||
            function->call_signatures == NULL ||
            pointer >= function->value_count || function->values == NULL)
            return false;
        view.signature = &function->call_signatures[id];
        const MinicCoreType *pointer_type = minic_core_call_type(function,
            minic_core_type_unqualified(function->type_context,
                function->values[pointer].core_type_id));
        if (pointer_type == NULL || pointer_type->kind != MINIC_CORE_TYPE_POINTER ||
            minic_core_type_unqualified(function->type_context,
                pointer_type->value.pointer.pointee) !=
                minic_core_type_unqualified(function->type_context,
                view.signature->core_function_type_id) ||
            !minic_core_call_is_kind(function,
                                     view.signature->core_function_type_id,
                                     MINIC_CORE_TYPE_FUNCTION))
            return false;
        function_type = minic_core_call_type(function,
            minic_core_type_unqualified(function->type_context,
                view.signature->core_function_type_id));
        if (function_type == NULL || function_type->kind != MINIC_CORE_TYPE_FUNCTION)
            return false;
        view.function_pointer = pointer;
        view.core_function_type_id = view.signature->core_function_type_id;
        view.core_return_type_id = function_type->value.function.return_type;
        view.core_parameter_type_ids = function_type->value.function.parameters;
        view.parameter_count = function_type->value.function.parameter_count;
        view.is_variadic = function_type->value.function.is_variadic;
        view.calling_convention = function_type->value.function.calling_convention;
        begin = instruction->value.indirect_call.argument_begin;
        count = instruction->value.indirect_call.argument_count;
    } else {
        return false;
    }
    if (!minic_function_calling_convention_valid(view.calling_convention) ||
        begin > function->call_argument_count ||
        count > function->call_argument_count - begin ||
        (count != 0U && function->call_arguments == NULL) ||
        view.parameter_count > count ||
        (!view.is_variadic && view.parameter_count != count))
        return false;
    if (!minic_core_call_view_core_signature_valid(function, &view,
                                                  instruction))
        return false;
    const MinicCoreObjectId result_object =
        instruction->kind == MINIC_CORE_INSTRUCTION_CALL
            ? instruction->value.call.result_object
            : instruction->value.indirect_call.result_object;
    if (minic_core_call_is_kind(function, instruction->core_type_id,
                                MINIC_CORE_TYPE_VOID)) {
        if (instruction->result != MINIC_CORE_VALUE_INVALID ||
            result_object != MINIC_CORE_OBJECT_INVALID)
            return false;
        view.return_form = MINIC_CORE_CALL_RETURN_VOID;
    } else if (minic_core_call_is_kind(function, instruction->core_type_id,
                                       MINIC_CORE_TYPE_RECORD)) {
        if (result_object >= function->object_count ||
            function->objects == NULL ||
            function->objects[result_object].core_type_id !=
                instruction->core_type_id ||
            instruction->result != MINIC_CORE_VALUE_INVALID)
            return false;
        view.return_form = MINIC_CORE_CALL_RETURN_OBJECT;
    } else {
        if (result_object != MINIC_CORE_OBJECT_INVALID ||
            instruction->result >= function->value_count ||
            function->values == NULL ||
            function->values[instruction->result].core_type_id !=
                instruction->core_type_id)
            return false;
        view.return_form = MINIC_CORE_CALL_RETURN_VALUE;
    }
    view.argument_begin = begin;
    view.argument_count = count;
    if (count != 0U)
        view.arguments = function->call_arguments + begin;
    /* All backends reject malformed Core argument storage before machine
     * placement; no machine ISA or psABI choice occurs during this check. */
    {
        size_t i;
        MinicCoreTypeId core_actual;
        for (i = 0U; i < count; ++i) {
            if (!minic_core_call_view_argument_core_type(function, &view,
                                                         i, &core_actual))
                return false;
        }
    }
    *result = view;
    return true;
}

bool minic_core_call_argument_actual_core_type(
    const MinicCoreFunction *function, const MinicCoreCallArgument *argument,
    MinicCoreTypeId *result) {
    MinicCoreTypeId id;
    if (function == NULL || function->type_context == NULL ||
        argument == NULL || result == NULL)
        return false;
    if (argument->kind == MINIC_CORE_CALL_ARGUMENT_VALUE) {
        const MinicCoreValueId index = argument->value.value_id;
        if (index >= function->value_count || function->values == NULL)
            return false;
        id = function->values[index].core_type_id;
        if (minic_core_call_type(function, id) == NULL ||
            minic_core_call_is_kind(function, id, MINIC_CORE_TYPE_RECORD) ||
            minic_core_call_is_kind(function, id, MINIC_CORE_TYPE_VOID) ||
            minic_core_call_is_kind(function, id, MINIC_CORE_TYPE_FUNCTION))
            return false;
    } else if (argument->kind == MINIC_CORE_CALL_ARGUMENT_OBJECT) {
        const MinicCoreObjectId index = argument->value.object_id;
        if (index >= function->object_count || function->objects == NULL)
            return false;
        id = function->objects[index].core_type_id;
        if (!minic_core_call_is_kind(function, id, MINIC_CORE_TYPE_RECORD))
            return false;
    } else {
        return false;
    }
    *result = id;
    return true;
}

bool minic_core_call_view_argument_core_type(
    const MinicCoreFunction *function, const MinicCoreCallView *view,
    size_t index, MinicCoreTypeId *actual) {
    const MinicCoreCallArgument *arg;
    if (function == NULL || function->type_context == NULL || view == NULL ||
        actual == NULL || index >= view->argument_count ||
        view->arguments == NULL || view->parameter_count > view->argument_count ||
        (view->parameter_count != 0U && view->core_parameter_type_ids == NULL))
        return false;
    arg = &view->arguments[index];
    if (!minic_core_call_argument_actual_core_type(function, arg, actual))
        return false;
    if (index < view->parameter_count) {
        const MinicCoreTypeId expected = view->core_parameter_type_ids[index];
        const bool expected_record = minic_core_call_is_kind(
            function, expected, MINIC_CORE_TYPE_RECORD);
        const bool actual_record = minic_core_call_is_kind(
            function, *actual, MINIC_CORE_TYPE_RECORD);
        /* The target decides scalar conversion and ABI placement. By-value
         * records, however, must preserve their exact Core nominal identity. */
        if (minic_core_call_type(function, expected) == NULL ||
            expected_record != actual_record ||
            (expected_record && expected != *actual))
            return false;
    }
    return true;
}
