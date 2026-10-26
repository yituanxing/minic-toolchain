#include "target/common/core_call_view.h"
#include "frontend/type.h"
#include <stdio.h>
#include <string.h>

/* A real Core metadata/type probe, independent of target ISA and psABI. */
static int demand(bool condition, int code) { return condition ? 0 : code; }
int main(void) {
    MinicCoreFunction fn = {0};
    MinicCoreCallee callee = {0};
    MinicCoreCallSignature signature = {0};
    MinicCoreCallArgument argument = {0};
    MinicCoreInstruction call = {0};
    MinicCoreValue values[2];
    MinicCoreObject object = {0};
    MinicCoreCallView view;
    MinicType actual, function_pointer, scalar = minic_type_int();
    MinicType record = minic_type_record(7U);
    int rc;
    if (!minic_type_pointer_to(minic_type_function(3U), &function_pointer))
        return 2;
    (void)memset(values, 0, sizeof(values));
    values[0].type = scalar;
    values[1].type = function_pointer;
    object.type = record;
    object.element_count = 1U;
    callee.name = "probe";
    callee.name_length = 5U;
    callee.return_type = scalar;
    callee.parameter_types = &scalar;
    callee.parameter_count = 1U;
    argument.kind = MINIC_CORE_CALL_ARGUMENT_VALUE;
    argument.value.value_id = 0U;
    fn.values = values; fn.value_count = 2U;
    fn.objects = &object; fn.object_count = 1U;
    fn.callees = &callee; fn.callee_count = 1U;
    fn.call_arguments = &argument; fn.call_argument_count = 1U;
    call.kind = MINIC_CORE_INSTRUCTION_CALL;
    call.type = scalar;
    call.result = 0U;
    call.value.call.callee_id = 0U;
    call.value.call.argument_begin = 0U;
    call.value.call.argument_count = 1U;
    call.value.call.result_object = MINIC_CORE_OBJECT_INVALID;
    if ((rc = demand(minic_core_call_view_build(&fn, &call, &view) &&
                     view.return_form == MINIC_CORE_CALL_RETURN_VALUE &&
                     minic_core_call_view_argument_type(&fn, &view, 0U, &actual) &&
                     minic_type_equal(actual, scalar), 3)) != 0) return rc;
    call.type = minic_type_double();
    if (minic_core_call_view_build(&fn, &call, &view)) return 4;
    call.type = scalar;
    call.result = MINIC_CORE_VALUE_INVALID;
    if (minic_core_call_view_build(&fn, &call, &view)) return 5;
    call.result = 0U;
    argument.value.value_id = 999U;
    if (minic_core_call_view_build(&fn, &call, &view)) return 6;
    argument.kind = MINIC_CORE_CALL_ARGUMENT_OBJECT;
    argument.value.object_id = 0U;
    if (minic_core_call_view_build(&fn, &call, &view)) return 7;
    callee.parameter_types = &record;
    if ((rc = demand(minic_core_call_view_build(&fn, &call, &view) &&
                     minic_core_call_view_argument_type(&fn, &view, 0U, &actual) &&
                     minic_type_equal(actual, record), 8)) != 0) return rc;
    {
        MinicType other = minic_type_record(8U);
        callee.parameter_types = &other;
        if (minic_core_call_view_build(&fn, &call, &view)) return 9;
    }
    callee.parameter_types = NULL;
    callee.parameter_count = 0U;
    call.value.call.argument_count = 0U;
    callee.return_type = record;
    call.type = record;
    call.result = MINIC_CORE_VALUE_INVALID;
    call.value.call.result_object = 0U;
    if ((rc = demand(minic_core_call_view_build(&fn, &call, &view) &&
                     view.return_form == MINIC_CORE_CALL_RETURN_OBJECT, 10)) != 0)
        return rc;
    call.value.call.result_object = MINIC_CORE_OBJECT_INVALID;
    if (minic_core_call_view_build(&fn, &call, &view)) return 11;
    call.value.call.result_object = 0U;
    object.type = minic_type_record(8U);
    if (minic_core_call_view_build(&fn, &call, &view)) return 12;
    object.type = record;
    callee.return_type = minic_type_void();
    call.type = minic_type_void();
    call.value.call.result_object = MINIC_CORE_OBJECT_INVALID;
    if ((rc = demand(minic_core_call_view_build(&fn, &call, &view) &&
                     view.return_form == MINIC_CORE_CALL_RETURN_VOID, 13)) != 0)
        return rc;
    call.result = 0U;
    if (minic_core_call_view_build(&fn, &call, &view)) return 14;
    call.result = MINIC_CORE_VALUE_INVALID;
    signature.function_type_id = 3U;
    signature.return_type = scalar;
    signature.parameter_types = &scalar;
    signature.parameter_count = 1U;
    fn.call_signatures = &signature;
    fn.call_signature_count = 1U;
    argument.kind = MINIC_CORE_CALL_ARGUMENT_VALUE;
    argument.value.value_id = 0U;
    call.kind = MINIC_CORE_INSTRUCTION_INDIRECT_CALL;
    call.type = scalar;
    call.result = 0U;
    call.value.indirect_call.callee = 1U;
    call.value.indirect_call.signature_id = 0U;
    call.value.indirect_call.argument_begin = 0U;
    call.value.indirect_call.argument_count = 1U;
    if ((rc = demand(minic_core_call_view_build(&fn, &call, &view) &&
                     view.signature == &signature &&
                     view.function_pointer == 1U, 15)) != 0) return rc;
    signature.function_type_id = 4U;
    if (minic_core_call_view_build(&fn, &call, &view)) return 16;
    puts("MINIC_FIVE_TARGET_CALL_SEMANTICS=PASS scalar_record_void_indirect malformed_reject=9");
    return 0;
}
