#ifndef MINIAS_COMMON_EMISSION_H
#define MINIAS_COMMON_EMISSION_H

#include "minias_internal.h"

/* Immutable view of all committed Fixups for layout and object serialization.
 * Valid until a later Emission append; consumers cannot mutate entries. */
typedef struct MiniAsFixupSpan {
    const MiniAsFixup *data;
    size_t count;
} MiniAsFixupSpan;

/* Fixups, staged writes and final Section materialization share one owner.
 * The assembly driver may invoke the transaction, never commit a fragment. */
bool minias_common_emission_init(MiniAs *);
MiniAsFixupSpan minias_emission_fixups(const MiniAs *);
bool minias_emission_mcdata_cursor(MiniAs *,const MiniAsStmt *,uint64_t *);
/* GNU `.reloc OFFSET,TYPE` uses STN_UNDEF=0, not a user symbol. Preserve
 * explicit Section ownership without exposing raw retained Fragment IDs. */
bool minias_add_explicit_section_abs0_fixup(MiniAs *,int,uint64_t,
                                            MiniAsFixupKind,unsigned);
bool minias_emit_sections(MiniAs *);
void minias_common_emission_destroy(MiniAs *);

#endif
