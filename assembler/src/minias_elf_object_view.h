#ifndef MINIAS_ELF_OBJECT_VIEW_H
#define MINIAS_ELF_OBJECT_VIEW_H

#include "minias_object_model.h"

/* This writer policy is a value projection of the target's ELF ABI only.
 * None of parse, encode, source mutation or target-state lifecycle is
 * callable through a terminal ObjectModel view. */
typedef struct MiniAsElfTargetPolicy {
    uint16_t elf_machine;
    bool emit_all_section_symbols;
    bool fold_defined_local_relocation_to_section;
    bool fold_local_pcrel_relocation_to_section;
    bool fold_synthetic_local_relocation_to_section;
    bool fold_local_abs_relocation_to_section;
    bool (*allow_local_section_symbol_fixup)(const MiniAsFixup *,
                                             const MiniAsSymbolView *);
    bool (*supports_fixup)(MiniAsTargetABIView,MiniAsFixupKind,unsigned);
    uint32_t (*fixup_relocation_type)(MiniAsTargetABIView,MiniAsFixupKind,unsigned);
    void (*object_attributes)(MiniAsTargetABIView,unsigned char,
                              MiniAsObjectAttribute *);
} MiniAsElfTargetPolicy;

typedef struct MiniAsElfObjectView {
    MiniAsObjectModelView model;
    MiniAsElfTargetPolicy policy;
    MiniAsTargetABIView target_abi;
    uint16_t object_machine;
    bool output_rel_relocations;
    uint32_t elf_flags;

    char error[MINIAS_MAX_ERROR];
} MiniAsElfObjectView;

bool minias_write_elf64(MiniAsElfObjectView *,const char *path);
bool minias_write_elf32(MiniAsElfObjectView *,const char *path);

#endif
