/* Independently constructed Core producer: two Module objects own one
 * assembler spelling, but a third unrelated static has no live edges. */
#include "compiler/core_reachability.h"

#include <stdio.h>
#include <string.h>

int main(void) {
    MinicCoreModuleGlobal globals[3] = {0};
    MinicCoreModuleFunctionSymbol function_symbol = {0};
    MinicCoreGlobal referenced_global = {0};
    MinicCoreInstruction instruction = {0};
    MinicCoreInstructionId instruction_id = 0U;
    MinicCoreBlock block = {0};
    MinicCoreFunction function = {0};
    MinicCoreLowerStatus status = MINIC_CORE_LOWER_OK;
    MinicCoreFunctionSet set = {0};

    globals[0].name = "same_asm_symbol";
    globals[0].definition_kind = MINIC_CORE_MODULE_DEFINITION_DECLARATION;
    globals[0].is_internal = false;
    globals[0].emittable = true;
    globals[0].owner_function_index = SIZE_MAX;

    globals[1].name = "same_asm_symbol";
    globals[1].definition_kind = MINIC_CORE_MODULE_DEFINITION_REGULAR;
    globals[1].is_internal = true;
    globals[1].emittable = true;
    globals[1].owner_function_index = SIZE_MAX;

    globals[2].name = "unrelated_dead_static";
    globals[2].definition_kind = MINIC_CORE_MODULE_DEFINITION_REGULAR;
    globals[2].is_internal = true;
    globals[2].emittable = true;
    globals[2].owner_function_index = SIZE_MAX;

    function_symbol.name = "main";
    function_symbol.is_defined = true;
    function_symbol.is_reachability_root = true;
    function_symbol.alias_target_index = SIZE_MAX;
    function_symbol.storage_owner_function_index = SIZE_MAX;
    referenced_global.name = "same_asm_symbol";
    referenced_global.name_length = strlen(referenced_global.name);
    instruction.kind = MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS;
    instruction.value.global_id = 0U;
    block.instructions = &instruction_id;
    block.instruction_count = 1U;
    function.blocks = &block;
    function.block_count = 1U;
    function.instructions = &instruction;
    function.instruction_count = 1U;
    function.globals = &referenced_global;
    function.global_count = 1U;
    set.functions = &function;
    set.statuses = &status;
    set.function_count = 1U;
    set.module.function_symbols = &function_symbol;
    set.module.function_count = 1U;
    set.module.globals = globals;
    set.module.global_count = 3U;

    if (!minic_core_function_set_close_reachability(&set, false) ||
        !globals[0].emittable || !globals[1].emittable ||
        globals[2].emittable) {
        fputs("CORE_DUPLICATE_OBJECT_RANGE=FAIL\n", stderr);
        return 1;
    }
    puts("CORE_DUPLICATE_OBJECT_RANGE=PASS duplicate_owners=2 unrelated_static=pruned");
    return 0;
}
