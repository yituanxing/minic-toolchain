#include "minias_internal.h"
#include "minias.h"

#include <string.h>

/* The sole backend inventory. Public names and internal TargetOps are both
 * derived from this factory list; neither CLI nor assembler core enumerates
 * instruction sets. Factories keep the table compile-time constant in C. */
typedef const MiniAsTargetOps *(*MiniAsTargetFactory)(void);
static const MiniAsTargetFactory target_factories[] = {
    minias_target_riscv,
    minias_target_aarch64,
    minias_target_x86_64,
    minias_target_loongarch64
};

size_t minias_supported_target_count(void) {
    return sizeof(target_factories) / sizeof(target_factories[0]);
}

const char *minias_supported_target_name(size_t index) {
    const MiniAsTargetOps *target;
    if (index >= minias_supported_target_count()) return NULL;
    target = target_factories[index]();
    return target != NULL ? target->name : NULL;
}

const MiniAsTargetOps *minias_find_target(const char *name) {
    size_t i;

    if (name == NULL) return NULL;
    for (i = 0U; i < minias_supported_target_count(); ++i) {
        const MiniAsTargetOps *target = target_factories[i]();
        if (target != NULL && strcmp(name, target->name) == 0)
            return target;
    }
    return NULL;
}
