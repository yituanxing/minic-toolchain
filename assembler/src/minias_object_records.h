#ifndef MINIAS_OBJECT_RECORDS_H
#define MINIAS_OBJECT_RECORDS_H

#include "minias_target_api.h"

/* The ONE retained Context-owned Section/Symbol entity declarations. The
 * terminal ObjectModel borrows only const records, without importing the
 * writable Common session, SourceGraph, MCLayout or Emission capabilities.
 * Target implementation modules consume value projections instead of this
 * header. No format-specific SHT/SHF/STB/STT wire values appear here. */
typedef struct MiniAsSection {
    const char *name;
    /* Group signature and linked-section source reference are identity. */
    const char *group_signature;
    bool group_comdat;
    const char *link_order_reference;
    bool has_unique_id;
    uint32_t unique_id;
    MiniAsSectionKind type;
    MiniAsSectionFlags flags;
    uint64_t align;
    uint64_t entry_size;
    /* Bytes and output NOBITS length are owned separately by Emission. */
    uint64_t layout_size;
} MiniAsSection;

typedef struct MiniAsSymbol {
    const char *name;
    /* Finalization-only repeated mapping names are not source bindings. */
    bool unindexed_mapping;
    int section;
    uint64_t value;
    uint64_t size;
    /* An explicit nonzero size/anchored span overrides tracked alias size. */
    bool has_explicit_size;
    bool alias_size_tracks_target;
    bool named_dwarf_view;
    MiniAsSymbolBinding bind;
    MiniAsSymbolKind type;
    MiniAsSymbolVisibility visibility;
    uint32_t subsection;
    bool defined;
    bool alias_pending;
    bool mutable_absolute_set;
    bool explicit_local_binding;
    size_t alias_target_index;
    bool has_alias_dependency;
    int64_t alias_addend;
    size_t alias_line;
    const char *alias_section_base;
    bool has_fragment_anchor;
    size_t fragment_anchor;
    int64_t fragment_anchor_addend;
    bool has_size_end_anchor;
    size_t size_end_anchor;
    uint32_t size_end_subsection;
    int64_t size_end_addend;
    bool has_size_start_symbol;
    size_t size_start_symbol_index;
    uint64_t size_end_value;
} MiniAsSymbol;

/* Pure, stateless Common services also used by ELF's terminal projection. */
bool minias_project_symbol(const MiniAsSymbol *,MiniAsSymbolView *);
bool minias_is_private_symbol_name(const char *name);
char *minias_strdup(const char *text);

#endif
