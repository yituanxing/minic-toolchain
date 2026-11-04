#ifndef MINIC_TARGET_COMMON_MODULE_GLOBAL_EMIT_H
#define MINIC_TARGET_COMMON_MODULE_GLOBAL_EMIT_H

#include "core/module_ir.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

/* Backend-owned GNU scalar directive selection, never stored in ModuleIR.
 * Null means a width this target does not encode for a data fragment. */
typedef const char *(*MinicModuleDataDirectiveFn)(size_t width);

/* Shared GNU RV32/RV64 scalar-object shape query. This walks only imported
 * Core types; target-specific record/zero-extent acceptance stays private. */
const MinicCoreType *minic_multi_module_global_core_base(
    const MinicCoreTypeContext *types, MinicCoreTypeId id);
bool minic_multi_module_global_scalar_width(
    const MinicCoreTypeContext *types, const MinicCoreType *type,
    size_t *width);

/* One finalized ELF object decision, built once before any assembly is
 * printed. CoreModule owns bytes and relocations; target writers supply only
 * their established GNU dialect/ABI differences (including section fallback).
 * Neither a C type nor an initializer expression reaches this boundary. */
typedef struct MinicModuleElfGlobalPlan {
    const MinicCoreModuleGlobal *global;
    const char *fallback_section;
    const char *explicit_section_type;
    const char *section_flags;
    const char *binding_directive;
    const char *visibility_directive;
    MinicModuleDataDirectiveFn data_directive;
    size_t relocation_width;
    bool signed_integer_bits;
} MinicModuleElfGlobalPlan;

/* NULL fallback_override uses the sealed ModuleIR default-section decision.
 * Legacy target policy may override only that last ELF section choice.
 * A true explicit_bss_nobits selects NOBITS for .bss and .bss.* names;
 * existing RV32/RV64 legacy paths deliberately use PROGBITS instead. */
bool minic_multi_module_elf_global_plan_build(
    const MinicCoreModuleGlobal *global, const char *fallback_override,
    bool explicit_bss_nobits, MinicModuleDataDirectiveFn directive,
    bool signed_integer_bits, size_t relocation_width,
    MinicModuleElfGlobalPlan *plan);
bool minic_multi_module_elf_global_emit_section(
    FILE *out, const MinicModuleElfGlobalPlan *plan);
bool minic_multi_module_elf_global_emit_binding(
    FILE *out, const MinicModuleElfGlobalPlan *plan);
bool minic_multi_module_elf_global_emit_visibility(
    FILE *out, const MinicModuleElfGlobalPlan *plan);
bool minic_multi_module_elf_global_emit_fragments(
    FILE *out, const MinicModuleElfGlobalPlan *plan);

/* Serialize an already checked, source-neutral CoreModule byte/relocation
 * stream. Exactly one contiguous walk covers storage_size; zero-size symbols
 * are supported, but malformed fragments cannot emit partial hidden data.
 * The surrounding program writer owns the output transaction/rollback. */
bool minic_multi_module_emit_global_fragments(
    FILE *out, const MinicCoreModuleGlobal *global,
    MinicModuleDataDirectiveFn directive, bool signed_integer_bits,
    size_t required_relocation_width);

/* Function symbols also use this independent visibility primitive. Global
 * object writers use their prevalidated MinicModuleElfGlobalPlan instead. */
bool minic_multi_module_emit_elf_symbol_visibility(
    FILE *out, MinicCoreModuleVisibility visibility, const char *symbol);

#endif
