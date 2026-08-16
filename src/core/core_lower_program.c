#include "core/core_lower_program.h"

#include "frontend/function_body.h"

#include <stdint.h>
#include <stdlib.h>

void minic_core_lowered_program_initialize(MinicCoreLoweredProgram *program) {
    if (program == NULL) {
        return;
    }
    program->functions = NULL;
    program->statuses = NULL;
    program->function_count = 0U;
}

void minic_core_lowered_program_destroy(MinicCoreLoweredProgram *program) {
    size_t function_index;

    if (program == NULL) {
        return;
    }
    for (function_index = 0U; function_index < program->function_count; ++function_index) {
        minic_core_function_destroy(&program->functions[function_index]);
    }
    free(program->functions);
    free(program->statuses);
    minic_core_lowered_program_initialize(program);
}

bool minic_core_lower_program(const MinicC0Program *source, MinicCoreLoweredProgram *output) {
    MinicCoreLoweredProgram lowered;
    size_t function_index;

    if (source == NULL || output == NULL ||
        source->function_count > SIZE_MAX / sizeof(*lowered.functions) ||
        source->function_count > SIZE_MAX / sizeof(*lowered.statuses)) {
        return false;
    }
    minic_core_lowered_program_initialize(&lowered);
    lowered.function_count = source->function_count;
    if (lowered.function_count != 0U) {
        lowered.functions =
            (MinicCoreFunction *)calloc(lowered.function_count, sizeof(*lowered.functions));
        lowered.statuses =
            (MinicCoreLowerStatus *)malloc(lowered.function_count * sizeof(*lowered.statuses));
        if (lowered.functions == NULL || lowered.statuses == NULL) {
            minic_core_lowered_program_destroy(&lowered);
            return false;
        }
    }

    for (function_index = 0U; function_index < lowered.function_count; ++function_index) {
        minic_core_function_initialize(&lowered.functions[function_index]);
        lowered.statuses[function_index] = MINIC_CORE_LOWER_UNSUPPORTED;
    }
    for (function_index = 0U; function_index < lowered.function_count; ++function_index) {
        const MinicFunction *function;
        MinicFunctionBodyView body;

        function = minic_c0_program_function(source, function_index);
        if (function == NULL) {
            lowered.statuses[function_index] = MINIC_CORE_LOWER_ERROR;
            continue;
        }
        if (!function->is_defined) {
            continue;
        }
        if (!minic_c0_function_body_view(source, function_index, &body)) {
            lowered.statuses[function_index] = MINIC_CORE_LOWER_ERROR;
            continue;
        }
        lowered.statuses[function_index] =
            minic_core_lower_function(&body, &lowered.functions[function_index]);
    }

    minic_core_lowered_program_destroy(output);
    *output = lowered;
    return true;
}
