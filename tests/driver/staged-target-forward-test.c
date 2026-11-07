/* Model a second, independently qualified staged target without changing
 * production catalog capabilities or calling an actual cross assembler. */
#include "driver_internal.h"

#include <stdio.h>
#include <string.h>

static const MinicTargetCatalogEntry mock_x86_staged_target = {
    MINIC_TARGET_ARCH_X86_64, "x86_64-linux",
    MINIC_TARGET_STAGE_ASSEMBLY_FROM_PREPROCESSED_C |
        MINIC_TARGET_STAGE_STAGED_ASSEMBLER_AND_LINKER,
    MINIC_TARGET_ISA_FAMILY_NONE
};

const MinicTargetCatalogEntry *minic_target_catalog_find(const char *name) {
    return name != NULL && strcmp(name, mock_x86_staged_target.name) == 0
               ? &mock_x86_staged_target : NULL;
}

static bool mock_cpp_predefines(char **arguments, size_t *count,
                                size_t capacity, bool hosted,
                                bool plain_char_unsigned,
                                const char *march, const char *mabi) {
    (void)arguments;
    (void)count;
    (void)capacity;
    (void)hosted;
    (void)plain_char_unsigned;
    (void)march;
    (void)mabi;
    return true;
}

int main(int argc, char **argv) {
    MinicTargetDriverStageRecipe recipe = {
        .architecture = MINIC_TARGET_ARCH_X86_64,
        .assembler_default_march = "mock-isa",
        .assembler_default_mabi = "mock-abi",
        .ld_emulation_option = "-mmock",
        .dynamic_linker_option = "--dynamic-linker=/mock",
        .append_cpp_predefines = mock_cpp_predefines,
    };
    MinicDriverInvocation invocation = {0};
    if (argc != 4 ||
        (strcmp(argv[3], "positive") != 0 &&
         strcmp(argv[3], "mismatched-recipe") != 0))
        return 3;
    if (strcmp(argv[3], "mismatched-recipe") == 0)
        recipe.architecture = MINIC_TARGET_ARCH_RISCV64;
    invocation.mode = MINIC_DRIVER_COMPILE;
    invocation.stage_recipe = &recipe;
    invocation.target_name = mock_x86_staged_target.name;
    invocation.inputs[0] = argv[1];
    invocation.input_count = 1U;
    invocation.output = argv[2];
    /* Fill every optional minic-cc argument slot to guard capacity. */
    invocation.march = "mock-isa";
    invocation.mabi = "mock-abi";
    invocation.language.plain_char = "-fsigned-char";
    invocation.language.wide_char = "-fshort-wchar";
    return minic_driver_execute(argv[0], &invocation);
}
