/* Source-C GNU ASM policy and sealed Core machine emission must be two
 * independently indexed target contracts, from one canonical inventory. */
#include "target/backend.h"
#include "target/backend_source.h"
#include "target/target_catalog.h"

#include <stdio.h>

int main(void) {
    const MinicTargetCatalogEntry *catalog;
    size_t count, index;
    unsigned int full_source_targets = 0U, generic_source_targets = 0U;
    catalog = minic_target_catalog_all(&count);
    if (catalog == NULL || count != 5U) return 1;
    for (index = 0U; index < count; ++index) {
        MinicTargetInfo target = {0};
        const MinicSourceBackend *source;
        const MinicBackend *machine;
        bool qualified_source;
        target.architecture = catalog[index].architecture;
        source = minic_source_backend_for_target(&target);
        machine = minic_backend_for_target(&target);
        if (source == NULL || machine == NULL || machine->emit_module == NULL ||
            !machine->emit_reachable_core_blocks_only) return 2;
        qualified_source = source->qualify_inline_asm != NULL &&
            source->requires_qualified_input_asm != NULL &&
            source->qualify_inline_asm_goto != NULL;
        if (target.architecture == MINIC_TARGET_ARCH_RISCV32 ||
            target.architecture == MINIC_TARGET_ARCH_RISCV64) {
            /* RISC-V keeps generic input ASM but admits the narrow,
             * source-checked named asm-goto contract. */
            if (qualified_source || source->seal_normalized_inline_asm != NULL ||
                source->clone_safe_opaque_asm != NULL ||
                source->qualify_inline_asm != NULL ||
                source->qualify_inline_asm_goto == NULL ||
                source->requires_qualified_input_asm != NULL) return 3;
            ++generic_source_targets;
        } else {
            if (!qualified_source) return 4;
            ++full_source_targets;
        }
    }
    {
        MinicTargetInfo invalid = {0};
        invalid.architecture = (MinicTargetArchitecture)999;
        if (minic_source_backend_for_target(NULL) != NULL ||
            minic_backend_for_target(NULL) != NULL ||
            minic_source_backend_for_target(&invalid) != NULL ||
            minic_backend_for_target(&invalid) != NULL) return 5;
    }
    if (full_source_targets != 3U || generic_source_targets != 2U) return 6;
    (void)puts("BACKEND_SOURCE_REGISTRATION=PASS targets=5 machine=5 source_qualified=3 generic=2 unknown=reject");
    return 0;
}
