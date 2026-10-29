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

bool minic_c0_program_set_function_inline(MinicC0Program *program,
                                          MinicFunctionId function_id,
                                          bool is_inline) {
    if (program == NULL || function_id >= program->function_count) {
        return false;
    }
    if (is_inline) {
        program->functions[function_id].is_inline = true;
    }
    return true;
}

bool minic_c0_program_set_function_noreturn(MinicC0Program *program,
                                            MinicFunctionId function_id,
                                            bool is_noreturn) {
    if (program == NULL || function_id >= program->function_count) {
        return false;
    }
    if (is_noreturn) {
        program->functions[function_id].is_noreturn = true;
    }
    return true;
}

bool minic_c0_program_set_function_referenced(MinicC0Program *program,
                                              MinicFunctionId function_id,
                                              bool is_referenced) {
    if (program == NULL || function_id >= program->function_count) {
        return false;
    }
    if (is_referenced) {
        program->functions[function_id].is_referenced = true;
    }
    return true;
}

bool minic_c0_program_set_function_constructor(MinicC0Program *program,
                                               MinicFunctionId function_id) {
    MinicFunction *function;
    if (program == NULL || function_id >= program->function_count) return false;
    function = &program->functions[function_id];
    if (!function->is_defined || !minic_type_is_void(function->return_type) ||
        function->parameter_count != 0U || function->is_variadic ||
        function->alias_target != MINIC_FUNCTION_INVALID) return false;
    function->is_constructor = true;
    /* Static constructors are linked through init_array, not source calls. */
    function->is_referenced = true;
    function->force_emit = true;
    return true;
}

bool minic_c0_program_set_function_force_emit(MinicC0Program *program,
                                              MinicFunctionId function_id,
                                              bool force_emit) {
    if (program == NULL || function_id >= program->function_count) {
        return false;
    }
    if (force_emit) {
        program->functions[function_id].force_emit = true;
    }
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
    /* An alias has no independent instruction entry to pad. Until the owner
     * alignment is proven, reject the impossible promise to align the alias
     * more strictly than its target. */
    if (function->explicit_alignment != 0U) {
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

bool minic_c0_program_set_function_alignment(MinicC0Program *program,
                                             MinicFunctionId function_id,
                                             size_t alignment) {
    MinicFunction *function;
    /* Guard against unbounded ELF section padding and invalid logarithms.
     * 1 MiB supports substantially more than Linux's normal cacheline and
     * hotpatch alignments, while giant attributes still fail closed. */
    if (program == NULL || function_id >= program->function_count ||
        alignment == 0U || alignment > 1048576U ||
        (alignment & (alignment - 1U)) != 0U)
        return false;
    function = &program->functions[function_id];
    if (function->alias_target != MINIC_FUNCTION_INVALID)
        return false;
    if (alignment > function->explicit_alignment)
        function->explicit_alignment = alignment;
    return true;
}
