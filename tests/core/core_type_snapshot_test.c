#include "core/core_ir.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        (void)fprintf(stderr, "CORE_TYPE_SNAPSHOT=FAIL line=%d expression=%s\n", \
                      __LINE__, #condition); \
        return 1; \
    } \
} while (0)

typedef struct TypeIds {
    MinicCoreTypeId boolean, i8, i32, u32, record_a, record_b, incomplete_record;
    MinicCoreTypeId ptr_record_a, ptr_record_b, ptr_i8, ptr_i32;
    MinicCoreTypeId function_a, function_b, ptr_function_a;
} TypeIds;

static bool make_type_context(MinicCoreTypeContext *context, TypeIds *ids) {
    MinicCoreType type = {0};
    MinicCoreRecordField field = {0};
    MinicCoreTypeId parameter;
    minic_core_type_context_initialize(context);
    type.kind = MINIC_CORE_TYPE_INTEGER;
    type.has_layout = true;
    type.size = 1U;
    type.alignment = 1U;
    type.value.integer.bit_width = 1U;
    type.value.integer.is_bool = true;
    if (!minic_core_type_register(context, &type, &ids->boolean)) return false;
    type.value.integer.bit_width = 8U;
    type.value.integer.is_bool = false;
    type.value.integer.is_signed = true;
    if (!minic_core_type_register(context, &type, &ids->i8)) return false;
    type.size = 4U;
    type.alignment = 4U;
    type.value.integer.bit_width = 32U;
    type.value.integer.is_signed = true;
    if (!minic_core_type_register(context, &type, &ids->i32)) return false;
    type.value.integer.is_signed = false;
    if (!minic_core_type_register(context, &type, &ids->u32)) return false;

    (void)memset(&type, 0, sizeof(type));
    type.kind = MINIC_CORE_TYPE_RECORD;
    if (!minic_core_type_register(context, &type, &ids->record_a) ||
        !minic_core_type_register(context, &type, &ids->record_b) ||
        !minic_core_type_register(context, &type, &ids->incomplete_record)) return false;
    (void)memset(&type, 0, sizeof(type));
    type.kind = MINIC_CORE_TYPE_POINTER;
    type.has_layout = true;
    type.size = 8U;
    type.alignment = 8U;
    type.value.pointer.pointee = ids->record_a;
    if (!minic_core_type_register(context, &type, &ids->ptr_record_a)) return false;
    type.value.pointer.pointee = ids->record_b;
    if (!minic_core_type_register(context, &type, &ids->ptr_record_b)) return false;
    type.value.pointer.pointee = ids->i32;
    if (!minic_core_type_register(context, &type, &ids->ptr_i32)) return false;
    type.value.pointer.pointee = ids->i8;
    if (!minic_core_type_register(context, &type, &ids->ptr_i8)) return false;

    field.type_id = ids->i32;
    field.byte_offset = 8U;
    field.alignment = 4U;
    (void)memset(&type, 0, sizeof(type));
    type.kind = MINIC_CORE_TYPE_RECORD;
    type.has_layout = true;
    type.size = 16U;
    type.alignment = 8U;
    type.value.record.is_complete = true;
    type.value.record.fields = &field;
    type.value.record.field_count = 1U;
    if (!minic_core_type_complete_record(context, ids->record_a, &type) ||
        !minic_core_type_complete_record(context, ids->record_b, &type)) return false;

    parameter = ids->i32;
    (void)memset(&type, 0, sizeof(type));
    type.kind = MINIC_CORE_TYPE_FUNCTION;
    type.value.function.return_type = ids->i32;
    type.value.function.parameters = &parameter;
    type.value.function.parameter_count = 1U;
    type.value.function.calling_convention = MINIC_CALLING_CONVENTION_DEFAULT;
    if (!minic_core_type_register(context, &type, &ids->function_a) ||
        !minic_core_type_register(context, &type, &ids->function_b) ||
        ids->function_a == ids->function_b) return false;
    (void)memset(&type, 0, sizeof(type));
    type.kind = MINIC_CORE_TYPE_POINTER;
    type.has_layout = true;
    type.size = 8U;
    type.alignment = 8U;
    type.value.pointer.pointee = ids->function_a;
    return minic_core_type_register(context, &type, &ids->ptr_function_a);
}

