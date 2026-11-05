#ifndef MINIAS_COFF_TARGET_POLICY_H
#define MINIAS_COFF_TARGET_POLICY_H

#include "minias_target_api.h"

/* Format-private terminal projection. Machine relocation numbers are owned
 * by the Target and have no meaning in Common's MC/Fixup graph. COFF stores
 * addends in the section payload instead of an ELF-style RELA record. */
typedef struct MiniAsCoffTargetPolicy {
    uint16_t machine;
    bool (*map_fixup)(MiniAsTargetABIView,MiniAsFixupKind,unsigned,uint16_t *);
} MiniAsCoffTargetPolicy;

#endif
