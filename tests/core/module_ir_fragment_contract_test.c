/* One target-neutral fragment contract for ModuleIR verification and every
 * architecture's emitter; neither may accept malformed ranges or symbols. */
#include "core/module_ir.h"

#include <stdio.h>
#include <stdint.h>

#define CHECK(test, code) do { if (!(test)) return (code); } while (0)

static int verify_module_provenance_edges(const MinicCoreModule *baseline) {
    MinicCoreModuleFunctionSymbol functions[3] = {
        {.name = "source", .is_defined = true, .alias_target_index = SIZE_MAX},
        {.name = "clone", .is_defined = true, .alias_target_index = SIZE_MAX,
         .is_specialization = true, .specialization_source_index = 0U},
        {.name = "other_clone", .is_defined = true, .alias_target_index = SIZE_MAX,
         .is_specialization = true, .specialization_source_index = 0U}
    };
    MinicCoreModuleReference label = {
        .kind = MINIC_CORE_MODULE_REFERENCE_OWNER_LABEL, .target_index = 0U
    };
    MinicCoreModuleGlobal local = {
        .name = "local_table", .alias_target_index = SIZE_MAX,
        .owner_function_index = 0U, .references = &label,
        .reference_count = 1U
    };
    MinicCoreModule module = *baseline;

    /* A canonical defined source may own multiple specialization clones,
     * and a static label reference may name its matching canonical owner. */
    module.function_symbols = functions;
    module.function_count = 3U;
    module.globals = &local;
    module.global_count = 1U;
    CHECK(minic_core_module_verify(&module), 20);

    functions[1].specialization_source_index = 1U;
    CHECK(!minic_core_module_verify(&module), 21); /* clone is its own source */
    functions[1].specialization_source_index = 3U;
    CHECK(!minic_core_module_verify(&module), 22); /* out of range */
    functions[1].specialization_source_index = 0U;
    functions[0].is_defined = false;
    CHECK(!minic_core_module_verify(&module), 23); /* undefined source */
    functions[0].is_defined = true;
    functions[2].specialization_source_index = 1U;
    CHECK(!minic_core_module_verify(&module), 24); /* nested clone source */
    functions[2].specialization_source_index = 0U;
    CHECK(minic_core_module_verify(&module), 25);

    label.target_index = 1U;
    CHECK(!minic_core_module_verify(&module), 26); /* wrong owner index */
    local.owner_function_index = 1U;
    CHECK(!minic_core_module_verify(&module), 27); /* clone cannot own label */
    local.owner_function_index = 0U;
    label.target_index = 0U;
    functions[1].is_specialization = false;
    functions[2].is_specialization = false;
    functions[0].is_defined = false;
    CHECK(!minic_core_module_verify(&module), 28); /* undefined label owner */
    functions[0].is_defined = true;
    local.owner_function_index = SIZE_MAX;
    CHECK(!minic_core_module_verify(&module), 29); /* orphaned source label */
    local.owner_function_index = 0U;
    label.target_index = 3U;
    CHECK(!minic_core_module_verify(&module), 30); /* forged out-of-range */
    label.target_index = 0U;
    CHECK(minic_core_module_verify(&module), 31);
    return 0;
}

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
    CHECK(verify_module_provenance_edges(&module) == 0, 32);
    symbols[0].name = NULL;
    CHECK(!minic_core_module_verify(&module), 19);
    minic_core_type_context_destroy(&types);
    puts("CORE_MODULE_FRAGMENT_CONTRACT=PASS bounds forms names alias-target clone_provenance=5 forged_owner_label=5 canonical_source=valid");
    return 0;
}
