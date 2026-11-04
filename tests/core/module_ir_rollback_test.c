/* Module construction commits only after every owned array is populated.
 * Simulate failure after count publication but before/during calloc so OOM
 * cleanup never dereferences a NULL array or leaks half-built fragments. */
#include "core/module_ir.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *owned(const char *value) {
    const size_t size = strlen(value) + 1U;
    char *copy = malloc(size);
    if (copy != NULL) memcpy(copy, value, size);
    return copy;
}

static bool erased(const MinicCoreModule *module) {
    return module->globals == NULL && module->global_count == 0U &&
           module->function_symbols == NULL && module->function_count == 0U &&
           module->file_asms == NULL && module->file_asm_count == 0U &&
           module->type_context == NULL && !module->finalized;
}

int main(void) {
    MinicCoreModule module;
    minic_core_module_initialize(&module);
    /* Case 1: first/second/third allocation failed before any array owns
     * memory. The published counts must not drive a NULL indexed read. */
    module.global_count = 3U;
    module.function_count = 2U;
    module.file_asm_count = 4U;
    minic_core_module_destroy(&module);
    if (!erased(&module)) return 1;

    /* Allocation of the fragment array itself can fail after its count
     * has been provisionally recorded. Destruction must not index NULL. */
    module.global_count = 1U;
    module.globals = calloc(1U, sizeof(*module.globals));
    if (module.globals == NULL) return 8;
    module.globals[0].fragment_count = 1U;
    minic_core_module_destroy(&module);
    if (!erased(&module)) return 9;

    /* Case 2: only globals were allocated; function/file ASM arrays failed.
     * A partially constructed relocation owns an independent target name. */
    module.global_count = 2U;
    module.globals = calloc(module.global_count, sizeof(*module.globals));
    if (module.globals == NULL) return 2;
    module.globals[0].name = owned("state");
    module.globals[0].fragments = calloc(1U, sizeof(*module.globals[0].fragments));
    module.globals[0].fragment_count = 1U;
    module.globals[0].fragment_capacity = 1U;
    if (module.globals[0].fragments != NULL)
        module.globals[0].fragments[0].relocation_symbol = owned("callee");
    module.globals[1].name = owned("unused");
    if (module.globals[0].name == NULL || module.globals[0].fragments == NULL ||
        module.globals[0].fragments[0].relocation_symbol == NULL ||
        module.globals[1].name == NULL) {
        minic_core_module_destroy(&module);
        return 3;
    }
    module.function_count = 3U;
    module.file_asm_count = 1U;
    minic_core_module_destroy(&module);
    if (!erased(&module)) return 4;

    /* Case 3: global allocation absent but function and file ASM ownership
     * published. Their strings must still be freed independently. */
    module.global_count = 1U;
    module.function_count = 1U;
    module.function_symbols = calloc(1U, sizeof(*module.function_symbols));
    module.file_asm_count = 1U;
    module.file_asms = calloc(1U, sizeof(*module.file_asms));
    if (module.function_symbols == NULL || module.file_asms == NULL) {
        minic_core_module_destroy(&module);
        return 5;
    }
    module.function_symbols[0].name = owned("main");
    module.function_symbols[0].specialization_source_index = SIZE_MAX;
    module.function_symbols[0].section_name = owned(".text.main");
    module.file_asms[0].text = owned(".globl marker");
    module.file_asms[0].length = strlen(".globl marker");
    if (module.function_symbols[0].name == NULL ||
        module.function_symbols[0].section_name == NULL ||
        module.file_asms[0].text == NULL) {
        minic_core_module_destroy(&module);
        return 6;
    }
    minic_core_module_destroy(&module);
    if (!erased(&module)) return 7;
    minic_core_module_destroy(&module);
    puts("CORE_MODULE_ROLLBACK=PASS null_arrays partial_fragments strings repeat_destroy");
    return 0;
}
