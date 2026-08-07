#include "frontend/ast.h"

bool minic_c0_program_set_function_internal(MinicC0Program *program,
                                            MinicFunctionId function_id,
                                            bool is_internal) {
    if (program == NULL || function_id >= program->function_count) {
        return false;
    }
    program->functions[function_id].is_internal = is_internal;
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