static bool set_snapshot_signature(MinicCoreFunction *function,
                                   MinicCoreTypeId return_id,
                                   const MinicCoreTypeId *parameters) {
    size_t count = function->parameter_count;
    function->core_return_type_id = return_id;
    if (count == 0U) return true;
    function->core_parameter_type_ids = malloc(count * sizeof(*parameters));
    if (function->core_parameter_type_ids == NULL) return false;
    (void)memcpy(function->core_parameter_type_ids, parameters,
                 count * sizeof(*parameters));
    return true;
}

static bool build_indirect_fixture(MinicCoreFunction *function, const TypeIds *ids) {
    MinicType source_parameters[2], source_function_pointer;
    MinicCoreTypeId core_parameters[2];
    MinicCoreBlockId block;
    MinicCoreCallSignatureId signature;
    MinicCoreValueId callee, argument, result;
    MinicCoreInstruction instruction = {0};
    MinicCoreCallArgument call_argument = {0};
    MinicCoreTerminator terminator = {0};
    size_t begin;

    minic_core_function_initialize(function);
    if (!minic_type_pointer_to(minic_type_function(0U), &source_function_pointer))
        return false;
    source_parameters[0] = source_function_pointer;
    source_parameters[1] = minic_type_int();
    core_parameters[0] = ids->ptr_function_a;
    core_parameters[1] = ids->i32;
    if (!minic_core_function_set_signature(function, "invoke", 6U,
                                           minic_type_int(), source_parameters, 2U) ||
        !set_snapshot_signature(function, ids->i32, core_parameters) ||
        !minic_core_function_add_call_signature(function, 0U, minic_type_int(),
                                                &source_parameters[1], 1U, false,
                                                MINIC_CALLING_CONVENTION_DEFAULT,
                                                &signature) ||
        !minic_core_function_add_block(function, &block)) return false;

    instruction.kind = MINIC_CORE_INSTRUCTION_PARAMETER;
    instruction.type = source_function_pointer;
    instruction.value.parameter_index = 0U;
    if (!minic_core_function_append_value_instruction(function, block, &instruction,
                                                       &callee)) return false;
    instruction.type = minic_type_int();
    instruction.value.parameter_index = 1U;
    if (!minic_core_function_append_value_instruction(function, block, &instruction,
                                                       &argument)) return false;
    call_argument.kind = MINIC_CORE_CALL_ARGUMENT_VALUE;
    call_argument.value.value_id = argument;
    if (!minic_core_function_append_call_arguments(function, &call_argument, 1U,
                                                   &begin)) return false;
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_INDIRECT_CALL;
    instruction.type = minic_type_int();
    instruction.value.indirect_call.callee = callee;
    instruction.value.indirect_call.signature_id = signature;
    instruction.value.indirect_call.argument_begin = begin;
    instruction.value.indirect_call.argument_count = 1U;
    instruction.value.indirect_call.result_object = MINIC_CORE_OBJECT_INVALID;
    if (!minic_core_function_append_value_instruction(function, block, &instruction,
                                                       &result)) return false;
    terminator.kind = MINIC_CORE_TERMINATOR_RETURN;
    terminator.return_value = result;
    terminator.return_object = MINIC_CORE_OBJECT_INVALID;
    if (!minic_core_function_set_terminator(function, block, &terminator)) return false;

    function->values[callee].core_type_id = ids->ptr_function_a;
    function->values[argument].core_type_id = ids->i32;
    function->values[result].core_type_id = ids->i32;
    function->instructions[0].core_type_id = ids->ptr_function_a;
    function->instructions[1].core_type_id = ids->i32;
    function->instructions[2].core_type_id = ids->i32;
    function->call_signatures[signature].core_function_type_id = ids->function_a;
    function->call_signatures[signature].core_return_type_id = ids->i32;
    function->call_signatures[signature].core_parameter_type_ids =
        malloc(sizeof(MinicCoreTypeId));
    if (function->call_signatures[signature].core_parameter_type_ids == NULL)
        return false;
    function->call_signatures[signature].core_parameter_type_ids[0] = ids->i32;
    return true;
}

