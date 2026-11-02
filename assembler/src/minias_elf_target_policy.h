#ifndef MINIAS_ELF_TARGET_POLICY_H
#define MINIAS_ELF_TARGET_POLICY_H

#include "minias_target_api.h"

/* Only ELF ABI selection/relocation policy lives here. Section and Symbol
 * intent is now generic MiniAs*Kind/Flags in the MC API; translation to ELF
 * SHT/SHF/STB/STT/STV wire values belongs solely to elf_writer.c. */

typedef struct MiniAsObjectAttribute {
    const char *name;
    uint32_t type;
    const unsigned char *data;
    size_t size;
} MiniAsObjectAttribute;

/* Exactly one static, immutable ELF policy per Target. The Driver and ELF
 * writer borrow this value directly; no second copied callback table or
 * flattened duplicates in MiniAsTargetOps can drift out of agreement.
 * Fixup classification is ABI-aware because i386 REL and AMD64 RELA share
 * an ISA parser yet have different supported relocation profiles. */
typedef struct MiniAsElfTargetPolicy {
    uint16_t elf_machine;
    bool emit_all_section_symbols;
    bool fold_defined_local_relocation_to_section;
    bool fold_local_pcrel_relocation_to_section;
    bool fold_synthetic_local_relocation_to_section;
    bool fold_local_abs_relocation_to_section;
    bool synthetic_expr_anchors_hidden;
    bool cfi_anchors_hidden;
    bool (*allow_local_section_symbol_fixup)(const MiniAsFixup *,
                                             const MiniAsSymbolView *);
    bool (*supports_fixup)(MiniAsTargetABIView,MiniAsFixupKind,unsigned);
    uint32_t (*fixup_relocation_type)(MiniAsTargetABIView,MiniAsFixupKind,unsigned);
    bool (*parse_named_relocation)(MiniAsTargetABIView,const char *,
                                   MiniAsFixupKind *,unsigned *);
    void (*object_attributes)(MiniAsTargetABIView,unsigned char,
                              MiniAsObjectAttribute *);
} MiniAsElfTargetPolicy;

#endif
