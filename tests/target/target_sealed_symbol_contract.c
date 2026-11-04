#define _POSIX_C_SOURCE 200809L
#include "core/symbol_identity.h"
#include "target/common/module_global_emit.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *data_directive(size_t width) {
    return width == 8U ? ".8byte" : width == 4U ? ".4byte" : NULL;
}

static bool fragment_assembly(const char *symbol,
                              MinicCoreModuleRelocationKind kind,
                              int64_t addend, bool accepted,
                              const char *expected) {
    MinicCoreModuleFragment fragment = {0};
    MinicCoreModuleGlobal global = {.alias_target_index = SIZE_MAX};
    FILE *output;
    char *assembly = NULL;
    size_t length = 0U;
    bool result, success;
    fragment.kind = MINIC_CORE_MODULE_FRAGMENT_RELOCATION;
    fragment.byte_count = 8U;
    fragment.relocation_kind = kind;
    fragment.relocation_symbol = (char *)symbol;
    fragment.relocation_addend = addend;
    global.fragment_count = 1U;
    global.fragments = &fragment;
    global.storage_size = 8U;
    output = open_memstream(&assembly, &length);
    if (output == NULL) return false;
    result = minic_multi_module_emit_global_fragments(
        output, &global, data_directive, false, 8U);
    if (fclose(output) != 0) {
        free(assembly);
        return false;
    }
    success = result == accepted &&
        (!accepted || (length == strlen(expected) &&
                       memcmp(assembly, expected, length) == 0));
    free(assembly);
    return success;
}

int main(void) {
    char *label = NULL, *deferred = NULL;
    bool passed = false;
    static const char *const invalid_labels[] = {
        "", ".Luser_", ".Luser_x", ".Luser_01", ".Luser_-1",
        ".Luser_7\n.text", ".Luser_7:","foo"
    };
    static const char *const invalid_deferred[] = {
        "", "__minic_deferred_asm_immediate_",
        "__minic_deferred_asm_immediate_07_0",
        "__minic_deferred_asm_immediate_7_1",
        "__minic_deferred_asm_immediate_7_0\n.globl evil",
        "__minic_deferred_asm_immediate_-7_0",
        ".Luser_7"
    };
    size_t i;

    if (!minic_core_format_label_symbol(42U, &label) ||
        !minic_core_format_deferred_asm_symbol(42U, &deferred))
        goto done;
    if (strcmp(label, ".Luser_42") != 0 ||
        strcmp(deferred, "__minic_deferred_asm_immediate_42_0") != 0 ||
        !minic_core_label_symbol_valid(label) ||
        !minic_core_deferred_asm_symbol_valid(deferred) ||
        !minic_core_label_symbol_valid(".Luser_0") ||
        !minic_core_deferred_asm_symbol_valid(
            "__minic_deferred_asm_immediate_0_0"))
        goto done;
    for (i = 0U; i < sizeof(invalid_labels) / sizeof(invalid_labels[0]); ++i)
        if (minic_core_label_symbol_valid(invalid_labels[i]))
            goto done;
    for (i = 0U; i < sizeof(invalid_deferred) / sizeof(invalid_deferred[0]); ++i)
        if (minic_core_deferred_asm_symbol_valid(invalid_deferred[i]))
            goto done;
    if (!fragment_assembly(label, MINIC_CORE_MODULE_RELOCATION_LABEL, 0,
                           true, "  .8byte .Luser_42\n") ||
        !fragment_assembly(".Luser_0", MINIC_CORE_MODULE_RELOCATION_LABEL, 0,
                           true, "  .8byte .Luser_0\n") ||
        !fragment_assembly(".Luser_42\n.globl evil",
                           MINIC_CORE_MODULE_RELOCATION_LABEL, 0,
                           false, NULL) ||
        !fragment_assembly(".Luser_01", MINIC_CORE_MODULE_RELOCATION_LABEL, 0,
                           false, NULL) ||
        !fragment_assembly(".Luser_42", MINIC_CORE_MODULE_RELOCATION_LABEL, 1,
                           false, NULL) ||
        !fragment_assembly(NULL, MINIC_CORE_MODULE_RELOCATION_LABEL, 0,
                           false, NULL) ||
        !fragment_assembly("extern_name", MINIC_CORE_MODULE_RELOCATION_OBJECT,
                           4, true, "  .8byte extern_name+4\n"))
        goto done;
    passed = true;
done:
    free(label);
    free(deferred);
    if (!passed) {
        (void)fputs("TARGET_SEALED_SYMBOL_CONTRACT=FAIL\n", stderr);
        return 1;
    }
    (void)puts("TARGET_SEALED_SYMBOL_CONTRACT=PASS label=8 deferred=7 reloc=7");
    return 0;
}
