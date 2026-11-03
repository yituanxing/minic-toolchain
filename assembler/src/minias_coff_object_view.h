#ifndef MINIAS_COFF_OBJECT_VIEW_H
#define MINIAS_COFF_OBJECT_VIEW_H

#include "minias_object_model.h"

/* The COFF writer's terminal, immutable projection. ObjectModel borrows
 * Common's finalized output; the Target supplies only machine/ABI relocation
 * semantics, never a mutable Context or an ELF relocation number. */
typedef struct MiniAsCoffObjectView {
    MiniAsObjectModelView model;
    MiniAsTargetABIView target_abi;
    uint16_t machine;
    bool (*map_fixup)(MiniAsTargetABIView,MiniAsFixupKind,unsigned,uint16_t *);
    char error[MINIAS_MAX_ERROR];
} MiniAsCoffObjectView;

bool minias_write_coff_object(MiniAsCoffObjectView *object,const char *path);

#endif
