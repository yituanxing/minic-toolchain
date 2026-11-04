/* A machine PIC/ELF decision must use ModuleIR's sealed linkage snapshot,
 * even if a later C Program declares a completely different symbol. */
#include "core/module_ir.h"
#include "target/target_info.h"

#include <stdio.h>

#define CHECK(check, code) do { if (!(check)) return (code); } while (0)

int main(void) {
    MinicCoreModuleGlobal globals[] = {
        {.name = "internal_object", .is_internal = true,
         .alias_target_index = SIZE_MAX},
        {.name = "hidden_object", .is_extern = true,
         .visibility = MINIC_CORE_MODULE_VISIBILITY_HIDDEN,
         .alias_target_index = SIZE_MAX},
        {.name = "weak_object", .is_extern = true, .is_weak = true,
         .alias_target_index = SIZE_MAX},
        {.name = "tentative_object", .is_extern = true, .is_weak = true,
         .is_tentative = true, .alias_target_index = SIZE_MAX},
        {.name = "scope_extern", .is_internal = true,
         .is_block_scope_extern_only = true,
         .alias_target_index = SIZE_MAX},
    };
    MinicCoreModuleFunctionSymbol functions[] = {
        {.name = "internal_function", .is_defined = true, .is_internal = true,
         .alias_target_index = SIZE_MAX},
        {.name = "hidden_function",
         .visibility = MINIC_CORE_MODULE_VISIBILITY_INTERNAL,
         .alias_target_index = SIZE_MAX},
        {.name = "weak_function", .is_weak = true,
         .alias_target_index = SIZE_MAX},
        {.name = "visible_function", .is_defined = true,
         .alias_target_index = SIZE_MAX},
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
    CHECK(minic_target_info_elf_module_object_is_tu_local(
              &module, "internal_object"), 1);
    CHECK(!minic_target_info_elf_module_object_is_tu_local(
              &module, "hidden_object"), 2);
    CHECK(!minic_target_info_elf_module_object_is_tu_local(
              &module, "scope_extern"), 3);
    CHECK(minic_target_info_elf_module_function_is_tu_local(
              &module, "internal_function"), 4);
    CHECK(!minic_target_info_elf_module_function_is_tu_local(
              &module, "hidden_function"), 5);
    CHECK(minic_target_info_elf_module_object_is_nonpreemptible(
              &module, "internal_object"), 6);
    CHECK(minic_target_info_elf_module_object_is_nonpreemptible(
              &module, "hidden_object"), 7);
    CHECK(!minic_target_info_elf_module_object_is_nonpreemptible(
              &module, "weak_object"), 8);
    CHECK(!minic_target_info_elf_module_object_is_nonpreemptible(
              &module, "scope_extern"), 9);
    CHECK(minic_target_info_elf_module_function_is_nonpreemptible(
              &module, "internal_function"), 10);
    CHECK(minic_target_info_elf_module_function_is_nonpreemptible(
              &module, "hidden_function"), 11);
    CHECK(!minic_target_info_elf_module_function_is_nonpreemptible(
              &module, "visible_function"), 12);
    CHECK(minic_target_info_elf_module_object_is_undefined_weak(
              &module, "weak_object"), 13);
    CHECK(!minic_target_info_elf_module_object_is_undefined_weak(
              &module, "tentative_object"), 14);
    CHECK(minic_target_info_elf_module_function_is_undefined_weak(
              &module, "weak_function"), 15);
    CHECK(!minic_target_info_elf_module_function_is_undefined_weak(
              &module, "internal_function"), 16);
    CHECK(!minic_target_info_elf_module_object_is_tu_local(
              &module, "source_only_name") &&
          !minic_target_info_elf_module_object_is_nonpreemptible(
              &module, "source_only_name") &&
          !minic_target_info_elf_module_object_is_undefined_weak(
              &module, "source_only_name") &&
          !minic_target_info_elf_module_function_is_tu_local(
              &module, "source_only_name") &&
          !minic_target_info_elf_module_function_is_nonpreemptible(
              &module, "source_only_name") &&
          !minic_target_info_elf_module_function_is_undefined_weak(
              &module, "source_only_name"), 17);
    module.finalized = false;
    CHECK(!minic_target_info_elf_module_object_is_nonpreemptible(
              &module, "internal_object") &&
          !minic_target_info_elf_module_function_is_undefined_weak(
              &module, "weak_function"), 18);
    module.finalized = true;
    module.globals = NULL;
    CHECK(!minic_target_info_elf_module_object_is_tu_local(
              &module, "internal_object"), 19);
    module.function_symbols = NULL;
    CHECK(!minic_target_info_elf_module_function_is_tu_local(
              &module, "internal_function"), 20);
    puts("MINIC_CORE_MODULE_ELF_LINKAGE=PASS six_predicates=6 cases=20");
    return 0;
}
