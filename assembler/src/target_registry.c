#include "minias_internal.h"

#include <string.h>

/* The sole backend inventory. New targets register here without changing
 * driver parsing, Section/Symbol/Fragment ownership or ELF serialization. */
const MiniAsTargetOps *minias_find_target(const char *name) {
    const MiniAsTargetOps *const targets[] = {
        minias_target_riscv(),
        minias_target_aarch64()
    };
    size_t i;

    if (name == NULL) return NULL;
    for (i = 0U; i < sizeof(targets) / sizeof(targets[0]); ++i) {
        if (strcmp(name, targets[i]->name) == 0) {
            return targets[i];
        }
    }
    return NULL;
}
