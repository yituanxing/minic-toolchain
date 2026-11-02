#ifndef MINIAS_COMMON_CONTEXT_H
#define MINIAS_COMMON_CONTEXT_H

#include "minias_internal.h"

/* Common owns persistent section identities and symbol/name indexes. Neither
 * Source nor Target can allocate or mutate these tables directly. */
typedef struct MiniAsSectionIdentity {
    const char *name;
    const char *group_signature;
    const char *link_order_reference;
    bool has_unique_id;
    uint32_t unique_id;
} MiniAsSectionIdentity;

/* Common Context alone commits Symbol declaration and definition transitions.
 * Lowering owns GNU syntax, Layout owns Fragment placement and Source owns
 * lexical sites; all consumers pass stable SymbolIDs or narrow value inputs. */
typedef enum MiniAsAbsoluteAssignKind {
    MINIAS_ABSOLUTE_EQU_IDEMPOTENT,
    MINIAS_ABSOLUTE_SET_MUTABLE,
    MINIAS_ABSOLUTE_SET_ONCE
} MiniAsAbsoluteAssignKind;
typedef enum MiniAsSourceAnchorKind {
    MINIAS_SOURCE_LABEL,
    MINIAS_SOURCE_EQU_DOT,
    MINIAS_SOURCE_SET_DOT,
    MINIAS_SOURCE_CFI
} MiniAsSourceAnchorKind;
typedef struct MiniAsCommonDeclaration {
    size_t symbol_id;
    bool defer_local_bss;
    int bss_section;
} MiniAsCommonDeclaration;
bool minias_common_context_declare_symbol_binding(MiniAs *,const char *,
                                                   bool,uint8_t,bool,uint8_t);
bool minias_common_context_declare_symbol_type(MiniAs *,const char *,
                                                uint8_t,bool,uint8_t);
bool minias_common_context_declare_symbol_size(MiniAs *,const char *,uint64_t);
bool minias_common_context_capture_symbol_size_end(MiniAs *,size_t,size_t,
                                                    uint32_t,size_t,int64_t,
                                                    uint64_t,bool);
bool minias_common_context_define_source_fragment_anchor(MiniAs *,const char *,
                                    MiniAsSourceAnchorKind,int,uint32_t,
                                    uint64_t,size_t,int64_t,size_t,bool,size_t *);
bool minias_common_context_assign_symbol_absolute(MiniAs *,const char *,
                                    uint64_t,MiniAsAbsoluteAssignKind,size_t);
bool minias_common_context_bind_symbol_alias(MiniAs *,const char *,size_t,
                                             int64_t,const char *,size_t);
bool minias_common_context_declare_common(MiniAs *,const char *,uint64_t,
                                    uint64_t,size_t,MiniAsCommonDeclaration *);
bool minias_common_context_finish_local_common(MiniAs *,size_t,int,uint64_t,size_t);

/* Post-parse synthetic owners all use one Common SymbolID definition path. */
bool minias_common_context_define_output_anchor(MiniAs *,const char *,int,
                                                 uint32_t,uint64_t,bool,size_t *);
bool minias_common_context_reserve_named_dwarf_view(MiniAs *,const char *,size_t *);
bool minias_common_context_publish_dwarf_view(MiniAs *,size_t,uint64_t);
bool minias_common_context_bind_fixup_reference(MiniAs *,size_t);

/* A checked SymbolID query returns a stack value, never a borrow into the
 * reallocatable Context table. Only the symbol's interned name is borrowed;
 * it remains stable through later SymbolID creation. These APIs let CFI,
 * DWARF and Emission use IDs without seeing alias/layout ownership fields. */
bool minias_common_context_symbol_view(const MiniAs *,size_t,
                                       MiniAsSymbolView *);
bool minias_common_context_find_symbol_view(const MiniAs *,const char *,
                                            MiniAsSymbolView *);
bool minias_common_context_symbol_ref_view(const MiniAs *,MiniAsSymbolRef,
                                           MiniAsSymbolView *);
bool minias_common_context_intern_symbol_id(MiniAs *,const char *,size_t *);
bool minias_common_context_find_symbol_id(const MiniAs *,const char *,size_t *);
size_t minias_common_context_symbol_count(const MiniAs *);

/* MCLayout alone needs retained source Fragment/size anchors. Keep this
 * stack value separate from general Symbol semantics and Context's alias DAG;
 * no caller borrows the reallocatable underlying entity or mutable array. */
typedef struct MiniAsLayoutSymbolView {
    const char *name;
    int section;
    uint32_t subsection;
    uint64_t value;
    uint64_t size;
    uint64_t size_end_value;
    bool defined;
    bool has_fragment_anchor;
    size_t fragment_anchor;
    int64_t fragment_anchor_addend;
    bool has_size_end_anchor;
    size_t size_end_anchor;
    uint32_t size_end_subsection;
    int64_t size_end_addend;
    bool has_size_start_symbol;
    size_t size_start_symbol_index;
} MiniAsLayoutSymbolView;
bool minias_common_context_layout_symbol_view(const MiniAs *,size_t,
                                              MiniAsLayoutSymbolView *);

/* Each MCLayout work pass stages Symbol values in scratch arrays. Context
 * applies them to its one live Symbol table and refreshes its alias DAG. */
typedef struct MiniAsSymbolLocalAnchorPlan {
    size_t symbol_count;
    const uint64_t *start_local;
    const uint64_t *end_local;
    const uint64_t *size_candidate;
} MiniAsSymbolLocalAnchorPlan;
typedef struct MiniAsSubsectionBaseReader {
    void *owner;
    bool (*base)(void *,int,uint32_t,uint64_t *);
} MiniAsSubsectionBaseReader;
bool minias_common_context_commit_local_symbol_anchors(MiniAs *,
                                        const MiniAsSymbolLocalAnchorPlan *);
bool minias_common_context_finalize_symbol_layout(MiniAs *,
                                        const MiniAsSubsectionBaseReader *,
                                        const uint64_t *,size_t);

/* The Target only sees an opaque object-finalization capability. */
struct MiniAsPostEmitObjectSink { MiniAs *as; };

bool minias_common_context_init(MiniAs *);
int minias_find_section_key(const MiniAs *, const MiniAsSectionIdentity *);
int minias_ensure_section_key(MiniAs *, const MiniAsSectionIdentity *,
                              uint32_t, uint32_t, uint64_t, uint64_t,
                              uint64_t);
/* Source alignment is monotone; unlike bootstrap alignment it can run
 * after Fragments exist and never lowers an ELF section requirement. */
bool minias_common_context_raise_section_alignment(MiniAs *,int,uint64_t);
/* GNU .ident is the one source directive that upgrades an existing .comment
 * Section's mergeable-string attributes, without changing its SectionKey. */
bool minias_common_context_apply_ident_comment(MiniAs *,int);
/* Only for bootstrap, before any source Fragment/output is present: the
 * target's configured default .text alignment may lower or raise the
 * pre-Target placeholder. This does not change Section identity. */
bool minias_common_context_set_section_alignment(MiniAs *,int,uint64_t);
void minias_common_context_destroy_sections(MiniAs *);
void minias_common_context_destroy_symbols(MiniAs *);

#endif
