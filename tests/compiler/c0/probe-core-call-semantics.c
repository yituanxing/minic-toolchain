#include "target/common/core_call_view.h"

#include <stdio.h>
#include <string.h>

/* Common CALL identity is entirely Core-owned, with no C source descriptors
 * in the public schema. Core CALL admission and call-index ownership can
 * therefore never depend on nominal C rank or frontend metadata. */
typedef struct TypeIds {
    MinicCoreTypeId integer, record, other_record;
    MinicCoreTypeId function, other_function, function_pointer;
} TypeIds;

static bool register_types(MinicCoreTypeContext *context, TypeIds *ids) {
    MinicCoreType type = {0};
    minic_core_type_context_initialize(context);
    type.kind = MINIC_CORE_TYPE_INTEGER;
    type.has_layout = true;
    type.size = 4U;
    type.alignment = 4U;
    type.value.integer.bit_width = 32U;
    type.value.integer.is_signed = true;
    if (!minic_core_type_register(context, &type, &ids->integer)) return false;
    (void)memset(&type, 0, sizeof(type));
    type.kind = MINIC_CORE_TYPE_RECORD;
    type.has_layout = true;
    type.size = 4U;
    type.alignment = 4U;
    type.value.record.is_complete = true;
    if (!minic_core_type_register(context, &type, &ids->record) ||
        !minic_core_type_register(context, &type, &ids->other_record)) return false;
    (void)memset(&type, 0, sizeof(type));
    type.kind = MINIC_CORE_TYPE_FUNCTION;
    type.value.function.return_type = ids->integer;
    type.value.function.parameters = &ids->integer;
    type.value.function.parameter_count = 1U;
    type.value.function.calling_convention = MINIC_CALLING_CONVENTION_MS_X64;
    if (!minic_core_type_register(context, &type, &ids->function) ||
        !minic_core_type_register(context, &type, &ids->other_function)) return false;
    (void)memset(&type, 0, sizeof(type));
    type.kind = MINIC_CORE_TYPE_POINTER;
    type.has_layout = true;
    type.size = 8U;
    type.alignment = 8U;
    type.value.pointer.pointee = ids->function;
    return minic_core_type_register(context, &type, &ids->function_pointer);
}

#define CHECK(expr, code) do { \
    if (!(expr)) { \
        (void)fprintf(stderr, "MINIC_CORE_CALL_SEMANTICS=FAIL code=%d line=%d\n", \
                      (code), __LINE__); \
        return (code); \
    } \
} while (0)

