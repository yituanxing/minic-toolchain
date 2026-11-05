#include "compiler/c_lower/core_ir_source_internal.h"

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
    MinicCoreTypeId boolean, i8, i32, u32, array_i32_2;
    MinicCoreTypeId f64, long_double;
    MinicCoreTypeId record_a, record_b, incomplete_record;
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
    type.kind = MINIC_CORE_TYPE_FLOAT;
    type.has_layout = true;
    type.size = 8U;
    type.alignment = 8U;
    type.value.floating.format = MINIC_CORE_FLOAT_BINARY64;
    if (!minic_core_type_register(context, &type, &ids->f64)) return false;
    type.size = 16U;
    type.alignment = 16U;
    type.value.floating.format = MINIC_CORE_FLOAT_EXTENDED;
    if (!minic_core_type_register(context, &type, &ids->long_double)) return false;

    /* Fixed object repetition lives in one Core ARRAY type; CoreObject itself
     * has no second independent extent. */
    (void)memset(&type, 0, sizeof(type));
    type.kind = MINIC_CORE_TYPE_ARRAY;
    type.has_layout = true;
    type.size = 8U;
    type.alignment = 4U;
    type.value.array.element = ids->i32;
    type.value.array.count = 2U;
    type.value.array.is_complete = true;
    if (!minic_core_type_register(context, &type, &ids->array_i32_2)) return false;

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
                                   MinicCoreTypeContext *context,
                                   MinicCoreTypeId return_id,
                                   const MinicCoreTypeId *parameters,
                                   size_t count) {
    return minic_core_function_define_signature(
        function, context, return_id, parameters, count,
        MINIC_CALLING_CONVENTION_DEFAULT, false);
}

static bool build_indirect_fixture(MinicCoreSourceBuilder *builder,
                                   MinicCoreTypeContext *context, const TypeIds *ids) {
    MinicCoreFunction *function = &builder->core;
    MinicType source_parameters[2], source_function_pointer;
    MinicCoreTypeId core_parameters[2];
    MinicCoreBlockId block;
    MinicCoreCallSignatureId signature;
    MinicCoreValueId callee, argument, result;
    MinicCoreInstruction instruction = {0};
    MinicCoreCallArgument call_argument = {0};
    MinicCoreTerminator terminator = {0};
    size_t begin;

    minic_core_source_builder_initialize(builder);
    if (!minic_type_pointer_to(minic_type_function(0U), &source_function_pointer))
        return false;
    source_parameters[0] = source_function_pointer;
    source_parameters[1] = minic_type_int();
    core_parameters[0] = ids->ptr_function_a;
    core_parameters[1] = ids->i32;
    if (!minic_core_function_set_signature(builder, "invoke", 6U,
                                           minic_type_int(), source_parameters, 2U) ||
        !set_snapshot_signature(function, context, ids->i32, core_parameters, 2U) ||
        !minic_core_function_add_call_signature(builder, 0U, minic_type_int(),
                                                &source_parameters[1], 1U, false,
                                                MINIC_CALLING_CONVENTION_DEFAULT,
                                                &signature) ||
        !minic_core_function_add_block(function, &block)) return false;

    instruction.kind = MINIC_CORE_INSTRUCTION_PARAMETER;
    instruction.value.parameter_index = 0U;
    if (!minic_core_function_append_source_value_instruction(
            builder, block, &instruction, source_function_pointer, &callee)) return false;
    instruction.value.parameter_index = 1U;
    if (!minic_core_function_append_source_value_instruction(
            builder, block, &instruction, minic_type_int(), &argument)) return false;
    call_argument.kind = MINIC_CORE_CALL_ARGUMENT_VALUE;
    call_argument.value.value_id = argument;
    if (!minic_core_function_append_call_arguments(function, &call_argument, 1U,
                                                   &begin)) return false;
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_INDIRECT_CALL;
    instruction.value.indirect_call.callee = callee;
    instruction.value.indirect_call.signature_id = signature;
    instruction.value.indirect_call.argument_begin = begin;
    instruction.value.indirect_call.argument_count = 1U;
    instruction.value.indirect_call.result_object = MINIC_CORE_OBJECT_INVALID;
    if (!minic_core_function_append_source_value_instruction(
            builder, block, &instruction, minic_type_int(), &result)) return false;
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
    return true;
}

