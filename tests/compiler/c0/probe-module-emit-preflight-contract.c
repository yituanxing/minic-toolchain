#include "target/common/program_emit.h"

#include <stdio.h>

/* The full-module writer's single preflight owns typed Module verification
 * and live-function arena identity. A discarded function's borrowed arena
 * is deliberately irrelevant: it is never emitted. */
int main(void) {
    MinicCoreTypeContext owned = {0}, foreign = {0};
    MinicCoreModuleFunctionSymbol symbols[2] = {
        {.name = "live", .is_defined = true,
         .alias_target_index = SIZE_MAX,
         .specialization_source_index = SIZE_MAX},
        {.name = "discarded", .is_defined = false,
         .alias_target_index = SIZE_MAX,
         .specialization_source_index = SIZE_MAX}
    };
    MinicCoreFunction functions[2] = {{0}, {0}};
    MinicCoreModuleFragment numeric = {
        .kind = MINIC_CORE_MODULE_FRAGMENT_BITS,
        .byte_count = 4U, .bits = 42U
    };
    MinicCoreModuleGlobal global = {
        .name = "forged", .emittable = true,
        .core_type_id = MINIC_CORE_TYPE_INVALID,
        .storage_alignment = 4U, .storage_size = 4U,
        .fragments = &numeric, .fragment_count = 1U,
        .alias_target_index = SIZE_MAX,
        .owner_function_index = SIZE_MAX
    };
    MinicCoreModule module = {
        .type_context = &owned, .finalized = true,
        .function_symbols = symbols, .function_count = 2U
    };
    functions[0].type_context = &owned;
    functions[1].type_context = &foreign;
    if (!minic_program_emit_module_preflight(&module, functions, 2U)) return 1;
    functions[0].type_context = &foreign;
    if (minic_program_emit_module_preflight(&module, functions, 2U)) return 2;
    functions[0].type_context = &owned;
    if (minic_program_emit_module_preflight(&module, NULL, 2U)) return 3;
    if (minic_program_emit_module_preflight(&module, functions, 1U)) return 4;
    module.finalized = false;
    if (minic_program_emit_module_preflight(&module, functions, 2U)) return 5;
    module.finalized = true;
    module.globals = &global;
    module.global_count = 1U;
    if (minic_program_emit_module_preflight(&module, functions, 2U)) return 6;
    module.globals = NULL;
    module.global_count = 0U;
    if (!minic_program_emit_module_preflight(&module, functions, 2U)) return 7;
    puts("MODULE_EMIT_PREFLIGHT_CONTRACT=PASS forged_finalized=1 alien_arena=1 dead_arena_exception=1 null_and_count=2 finalized=1");
    return 0;
}