int main(void) {
    MinicCoreTypeContext context;
    TypeIds types = {0};
    MinicCoreFunction fn = {0};
    MinicCoreCallee callee = {0};
    MinicCoreCallSignature signature = {0};
    MinicCoreCallArgument argument = {0};
    MinicCoreInstruction call = {0};
    MinicCoreValue values[2] = {0};
    MinicCoreObject object = {0};
    MinicCoreCallView view;
    MinicCoreTypeId actual;

    CHECK(register_types(&context, &types), 2);
    fn.type_context = &context;
    fn.values = values; fn.value_count = 2U;
    fn.objects = &object; fn.object_count = 1U;
    fn.callees = &callee; fn.callee_count = 1U;
    fn.call_arguments = &argument; fn.call_argument_count = 1U;
    values[0].core_type_id = types.integer;
    values[1].core_type_id = types.function_pointer;
    object.core_type_id = types.record;
    callee.name = "probe";
    callee.name_length = 5U;
    callee.core_return_type_id = types.integer;
    callee.core_parameter_type_ids = &types.integer;
    callee.parameter_count = 1U;
    argument.kind = MINIC_CORE_CALL_ARGUMENT_VALUE;
    argument.value.value_id = 0U;
    call.kind = MINIC_CORE_INSTRUCTION_CALL;
    call.core_type_id = types.integer;
    call.result = 0U;
    call.value.call.callee_id = 0U;
    call.value.call.argument_count = 1U;
    call.value.call.result_object = MINIC_CORE_OBJECT_INVALID;

    CHECK(minic_core_call_view_build(&fn, &call, &view) &&
          view.return_form == MINIC_CORE_CALL_RETURN_VALUE &&
          view.calling_convention == MINIC_CALLING_CONVENTION_DEFAULT &&
          minic_core_call_view_argument_core_type(&fn, &view, 0U, &actual) &&
          actual == types.integer, 3);
    callee.calling_convention = MINIC_CALLING_CONVENTION_MS_X64;
    CHECK(minic_core_call_view_build(&fn, &call, &view) &&
          view.calling_convention == MINIC_CALLING_CONVENTION_MS_X64, 28);
    callee.calling_convention = (MinicFunctionCallingConvention)99;
    CHECK(!minic_core_call_view_build(&fn, &call, &view), 29);
    callee.calling_convention = MINIC_CALLING_CONVENTION_DEFAULT;

    call.core_type_id = types.record;
    CHECK(!minic_core_call_view_build(&fn, &call, &view), 4);
    call.core_type_id = types.integer;
    call.result = MINIC_CORE_VALUE_INVALID;
    CHECK(!minic_core_call_view_build(&fn, &call, &view), 5);
    call.result = 0U;
    argument.value.value_id = 999U;
    CHECK(!minic_core_call_view_build(&fn, &call, &view), 6);
    argument.kind = MINIC_CORE_CALL_ARGUMENT_OBJECT;
    argument.value.object_id = 0U;
    CHECK(!minic_core_call_view_build(&fn, &call, &view), 7);

    callee.core_parameter_type_ids = &types.record;
    CHECK(minic_core_call_view_build(&fn, &call, &view) &&
          minic_core_call_view_argument_core_type(&fn, &view, 0U, &actual) &&
          actual == types.record, 8);
    callee.core_parameter_type_ids = &types.other_record;
    CHECK(!minic_core_call_view_build(&fn, &call, &view), 9);
    callee.core_parameter_type_ids = NULL;
    callee.parameter_count = 0U;
    call.value.call.argument_count = 0U;
    callee.core_return_type_id = types.record;
    call.core_type_id = types.record;
    call.result = MINIC_CORE_VALUE_INVALID;
    call.value.call.result_object = 0U;
    CHECK(minic_core_call_view_build(&fn, &call, &view) &&
          view.return_form == MINIC_CORE_CALL_RETURN_OBJECT, 10);
    call.value.call.result_object = MINIC_CORE_OBJECT_INVALID;
    CHECK(!minic_core_call_view_build(&fn, &call, &view), 11);
    call.value.call.result_object = 0U;
    object.core_type_id = types.other_record;
    CHECK(!minic_core_call_view_build(&fn, &call, &view), 12);
    object.core_type_id = types.record;

    /* Empty or invalid Core type denotes neither scalar nor record return.
     * Use a real Core void type instead. */
    {
        MinicCoreType type = {0};
        MinicCoreTypeId void_id;
        type.kind = MINIC_CORE_TYPE_VOID;
        CHECK(minic_core_type_register(&context, &type, &void_id), 31);
        callee.core_return_type_id = void_id;
        call.core_type_id = void_id;
        call.value.call.result_object = MINIC_CORE_OBJECT_INVALID;
        CHECK(minic_core_call_view_build(&fn, &call, &view) &&
              view.return_form == MINIC_CORE_CALL_RETURN_VOID, 13);
        call.result = 0U;
        CHECK(!minic_core_call_view_build(&fn, &call, &view), 14);
    }
    call.result = MINIC_CORE_VALUE_INVALID;

    signature.core_function_type_id = types.function;
    fn.call_signatures = &signature;
    fn.call_signature_count = 1U;
    argument.kind = MINIC_CORE_CALL_ARGUMENT_VALUE;
    argument.value.value_id = 0U;
    call.kind = MINIC_CORE_INSTRUCTION_INDIRECT_CALL;
    call.core_type_id = types.integer;
    call.result = 0U;
    call.value.indirect_call.callee = 1U;
    call.value.indirect_call.signature_id = 0U;
    call.value.indirect_call.argument_count = 1U;
    call.value.indirect_call.result_object = MINIC_CORE_OBJECT_INVALID;
    CHECK(minic_core_call_view_build(&fn, &call, &view) &&
          view.signature == &signature &&
          view.calling_convention == MINIC_CALLING_CONVENTION_MS_X64 &&
          view.function_pointer == 1U, 15);
    context.types[types.function].value.function.calling_convention =
        (MinicFunctionCallingConvention)99;
    CHECK(!minic_core_call_view_build(&fn, &call, &view), 30);
    context.types[types.function].value.function.calling_convention =
        MINIC_CALLING_CONVENTION_MS_X64;

    /* The sparse Common CALL index is shared by all five target ABI plans. */
    {
        MinicCoreCallIndex index = {0};
        MinicCoreInstruction ordered[5] = {0};
        size_t ordinal = SIZE_MAX;
        const MinicCoreCallView *cached;
        MinicCoreCallIndexHandle owned = {0}, borrowed = {0};
        MinicCoreCallIndex invalid = {0};

        ordered[1] = call;
        ordered[4] = call;
        ordered[4].kind = MINIC_CORE_INSTRUCTION_CALL;
        ordered[4].value.call.callee_id = 0U;
        ordered[4].value.call.argument_count = 1U;
        ordered[4].value.call.result_object = MINIC_CORE_OBJECT_INVALID;
        callee.core_return_type_id = types.integer;
        callee.core_parameter_type_ids = &types.integer;
        callee.parameter_count = 1U;
        fn.instructions = ordered;
        fn.instruction_count = 5U;
        CHECK(minic_core_call_index_build(&fn, 2U, &index) &&
              index.count == 2U &&
              minic_core_call_index_find(&index, 0U, &ordinal) == NULL &&
              minic_core_call_index_find(&index, 3U, &ordinal) == NULL &&
              minic_core_call_index_find(&index, 1U, NULL) == NULL, 16);
        cached = minic_core_call_index_find(&index, 1U, &ordinal);
        CHECK(cached != NULL && ordinal == 0U &&
              cached->calling_convention == MINIC_CALLING_CONVENTION_MS_X64 &&
              cached->function_pointer == 1U, 16);
        cached = minic_core_call_index_find(&index, 4U, &ordinal);
        CHECK(cached != NULL && ordinal == 1U &&
              cached->calling_convention == MINIC_CALLING_CONVENTION_DEFAULT &&
              cached->callee == &callee, 16);
        minic_core_call_index_destroy(&index);
        CHECK(index.count == 0U && index.sites == NULL, 17);
        ordered[1].value.indirect_call.signature_id = 9U;
        CHECK(!minic_core_call_index_build(&fn, 2U, &index) &&
              index.sites == NULL && index.count == 0U, 18);
        ordered[1] = call;
        CHECK(!minic_core_call_index_build(&fn, 1U, &index), 19);
        minic_core_call_index_destroy(&index);
        CHECK(minic_core_call_index_handle_prepare(&fn, NULL, &owned) &&
              owned.index == &owned.owned && owned.owned.count == 2U &&
              minic_core_call_index_find(owned.index, 4U, &ordinal) != NULL &&
              ordinal == 1U, 21);
        CHECK(!minic_core_call_index_handle_prepare(&fn, NULL, &owned), 21);
        minic_core_call_index_handle_destroy(&owned);
        CHECK(owned.index == NULL && owned.owned.sites == NULL &&
              owned.owned.count == 0U, 22);
        CHECK(minic_core_call_index_build(&fn, 2U, &index) &&
              minic_core_call_index_handle_prepare(&fn, &index, &borrowed) &&
              borrowed.index == &index && borrowed.owned.sites == NULL, 23);
        minic_core_call_index_handle_destroy(&borrowed);
        CHECK(borrowed.index == NULL && index.count == 2U &&
              minic_core_call_index_find(&index, 1U, &ordinal) != NULL, 24);
        minic_core_call_index_destroy(&index);
        invalid.count = 6U;
        CHECK(!minic_core_call_index_handle_prepare(&fn, &invalid, &owned), 25);
        ordered[1].value.indirect_call.signature_id = 9U;
        CHECK(!minic_core_call_index_handle_prepare(&fn, NULL, &owned) &&
              owned.index == NULL && owned.owned.sites == NULL &&
              owned.owned.count == 0U, 26);
        ordered[1] = call;
        fn.instruction_count = 0U;
        CHECK(minic_core_call_index_handle_prepare(&fn, NULL, &owned) &&
              owned.index == &owned.owned && owned.owned.count == 0U, 27);
        minic_core_call_index_handle_destroy(&owned);
        fn.instruction_count = 5U;
        signature.core_function_type_id = types.other_function;
        CHECK(!minic_core_call_view_build(&fn, &call, &view), 20);
    }

    minic_core_type_context_destroy(&context);
    (void)puts("MINIC_FIVE_TARGET_CALL_SEMANTICS=PASS CoreType_C_free scalar_record_void_indirect malformed_reject index");
    return 0;
}
