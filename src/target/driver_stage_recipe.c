#include "target/driver_stage_recipe.h"

extern const MinicTargetDriverStageRecipe minic_riscv64_driver_stage_recipe;

/* Only independently qualified staged toolchains appear here. Adding an
 * assembly backend never grants its target a preprocessor, assembler or LD. */
static const MinicTargetDriverStageRecipe *const staged_recipes[] = {
    &minic_riscv64_driver_stage_recipe,
};

const MinicTargetDriverStageRecipe *minic_target_driver_stage_recipe_for_architecture(
    MinicTargetArchitecture architecture) {
    const MinicTargetCatalogEntry *entry =
        minic_target_catalog_for_architecture(architecture);
    size_t index;
    if (entry == NULL ||
        (entry->product_driver_stages &
         MINIC_TARGET_STAGE_STAGED_ASSEMBLER_AND_LINKER) == 0U)
        return NULL;
    for (index = 0U; index < sizeof(staged_recipes) / sizeof(staged_recipes[0]);
         ++index) {
        const MinicTargetDriverStageRecipe *recipe = staged_recipes[index];
        if (recipe->architecture == architecture &&
            recipe->assembler_default_march != NULL &&
            recipe->assembler_default_mabi != NULL &&
            recipe->ld_emulation_option != NULL &&
            recipe->dynamic_linker_option != NULL &&
            recipe->validate_isa_abi != NULL &&
            recipe->append_cpp_predefines != NULL)
            return recipe;
    }
    return NULL;
}
