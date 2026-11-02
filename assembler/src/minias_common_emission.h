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
bool minias_record_fixup(MiniAs *, int, uint64_t, MiniAsFixupKind, unsigned,
                         const char *, size_t, int64_t, MiniAsFixupOrigin,
                         bool, bool, size_t);
bool minias_section_append_pattern(MiniAs *, int, const unsigned char *,
                                   size_t, size_t, bool);
bool minias_section_append_fill(MiniAs *, int, size_t, uint8_t, bool);
bool minias_emit_sections(MiniAs *);
void minias_common_emission_destroy(MiniAs *);

#endif
