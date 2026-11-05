#include "minias_target_api.h"

/* The general Target header exposes an opaque ELF policy capability, not
 * ELF numeric constants, layout knobs or mutable policy storage. */
bool generic_target_consumer(const MiniAsTargetOps *target) {
    return target!=NULL && target->elf_policy!=NULL;
}

#if defined(TRY_ELF_CONSTANT)
unsigned invalid_elf_constant(void) { return MINIAS_SHF_EXECINSTR; }
#elif defined(TRY_ELF_MACHINE)
unsigned invalid_elf_machine(const MiniAsTargetOps *target) {
    return target->elf_machine;
}
#elif defined(TRY_ELF_FIXUP)
bool invalid_elf_fixup(const MiniAsTargetOps *target) {
    return target->supports_fixup!=NULL;
}
#elif defined(TRY_ELF_FOLDING)
bool invalid_elf_folding(const MiniAsTargetOps *target) {
    return target->fold_local_abs_relocation_to_section;
}
#elif defined(TRY_ELF_POLICY_MUTATE)
void invalid_elf_policy_mutate(MiniAsTargetOps *target) {
    target->elf_policy->elf_machine=0U;
}
#endif
