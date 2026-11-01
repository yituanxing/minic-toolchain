#include "core/core_ir.h"

#include <stdio.h>
#include <string.h>

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
    instruction.type = function_pointer;
    instruction.value.function_symbol_id = symbol;
    if (!minic_core_function_append_value_instruction(
            function, block, &instruction, &function_address)) return false;

    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
    instruction.type = minic_type_int();
    instruction.value.integer_value = 7;
    if (!minic_core_function_append_value_instruction(
            function, block, &instruction, &integer)) return false;

    (void)memset(arguments, 0, sizeof(arguments));
    arguments[0].kind = MINIC_CORE_CALL_ARGUMENT_VALUE;
    arguments[0].value.value_id = integer;
    arguments[1].kind = MINIC_CORE_CALL_ARGUMENT_OBJECT;
    arguments[1].value.object_id = object;
    if (!minic_core_function_append_call_arguments(
            function, arguments, 2U, &argument_begin)) return false;

    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_CALL;
    instruction.type = minic_type_int();
    instruction.value.call.callee_id = callee;
    instruction.value.call.argument_begin = argument_begin;
    instruction.value.call.argument_count = 2U;
    instruction.value.call.result_object = MINIC_CORE_OBJECT_INVALID;
    if (!minic_core_function_append_value_instruction(
            function, block, &instruction, &result)) return false;

    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_INDIRECT_CALL;
    instruction.type = minic_type_int();
    instruction.value.indirect_call.callee = function_address;
    instruction.value.indirect_call.signature_id = signature;
    instruction.value.indirect_call.argument_begin = argument_begin;
    instruction.value.indirect_call.argument_count = 2U;
    instruction.value.indirect_call.result_object = MINIC_CORE_OBJECT_INVALID;
    if (!minic_core_function_append_value_instruction(
            function, block, &instruction, &result)) return false;

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
    FILE *output;
    char buffer[sizeof(expected) + 8U];
    size_t length;
    int status = 0;

    minic_core_function_initialize(&function);
    output = tmpfile();
    if (output == NULL || !build_call_dump_function(&function) ||
        !minic_core_function_verify(&function) ||
        !minic_core_function_dump(output, &function) ||
        fflush(output) != 0 || fseek(output, 0L, SEEK_SET) != 0) {
        status = 1;
    } else {
        length = fread(buffer, 1U, sizeof(buffer) - 1U, output);
        buffer[length] = '\0';
        if (ferror(output) || strcmp(buffer, expected) != 0) status = 2;
    }
    if (output != NULL) (void)fclose(output);
    minic_core_function_destroy(&function);
    if (status == 0) (void)puts("CORE_CALL_DUMP=PASS direct+indirect value+object exact-text");
    return status;
}
