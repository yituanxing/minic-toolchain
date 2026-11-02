#ifndef MINIAS_COMMON_LAYOUT_H
#define MINIAS_COMMON_LAYOUT_H

#include "minias_internal.h"

/* MCLayout owns parser subsection identity, canonical subsection storage
 * and the stable anchor graph. Other Common phases borrow only a lookup. */
bool minias_layout_init(MiniAs *);
void minias_layout_destroy(MiniAs *);
const MiniAsSubsection *minias_layout_find_subsection(const MiniAs *,int,
                                                     uint32_t);

#endif
