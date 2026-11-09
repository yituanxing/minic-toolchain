#ifndef MINIC_TARGET_COMMON_GLOBAL_EMIT_H
#define MINIC_TARGET_COMMON_GLOBAL_EMIT_H

#include <stdbool.h>

/* Section-name validation is shared by the live RV32/RV64 Module writers. */
bool minic_multi_elf_safe_section_name(const char *name);

#endif
