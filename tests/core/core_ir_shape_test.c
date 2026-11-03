/* MC-055: these are deliberately malformed public Core API objects, NOT
 * cases claimed to be emitted by a valid C frontend producer. */
#include "core/core_ir_internal.h"

#include <stdio.h>
#include <string.h>

static bool build_function(MinicCoreFunction *function, const char *mode) {
    MinicType result_type;
    MinicCoreBlockId block;
    MinicCoreValueId value;
    MinicCoreInstruction instruction;
    MinicCoreTerminator terminator;
    MinicType parameter_type;
    size_t parameter_count = 0U;

    /* Source builders publish a draft signature first. They may add source
     * fixed bindings and enum identities only while that draft is alive. */
    if (strcmp(mode, "fixed") == 0 || strcmp(mode, "variadic") == 0)
        result_type = minic_type_int();
    else if (strcmp(mode, "symbol") == 0) {
        if (!minic_type_pointer_to(minic_type_function(0U), &result_type))
            return false;
    } else if (strcmp(mode, "enum") == 0 ||
               strcmp(mode, "enum-missing") == 0)
        result_type = minic_type_enum(123U);
    else if (strcmp(mode, "enum-pointer") == 0) {
        if (!minic_type_pointer_to(minic_type_enum(123U), &result_type))
            return false;
    } else return false;
    if (strcmp(mode, "enum-pointer") == 0 || strcmp(mode, "variadic") == 0) {
        parameter_type = result_type;
        parameter_count = 1U;
    }
    if (!minic_core_function_set_signature(function, "probe", 5U,
                                           result_type,
                                           parameter_count == 0U ? NULL : &parameter_type,
                                           parameter_count)) return false;
    if (strcmp(mode, "fixed") == 0) {
        size_t binding;
        if (!minic_core_function_add_fixed_register_binding(
                function, "rax", 3U, result_type, false, &binding)) return false;
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_FIXED_REGISTER_READ;
        instruction.value.fixed_register_binding_id = binding;
    } else if (strcmp(mode, "symbol") == 0) {
        MinicCoreFunctionSymbolId symbol;
        if (!minic_core_function_add_function_symbol(
                function, "callee", 6U, &symbol)) return false;
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS;
        instruction.value.function_symbol_id = symbol;
    } else if (strcmp(mode, "enum") == 0 || strcmp(mode, "enum-missing") == 0) {
        if (strcmp(mode, "enum") == 0 &&
            !minic_core_function_add_enum_type(function, 123U, minic_type_int()))
            return false;
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
        instruction.value.integer_value = 7;
    } else if (strcmp(mode, "enum-pointer") == 0) {
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_PARAMETER;
        instruction.value.parameter_index = 0U;
    } else if (strcmp(mode, "variadic") == 0) {
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_PARAMETER;
        instruction.value.parameter_index = 0U;
    } else {
        return false;
    }
    if ((strcmp(mode, "variadic") == 0 &&
         !minic_core_function_set_variadic(function, true)) ||
        !minic_core_function_add_block(function, &block) ||
        !minic_core_function_append_source_value_instruction(
            function, block, &instruction, result_type, &value)) return false;
    (void)memset(&terminator, 0, sizeof(terminator));
    terminator.kind = MINIC_CORE_TERMINATOR_RETURN;
    terminator.return_value = value;
    terminator.return_object = MINIC_CORE_OBJECT_INVALID;
    return minic_core_function_set_terminator(function, block, &terminator);
}

static int run_case(const char *case_name) {
    MinicCoreFunction function;
    const char *kind = NULL;
    bool expected;
    bool verified;

    if (strncmp(case_name, "fixed", 5U) == 0) kind = "fixed";
    else if (strncmp(case_name, "symbol", 6U) == 0) kind = "symbol";
    else if (strcmp(case_name, "enum-pointer") == 0) kind = "enum-pointer";
    else if (strncmp(case_name, "enum-missing", 12U) == 0) kind = "enum-missing";
    else if (strncmp(case_name, "enum", 4U) == 0) kind = "enum";
    else if (strcmp(case_name, "variadic") == 0) kind = "variadic";
    if (kind == NULL) return 90;

    minic_core_function_initialize(&function);
    if (!build_function(&function, kind)) {
        minic_core_function_destroy(&function);
        return 91;
    }
    if (strcmp(case_name, "variadic") == 0 &&
        (!function.is_variadic ||
         !minic_core_function_set_variadic(&function, false) ||
         function.is_variadic ||
         !minic_core_function_set_variadic(&function, true) ||
         !function.is_variadic)) {
        minic_core_function_destroy(&function);
        return 94;
    }
    if (strcmp(case_name, "fixed-null") == 0) {
        MinicCoreFixedRegisterBinding *saved = function.fixed_register_bindings;
        function.fixed_register_bindings = NULL;
        verified = minic_core_function_verify_source_ingress(&function);
        function.fixed_register_bindings = saved;
        expected = false;
    } else if (strcmp(case_name, "fixed-overcapacity") == 0) {
        size_t saved = function.fixed_register_binding_count;
        function.fixed_register_binding_count = function.fixed_register_binding_capacity + 1U;
        verified = minic_core_function_verify_source_ingress(&function);
        function.fixed_register_binding_count = saved;
        expected = false;
    } else if (strcmp(case_name, "symbol-null") == 0) {
        MinicCoreFunctionSymbol *saved = function.function_symbols;
        function.function_symbols = NULL;
        verified = minic_core_function_verify_source_ingress(&function);
        function.function_symbols = saved;
        expected = false;
    } else if (strcmp(case_name, "symbol-overcapacity") == 0) {
        size_t saved = function.function_symbol_count;
        function.function_symbol_count = function.function_symbol_capacity + 1U;
        verified = minic_core_function_verify_source_ingress(&function);
        function.function_symbol_count = saved;
        expected = false;
    } else if (strcmp(case_name, "enum-null") == 0) {
        MinicCoreEnumType *saved = function.source_draft->enum_types;
        function.source_draft->enum_types = NULL;
        verified = minic_core_function_verify_source_ingress(&function);
        function.source_draft->enum_types = saved;
        expected = false;
    } else if (strcmp(case_name, "enum-overcapacity") == 0) {
        size_t saved = function.source_draft->enum_type_count;
        function.source_draft->enum_type_count =
            function.source_draft->enum_type_capacity + 1U;
        verified = minic_core_function_verify_source_ingress(&function);
        function.source_draft->enum_type_count = saved;
        expected = false;
    } else {
        verified = minic_core_function_verify_source_ingress(&function);
        expected = strcmp(case_name, "enum-missing") != 0;
    }
    minic_core_function_destroy(&function);
    if (verified != expected) {
        (void)fprintf(stderr, "CORE_IR_SHAPE=FAIL case=%s verified=%d expected=%d\n",
                      case_name, verified, expected);
        return 92;
    }
    (void)printf("CORE_IR_SHAPE=PASS case=%s verified=%d\n", case_name, verified);
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 2) return 93;
    return run_case(argv[1]);
}
