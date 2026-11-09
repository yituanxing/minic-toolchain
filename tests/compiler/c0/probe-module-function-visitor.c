/* The machine visitor has exactly one declaration owner: sealed ModuleIR.
 * It never accepts C Program, including as a seemingly-unused argument. */
#include "target/common/program_visit.h"
#include <stdio.h>
#include <string.h>

typedef struct ProbeState {
    const MinicCoreModule *module;
    const MinicCoreModuleFunctionSymbol *symbol;
    bool expect_definition;
    size_t visited;
} ProbeState;

static size_t plan_builds;

bool minic_core_function_plan_build(
    const MinicCoreFunction *function, MinicCoreFunctionPlan *plan) {
    if (function == NULL || plan == NULL || plan->prepared) return false;
    ++plan_builds;
    plan->prepared = true;
    return true;
}

void minic_core_function_plan_destroy(MinicCoreFunctionPlan *plan) {
    if (plan != NULL) (void)memset(plan, 0, sizeof(*plan));
}

static bool inspect(FILE *out, const MinicCoreModule *module,
                    const MinicTargetInfo *target,
                    const MinicCoreModuleFunctionSymbol *symbol,
                    const MinicCoreFunction *function,
                    const MinicCoreFunctionFacts *facts,
                    const MinicCoreObjectStoragePlan *objects,
                    size_t index, bool sections, void *context) {
    ProbeState *state = context;
    (void)out;
    (void)target;
    (void)function;
    (void)sections;
    if (state == NULL || module != state->module || index != 0U ||
        symbol != state->symbol || symbol == NULL ||
        symbol->name == NULL || strcmp(symbol->name, "module_symbol") != 0 ||
        symbol->section_name == NULL ||
        strcmp(symbol->section_name, ".text.module_only") != 0 ||
        !symbol->is_weak || symbol->explicit_alignment != 32U ||
        symbol->is_defined != state->expect_definition ||
        (facts != NULL) != state->expect_definition ||
        (objects != NULL) != state->expect_definition)
        return false;
    ++state->visited;
    return true;
}

int main(void) {
    MinicCoreFunction function = {0};
    MinicCoreModuleFunctionSymbol symbol = {0};
    MinicCoreModule module = {0};
    MinicTargetInfo target = {0};
    ProbeState state = {0};
    FILE *out = tmpfile();
    if (out == NULL) return 1;

    symbol.name = "module_symbol";
    symbol.section_name = ".text.module_only";
    symbol.is_weak = true;
    symbol.explicit_alignment = 32U;
    symbol.is_defined = true;
    symbol.is_referenced = true;
    symbol.storage_owner_function_index = SIZE_MAX;
    module.function_symbols = &symbol;
    module.function_count = 1U;
    module.finalized = true;
    state.module = &module;
    state.symbol = &symbol;
    state.expect_definition = true;

    if (!minic_program_visit_prepared_functions_module(
            out, &module, &target, &function, 1U, false,
            MINIC_PROGRAM_FUNCTION_LIVE_DEFINITIONS, false,
            inspect, &state) ||
        state.visited != 1U || plan_builds != 1U) return 2;

    /* Declarations do not prepare a Core plan. Definitions are selected
     * exclusively by the module symbol, including internal liveness. */
    symbol.is_defined = false;
    state.visited = 0U;
    state.expect_definition = false;
    if (!minic_program_visit_prepared_functions_module(
            out, &module, &target, &function, 1U, false,
            MINIC_PROGRAM_FUNCTION_DECLARATIONS, false,
            inspect, &state) ||
        state.visited != 1U || plan_builds != 1U) return 3;
    state.visited = 0U;
    if (!minic_program_visit_prepared_functions_module(
            out, &module, &target, &function, 1U, false,
            MINIC_PROGRAM_FUNCTION_LIVE_DEFINITIONS, false,
            inspect, &state) || state.visited != 0U) return 4;

    module.finalized = false;
    if (minic_program_visit_prepared_functions_module(
            out, &module, &target, &function, 1U, false,
            MINIC_PROGRAM_FUNCTION_DECLARATIONS, false,
            inspect, &state)) return 5;
    module.finalized = true;
    module.function_count = 0U;
    if (minic_program_visit_prepared_functions_module(
            out, &module, &target, &function, 1U, false,
            MINIC_PROGRAM_FUNCTION_DECLARATIONS, false,
            inspect, &state)) return 6;
    if (minic_program_visit_prepared_functions_module(
            out, NULL, &target, &function, 1U, false,
            MINIC_PROGRAM_FUNCTION_DECLARATIONS, false,
            inspect, &state)) return 7;
    if (fclose(out) != 0) return 8;
    puts("MINIC_MODULE_FUNCTION_OWNER=PASS sealed_module_only=1 definition=1 declaration=1 rollback=3");
    return 0;
}
