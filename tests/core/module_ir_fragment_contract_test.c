/* One target-neutral fragment contract for ModuleIR verification and every
 * architecture's emitter; neither may accept malformed ranges or symbols. */
#include "core/module_ir.h"

#include <stdio.h>
#include <stdint.h>

#define CHECK(test, code) do { if (!(test)) return (code); } while (0)

int main(void) {
    MinicCoreModuleFragment parts[3] = {
        {.kind = MINIC_CORE_MODULE_FRAGMENT_ZERO,
         .byte_offset = 0U, .byte_count = 3U},
        {.kind = MINIC_CORE_MODULE_FRAGMENT_BITS,
         .byte_offset = 3U, .byte_count = 2U, .bits = 0x1234U},
        {.kind = MINIC_CORE_MODULE_FRAGMENT_RELOCATION,
         .byte_offset = 5U, .byte_count = 8U,
         .relocation_kind = MINIC_CORE_MODULE_RELOCATION_FUNCTION,
         .relocation_symbol = "callee"}
    };
    MinicCoreModuleGlobal object = {
        .name = "valid", .storage_size = 13U,
        .fragments = parts, .fragment_count = 3U
    };
    MinicCoreTypeContext types;
    MinicCoreModuleFunctionSymbol symbols[2] = {
        {.name = "callee", .is_defined = true, .alias_target_index = SIZE_MAX},
        {.name = "alias", .alias_target_index = 0U,
         .alias_target_name = "callee"}
    };
    MinicCoreModule module = {
        .type_context = &types,
        .function_symbols = symbols, .function_count = 2U,
        .finalized = true
    };

    minic_core_type_context_initialize(&types);
    CHECK(minic_core_module_global_fragments_valid(&object), 1);
    CHECK(!minic_core_module_global_fragments_valid(NULL), 2);

    object.storage_size = 12U;
    CHECK(!minic_core_module_global_fragments_valid(&object), 3);
    object.storage_size = 13U;
    parts[2].byte_offset = SIZE_MAX;
    CHECK(!minic_core_module_global_fragments_valid(&object), 4);
    parts[2].byte_offset = 5U;
    parts[2].byte_count = SIZE_MAX;
    CHECK(!minic_core_module_global_fragments_valid(&object), 5);
    parts[2].byte_count = 8U;
    parts[2].relocation_symbol = "callee\n.injected";
    CHECK(!minic_core_module_global_fragments_valid(&object), 6);
    parts[2].relocation_symbol = "callee";

    parts[2].relocation_kind = MINIC_CORE_MODULE_RELOCATION_LABEL;
    parts[2].relocation_symbol = ".Luser_007";
    CHECK(!minic_core_module_global_fragments_valid(&object), 7);
    parts[2].relocation_symbol = ".Luser_7";
    parts[2].relocation_addend = 1;
    CHECK(!minic_core_module_global_fragments_valid(&object), 8);
    parts[2].relocation_addend = 0;
    CHECK(minic_core_module_global_fragments_valid(&object), 9);

    parts[2].kind = MINIC_CORE_MODULE_FRAGMENT_ZERO;
    CHECK(!minic_core_module_global_fragments_valid(&object), 10);
    parts[2].relocation_symbol = NULL;
    CHECK(minic_core_module_global_fragments_valid(&object), 11);
    parts[2].kind = MINIC_CORE_MODULE_FRAGMENT_RELOCATION;
    parts[2].relocation_kind = MINIC_CORE_MODULE_RELOCATION_OBJECT;
    parts[2].relocation_symbol = "obj.name$1";
    CHECK(minic_core_module_global_fragments_valid(&object), 12);

    parts[1].byte_count = 3U;
    CHECK(!minic_core_module_global_fragments_valid(&object), 13);
    parts[1].byte_count = 2U;
    parts[1].relocation_symbol = "not_permitted";
    CHECK(!minic_core_module_global_fragments_valid(&object), 14);
    parts[1].relocation_symbol = NULL;

    object.fragments = NULL;
    CHECK(!minic_core_module_global_fragments_valid(&object), 15);
    object.fragments = parts;
    object.fragment_count = 0U;
    CHECK(!minic_core_module_global_fragments_valid(&object), 16);
    object.storage_size = 0U;
    CHECK(minic_core_module_global_fragments_valid(&object), 17);

    CHECK(minic_core_module_verify(&module), 18);
    symbols[0].name = NULL;
    CHECK(!minic_core_module_verify(&module), 19);
    minic_core_type_context_destroy(&types);
    puts("CORE_MODULE_FRAGMENT_CONTRACT=PASS bounds forms names alias-target");
    return 0;
}
