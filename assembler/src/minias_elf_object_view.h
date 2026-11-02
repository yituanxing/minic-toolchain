#ifndef MINIAS_ELF_OBJECT_VIEW_H
#define MINIAS_ELF_OBJECT_VIEW_H

#include "minias_object_model.h"
#include "minias_elf_target_policy.h"

/* Borrow the immutable Target ELF policy, not a copied callback table.
 * None of parse, encode, source mutation or target-state lifecycle is
 * callable through a terminal ObjectModel view. */
typedef struct MiniAsElfObjectView {
    MiniAsObjectModelView model;
    const MiniAsElfTargetPolicy *policy;
    MiniAsTargetABIView target_abi;
    uint16_t object_machine;
    bool output_rel_relocations;
    uint32_t elf_flags;

    char error[MINIAS_MAX_ERROR];
} MiniAsElfObjectView;

/* ELF class is a projection of the single immutable target address-size
 * layout; neither the Driver nor the object view keeps a second elf32 bit. */
bool minias_write_elf_object(MiniAsElfObjectView *,const char *path);

#endif
