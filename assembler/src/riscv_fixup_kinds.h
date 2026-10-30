#ifndef MINIAS_RV_FIXUP_KINDS_H
#define MINIAS_RV_FIXUP_KINDS_H

/* RISC-V. Machine fixup identities are not ELF R_* numbers.
 * Shared MC/ELF/CLI code has no knowledge of these target-local enums.
 * New architectures allocate their own namespace without Common edits. */
enum {
    MINIAS_FIXUP_RV_CALL_PLT = 0x1000,
    MINIAS_FIXUP_RV_PCREL_HI20,
    MINIAS_FIXUP_RV_GOT_HI20,
    MINIAS_FIXUP_RV_PCREL_LO12_I,
    MINIAS_FIXUP_RV_PCREL_LO12_S,
    MINIAS_FIXUP_RV_JAL,
    MINIAS_FIXUP_RV_HI20,
    MINIAS_FIXUP_RV_LO12_I,
    MINIAS_FIXUP_RV_LO12_S,
    MINIAS_FIXUP_RV_BRANCH,
    MINIAS_FIXUP_RV_RVC_BRANCH,
    MINIAS_FIXUP_RV_RVC_JUMP
};

#endif
