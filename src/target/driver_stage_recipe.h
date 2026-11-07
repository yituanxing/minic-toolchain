#ifndef MINIC_TARGET_DRIVER_STAGE_RECIPE_H
#define MINIC_TARGET_DRIVER_STAGE_RECIPE_H

#include "target/target_catalog.h"

#include <stdbool.h>
#include <stddef.h>

/* A fully qualified product CPP/AS/LD pipeline is separate from a registered
 * C-to-assembly backend. This description belongs to the target providing
 * the staged tools; the standalone driver only orchestrates its recipe. */
typedef struct MinicTargetDriverStageRecipe {
    MinicTargetArchitecture architecture;
    const char *assembler_default_march;
    const char *assembler_default_mabi;
    const char *ld_emulation_option;
    const char *dynamic_linker_option;
    const char *explicit_runtime_archive_march;
    const char *omit_default_lgcc_march;
    bool (*validate_isa_abi)(const char *march, const char **mabi,
                             char *reason, size_t reason_capacity);
    bool (*append_cpp_predefines)(char **arguments, size_t *count,
                                  size_t capacity, bool hosted,
                                  bool plain_char_unsigned,
                                  const char *march, const char *mabi);
} MinicTargetDriverStageRecipe;

/* A product stage cannot be inferred from pointer size, ISA family, or an
 * assembly backend. Missing/unqualified recipes always reject. */
const MinicTargetDriverStageRecipe *minic_target_driver_stage_recipe_for_architecture(
    MinicTargetArchitecture architecture);

#endif
