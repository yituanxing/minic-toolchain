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
/* MCLayout computes a complete transient measurement; Context publishes the
 * Section-visible layout sizes together, preserving one SectionID graph. */
bool minias_common_context_publish_layout_sizes(MiniAs *,const uint64_t *,size_t);
/* Only for bootstrap, before any source Fragment/output is present: the
 * target's configured default .text alignment may lower or raise the
 * pre-Target placeholder. This does not change Section identity. */
bool minias_common_context_set_section_alignment(MiniAs *,int,uint64_t);
void minias_common_context_destroy_sections(MiniAs *);
void minias_common_context_destroy_symbols(MiniAs *);

#endif
