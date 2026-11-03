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
    (void)program;
    (void)target;
    (void)function;
    (void)sections;
    if (source != NULL || state == NULL || index != 0U ||
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

static bool inspect_legacy(FILE *out, const MinicC0Program *program,
                           const MinicTargetInfo *target, const MinicFunction *source,
                           const MinicCoreModuleFunctionSymbol *symbol,
                           const MinicCoreFunction *function,
                           const MinicCoreFunctionFacts *facts,
                           const MinicCoreObjectStoragePlan *objects,
                           size_t index, bool sections, void *context) {
    size_t *visited = context;
    (void)out;
    (void)target;
    (void)function;
    (void)sections;
    if (program == NULL || source != &program->functions[0] ||
        source->name == NULL || strcmp(source->name, "poison_c_symbol") != 0 ||
        symbol != NULL || facts != NULL || objects != NULL ||
        index != 0U || visited == NULL) return false;
    ++*visited;
    return true;
}

static bool inspect_unprepared(FILE *out, const MinicC0Program *program,
                               const MinicTargetInfo *target, const MinicFunction *source,
                               const MinicCoreFunction *function, size_t index,
                               bool sections, void *context) {
    size_t *visited = context;
    (void)out;
    (void)target;
    (void)function;
    (void)sections;
    if (source != (program != NULL ? &program->functions[0] : NULL) ||
        index != 0U || visited == NULL) return false;
    ++*visited;
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

    /* A finalized Module also works with no borrowed Program at all.
     * Both visitor families must expose NULL source declarations. */
    state.visited = 0U;
    if (!minic_program_visit_prepared_functions_module(
            out, NULL, &target, &function, 1U, false,
            MINIC_PROGRAM_FUNCTION_LIVE_DEFINITIONS, false,
            inspect, &state, &module) ||
        state.visited != 1U || plan_builds != 2U) return 8;
    state.visited = 0U;
    if (!minic_program_visit_functions_module(
            out, NULL, &target, &function, 1U, false,
            MINIC_PROGRAM_FUNCTION_LIVE_DEFINITIONS, false,
            inspect_unprepared, &state.visited, &module) ||
        state.visited != 1U) return 9;

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
        state.visited != 1U || plan_builds != 2U) return 3;
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
    /* Non-module legacy clients still receive the original source pointer. */
    module_symbol.is_defined = false;
    source.is_defined = false;
    source.is_internal = false;
    state.visited = 0U;
    if (!minic_program_visit_prepared_functions_module(
            out, &program, &target, &function, 1U, false,
            MINIC_PROGRAM_FUNCTION_DECLARATIONS, false,
            inspect_legacy, &state.visited, NULL) ||
        state.visited != 1U || plan_builds != 2U) return 10;
    state.visited = 0U;
    if (!minic_program_visit_functions_module(
            out, &program, &target, &function, 1U, false,
            MINIC_PROGRAM_FUNCTION_DECLARATIONS, false,
            inspect_unprepared, &state.visited, NULL) ||
        state.visited != 1U) return 11;
    if (fclose(out) != 0) return 7;
    puts("MINIC_MODULE_FUNCTION_OWNER=PASS sealed_source_null=2 no_program=2 legacy_source=2 rollback=2");
    return 0;
}