static bool build_field_fixture(MinicCoreFunction *function, const TypeIds *ids) {
    MinicType source_pointer_record, source_pointer_int;
    MinicCoreTypeId core_parameter;
    MinicCoreBlockId block;
    MinicCoreValueId base, result;
    MinicCoreInstruction instruction = {0};
    MinicCoreTerminator terminator = {0};

    minic_core_function_initialize(function);
    if (!minic_type_pointer_to(minic_type_record(0U), &source_pointer_record) ||
        !minic_type_pointer_to(minic_type_int(), &source_pointer_int)) return false;
    core_parameter = ids->ptr_record_a;
    if (!minic_core_function_set_signature(function, "member", 6U,
                                           source_pointer_int, &source_pointer_record, 1U) ||
        !set_snapshot_signature(function, ids->ptr_i32, &core_parameter) ||
        !minic_core_function_add_block(function, &block)) return false;
    instruction.kind = MINIC_CORE_INSTRUCTION_PARAMETER;
    instruction.type = source_pointer_record;
    instruction.value.parameter_index = 0U;
    if (!minic_core_function_append_value_instruction(function, block, &instruction,
                                                       &base)) return false;
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_FIELD_ADDRESS;
    instruction.type = source_pointer_int;
    instruction.value.field_address.base = base;
    instruction.value.field_address.record_id = 0U;
    instruction.value.field_address.field_index = 0U;
    instruction.value.field_address.byte_offset = 8U;
    if (!minic_core_function_append_value_instruction(function, block, &instruction,
                                                       &result)) return false;
    terminator.kind = MINIC_CORE_TERMINATOR_RETURN;
    terminator.return_value = result;
    terminator.return_object = MINIC_CORE_OBJECT_INVALID;
    if (!minic_core_function_set_terminator(function, block, &terminator)) return false;
    function->values[base].core_type_id = ids->ptr_record_a;
    function->values[result].core_type_id = ids->ptr_i32;
    function->instructions[0].core_type_id = ids->ptr_record_a;
    function->instructions[1].core_type_id = ids->ptr_i32;
    function->instructions[1].value.field_address.core_record_type_id = ids->record_a;
    return true;
}

static bool build_object_fixture(MinicCoreFunction *function, const TypeIds *ids) {
    MinicCoreObjectId object_id;
    MinicSourceSpan span = {0};
    minic_core_function_initialize(function);
    if (!minic_core_function_set_signature(function, "storage", 7U,
                                           minic_type_int(), NULL, 0U) ||
        !set_snapshot_signature(function, ids->i32, NULL) ||
        !minic_core_function_add_repeated_object(function, span, minic_type_int(),
                                                 2U, &object_id)) return false;
    function->objects[object_id].core_type_id = ids->i32;
    return true;
}

typedef struct ResolverState {
    MinicCoreTypeId i32;
    size_t call_count;
    size_t fail_on_call;
} ResolverState;

static bool resolve_source_type(void *data, MinicType source, MinicCoreTypeId *id) {
    ResolverState *state = data;
    ++state->call_count;
    if (!minic_type_equal(source, minic_type_int()) ||
        state->call_count == state->fail_on_call) return false;
    *id = state->i32;
    return true;
}

static bool build_simple_function(MinicCoreFunction *function) {
    MinicCoreBlockId block;
    MinicCoreValueId value;
    MinicCoreInstruction instruction = {0};
    MinicCoreTerminator terminator = {0};
    minic_core_function_initialize(function);
    if (!minic_core_function_set_signature(function, "answer", 6U,
                                           minic_type_int(), NULL, 0U) ||
        !minic_core_function_add_block(function, &block)) return false;
    instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
    instruction.type = minic_type_int();
    instruction.value.integer_value = 7;
    if (!minic_core_function_append_value_instruction(function, block, &instruction,
                                                       &value)) return false;
    terminator.kind = MINIC_CORE_TERMINATOR_RETURN;
    terminator.return_value = value;
    terminator.return_object = MINIC_CORE_OBJECT_INVALID;
    return minic_core_function_set_terminator(function, block, &terminator);
}

