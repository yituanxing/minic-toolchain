#ifndef MINIC_TARGET_COMMON_ELF_SECTION_NAME_H
#define MINIC_TARGET_COMMON_ELF_SECTION_NAME_H

#include <stdbool.h>
#include <stddef.h>

/* One source-neutral GNU/ELF section-name validation contract. Source
 * attributes and already-sealed ModuleIR names use the identical guard. */
static inline bool minic_elf_section_name_valid(const char *name) {
    size_t index;
    if (name == NULL || name[0] == '\0') return false;
    for (index = 0U; name[index] != '\0'; ++index) {
        const char c = name[index];
        if (index >= 255U ||
            !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '.' ||
              c == '$' || c == '-')) return false;
    }
    return true;
}

#endif
