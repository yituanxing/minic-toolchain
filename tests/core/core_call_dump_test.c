#include "core/core_ir_source_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct DumpTypes {
    MinicCoreTypeId integer, record, function, pointer_function;
} DumpTypes;

static bool make_dump_types(MinicCoreTypeContext *context, DumpTypes *types) {
    MinicCoreType type = {0};
    MinicCoreTypeId params[2];
    minic_core_type_context_initialize(context);
    type.kind = MINIC_CORE_TYPE_INTEGER;
    type.has_layout = true;
    type.size = 4U;
    type.alignment = 4U;
    type.value.integer.bit_width = 32U;
    type.value.integer.is_signed = true;
    if (!minic_core_type_register(context, &type, &types->integer)) return false;
    (void)memset(&type, 0, sizeof(type));
    type.kind = MINIC_CORE_TYPE_RECORD;
    type.has_layout = true;
    type.size = 8U;
    type.alignment = 8U;
    type.value.record.is_complete = true;
    if (!minic_core_type_register(context, &type, &types->record)) return false;
    (void)memset(&type, 0, sizeof(type));
    params[0] = types->integer;
    params[1] = types->record;
    type.kind = MINIC_CORE_TYPE_FUNCTION;
    type.value.function.return_type = types->integer;
    type.value.function.parameters = params;
    type.value.function.parameter_count = 2U;
    if (!minic_core_type_register(context, &type, &types->function)) return false;
    (void)memset(&type, 0, sizeof(type));
    type.kind = MINIC_CORE_TYPE_POINTER;
    type.has_layout = true;
    type.size = 8U;
    type.alignment = 8U;
    type.value.pointer.pointee = types->function;
    return minic_core_type_register(context, &type, &types->pointer_function);
}

static bool attach_dump_types(MinicCoreFunction *function,
                              const MinicCoreTypeContext *context,
                              const DumpTypes *types) {
    size_t i;
    MinicCoreCallee *callee = &function->callees[0];
    MinicCoreCallSignature *signature = &function->call_signatures[0];
    if (function->value_count != 4U || function->instruction_count != 4U ||
        function->object_count != 1U || function->callee_count != 1U ||
        function->call_signature_count != 1U) return false;
    function->core_return_type_id = types->integer;
    function->objects[0].core_type_id = types->record;
    function->values[0].core_type_id = types->pointer_function;
    function->instructions[0].core_type_id = types->pointer_function;
    for (i = 1U; i < 4U; ++i) {
        function->values[i].core_type_id = types->integer;
        function->instructions[i].core_type_id = types->integer;
    }
    callee->core_return_type_id = types->integer;
    signature->core_function_type_id = types->function;
    signature->core_return_type_id = types->integer;
    callee->core_parameter_type_ids = malloc(2U * sizeof(MinicCoreTypeId));
    signature->core_parameter_type_ids = malloc(2U * sizeof(MinicCoreTypeId));
    if (callee->core_parameter_type_ids == NULL ||
        signature->core_parameter_type_ids == NULL) return false;
    callee->core_parameter_type_ids[0] = types->integer;
    callee->core_parameter_type_ids[1] = types->record;
    signature->core_parameter_type_ids[0] = types->integer;
    signature->core_parameter_type_ids[1] = types->record;
    return minic_core_function_attach_type_context(function, context);
}