/* A source enum pointer is represented as a plain Core i8 pointer. The
 * ingress must authenticate its source-eligibility fact before attaching
 * the function; otherwise AArch64's supported i8 overflow mode could
 * silently accept a C-invalid enum destination. */
static bool build_enum_overflow_function(MinicCoreFunction *function) {
    MinicType parameter_types[3];
    MinicCoreBlockId block;
    MinicCoreValueId values[3], result;
    MinicCoreInstruction instruction = {0};
    MinicCoreTerminator terminator = {0};
    size_t i;
    minic_core_function_initialize(function);
    parameter_types[0] = minic_type_int();
    parameter_types[1] = minic_type_int();
    if (!minic_type_pointer_to(minic_type_enum(17U), &parameter_types[2]) ||
        !minic_core_function_set_signature(function, "enumcheck", 9U,
                                           minic_type_bool(), parameter_types, 3U) ||
        !minic_core_function_add_block(function, &block)) return false;
    for (i = 0U; i < 3U; ++i) {
        instruction.kind = MINIC_CORE_INSTRUCTION_PARAMETER;
        instruction.type = parameter_types[i];
        instruction.value.parameter_index = i;
        if (!minic_core_function_append_value_instruction(
                function, block, &instruction, &values[i])) return false;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW;
    instruction.type = minic_type_bool();
    instruction.value.integer_overflow.operator_kind = MINIC_CORE_INTEGER_OVERFLOW_ADD;
    instruction.value.integer_overflow.left = values[0];
    instruction.value.integer_overflow.right = values[1];
    instruction.value.integer_overflow.result_address = values[2];
    instruction.value.integer_overflow.source_overflow_destination_ineligible = true;
    if (!minic_core_function_append_value_instruction(
            function, block, &instruction, &result)) return false;
    terminator.kind = MINIC_CORE_TERMINATOR_RETURN;
    terminator.return_value = result;
    terminator.return_object = MINIC_CORE_OBJECT_INVALID;
    return minic_core_function_set_terminator(function, block, &terminator);
}

static bool resolve_enum_overflow_type(void *data, MinicType source,
                                       MinicCoreTypeId *id) {
    const TypeIds *ids = data;
    MinicType pointee;
    if (minic_type_equal(source, minic_type_bool())) *id = ids->boolean;
    else if (minic_type_equal(source, minic_type_int())) *id = ids->i32;
    else if (minic_type_pointee(source, &pointee) && minic_type_is_enum(pointee))
        *id = ids->ptr_i8;
    else return false;
    return true;
}

int main(void) {
    MinicCoreTypeContext context;
    TypeIds ids = {0};
    MinicCoreFunction function;
    MinicCoreTypeId saved;
    MinicCoreValueId value;
    size_t extent, alignment;
    ResolverState resolver;

    CHECK(make_type_context(&context, &ids));
    CHECK(build_indirect_fixture(&function, &ids));
    CHECK(minic_core_function_attach_type_context(&function, &context));
    CHECK(minic_core_function_verify_core_type_snapshot(&function));
    CHECK(!minic_core_function_attach_type_context(&function, &context));
    CHECK(minic_core_function_type_get(&function, ids.function_a) != NULL);
    saved = function.core_return_type_id;
    function.core_return_type_id = MINIC_CORE_TYPE_INVALID;
    CHECK(!minic_core_function_verify_core_type_snapshot(&function));
    function.core_return_type_id = saved;
    function.core_parameter_type_ids[0] = MINIC_CORE_TYPE_INVALID;
    CHECK(!minic_core_function_verify_core_type_snapshot(&function));
    function.core_parameter_type_ids[0] = ids.ptr_function_a;

    /* Same ABI shape is not the same nominal indirect function identity. */
    saved = function.call_signatures[0].core_function_type_id;
    function.call_signatures[0].core_function_type_id = ids.function_b;
    CHECK(!minic_core_function_verify_core_type_snapshot(&function));
    function.call_signatures[0].core_function_type_id = saved;
    function.call_signatures[0].core_parameter_type_ids[0] = ids.u32;
    CHECK(!minic_core_function_verify_core_type_snapshot(&function));
    function.call_signatures[0].core_parameter_type_ids[0] = ids.i32;
    saved = function.values[0].core_type_id;
    function.values[0].core_type_id = MINIC_CORE_TYPE_INVALID;
    CHECK(!minic_core_function_verify_core_type_snapshot(&function));
    function.values[0].core_type_id = saved;
    CHECK(minic_core_function_verify_core_type_snapshot(&function));
    minic_core_function_destroy(&function);

    CHECK(build_field_fixture(&function, &ids));
    CHECK(minic_core_function_attach_type_context(&function, &context));
    CHECK(minic_core_function_verify_core_type_snapshot(&function));
    saved = function.instructions[1].value.field_address.core_record_type_id;
    function.instructions[1].value.field_address.core_record_type_id = ids.record_b;
    CHECK(!minic_core_function_verify_core_type_snapshot(&function));
    function.instructions[1].value.field_address.core_record_type_id = ids.incomplete_record;
    CHECK(!minic_core_function_verify_core_type_snapshot(&function));
    function.instructions[1].value.field_address.core_record_type_id = saved;
    function.instructions[1].value.field_address.byte_offset = 12U;
    CHECK(!minic_core_function_verify_core_type_snapshot(&function));
    function.instructions[1].value.field_address.byte_offset = 8U;
    function.values[0].core_type_id = ids.ptr_record_b;
    CHECK(!minic_core_function_verify_core_type_snapshot(&function));
    function.values[0].core_type_id = ids.ptr_record_a;
    CHECK(minic_core_function_verify_core_type_snapshot(&function));
    minic_core_function_destroy(&function);

    CHECK(build_object_fixture(&function, &ids));
    CHECK(minic_core_function_attach_type_context(&function, &context));
    CHECK(minic_core_function_object_layout(&function, 0U, &extent, &alignment));
    CHECK(extent == 8U && alignment == 4U);
    function.objects[0].element_count = SIZE_MAX;
    CHECK(!minic_core_function_verify_core_type_snapshot(&function));
    function.objects[0].element_count = 2U;
    function.objects[0].core_type_id = ids.incomplete_record;
    CHECK(!minic_core_function_verify_core_type_snapshot(&function));
    function.objects[0].core_type_id = ids.i32;
    CHECK(minic_core_function_verify_core_type_snapshot(&function));
    minic_core_function_destroy(&function);

    /* The bulk-ingress callback must roll back ALL snapshots on failure. */
    CHECK(build_simple_function(&function));
    CHECK(minic_core_function_verify(&function));
    resolver.i32 = ids.i32;
    resolver.call_count = 0U;
    resolver.fail_on_call = 2U;
    CHECK(!minic_core_function_snapshot_types(&function, &context,
                                               resolve_source_type, &resolver));
    CHECK(function.type_context == NULL &&
          function.core_return_type_id == MINIC_CORE_TYPE_INVALID);
    CHECK(function.values[0].core_type_id == MINIC_CORE_TYPE_INVALID);
    resolver.call_count = 0U;
    resolver.fail_on_call = 0U;
    CHECK(minic_core_function_snapshot_types(&function, &context,
                                              resolve_source_type, &resolver));
    CHECK(minic_core_function_verify_core_type_snapshot(&function));
    value = function.instructions[0].result;
    CHECK(function.values[value].core_type_id == ids.i32);
    CHECK(function.instructions[0].core_type_id == ids.i32);
    minic_core_function_destroy(&function);

    CHECK(build_enum_overflow_function(&function));
    CHECK(minic_core_function_verify(&function));
    function.instructions[3].value.integer_overflow.source_overflow_destination_ineligible = false;
    CHECK(!minic_core_function_snapshot_types(&function, &context,
                                              resolve_enum_overflow_type, &ids));
    CHECK(function.type_context == NULL);
    function.instructions[3].value.integer_overflow.source_overflow_destination_ineligible = true;
    CHECK(minic_core_function_snapshot_types(&function, &context,
                                             resolve_enum_overflow_type, &ids));
    CHECK(minic_core_function_verify_core_type_snapshot(&function));
    minic_core_function_destroy(&function);
    minic_core_type_context_destroy(&context);
    (void)puts("CORE_TYPE_SNAPSHOT=PASS valid malformed indirect_nominal field_nominal overflow enum_source_proof rollback");
    return 0;
}
