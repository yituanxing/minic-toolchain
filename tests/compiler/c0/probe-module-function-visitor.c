/* A sealed ModuleIR, not the C declaration, owns production function
 * selection and symbol metadata.  Deliberately disagree with the source
 * declaration to detect accidental old-Program decisions in the visitor. */
#include "target/common/program_visit.h"
#include <stdio.h>
#include <string.h>

typedef struct ProbeState {
    const MinicCoreModuleFunctionSymbol *symbol;
    bool expect_definition;
    size_t visited;
} ProbeState;

static size_t plan_builds;

/* A zero-object synthetic function needs no target geometry.  The real
 * FunctionPlan/verifier are independently tested; this probe owns only the
 * visitor's choice of producer and the borrowed callback contract. */
bool minic_core_function_plan_build(
    const MinicCoreFunction *function, MinicCoreFunctionPlan *plan) {
    if (function == NULL || plan == NULL ||
        plan->prepared) return false;
    ++plan_builds;
    plan->prepared = true;
    return true;
}

void minic_core_function_plan_destroy(MinicCoreFunctionPlan *plan) {
    if (plan != NULL) (void)memset(plan, 0, sizeof(*plan));
}

static bool inspect(FILE *out, const MinicC0Program *program,
                    const MinicTargetInfo *target, const MinicFunction *source,
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
    if (program == NULL || source == NULL || state == NULL || index != 0U ||
        source->name == NULL || strcmp(source->name, "poison_c_symbol") != 0 ||
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
    MinicFunction source = {0};
    MinicC0Program program = {0};
    MinicCoreFunction function = {0};
    MinicCoreModuleFunctionSymbol module_symbol = {0};
    MinicCoreModule module = {0};
    MinicTargetInfo target = {0};
    ProbeState state = {0};
    FILE *out = tmpfile();
    if (out == NULL) return 1;

    source.name = "poison_c_symbol";
    source.is_defined = false;
    source.is_internal = true;
    program.functions = &source;
    program.function_count = 1U;

    module_symbol.name = "module_symbol";
    module_symbol.section_name = ".text.module_only";
    module_symbol.is_weak = true;
    module_symbol.explicit_alignment = 32U;
    module_symbol.is_defined = true;
    module_symbol.is_referenced = true;
    module.function_symbols = &module_symbol;
    module.function_count = 1U;
    module.finalized = true;
    state.symbol = &module_symbol;
    state.expect_definition = true;

    if (!minic_program_visit_prepared_functions_module(
            out, &program, &target, &function, 1U, false,
            MINIC_PROGRAM_FUNCTION_LIVE_DEFINITIONS, false,
            inspect, &state, &module) ||
        state.visited != 1U || plan_builds != 1U) return 2;

    /* A C definition cannot turn an unsealed Module declaration into an
     * emitted definition, and declarations must not prepare a FunctionPlan. */
    source.is_defined = true;
    source.is_internal = false;
    module_symbol.is_defined = false;
    state.visited = 0U;
    state.expect_definition = false;
    if (!minic_program_visit_prepared_functions_module(
            out, &program, &target, &function, 1U, false,
            MINIC_PROGRAM_FUNCTION_DECLARATIONS, false,
            inspect, &state, &module) ||
        state.visited != 1U || plan_builds != 1U) return 3;
    state.visited = 0U;
    if (!minic_program_visit_prepared_functions_module(
            out, &program, &target, &function, 1U, false,
            MINIC_PROGRAM_FUNCTION_LIVE_DEFINITIONS, false,
            inspect, &state, &module) || state.visited != 0U) return 4;

    module.finalized = false;
    if (minic_program_visit_prepared_functions_module(
            out, &program, &target, &function, 1U, false,
            MINIC_PROGRAM_FUNCTION_DECLARATIONS, false,
            inspect, &state, &module)) return 5;
    module.finalized = true;
    module.function_count = 0U;
    if (minic_program_visit_prepared_functions_module(
            out, &program, &target, &function, 1U, false,
            MINIC_PROGRAM_FUNCTION_DECLARATIONS, false,
            inspect, &state, &module)) return 6;
    if (fclose(out) != 0) return 7;
    puts("MINIC_MODULE_FUNCTION_OWNER=PASS definition=1 declaration=1 poison_C=1 rollback=2");
    return 0;
}