static bool build_call_dump_function(MinicCoreFunction *function) {
    MinicType parameters[2] = {minic_type_int(), minic_type_record(0U)};
    MinicType function_pointer;
    MinicCoreBlockId block;
    MinicCoreObjectId object;
    MinicCoreFunctionSymbolId symbol;
    MinicCoreCalleeId callee;
    MinicCoreCallSignatureId signature;
    MinicCoreValueId function_address, integer, result;
    MinicCoreCallArgument arguments[2];
    MinicCoreInstruction instruction;
    MinicCoreTerminator terminator;
    size_t argument_begin;

    if (!minic_type_pointer_to(minic_type_function(0U), &function_pointer) ||
        !minic_core_function_set_signature(function, "probe", 5U,
                                           minic_type_int(), NULL, 0U) ||
        !minic_core_function_add_block(function, &block) ||
        !minic_core_function_add_object(function, (MinicSourceSpan){0},
                                        parameters[1], &object) ||
        !minic_core_function_add_function_symbol(function, "target", 6U, &symbol) ||
        !minic_core_function_add_callee(function, "target", 6U,
                                        minic_type_int(), parameters, 2U, false,
                                        MINIC_CALLING_CONVENTION_DEFAULT, &callee) ||
        !minic_core_function_add_call_signature(function, 0U,
                                                 minic_type_int(), parameters, 2U,
                                                 false, MINIC_CALLING_CONVENTION_DEFAULT,
                                                 &signature)) return false;

    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS;
    instruction.value.function_symbol_id = symbol;
    if (!minic_core_function_append_source_value_instruction(
            function, block, &instruction, function_pointer, &function_address)) return false;

    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
    instruction.value.integer_value = 7;
    if (!minic_core_function_append_source_value_instruction(
            function, block, &instruction, minic_type_int(), &integer)) return false;

    (void)memset(arguments, 0, sizeof(arguments));
    arguments[0].kind = MINIC_CORE_CALL_ARGUMENT_VALUE;
    arguments[0].value.value_id = integer;
    arguments[1].kind = MINIC_CORE_CALL_ARGUMENT_OBJECT;
    arguments[1].value.object_id = object;
    if (!minic_core_function_append_call_arguments(
            function, arguments, 2U, &argument_begin)) return false;

    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_CALL;
    instruction.value.call.callee_id = callee;
    instruction.value.call.argument_begin = argument_begin;
    instruction.value.call.argument_count = 2U;
    instruction.value.call.result_object = MINIC_CORE_OBJECT_INVALID;
    if (!minic_core_function_append_source_value_instruction(
            function, block, &instruction, minic_type_int(), &result)) return false;

    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_INDIRECT_CALL;
    instruction.value.indirect_call.callee = function_address;
    instruction.value.indirect_call.signature_id = signature;
    instruction.value.indirect_call.argument_begin = argument_begin;
    instruction.value.indirect_call.argument_count = 2U;
    instruction.value.indirect_call.result_object = MINIC_CORE_OBJECT_INVALID;
    if (!minic_core_function_append_source_value_instruction(
            function, block, &instruction, minic_type_int(), &result)) return false;

    (void)memset(&terminator, 0, sizeof(terminator));
    terminator.kind = MINIC_CORE_TERMINATOR_RETURN;
    terminator.return_value = result;
    terminator.return_object = MINIC_CORE_OBJECT_INVALID;
    return minic_core_function_set_terminator(function, block, &terminator);
}

int main(void) {
    static const char expected[] =
        "core function @probe\n"
        "object %o0\n"
        "bb0:\n"
        "  %0 = function.addr @target\n"
        "  %1 = const.int 7\n"
        "  %2 = call @target(%1, %o0)\n"
        "  %3 = call.indirect %0(%1, %o0) signature=0\n"
        "  return %3\n";
    MinicCoreFunction function;
    MinicCoreTypeContext type_context;
    DumpTypes types = {0};
    FILE *output;
    char buffer[sizeof(expected) + 8U];
    size_t length;
    int status = 0;
    bool type_context_ready;
    bool built, attached, verified, dumped;

    minic_core_function_initialize(&function);
    type_context_ready = make_dump_types(&type_context, &types);
    output = tmpfile();
    built = type_context_ready && build_call_dump_function(&function);
    attached = built && attach_dump_types(&function, &type_context, &types);
    verified = attached && minic_core_function_verify(&function);
    dumped = verified && output != NULL && minic_core_function_dump(output, &function);
    if (!dumped || fflush(output) != 0 || fseek(output, 0L, SEEK_SET) != 0) {
        (void)fprintf(stderr,
                      "CORE_CALL_DUMP=FAIL typed=%d built=%d attached=%d verified=%d dumped=%d\n",
                      type_context_ready, built, attached, verified, dumped);
        status = 1;
    } else {
        length = fread(buffer, 1U, sizeof(buffer) - 1U, output);
        buffer[length] = '\0';
        if (ferror(output) || strcmp(buffer, expected) != 0) status = 2;
    }
    if (output != NULL) (void)fclose(output);
    minic_core_function_destroy(&function);
    minic_core_type_context_destroy(&type_context);
    if (status == 0) (void)puts("CORE_CALL_DUMP=PASS direct+indirect value+object exact-text");
    return status;
}
