#include "frontend/ast.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

bool minic_c0_program_set_function_internal(MinicC0Program *program,
                                            MinicFunctionId function_id,
                                            bool is_internal) {
    if (program == NULL || function_id >= program->function_count ||
        (is_internal && program->functions[function_id].is_weak)) {
        return false;
    }
    program->functions[function_id].is_internal = is_internal;
    return true;
}

bool minic_c0_program_set_function_weak(MinicC0Program *program,
                                        MinicFunctionId function_id,
                                        bool is_weak) {
    if (program == NULL || function_id >= program->function_count ||
        (is_weak && program->functions[function_id].is_internal)) {
        return false;
    }
    program->functions[function_id].is_weak = is_weak;
    return true;
}

bool minic_c0_program_set_function_alias(MinicC0Program *program,
                                         MinicFunctionId function_id,
                                         MinicFunctionId target_function_id) {
    MinicFunction *function;

    if (program == NULL || function_id >= program->function_count ||
        target_function_id >= program->function_count || function_id == target_function_id) {
        return false;
    }
    function = &program->functions[function_id];
    if (function->is_defined) {
        return false;
    }
    if (function->alias_target != MINIC_FUNCTION_INVALID) {
        return function->alias_target == target_function_id;
    }
    function->alias_target = target_function_id;
    return true;
}

bool minic_c0_program_set_function_variadic(MinicC0Program *program,
                                            MinicFunctionId function_id,
                                            bool is_variadic) {
    if (program == NULL || function_id >= program->function_count) {
        return false;
    }
    program->functions[function_id].is_variadic = is_variadic;
    return true;
}

bool minic_c0_program_set_function_assembler_name(MinicC0Program *program,
                                                  MinicFunctionId function_id,
                                                  const char *name,
                                                  size_t name_length) {
    MinicFunction *function;
    char *copy;

    if (program == NULL || function_id >= program->function_count || name == NULL ||
        name_length == 0U || name_length == SIZE_MAX) {
        return false;
    }
    function = &program->functions[function_id];
    if (function->assembler_name != NULL) {
        return function->assembler_name_length == name_length &&
               memcmp(function->assembler_name, name, name_length) == 0;
    }
    copy = (char *)malloc(name_length + 1U);
    if (copy == NULL) {
        return false;
    }
    (void)memcpy(copy, name, name_length);
    copy[name_length] = '\0';
    function->assembler_name = copy;
    function->assembler_name_length = name_length;
    return true;
}

const char *minic_c0_function_symbol_name(const MinicFunction *function) {
    if (function == NULL) {
        return NULL;
    }
    return function->assembler_name != NULL ? function->assembler_name : function->name;
}

bool minic_c0_program_set_function_visibility(MinicC0Program *program,
                                              MinicFunctionId function_id,
                                              MinicSymbolVisibility visibility) {
    MinicFunction *function;

    if (program == NULL || function_id >= program->function_count ||
        visibility < MINIC_SYMBOL_VISIBILITY_DEFAULT ||
        visibility > MINIC_SYMBOL_VISIBILITY_PROTECTED) {
        return false;
    }
    function = &program->functions[function_id];
    if (function->visibility != MINIC_SYMBOL_VISIBILITY_DEFAULT &&
        function->visibility != visibility) {
        return false;
    }
    function->visibility = visibility;
    return true;
}

bool minic_c0_program_set_function_section(MinicC0Program *program,
                                           MinicFunctionId function_id,
                                           const char *name,
                                           size_t name_length) {
    MinicFunction *function;
    char *copy;

    if (program == NULL || function_id >= program->function_count || name == NULL ||
        name_length == 0U || name_length == SIZE_MAX) {
        return false;
    }
    function = &program->functions[function_id];
    if (function->section_name != NULL) {
        return function->section_name_length == name_length &&
               memcmp(function->section_name, name, name_length) == 0;
    }
    copy = (char *)malloc(name_length + 1U);
    if (copy == NULL) {
        return false;
    }
    (void)memcpy(copy, name, name_length);
    copy[name_length] = '\0';
    function->section_name = copy;
    function->section_name_length = name_length;
    return true;
}
