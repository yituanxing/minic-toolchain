#ifndef MINIC_TARGET_COMMON_MODULE_GLOBAL_EMIT_H
#define MINIC_TARGET_COMMON_MODULE_GLOBAL_EMIT_H

#include "core/module_ir.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

/* Backend-owned GNU scalar directive selection, never stored in ModuleIR.
 * Null means a width this target does not encode for a data fragment. */
typedef const char *(*MinicModuleDataDirectiveFn)(size_t width);

/* Serialize an already checked, source-neutral CoreModule byte/relocation
 * stream. Exactly one contiguous walk covers storage_size; zero-size symbols
 * are supported, but malformed fragments cannot emit partial hidden data.
 * The surrounding program writer owns the output transaction/rollback. */
bool minic_multi_module_emit_global_fragments(
    FILE *out, const MinicCoreModuleGlobal *global,
    MinicModuleDataDirectiveFn directive, bool signed_integer_bits,
    size_t required_relocation_width);

/* Source-free counterparts of the historical Common ELF global policy.
 * Explicit .bss.* sections use NOBITS here; RV64 intentionally owns its
 * separate @progbits policy in its emitter. */
bool minic_multi_module_emit_elf_section(
    FILE *out, const MinicCoreModuleGlobal *global, const char *fallback);
bool minic_multi_module_emit_elf_binding(
    FILE *out, const MinicCoreModuleGlobal *global);
bool minic_multi_module_emit_elf_symbol_visibility(
    FILE *out, MinicCoreModuleVisibility visibility, const char *symbol);

#endif
