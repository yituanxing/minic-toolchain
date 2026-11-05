#ifndef MINIAS_COMMON_LAYOUT_H
#define MINIAS_COMMON_LAYOUT_H

#include "minias_internal.h"

/* MCLayout owns parser subsection identity, canonical subsection storage
 * and the stable anchor graph. Other Common phases borrow only a lookup. */
bool minias_layout_init(MiniAs *);
void minias_layout_destroy(MiniAs *);
const MiniAsSubsection *minias_layout_find_subsection(const MiniAs *,int,
                                                     uint32_t);

/* A value snapshot of the parser's subsection cursor and code-after-data
 * transition. No caller may change MCLayout's subsection storage directly. */
typedef struct MiniAsSubsectionParseState {
    uint64_t cursor;
    bool pending_instruction_alignment;
} MiniAsSubsectionParseState;
bool minias_layout_touch_subsection(MiniAs *,int,uint32_t);
bool advance_current_location(MiniAs *,uint64_t);
bool minias_layout_subsection_parse_state(MiniAs *,int,uint32_t,
                                         MiniAsSubsectionParseState *);
bool minias_layout_record_source_fragment(MiniAs *,int,uint32_t,
                                          MiniAsFragmentKind);
bool minias_layout_clear_instruction_alignment(MiniAs *,int,uint32_t);

#endif
