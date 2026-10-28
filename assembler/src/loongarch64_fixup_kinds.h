#ifndef MINIAS_LARCH_FIXUP_KINDS_H
#define MINIAS_LARCH_FIXUP_KINDS_H

/* LoongArch64. Machine fixup identities are not ELF R_* numbers.
 * Shared MC/ELF/CLI code has no knowledge of these target-local enums.
 * New architectures allocate their own namespace without Common edits. */
enum {
    MINIAS_FIXUP_LARCH_PCALA_HI20 = 0x4000,
    MINIAS_FIXUP_LARCH_PCALA_LO12,
    MINIAS_FIXUP_LARCH_B16,
    MINIAS_FIXUP_LARCH_B21,
    MINIAS_FIXUP_LARCH_B26
};

#endif
