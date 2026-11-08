#ifndef MINIC_TARGET_COMMON_MACHINE_PRODUCTION_PREFLIGHT_H
#define MINIC_TARGET_COMMON_MACHINE_PRODUCTION_PREFLIGHT_H

#include "target/common/machine_core_coverage.h"
#include "target/common/machine_gnu_asm.h"

#include <string.h>

/* One admission gate for a target's COMPLETE Core->Machine function, before
 * its owner emits any function section, visibility, symbol or ELF type text.
 * The caller owns physical ABI/frame equality checks against its target plan
 * and MUST fail the entire function on false; legacy FILE* emission is never
 * a fallback from a failed Machine selector or verifier.
 *
 * This binds a selected physical function to the sealed Core effects and
 * calling convention, target instance and the emitted ModuleIR symbol. A
 * Core name is a counted source identity, not necessarily a NUL-terminated
 * alias of the emitted ELF symbol, so the two names are not compared. This
 * gate does not know target opcodes, ISA bits, register allocation, frame
 * conventions or GNU spelling: those belong to the physical target policy. */
typedef struct MinicMachineProductionSource {
    const MinicCoreFunction *core;
    const bool *reachable_blocks;
    const MinicTargetInfo *target;
    const char *symbol;
    const MinicMachineCoreCoveragePolicy *core_asm_policy;
} MinicMachineProductionSource;

static inline bool minic_machine_production_preflight_prepare(
    const MinicMachineProductionSource *source,
    const MinicMachineFunction *selected,
    const MinicMachineGNUAsmPolicy *policy,
    MinicMachineGNUAsmPrepared **prepared) {
    if (prepared == NULL) return false;
    *prepared = NULL;
    if (source == NULL || source->core == NULL ||
        source->reachable_blocks == NULL || source->target == NULL ||
        source->symbol == NULL || source->symbol[0] == '\0' ||
        selected == NULL || policy == NULL ||
        selected->target != source->target ||
        selected->architecture != source->target->architecture ||
        selected->name == NULL || source->core->name == NULL ||
        source->core->name_length == 0U ||
        strlen(selected->name) != source->core->name_length ||
        memcmp(selected->name, source->core->name,
               source->core->name_length) != 0 ||
        selected->calling_convention !=
            minic_core_function_calling_convention(source->core))
        return false;
    return minic_machine_core_coverage_verify_with_policy(
            source->core, source->reachable_blocks, selected,
            source->core_asm_policy) &&
        minic_machine_gnu_asm_prepare_body(selected, policy, prepared);
}

/* Standalone checks keep their previous one-shot contract; production
 * callers retain the prepared plan through the body emission so they never
 * reverify the same physical instructions or rebuild labels in the writer. */
static inline bool minic_machine_production_preflight(
    const MinicMachineProductionSource *source,
    const MinicMachineFunction *selected,
    const MinicMachineGNUAsmPolicy *policy) {
    MinicMachineGNUAsmPrepared *prepared = NULL;
    bool ok = minic_machine_production_preflight_prepare(
        source, selected, policy, &prepared);
    minic_machine_gnu_asm_prepared_destroy(prepared);
    return ok;
}

#endif