static bool build_field_fixture(MinicCoreSourceBuilder *builder,
                                MinicCoreTypeContext *context, const TypeIds *ids) {
    MinicCoreFunction *function = &builder->core;
    MinicType source_pointer_record, source_pointer_int;
    MinicCoreTypeId core_parameter;
    MinicCoreBlockId block;
    MinicCoreValueId base, result;
    MinicCoreInstruction instruction = {0};
    MinicCoreTerminator terminator = {0};

    minic_core_source_builder_initialize(builder);
    if (!minic_type_pointer_to(minic_type_record(0U), &source_pointer_record) ||
        !minic_type_pointer_to(minic_type_int(), &source_pointer_int)) return false;
    core_parameter = ids->ptr_record_a;
    if (!minic_core_function_set_signature(builder, "member", 6U,
                                           source_pointer_int, &source_pointer_record, 1U) ||
        !set_snapshot_signature(function, context, ids->ptr_i32, &core_parameter, 1U) ||
        !minic_core_function_add_block(function, &block)) return false;
    instruction.kind = MINIC_CORE_INSTRUCTION_PARAMETER;
    instruction.value.parameter_index = 0U;
    if (!minic_core_function_append_source_value_instruction(
            builder, block, &instruction, source_pointer_record, &base)) return false;
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_FIELD_ADDRESS;
    instruction.value.field_address.base = base;
    instruction.value.field_address.field_index = 0U;
    instruction.value.field_address.byte_offset = 8U;
    if (!minic_core_function_append_source_value_instruction(
            builder, block, &instruction, source_pointer_int, &result)) return false;
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

static bool build_object_fixture(MinicCoreSourceBuilder *builder,
                                 MinicCoreTypeContext *context, const TypeIds *ids) {
    MinicCoreFunction *function = &builder->core;
    MinicCoreObjectId object_id;
    MinicCoreBlockId block;
    MinicCoreValueId result;
    MinicCoreInstruction instruction = {0};
    MinicCoreTerminator terminator = {0};
    MinicSourceSpan span = {0};
    minic_core_source_builder_initialize(builder);
    if (!minic_core_function_set_signature(builder, "storage", 7U,
                                           minic_type_int(), NULL, 0U) ||
        !set_snapshot_signature(function, context, ids->i32, NULL, 0U) ||
        !minic_core_function_add_object(builder, span, minic_type_int(), &object_id) ||
        !minic_core_function_add_block(function, &block)) return false;
    function->objects[object_id].core_type_id = ids->array_i32_2;
    instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
    instruction.value.integer_value = 0;
    if (!minic_core_function_append_source_value_instruction(
            builder, block, &instruction, minic_type_int(), &result)) return false;
    function->values[result].core_type_id = ids->i32;
    function->instructions[0].core_type_id = ids->i32;
    terminator.kind = MINIC_CORE_TERMINATOR_RETURN;
    terminator.return_value = result;
    terminator.return_object = MINIC_CORE_OBJECT_INVALID;
    return minic_core_function_set_terminator(function, block, &terminator);
}

/* An independent Core producer does not import C declarations, allocate a
 * SourceDraft, or call the C-facing builder. Publication is CoreType-only. */
static bool build_core_only_function(MinicCoreFunction *function,
                                     MinicCoreTypeContext *context, const TypeIds *ids) {
    MinicCoreInstruction instruction = {0};
    MinicCoreTerminator terminator = {0};
    MinicCoreBlockId block;
    MinicCoreValueId value;
    const char name[] = "core_only";

    minic_core_function_initialize(function);
    function->name = malloc(sizeof(name));
    if (function->name == NULL) return false;
    (void)memcpy(function->name, name, sizeof(name));
    function->name_length = sizeof(name) - 1U;
    if (!minic_core_function_define_signature(
            function, context, ids->i32, NULL, 0U,
            MINIC_CALLING_CONVENTION_DEFAULT, false)) return false;
    if (!minic_core_function_add_block(function, &block)) return false;
    instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
    instruction.core_type_id = ids->i32;
    instruction.value.integer_value = 42;
    if (!minic_core_function_append_value_instruction(
            function, block, &instruction, &value)) return false;
    if (function->values[value].core_type_id != ids->i32 ||
        function->instructions[0].core_type_id != ids->i32)
        return false;
    terminator.kind = MINIC_CORE_TERMINATOR_RETURN;
    terminator.return_value = value;
    terminator.return_object = MINIC_CORE_OBJECT_INVALID;
    return minic_core_function_set_terminator(function, block, &terminator);
}

/* The C ingress stamps volatile onto each COPY access, not onto CoreType.
 * A single record execution type therefore permits independent source and
 * destination effects without retaining source cv type identities. */
static bool test_source_copy_access_effects(void) {
    MinicCoreSourceBuilder builder;
    MinicCoreBlockId block;
    MinicCoreValueId destination, source;
    MinicCoreInstruction operation = {0};
    MinicType record = minic_type_record(0U);
    MinicType volatile_record, pointer, volatile_pointer;
    MinicType parameters[2];
    bool ok = false;
    if (!minic_type_add_volatile(record, &volatile_record) ||
        !minic_type_pointer_to(record, &pointer) ||
        !minic_type_pointer_to(volatile_record, &volatile_pointer))
        return false;
    parameters[0] = pointer;
    parameters[1] = volatile_pointer;
    minic_core_source_builder_initialize(&builder);
    if (!minic_core_function_set_signature(
            &builder, "copy_effects", 12U, minic_type_int(), parameters, 2U) ||
        !minic_core_function_add_block(&builder.core, &block))
        goto done;
    operation.kind = MINIC_CORE_INSTRUCTION_PARAMETER;
    operation.value.parameter_index = 0U;
    if (!minic_core_function_append_source_value_instruction(
            &builder, block, &operation, pointer, &destination))
        goto done;
    operation.value.parameter_index = 1U;
    if (!minic_core_function_append_source_value_instruction(
            &builder, block, &operation, volatile_pointer, &source))
        goto done;
    operation = (MinicCoreInstruction){0};
    operation.kind = MINIC_CORE_INSTRUCTION_RECORD_COPY;
    operation.value.record_copy.destination_address = destination;
    operation.value.record_copy.source_address = source;
    if (!minic_core_function_append_source_effect_instruction(
            &builder, block, &operation, record) ||
        builder.core.instruction_count != 3U ||
        builder.core.instructions[2].value.record_copy.destination_volatile ||
        !builder.core.instructions[2].value.record_copy.source_volatile)
        goto done;
    operation.value.record_copy.destination_address = source;
    operation.value.record_copy.source_address = destination;
    if (!minic_core_function_append_source_effect_instruction(
            &builder, block, &operation, record) ||
        builder.core.instruction_count != 4U ||
        !builder.core.instructions[3].value.record_copy.destination_volatile ||
        builder.core.instructions[3].value.record_copy.source_volatile)
        goto done;
    operation.value.record_copy.source_address = MINIC_CORE_VALUE_INVALID;
    if (minic_core_function_append_source_effect_instruction(
            &builder, block, &operation, record) ||
        builder.core.instruction_count != 4U)
        goto done;
    ok = true;
done:
    minic_core_source_builder_destroy(&builder);
    return ok;
}

typedef struct ResolverState {
    MinicCoreTypeId i32;
    MinicCoreTypeId f64, long_double;
    MinicCoreTypeId function_i32_i32;
    size_t call_count;
    size_t fail_on_call;
} ResolverState;

static bool resolve_source_type(void *data, MinicType source, MinicCoreTypeId *id) {
    ResolverState *state = data;
    ++state->call_count;
    if (state->call_count == state->fail_on_call) return false;
    if (minic_type_equal(source, minic_type_int()))
        *id = state->i32;
    else if (minic_type_equal(source, minic_type_double()))
        *id = state->f64;
    else if (minic_type_equal(source, minic_type_long_double()))
        *id = state->long_double;
    else if (minic_type_equal(source, minic_type_function(29U)))
        *id = state->function_i32_i32;
    else return false;
    return true;
}

/* Synthetic TF libcalls have no C Program FunctionId/Module declaration.
 * The direct callee's independent linker name must still own one nominal
 * Core FUNCTION identity, including both result and fixed argument types. */
static bool build_tf_libcall_function(MinicCoreSourceBuilder *builder) {
    MinicCoreFunction *function = &builder->core;
    const MinicType tf = minic_type_long_double();
    const MinicType f64 = minic_type_double();
    MinicType input_types[2] = {0};
    MinicCoreCalleeId add, trunc, repeated_add;
    MinicCoreBlockId block;
    MinicCoreValueId inputs[2], sum, result;
    MinicCoreCallArgument args[2] = {{0}, {0}};
    MinicCoreInstruction instruction = {0};
    MinicCoreTerminator terminator = {0};
    size_t begin;

    input_types[0] = input_types[1] = tf;
    minic_core_source_builder_initialize(builder);
    if (!minic_core_function_set_signature(builder, "tf_roundtrip", 12U,
                                           f64, input_types, 2U) ||
        !minic_core_function_add_callee(builder, "__addtf3", 8U, tf,
                                       input_types, 2U, false,
                                       MINIC_CALLING_CONVENTION_DEFAULT, &add) ||
        !minic_core_function_add_callee(builder, "__addtf3", 8U, tf,
                                       input_types, 2U, false,
                                       MINIC_CALLING_CONVENTION_DEFAULT,
                                       &repeated_add) ||
        add != repeated_add ||
        !minic_core_function_add_callee(builder, "__trunctfdf2", 12U, f64,
                                       &tf, 1U, false,
                                       MINIC_CALLING_CONVENTION_DEFAULT, &trunc) ||
        add == trunc || function->callee_count != 2U ||
        !minic_core_function_add_block(function, &block)) return false;
    instruction.kind = MINIC_CORE_INSTRUCTION_PARAMETER;
    instruction.value.parameter_index = 0U;
    if (!minic_core_function_append_source_value_instruction(builder, block,
            &instruction, tf, &inputs[0])) return false;
    instruction.value.parameter_index = 1U;
    if (!minic_core_function_append_source_value_instruction(builder, block,
            &instruction, tf, &inputs[1])) return false;

    args[0].kind = args[1].kind = MINIC_CORE_CALL_ARGUMENT_VALUE;
    args[0].value.value_id = inputs[0];
    args[1].value.value_id = inputs[1];
    if (!minic_core_function_append_call_arguments(function, args, 2U, &begin))
        return false;
    instruction = (MinicCoreInstruction){0};
    instruction.kind = MINIC_CORE_INSTRUCTION_CALL;
    instruction.value.call.callee_id = add;
    instruction.value.call.argument_begin = begin;
    instruction.value.call.argument_count = 2U;
    instruction.value.call.result_object = MINIC_CORE_OBJECT_INVALID;
    if (!minic_core_function_append_source_value_instruction(builder, block,
            &instruction, tf, &sum)) return false;

    args[0].value.value_id = sum;
    if (!minic_core_function_append_call_arguments(function, args, 1U, &begin))
        return false;
    instruction = (MinicCoreInstruction){0};
    instruction.kind = MINIC_CORE_INSTRUCTION_CALL;
    instruction.value.call.callee_id = trunc;
    instruction.value.call.argument_begin = begin;
    instruction.value.call.argument_count = 1U;
    instruction.value.call.result_object = MINIC_CORE_OBJECT_INVALID;
    if (!minic_core_function_append_source_value_instruction(builder, block,
            &instruction, f64, &result)) return false;
    terminator.kind = MINIC_CORE_TERMINATOR_RETURN;
    terminator.return_value = result;
    terminator.return_object = MINIC_CORE_OBJECT_INVALID;
    return minic_core_function_set_terminator(function, block, &terminator);
}

static bool build_simple_function(MinicCoreSourceBuilder *builder) {
    MinicCoreFunction *function = &builder->core;
    MinicCoreObjectId object_id;
    MinicCoreGlobalId global_id;
    MinicCoreCalleeId callee_id;
    MinicCoreCallSignatureId signature_id;
    MinicType parameter = minic_type_int();
    MinicSourceSpan span = {0};
    size_t fixed_register_id;
    MinicCoreBlockId block;
    MinicCoreValueId value;
    MinicCoreInstruction instruction = {0};
    MinicCoreTerminator terminator = {0};
    minic_core_source_builder_initialize(builder);
    if (!minic_core_function_set_signature(builder, "answer", 6U,
                                           minic_type_int(), NULL, 0U) ||
        !minic_core_function_add_object(builder, span, minic_type_int(),
                                        &object_id) ||
        !minic_core_function_add_global(builder, "scratch", 7U,
                                        minic_type_int(), &global_id) ||
        !minic_core_function_add_fixed_register_binding(builder, "r12", 3U,
                                                         minic_type_int(), true,
                                                         &fixed_register_id) ||
        !minic_core_function_add_callee(builder, "other", 5U, minic_type_int(),
                                        &parameter, 1U, false,
                                        MINIC_CALLING_CONVENTION_DEFAULT, &callee_id) ||
        !minic_core_function_add_call_signature(builder, 29U, minic_type_int(),
                                                &parameter, 1U, false,
                                                MINIC_CALLING_CONVENTION_DEFAULT,
                                                &signature_id) ||
        !minic_core_function_add_block(function, &block)) return false;
    instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
    instruction.value.integer_value = 7;
    if (!minic_core_function_append_source_value_instruction(
            builder, block, &instruction, minic_type_int(), &value)) return false;
    terminator.kind = MINIC_CORE_TERMINATOR_RETURN;
    terminator.return_value = value;
    terminator.return_object = MINIC_CORE_OBJECT_INVALID;
    return minic_core_function_set_terminator(function, block, &terminator);
}

/* A source enum pointer and an ordinary char pointer may lower to the same
 * Core i8-pointer execution shape. C admission must reject the former before
 * publication, without adding source-origin flags to the Core instruction. */
static bool build_overflow_function(MinicCoreSourceBuilder *builder,
                                    bool enum_destination) {
    MinicCoreFunction *function = &builder->core;
    MinicType parameter_types[3];
    MinicCoreBlockId block;
    MinicCoreValueId values[3], result;
    MinicCoreInstruction instruction = {0};
    MinicCoreTerminator terminator = {0};
    size_t i;
    minic_core_source_builder_initialize(builder);
    parameter_types[0] = minic_type_int();
    parameter_types[1] = minic_type_int();
    if (!minic_type_pointer_to(enum_destination ? minic_type_enum(17U)
                                                : minic_type_signed_char(),
                               &parameter_types[2]) ||
        !minic_core_function_set_signature(builder, "enumcheck", 9U,
                                           minic_type_bool(), parameter_types, 3U) ||
        !minic_core_function_add_block(function, &block)) return false;
    for (i = 0U; i < 3U; ++i) {
        instruction.kind = MINIC_CORE_INSTRUCTION_PARAMETER;
        instruction.value.parameter_index = i;
        if (!minic_core_function_append_source_value_instruction(
                builder, block, &instruction, parameter_types[i], &values[i])) return false;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW;
    instruction.value.integer_overflow.operator_kind = MINIC_CORE_INTEGER_OVERFLOW_ADD;
    instruction.value.integer_overflow.left = values[0];
    instruction.value.integer_overflow.right = values[1];
    instruction.value.integer_overflow.result_address = values[2];
    if (!minic_core_function_append_source_value_instruction(
            builder, block, &instruction, minic_type_bool(), &result)) return false;
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
    else if (minic_type_pointee(source, &pointee) &&
             (minic_type_is_enum(pointee) ||
              minic_type_equal(pointee, minic_type_signed_char())))
        *id = ids->ptr_i8;
    else return false;
    return true;
}

int main(void) {
    MinicCoreTypeContext context;
    TypeIds ids = {0};
    MinicCoreSourceBuilder builder;
    MinicCoreFunction *function = &builder.core;
    MinicCoreFunction pure;
    MinicCoreFunction published;
    MinicCoreTypeId saved;
    MinicCoreValueId value;
    size_t extent, alignment;
    ResolverState resolver;

    CHECK(make_type_context(&context, &ids));
    CHECK(test_source_copy_access_effects());
    CHECK(build_core_only_function(&pure, &context, &ids));
    CHECK(minic_core_function_attach_type_context(&pure, &context));
    CHECK(minic_core_function_verify(&pure));
    minic_core_function_destroy(&pure);

    CHECK(build_indirect_fixture(&builder, &context, &ids));
    CHECK(minic_core_function_attach_type_context(function, &context));
    CHECK(minic_core_function_verify_core_type_snapshot(function));
    CHECK(!minic_core_function_attach_type_context(function, &context));
    CHECK(minic_core_function_type_get(function, ids.function_a) != NULL);
    CHECK(minic_core_function_type_is_kind(function, ids.i32,
                                           MINIC_CORE_TYPE_INTEGER));
    CHECK(minic_core_function_value_is_kind(function, 0U,
                                            MINIC_CORE_TYPE_POINTER));
    CHECK(!minic_core_function_value_is_kind(function, 0U,
                                             MINIC_CORE_TYPE_INTEGER));
    CHECK(!minic_core_function_value_is_kind(function,
                                             MINIC_CORE_VALUE_INVALID,
                                             MINIC_CORE_TYPE_POINTER));
    saved = function->core_function_type_id;
    function->core_function_type_id = MINIC_CORE_TYPE_INVALID;
    CHECK(!minic_core_function_verify_core_type_snapshot(function));
    function->core_function_type_id = saved;
    context.types[saved].value.function.parameters[0] = MINIC_CORE_TYPE_INVALID;
    CHECK(!minic_core_function_verify_core_type_snapshot(function));
    context.types[saved].value.function.parameters[0] = ids.ptr_function_a;

    /* Same ABI shape is not the same nominal indirect function identity. */
    saved = function->call_signatures[0].core_function_type_id;
    function->call_signatures[0].core_function_type_id = ids.function_b;
    CHECK(!minic_core_function_verify_core_type_snapshot(function));
    function->call_signatures[0].core_function_type_id = saved;
    context.types[ids.function_a].value.function.parameters[0] = MINIC_CORE_TYPE_INVALID;
    CHECK(!minic_core_function_verify_core_type_snapshot(function));
    context.types[ids.function_a].value.function.parameters[0] = ids.i32;
    saved = function->values[0].core_type_id;
    function->values[0].core_type_id = MINIC_CORE_TYPE_INVALID;
    CHECK(!minic_core_function_verify_core_type_snapshot(function));
    function->values[0].core_type_id = saved;
    CHECK(minic_core_function_verify_core_type_snapshot(function));
    minic_core_source_builder_destroy(&builder);

    CHECK(build_field_fixture(&builder, &context, &ids));
    CHECK(minic_core_function_attach_type_context(function, &context));
    CHECK(minic_core_function_verify_core_type_snapshot(function));
    CHECK(minic_core_function_verify(function));
    /* A record member's pointer type is a sealed Core constraint. The
     * retired array-element shortcut (numeric form 1) cannot be re-admitted;
     * a bit-field storage carrier also requires an actual bit-field. */
    function->instructions[1].value.field_address.access_form =
        (MinicCoreFieldAccessForm)1;
    CHECK(!minic_core_function_verify(function));
    function->instructions[1].value.field_address.access_form =
        MINIC_CORE_FIELD_ACCESS_BITFIELD_STORAGE;
    CHECK(!minic_core_function_verify(function));
    function->instructions[1].value.field_address.access_form =
        (MinicCoreFieldAccessForm)99;
    CHECK(!minic_core_function_verify(function));
    function->instructions[1].value.field_address.access_form =
        MINIC_CORE_FIELD_ACCESS_EXACT;
    function->values[1].core_type_id = ids.ptr_i8;
    function->instructions[1].core_type_id = ids.ptr_i8;
    CHECK(!minic_core_function_verify(function));
    function->values[1].core_type_id = ids.ptr_i32;
    function->instructions[1].core_type_id = ids.ptr_i32;
    CHECK(minic_core_function_verify(function));
    saved = function->instructions[1].value.field_address.core_record_type_id;
    function->instructions[1].value.field_address.core_record_type_id = ids.record_b;
    CHECK(!minic_core_function_verify_core_type_snapshot(function));
    function->instructions[1].value.field_address.core_record_type_id = ids.incomplete_record;
    CHECK(!minic_core_function_verify_core_type_snapshot(function));
    function->instructions[1].value.field_address.core_record_type_id = saved;
    function->instructions[1].value.field_address.byte_offset = 12U;
    CHECK(!minic_core_function_verify_core_type_snapshot(function));
    function->instructions[1].value.field_address.byte_offset = 8U;
    function->values[0].core_type_id = ids.ptr_record_b;
    CHECK(!minic_core_function_verify_core_type_snapshot(function));
    function->values[0].core_type_id = ids.ptr_record_a;
    CHECK(minic_core_function_verify_core_type_snapshot(function));
    minic_core_source_builder_destroy(&builder);

    CHECK(build_object_fixture(&builder, &context, &ids));
    CHECK(minic_core_function_attach_type_context(function, &context));
    CHECK(minic_core_function_object_layout(function, 0U, &extent, &alignment));
    CHECK(extent == 8U && alignment == 4U);
    /* A mismatched/overflowing array layout must be rejected by CoreType
     * registration, not by a second object-level count multiplier. */
    {
        MinicCoreType overflow_array = {0};
        MinicCoreTypeId ignored_id = MINIC_CORE_TYPE_INVALID;
        overflow_array.kind = MINIC_CORE_TYPE_ARRAY;
        overflow_array.has_layout = true;
        overflow_array.size = SIZE_MAX;
        overflow_array.alignment = 4U;
        overflow_array.value.array.element = ids.i32;
        overflow_array.value.array.count = SIZE_MAX;
        overflow_array.value.array.is_complete = true;
        CHECK(!minic_core_type_register(&context, &overflow_array, &ignored_id));
    }
    function->objects[0].core_type_id = ids.incomplete_record;
    CHECK(!minic_core_function_verify_core_type_snapshot(function));
    function->objects[0].core_type_id = ids.array_i32_2;
    CHECK(minic_core_function_verify_core_type_snapshot(function));
    minic_core_source_builder_destroy(&builder);

    /* The bulk-ingress callback must roll back ALL snapshots on failure. */
    CHECK(build_simple_function(&builder));
    CHECK(minic_core_function_verify_source_ingress(&builder));
    {
        MinicCoreSourceDraft *saved_draft = builder.source_draft;
        builder.source_draft = NULL;
        CHECK(!minic_core_function_verify_source_ingress(&builder));
        builder.source_draft = saved_draft;
        CHECK(minic_core_function_verify_source_ingress(&builder));
    }
    minic_core_function_initialize(&published);
    CHECK(!minic_core_source_builder_take_core(&builder, &published));
    CHECK(published.name == NULL && builder.source_draft != NULL);
    CHECK(function->object_count == 1U && function->global_count == 1U &&
          function->fixed_register_binding_count == 1U &&
          function->callee_count == 1U && function->call_signature_count == 1U);
    CHECK(!minic_core_function_verify(function)); /* unattached source is not Core */
    resolver.i32 = ids.i32;
    resolver.function_i32_i32 = ids.function_a;
    resolver.call_count = 0U;
    resolver.fail_on_call = 2U;
    CHECK(!minic_core_function_snapshot_types(&builder, &context,
                                               resolve_source_type, &resolver));
    CHECK(function->type_context == NULL &&
          function->core_function_type_id == MINIC_CORE_TYPE_INVALID);
    CHECK(function->callees[0].core_function_type_id == MINIC_CORE_TYPE_INVALID);
    CHECK(function->values[0].core_type_id == MINIC_CORE_TYPE_INVALID);
    /* A rejected import must leave every C draft available for a retry. */
    CHECK(builder.source_draft != NULL &&
          minic_type_equal(builder.source_draft->return_type, minic_type_int()) &&
          minic_type_equal(builder.source_draft->callees[0].return_type,
                           minic_type_int()) &&
          minic_type_equal(builder.source_draft->object_types[0], minic_type_int()) &&
          minic_type_equal(builder.source_draft->global_types[0], minic_type_int()) &&
          minic_type_equal(builder.source_draft->fixed_register_binding_types[0],
                           minic_type_int()) &&
          builder.source_draft->call_signatures[0].function_type_id == 29U &&
          minic_type_equal(minic_core_function_source_value_type(&builder, 0U),
                           minic_type_int()) &&
          builder.source_draft->callees[0].parameter_types != NULL &&
          builder.source_draft->call_signatures[0].parameter_types != NULL);
    /* Source reads resolve the defining instruction, not a separately copied
     * Value descriptor. Exact C rank must survive this ingress adapter. */
    builder.source_draft->instruction_types[0] = minic_type_long_long();
    CHECK(minic_type_equal(minic_core_function_source_value_type(&builder, 0U),
                           minic_type_long_long()) &&
          !minic_type_equal(minic_core_function_source_value_type(&builder, 0U),
                            minic_type_long()));
    builder.source_draft->instruction_types[0] = minic_type_int();
    resolver.call_count = 0U;
    resolver.fail_on_call = 0U;
    /* A declared source convention must match its imported nominal FUNCTION
     * type. The verifier no longer has a duplicate Core ABI copy to compare;
     * the single C -> Core boundary rejects contradictory source facts. */
    builder.source_draft->call_signatures[0].calling_convention =
        MINIC_CALLING_CONVENTION_MS_X64;
    CHECK(!minic_core_function_snapshot_types(&builder, &context,
                                              resolve_source_type, &resolver));
    CHECK(function->type_context == NULL && builder.source_draft != NULL &&
          function->callees[0].core_function_type_id == MINIC_CORE_TYPE_INVALID &&
          function->call_signatures[0].core_function_type_id ==
              MINIC_CORE_TYPE_INVALID);
    builder.source_draft->call_signatures[0].calling_convention =
        MINIC_CALLING_CONVENTION_DEFAULT;
    resolver.call_count = 0U;
    CHECK(minic_core_function_snapshot_types(&builder, &context,
                                              resolve_source_type, &resolver));
    CHECK(minic_core_function_verify_core_type_snapshot(function));
    {
        const MinicCoreType *callee_type = minic_core_type_get(
            &context, function->callees[0].core_function_type_id);
        CHECK(callee_type != NULL && callee_type->kind == MINIC_CORE_TYPE_FUNCTION &&
              callee_type->value.function.return_type == ids.i32 &&
              callee_type->value.function.parameter_count == 1U &&
              callee_type->value.function.parameters != NULL &&
              callee_type->value.function.parameters[0] == ids.i32 &&
              !callee_type->value.function.is_variadic &&
              callee_type->value.function.calling_convention ==
                  MINIC_CALLING_CONVENTION_DEFAULT &&
              function->callees[0].core_function_type_id !=
                  function->call_signatures[0].core_function_type_id);
    }
    /* Publication physically releases the C ingress sidecar; Core has no
     * residual C descriptors to poison or accidentally inspect. */
    CHECK(function->type_context == &context && builder.source_draft == NULL);
    CHECK(minic_core_function_source_value_type(&builder, 0U).base_kind ==
          (MinicTypeBaseKind)0x7fffffff);
    CHECK(minic_core_function_verify(function));
    value = function->instructions[0].result;
    CHECK(function->values[value].core_type_id == ids.i32);
    CHECK(function->instructions[0].core_type_id == ids.i32);
    CHECK(minic_core_source_builder_take_core(&builder, &published));
    minic_core_source_builder_destroy(&builder);
    CHECK(minic_core_function_verify(&published));
    CHECK(published.type_context == &context);
    minic_core_function_destroy(&published);

    /* The first synthesized callee may have been registered when a later
     * libcalls import fails. Retry must clear both direct IDs, without
     * requiring the synthetic linker names to exist in a C symbol table. */
    CHECK(build_tf_libcall_function(&builder));
    resolver.i32 = ids.i32;
    resolver.f64 = ids.f64;
    resolver.long_double = ids.long_double;
    resolver.call_count = 0U;
    resolver.fail_on_call = 7U; /* second direct callee's return type */
    CHECK(!minic_core_function_snapshot_types(&builder, &context,
                                              resolve_source_type, &resolver));
    CHECK(builder.source_draft != NULL && function->type_context == NULL &&
          function->callees[0].core_function_type_id == MINIC_CORE_TYPE_INVALID &&
          function->callees[1].core_function_type_id == MINIC_CORE_TYPE_INVALID);
    resolver.call_count = 0U;
    resolver.fail_on_call = 0U;
    CHECK(minic_core_function_snapshot_types(&builder, &context,
                                             resolve_source_type, &resolver));
    CHECK(minic_core_function_verify(function));
    {
        const MinicCoreCallee *add = &function->callees[0];
        const MinicCoreCallee *trunc = &function->callees[1];
        const MinicCoreType *add_type = minic_core_type_get(
            &context, add->core_function_type_id);
        const MinicCoreType *trunc_type = minic_core_type_get(
            &context, trunc->core_function_type_id);
        CHECK(strcmp(add->name, "__addtf3") == 0 &&
              strcmp(trunc->name, "__trunctfdf2") == 0 &&
              add->core_function_type_id != trunc->core_function_type_id &&
              add_type != NULL && add_type->kind == MINIC_CORE_TYPE_FUNCTION &&
              add_type->value.function.return_type == ids.long_double &&
              add_type->value.function.parameter_count == 2U &&
              add_type->value.function.parameters != NULL &&
              add_type->value.function.parameters[0] == ids.long_double &&
              add_type->value.function.parameters[1] == ids.long_double &&
              trunc_type != NULL && trunc_type->kind == MINIC_CORE_TYPE_FUNCTION &&
              trunc_type->value.function.return_type == ids.f64 &&
              trunc_type->value.function.parameter_count == 1U &&
              trunc_type->value.function.parameters != NULL &&
              trunc_type->value.function.parameters[0] == ids.long_double);
    }
    minic_core_source_builder_destroy(&builder);

    CHECK(build_overflow_function(&builder, true));
    CHECK(!minic_core_function_verify_source_ingress(&builder));
    CHECK(!minic_core_function_snapshot_types(&builder, &context,
                                              resolve_enum_overflow_type, &ids));
    CHECK(function->type_context == NULL && builder.source_draft != NULL);
    minic_core_source_builder_destroy(&builder);
    CHECK(build_overflow_function(&builder, false));
    CHECK(minic_core_function_verify_source_ingress(&builder));
    CHECK(minic_core_function_snapshot_types(&builder, &context,
                                             resolve_enum_overflow_type, &ids));
    CHECK(minic_core_function_verify_core_type_snapshot(function));
    CHECK(builder.source_draft == NULL &&
          minic_core_function_source_value_type(&builder, 2U).base_kind ==
              (MinicTypeBaseKind)0x7fffffff);
    minic_core_source_builder_destroy(&builder);
    CHECK(build_simple_function(&builder));
    CHECK(builder.source_draft != NULL);
    /* A canceled, unsealed C draft and repeated destructor must be safe. */
    minic_core_source_builder_destroy(&builder);
    minic_core_source_builder_destroy(&builder);

    /* A partially published allocation count is not proof that its array
     * exists: an OOM/cancelled Core builder must be destructible even when
     * several independent allocations did not complete. The C ingress
     * sidecar follows the same rollback contract as the pure Core arrays. */
    minic_core_source_builder_initialize(&builder);
    CHECK(minic_core_function_set_signature(
        &builder, "partial", 7U, minic_type_int(), NULL, 0U));
    function->block_count = 2U;
    function->callee_count = 2U;
    function->call_signature_count = 2U;
    function->global_count = 2U;
    function->function_symbol_count = 2U;
    function->fixed_register_binding_count = 2U;
    function->inline_asm_count = 2U;
    builder.source_draft->callee_count = 2U;
    builder.source_draft->call_signature_count = 2U;
    minic_core_source_builder_destroy(&builder);
    CHECK(builder.source_draft == NULL && function->blocks == NULL &&
          function->callees == NULL && function->inline_asms == NULL &&
          function->block_count == 0U);
    minic_core_source_builder_destroy(&builder);
    minic_core_type_context_destroy(&context);
    (void)puts("CORE_TYPE_SNAPSHOT=PASS valid malformed direct_core_producer indirect_nominal field_nominal overflow enum_source_admission rollback partial_oom");
    return 0;
}
