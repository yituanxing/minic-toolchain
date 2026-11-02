#ifndef MINIAS_ELF_OBJECT_VIEW_H
#define MINIAS_ELF_OBJECT_VIEW_H

#include "minias_internal.h"

/* One terminal, borrowed ObjectModel projection. The ELF serializer sees
 * finalized const entities and stage-specific queries, never a mutable
 * MiniAs session or Source/Layout/Emission owner. No duplicate entity graph
 * is allocated: every SectionID/SymbolID keeps its existing identity. */
typedef struct MiniAsElfFragmentLocation {
    int section;
    uint64_t offset;
    size_t size;
    size_t line;
} MiniAsElfFragmentLocation;

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
    const MiniAsSection *sections;
    size_t section_count;
    const MiniAsSymbol *symbols;
    size_t symbol_count;
    const MiniAsFixup *fixups;
    size_t fixup_count;
    size_t cu_file_count;
    MiniAsElfTargetPolicy policy;
    MiniAsTargetABIView target_abi;
    uint16_t object_machine;
    bool output_rel_relocations;
    uint32_t elf_flags;

    const void *reader;
    const char *(*cu_file)(const void *,size_t);
    const MiniAsSymbol *(*find_symbol)(const void *,const char *);
    int (*find_section)(const void *,const char *);
    bool (*fragment_location)(const void *,size_t,MiniAsElfFragmentLocation *);
    bool (*section_bytes)(const void *,int,const unsigned char **,size_t *);
    char error[MINIAS_MAX_ERROR];
} MiniAsElfObjectView;

bool minias_write_elf64(MiniAsElfObjectView *,const char *path);
bool minias_write_elf32(MiniAsElfObjectView *,const char *path);

#endif
