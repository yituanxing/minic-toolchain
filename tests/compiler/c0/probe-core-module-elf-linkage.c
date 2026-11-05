/* A machine PIC/ELF decision must use ModuleIR's sealed linkage snapshot,
 * even if a later C Program declares a completely different symbol. */
#include "core/module_ir.h"
#include "target/common/elf_module_symbol_info.h"

#include <stdio.h>

#define CHECK(check, code) do { if (!(check)) return (code); } while (0)
#define OBJ(symbol) minic_multi_elf_module_symbol_facts( \
    &module, (symbol), MINIC_ELF_MODULE_SYMBOL_OBJECT)
#define FN(symbol) minic_multi_elf_module_symbol_facts( \
    &module, (symbol), MINIC_ELF_MODULE_SYMBOL_FUNCTION)

int main(void) {
    MinicCoreModuleGlobal globals[] = {
        {.name = "internal_object", .is_internal = true,
         .emittable = true, .alias_target_index = SIZE_MAX},
        {.name = "hidden_object", .definition_kind = MINIC_CORE_MODULE_DEFINITION_DECLARATION,
         .visibility = MINIC_CORE_MODULE_VISIBILITY_HIDDEN,
         .alias_target_index = SIZE_MAX},
        {.name = "weak_object", .definition_kind = MINIC_CORE_MODULE_DEFINITION_DECLARATION,
         .is_weak = true,
         .alias_target_index = SIZE_MAX},
        {.name = "tentative_object", .is_weak = true, .emittable = true,
         .definition_kind = MINIC_CORE_MODULE_DEFINITION_ZERO_FILL,
         .data_policy = MINIC_CORE_MODULE_DATA_ZERO_ONLY,
         .alias_target_index = SIZE_MAX},
        {.name = "scope_extern", .is_internal = true,
         .definition_kind = MINIC_CORE_MODULE_DEFINITION_DECLARATION,
         .alias_target_index = SIZE_MAX},
    };
    MinicCoreModuleFunctionSymbol functions[] = {
        {.name = "internal_function", .is_defined = true, .is_internal = true,
         .is_referenced = true, .alias_target_index = SIZE_MAX,
         .specialization_source_index = SIZE_MAX},
        {.name = "hidden_function",
         .visibility = MINIC_CORE_MODULE_VISIBILITY_INTERNAL,
         .alias_target_index = SIZE_MAX, .specialization_source_index = SIZE_MAX},
        {.name = "weak_function", .is_weak = true,
         .alias_target_index = SIZE_MAX, .specialization_source_index = SIZE_MAX},
        {.name = "visible_function", .is_defined = true,
         .alias_target_index = SIZE_MAX, .specialization_source_index = SIZE_MAX},
    };
    MinicCoreModuleGlobal mixed_globals[] = {
        {.name = "weak_first", .definition_kind = MINIC_CORE_MODULE_DEFINITION_DECLARATION,
         .is_weak = true,
         .alias_target_index = SIZE_MAX},
        {.name = "weak_first", .is_internal = true, .emittable = true,
         .alias_target_index = SIZE_MAX},
        {.name = "strong_first", .is_internal = true, .emittable = true,
         .alias_target_index = SIZE_MAX},
        {.name = "strong_first", .definition_kind = MINIC_CORE_MODULE_DEFINITION_DECLARATION,
         .is_weak = true,
         .alias_target_index = SIZE_MAX},
        {.name = "weak_alias", .definition_kind = MINIC_CORE_MODULE_DEFINITION_DECLARATION,
         .is_weak = true,
         .emittable = true, .alias_target_index = 1U},
        {.name = "pruned", .is_internal = true,
         .alias_target_index = SIZE_MAX},
        {.name = "pruned", .definition_kind = MINIC_CORE_MODULE_DEFINITION_DECLARATION,
         .is_weak = true,
         .alias_target_index = SIZE_MAX},
        {.name = "local_alias", .is_internal = true,
         .definition_kind = MINIC_CORE_MODULE_DEFINITION_DECLARATION,
         .emittable = true, .alias_target_index = 1U},
        /* ELF names are untyped: opposite-kind strong owners take precedence. */
        {.name = "function_owned_object",
         .definition_kind = MINIC_CORE_MODULE_DEFINITION_DECLARATION,
         .is_weak = true, .alias_target_index = SIZE_MAX},
        {.name = "object_owned_function", .emittable = true,
         .alias_target_index = SIZE_MAX},
    };
    MinicCoreModuleFunctionSymbol mixed_functions[] = {
        {.name = "weak_first", .is_weak = true,
         .alias_target_index = SIZE_MAX, .specialization_source_index = SIZE_MAX},
        {.name = "weak_first", .is_defined = true, .is_internal = true,
         .is_referenced = true, .alias_target_index = SIZE_MAX,
         .specialization_source_index = SIZE_MAX},
        {.name = "strong_first", .is_defined = true, .is_internal = true,
         .is_referenced = true, .alias_target_index = SIZE_MAX,
         .specialization_source_index = SIZE_MAX},
        {.name = "strong_first", .is_weak = true,
         .alias_target_index = SIZE_MAX, .specialization_source_index = SIZE_MAX},
        {.name = "weak_alias", .is_weak = true, .alias_target_index = 1U,
         .specialization_source_index = SIZE_MAX},
        {.name = "pruned", .is_defined = true, .is_internal = true,
         .alias_target_index = SIZE_MAX, .specialization_source_index = SIZE_MAX},
        {.name = "pruned", .is_weak = true,
         .alias_target_index = SIZE_MAX, .specialization_source_index = SIZE_MAX},
        {.name = "local_alias", .is_internal = true,
         .alias_target_index = 1U, .specialization_source_index = SIZE_MAX},
        {.name = "function_owned_object", .is_defined = true,
         .alias_target_index = SIZE_MAX, .specialization_source_index = SIZE_MAX},
        {.name = "object_owned_function", .is_weak = true,
         .alias_target_index = SIZE_MAX, .specialization_source_index = SIZE_MAX},
    };
    MinicCoreModule module = {
        .globals = globals,
        .global_count = sizeof(globals) / sizeof(globals[0]),
        .function_symbols = functions,
        .function_count = sizeof(functions) / sizeof(functions[0]),
        .finalized = true
    };

    /* Locality is stricter than nonpreemptibility: external hidden
     * declarations may be nonpreemptible without a TU-owned definition. */
    CHECK(OBJ("internal_object").tu_local, 1);
    CHECK(!OBJ("hidden_object").tu_local, 2);
    CHECK(!OBJ("scope_extern").tu_local, 3);
    CHECK(FN("internal_function").tu_local, 4);
    CHECK(!FN("hidden_function").tu_local, 5);
    CHECK(OBJ("internal_object").nonpreemptible, 6);
    CHECK(OBJ("hidden_object").nonpreemptible, 7);
    CHECK(!OBJ("weak_object").nonpreemptible, 8);
    CHECK(!OBJ("scope_extern").nonpreemptible, 9);
    CHECK(FN("internal_function").nonpreemptible, 10);
    CHECK(FN("hidden_function").nonpreemptible, 11);
    CHECK(!FN("visible_function").nonpreemptible, 12);
    CHECK(OBJ("weak_object").undefined_weak, 13);
    CHECK(!OBJ("tentative_object").undefined_weak, 14);
    CHECK(FN("weak_function").undefined_weak, 15);
    CHECK(!FN("internal_function").undefined_weak, 16);
    CHECK(OBJ("weak_function").undefined_weak &&
          FN("weak_object").undefined_weak, 35);
    CHECK(!OBJ("source_only_name").tu_local &&
          !OBJ("source_only_name").nonpreemptible &&
          !OBJ("source_only_name").undefined_weak &&
          !FN("source_only_name").tu_local &&
          !FN("source_only_name").nonpreemptible &&
          !FN("source_only_name").undefined_weak, 17);
    module.finalized = false;
    CHECK(!OBJ("internal_object").nonpreemptible &&
          !FN("weak_function").undefined_weak, 18);
    module.finalized = true;
    module.globals = NULL;
    CHECK(!OBJ("internal_object").tu_local, 19);
    module.function_symbols = NULL;
    CHECK(!FN("internal_function").tu_local, 20);
    module.globals = mixed_globals;
    module.global_count = sizeof(mixed_globals) / sizeof(mixed_globals[0]);
    module.function_symbols = mixed_functions;
    module.function_count = sizeof(mixed_functions) / sizeof(mixed_functions[0]);
    /* The emitted symbol owner wins over an extern-only weak declaration,
     * irrespective of source order. A weak alias is itself emitted, not an
     * unresolved weak. A dead internal body does not claim a local symbol. */
    CHECK(OBJ("weak_first").tu_local && OBJ("weak_first").nonpreemptible &&
          !OBJ("weak_first").undefined_weak, 21);
    CHECK(OBJ("strong_first").tu_local &&
          !OBJ("strong_first").undefined_weak, 22);
    CHECK(!OBJ("weak_alias").undefined_weak, 23);
    CHECK(!OBJ("pruned").tu_local && OBJ("pruned").undefined_weak, 24);
    CHECK(FN("weak_first").tu_local && FN("weak_first").nonpreemptible &&
          !FN("weak_first").undefined_weak, 25);
    CHECK(FN("strong_first").tu_local &&
          !FN("strong_first").undefined_weak, 26);
    CHECK(!FN("weak_alias").undefined_weak, 27);
    CHECK(!FN("pruned").tu_local && FN("pruned").undefined_weak, 28);
    CHECK(!minic_multi_elf_module_symbol_facts(
          &module, "weak_first", (MinicElfModuleSymbolKind)99).tu_local, 29);
    CHECK(OBJ("local_alias").tu_local &&
          OBJ("local_alias").nonpreemptible, 30);
    CHECK(FN("local_alias").tu_local &&
          FN("local_alias").nonpreemptible, 31);
    CHECK(!OBJ("function_owned_object").undefined_weak &&
          !FN("function_owned_object").undefined_weak, 32);
    CHECK(!OBJ("object_owned_function").undefined_weak &&
          !FN("object_owned_function").undefined_weak, 33);
    CHECK(!OBJ("function_owned_object").tu_local &&
          !FN("object_owned_function").tu_local, 34);
    puts("MINIC_CORE_MODULE_ELF_LINKAGE=PASS facts=3 kinds=2 cases=35 cross_kind=4");
    return 0;
}
