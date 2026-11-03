#include "target/common/global_emit.h"
#include "target/common/elf_section_name.h"

/* Validate explicit ELF section names before interpolating GNU assembly. */
bool minic_multi_elf_safe_section_name(const char *name) {
    return minic_elf_section_name_valid(name);
}
