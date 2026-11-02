#include "target/common/core_call_view.h"

#include <stdio.h>
#include <string.h>

/* Target-independent type-owned CALL checks: no C type-table/AST lookup. */
static bool register_type(MinicCoreTypeContext *context, MinicCoreType type,
                          MinicCoreTypeId *result) {
    return minic_core_type_register(context, &type, result);
}

int main(void) {
    MinicCoreTypeContext context;
    MinicCoreType type = {0};
    MinicCoreTypeId integer, record, other_record, signature_type;
    MinicCoreTypeId pointer_type, other_signature, other_pointer;
    MinicCoreTypeId qualified_record, qualified_pointer;
    MinicCoreValue values[2];
    MinicCoreObject objects[2];
    MinicCoreCallee callee = {0};
    MinicCoreCallSignature signature = {0};
    MinicCoreCallArgument argument = {0};
    MinicCoreInstruction call = {0};
    MinicCoreCallView view;
    MinicCoreTypeId actual;
    MinicCoreFunction function = {0};
    MinicType legacy_scalar = {0};

    minic_core_type_context_initialize(&context);
    legacy_scalar.base_kind = MINIC_TYPE_BASE_INT;
    (void)memset(values, 0, sizeof(values));
    (void)memset(objects, 0, sizeof(objects));
    type.kind = MINIC_CORE_TYPE_INTEGER;
    type.has_layout = true;
    type.size = 4U;
    type.alignment = 4U;
    type.value.integer.bit_width = 32U;
    type.value.integer.is_signed = true;
    if (!register_type(&context, type, &integer)) return 1;

    (void)memset(&type, 0, sizeof(type));
    type.kind = MINIC_CORE_TYPE_RECORD;
    type.has_layout = true;
    type.size = 4U;
    type.alignment = 4U;
    type.value.record.is_complete = true;
    if (!register_type(&context, type, &record) ||
        !register_type(&context, type, &other_record) || record == other_record)
        return 2;

    (void)memset(&type, 0, sizeof(type));
    type.kind = MINIC_CORE_TYPE_FUNCTION;
    type.value.function.return_type = integer;
    type.value.function.parameters = &integer;
    type.value.function.parameter_count = 1U;
    if (!register_type(&context, type, &signature_type) ||
        !register_type(&context, type, &other_signature) ||
        signature_type == other_signature)
        return 3;

    (void)memset(&type, 0, sizeof(type));
    type.kind = MINIC_CORE_TYPE_POINTER;
    type.has_layout = true;
    type.size = 8U;
    type.alignment = 8U;
    type.value.pointer.pointee = signature_type;
    if (!register_type(&context, type, &pointer_type)) return 4;
    type.value.pointer.pointee = other_signature;
    if (!register_type(&context, type, &other_pointer)) return 5;

    (void)memset(&type, 0, sizeof(type));
    type.kind = MINIC_CORE_TYPE_QUALIFIED;
    type.has_layout = true;
    type.size = 4U;
    type.alignment = 4U;
    type.is_read_only = true;
    type.value.qualified.base = record;
    if (!register_type(&context, type, &qualified_record)) return 15;
    type.size = 8U;
    type.alignment = 8U;
    type.value.qualified.base = pointer_type;
    if (!register_type(&context, type, &qualified_pointer)) return 16;

    function.type_context = &context;
    function.values = values;
    function.value_count = 2U;
    function.objects = objects;
    function.object_count = 2U;
    function.callees = &callee;
    function.callee_count = 1U;
    function.call_signatures = &signature;
    function.call_signature_count = 1U;
    function.call_arguments = &argument;
    function.call_argument_count = 1U;
    values[0].core_type_id = integer;
    values[1].core_type_id = pointer_type;
    objects[0].core_type_id = record;
    objects[1].core_type_id = other_record;

    callee.core_return_type_id = integer;
    callee.core_parameter_type_ids = &integer;
    callee.parameter_types = &legacy_scalar;
    callee.parameter_count = 1U;
    argument.kind = MINIC_CORE_CALL_ARGUMENT_VALUE;
    argument.value.value_id = 0U;
    call.kind = MINIC_CORE_INSTRUCTION_CALL;
    call.core_type_id = integer;
    call.result = 0U;
    call.value.call.callee_id = 0U;
    call.value.call.argument_count = 1U;
    call.value.call.result_object = MINIC_CORE_OBJECT_INVALID;
    if (!minic_core_call_view_build(&function, &call, &view) ||
        !minic_core_call_view_argument_core_type(&function, &view, 0U, &actual) ||
        actual != integer || view.core_return_type_id != integer)
        return 6;
    call.core_type_id = record;
    if (minic_core_call_view_build(&function, &call, &view)) return 7;
    call.core_type_id = integer;
    callee.core_return_type_id = MINIC_CORE_TYPE_INVALID;
    if (minic_core_call_view_build(&function, &call, &view)) return 20;
    callee.core_return_type_id = integer;
    values[0].core_type_id = MINIC_CORE_TYPE_INVALID;
    if (minic_core_call_view_build(&function, &call, &view)) return 21;
    values[0].core_type_id = integer;

    /* Record by value: same shape and layout never replaces nominal type. */
    argument.kind = MINIC_CORE_CALL_ARGUMENT_OBJECT;
    argument.value.object_id = 0U;
    callee.core_parameter_type_ids = &record;
    if (!minic_core_call_view_build(&function, &call, &view)) return 8;
    argument.value.object_id = 1U;
    if (minic_core_call_view_build(&function, &call, &view)) return 9;
    argument.value.object_id = 0U;
    objects[0].core_type_id = qualified_record;
    if (minic_core_call_view_build(&function, &call, &view)) return 17;
    callee.core_parameter_type_ids = &qualified_record;
    if (!minic_core_call_view_build(&function, &call, &view)) return 18;
    objects[0].core_type_id = record;
    callee.core_parameter_type_ids = &record;

    /* Aggregate return identity and storage form are Core obligations. */
    callee.parameter_count = 0U;
    callee.core_parameter_type_ids = NULL;
    call.value.call.argument_count = 0U;
    callee.core_return_type_id = record;
    call.core_type_id = record;
    call.result = MINIC_CORE_VALUE_INVALID;
    call.value.call.result_object = 0U;
    if (!minic_core_call_view_build(&function, &call, &view) ||
        view.return_form != MINIC_CORE_CALL_RETURN_OBJECT)
        return 10;
    call.value.call.result_object = 1U;
    if (minic_core_call_view_build(&function, &call, &view)) return 11;

    signature.core_function_type_id = signature_type;
    signature.core_return_type_id = integer;
    signature.core_parameter_type_ids = &integer;
    signature.parameter_types = &legacy_scalar;
    signature.parameter_count = 1U;
    argument.kind = MINIC_CORE_CALL_ARGUMENT_VALUE;
    argument.value.value_id = 0U;
    call.kind = MINIC_CORE_INSTRUCTION_INDIRECT_CALL;
    call.core_type_id = integer;
    call.result = 0U;
    call.value.indirect_call.callee = 1U;
    call.value.indirect_call.signature_id = 0U;
    call.value.indirect_call.argument_count = 1U;
    call.value.indirect_call.result_object = MINIC_CORE_OBJECT_INVALID;
    if (!minic_core_call_view_build(&function, &call, &view) ||
        view.core_function_type_id != signature_type)
        return 12;
    values[1].core_type_id = qualified_pointer;
    if (!minic_core_call_view_build(&function, &call, &view)) return 19;
    /* Structurally identical but nominally different function signature. */
    values[1].core_type_id = other_pointer;
    if (minic_core_call_view_build(&function, &call, &view)) return 13;
    values[1].core_type_id = pointer_type;
    signature.core_function_type_id = MINIC_CORE_TYPE_INVALID;
    if (minic_core_call_view_build(&function, &call, &view)) return 22;
    signature.core_function_type_id = signature_type;
    signature.core_parameter_type_ids = &record;
    if (minic_core_call_view_build(&function, &call, &view)) return 14;

    minic_core_type_context_destroy(&context);
    puts("MINIC_CORE_CALL_CORETYPE=PASS direct_indirect_nominal_record_return_qualifiers");
    return 0;
}
