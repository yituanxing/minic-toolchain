#ifndef MINIAS_X64_FIXUP_KINDS_H
#define MINIAS_X64_FIXUP_KINDS_H

/* x86-64. Machine fixup identities are not ELF R_* numbers.
 * Shared MC/ELF/CLI code has no knowledge of these target-local enums.
 * New architectures allocate their own namespace without Common edits. */
enum {
    MINIAS_FIXUP_X64_PC32 = 0x3000,
    MINIAS_FIXUP_X64_PLT32,
    MINIAS_FIXUP_X64_ABS32S,
    MINIAS_FIXUP_X64_ABS64
};

#endif
