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
                     view.calling_convention == MINIC_CALLING_CONVENTION_DEFAULT &&
                     minic_core_call_view_argument_type(&fn, &view, 0U, &actual) &&
                     minic_type_equal(actual, scalar), 3)) != 0) return rc;
    /* Preserve target-neutral ABI identity; no machine convention is chosen
     * by the Common semantic view. Corrupt IDs must never reach a backend. */
    callee.calling_convention = MINIC_CALLING_CONVENTION_MS_X64;
    if (!minic_core_call_view_build(&fn, &call, &view) ||
        view.calling_convention != MINIC_CALLING_CONVENTION_MS_X64)
        return 28;
    callee.calling_convention = (MinicFunctionCallingConvention)99;
    if (minic_core_call_view_build(&fn, &call, &view)) return 29;
    callee.calling_convention = MINIC_CALLING_CONVENTION_DEFAULT;
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
    signature.calling_convention = MINIC_CALLING_CONVENTION_MS_X64;
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
                     view.calling_convention == MINIC_CALLING_CONVENTION_MS_X64 &&
                     view.function_pointer == 1U, 15)) != 0) return rc;
    signature.calling_convention = (MinicFunctionCallingConvention)99;
    if (minic_core_call_view_build(&fn, &call, &view)) return 30;
    signature.calling_convention = MINIC_CALLING_CONVENTION_MS_X64;
    /* The production visitor precomputes each CALL view once in a compact
     * Core instruction ID-sorted index. Exercise sparse IDs and fail-closed
     * corrupt semantic operands outside all psABI-dependent backends. */
    {
        MinicCoreCallIndex index = {0};
        MinicCoreInstruction ordered[5] = {0};
        MinicCoreCallee saved = callee;
        signature.function_type_id = 3U;
        ordered[1] = call;
        ordered[4] = call;
        ordered[4].kind = MINIC_CORE_INSTRUCTION_CALL;
        ordered[4].value.call.callee_id = 0U;
        ordered[4].value.call.argument_begin = 0U;
        ordered[4].value.call.argument_count = 1U;
        ordered[4].value.call.result_object = MINIC_CORE_OBJECT_INVALID;
        callee = saved;
        callee.return_type = scalar;
        callee.parameter_types = &scalar;
        callee.parameter_count = 1U;
        fn.instructions = ordered;
        fn.instruction_count = 5U;
        size_t ordinal = SIZE_MAX;
        const MinicCoreCallView *cached;
        if (!minic_core_call_index_build(&fn, 2U, &index) ||
            index.count != 2U ||
            minic_core_call_index_find(&index, 0U, &ordinal) != NULL ||
            minic_core_call_index_find(&index, 3U, &ordinal) != NULL ||
            minic_core_call_index_find(&index, 1U, NULL) != NULL) {
            minic_core_call_index_destroy(&index);
            return 16;
        }
        cached = minic_core_call_index_find(&index, 1U, &ordinal);
        if (cached == NULL || ordinal != 0U ||
            cached->calling_convention != MINIC_CALLING_CONVENTION_MS_X64 ||
            cached->function_pointer != 1U) {
            minic_core_call_index_destroy(&index);
            return 16;
        }
        cached = minic_core_call_index_find(&index, 4U, &ordinal);
        if (cached == NULL || ordinal != 1U ||
            cached->calling_convention != MINIC_CALLING_CONVENTION_DEFAULT ||
            cached->callee != &callee) {
            minic_core_call_index_destroy(&index);
            return 16;
        }
        minic_core_call_index_destroy(&index);
        if (index.count != 0U || index.sites != NULL) return 17;
        ordered[1].value.indirect_call.signature_id = 9U;
        if (minic_core_call_index_build(&fn, 2U, &index) ||
            index.sites != NULL || index.count != 0U) return 18;
        ordered[1] = call;
        if (minic_core_call_index_build(&fn, 1U, &index)) return 19;
        minic_core_call_index_destroy(&index);
        /* Borrow production's exact index, or own the same shape when a
         * standalone caller has no visitor. Both must share ONE lookup. */
        {
            MinicCoreCallIndexHandle owned = {0}, borrowed = {0};
            MinicCoreCallIndex invalid = {0};
            if (!minic_core_call_index_handle_prepare(&fn, NULL, &owned) ||
                owned.index != &owned.owned || owned.owned.count != 2U ||
                minic_core_call_index_find(owned.index, 4U, &ordinal) == NULL ||
                ordinal != 1U ||
                minic_core_call_index_handle_prepare(&fn, NULL, &owned))
                return 21;
            minic_core_call_index_handle_destroy(&owned);
            if (owned.index != NULL || owned.owned.sites != NULL ||
                owned.owned.count != 0U)
                return 22;
            if (!minic_core_call_index_build(&fn, 2U, &index) ||
                !minic_core_call_index_handle_prepare(&fn, &index, &borrowed) ||
                borrowed.index != &index || borrowed.owned.sites != NULL)
                return 23;
            minic_core_call_index_handle_destroy(&borrowed);
            if (borrowed.index != NULL || index.count != 2U ||
                minic_core_call_index_find(&index, 1U, &ordinal) == NULL)
                return 24;
            minic_core_call_index_destroy(&index);
            invalid.count = 6U; /* more CALLs than instructions */
            if (minic_core_call_index_handle_prepare(&fn, &invalid, &owned))
                return 25;
            ordered[1].value.indirect_call.signature_id = 9U;
            if (minic_core_call_index_handle_prepare(&fn, NULL, &owned) ||
                owned.index != NULL || owned.owned.sites != NULL ||
                owned.owned.count != 0U)
                return 26;
            ordered[1] = call;
            fn.instruction_count = 0U;
            if (!minic_core_call_index_handle_prepare(&fn, NULL, &owned) ||
                owned.index != &owned.owned || owned.owned.count != 0U)
                return 27;
            minic_core_call_index_handle_destroy(&owned);
            fn.instruction_count = 5U;
        }
    }
    signature.function_type_id = 4U;
    if (minic_core_call_view_build(&fn, &call, &view)) return 20;
    puts("MINIC_FIVE_TARGET_CALL_SEMANTICS=PASS scalar_record_void_indirect malformed_reject=9");
    return 0;
}
