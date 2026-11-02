#include "target/x86_64/global_emit.h"
#include "target/common/global_emit.h"
#include "target/common/module_global_emit.h"
#include "target/data_layout.h"
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Target-owned x86 ELF scalar/relocation spelling. Global payload and
 * section classification belong to common GlobalEmissionPlan. */
static const char *x86_data_directive(size_t size) {
    return size == 1U ? ".byte" : size == 2U ? ".word" :
           size == 4U ? ".long" : size == 8U ? ".quad" : NULL;
}

static bool x86_emit_data_bits(FILE *out, size_t size, uint64_t bits) {
    const char *directive = x86_data_directive(size);
    if (directive == NULL) return false;
    if (size < 8U) bits &= (UINT64_C(1) << (size * 8U)) - UINT64_C(1);
    return fprintf(out, "  %s %" PRIu64 "\n", directive, bits) >= 0;
}

static bool x86_emit_relocation(FILE *out, const MinicC0Program *program,
                                const MinicTargetInfo *target,
                                const MinicGlobalRelocation *reloc, size_t size) {
    const char *name = NULL;
    const char *directive = x86_data_directive(size);
    int64_t addend;
    if (reloc == NULL || directive == NULL || size != 8U) return false;
    if (reloc->target_kind == MINIC_GLOBAL_RELOCATION_LABEL) {
        /* Lua lvm.c's dispatch table has one relocation for each local
         * GNU label. Core already records the statement ID and emits its
         * .Luser_<id> alias at the owning basic block, just as RV64 does. */
        const MinicStatement *label = minic_c0_program_statement(program, reloc->target_id);
        return label != NULL && label->kind == MINIC_STATEMENT_LABEL &&
               fprintf(out, "  %s .Luser_%zu\n", directive, reloc->target_id) >= 0;
    }
    name = minic_multi_global_relocation_target_name(program, reloc);
    if (name == NULL || name[0] == '\0' ||
        !minic_data_layout_global_relocation_target_addend(target->data_layout,
                                                           program, reloc, &addend)) return false;
    if (addend == 0)
        return fprintf(out, "  %s %s\n", directive, name) >= 0;
    return fprintf(out, "  %s %s%+" PRId64 "\n", directive, name, addend) >= 0;
}

/* Flat records now use one shared layout walk.  Retain x86's legacy
 * .word/.long/.quad scalar spelling in the target-owned callback. */
static bool x86_emit_typed_bits(FILE *out, const MinicC0Program *program,
                                const MinicTargetInfo *target, MinicType type,
                                uint64_t bits) {
    size_t width, alignment;
    return target != NULL &&
           minic_data_layout_type(target->data_layout, program, type,
                                  &width, &alignment) &&
           x86_emit_data_bits(out, width, bits);
}

/* x86 retains only target ELF spelling. Common GlobalEmissionPlan owns
 * storage classification, zero/bss policy, nested aggregate traversal,
 * relocation locations and flexible record payloads for every backend. */
bool minic_x86_64_emit_global(FILE *out, const MinicC0Program *program,
                            const MinicTargetInfo *target, const MinicGlobalObject *obj) {
    MinicMultiGlobalEmissionPlan plan;
    if (out == NULL || !minic_multi_global_emission_plan_build(program, target, obj, &plan) ||
        !minic_multi_emit_elf_global_section(out, obj, plan.default_elf_section)) {
        return false;
    }
    if (!minic_multi_emit_elf_global_binding(out, obj) ||
        (!obj->is_internal &&
         !minic_multi_emit_elf_symbol_visibility(out, obj->visibility, obj->name))) {
        return false;
    }
    if (fprintf(out, "  .balign %zu\n.type %s,@object\n%s:\n",
                plan.storage_alignment, obj->name, obj->name) < 0 ||
        !minic_multi_emit_global_payload(
            out, program, target, obj, &plan, x86_emit_relocation, x86_emit_typed_bits)) {
        return false;
    }
    return fprintf(out, ".size %s,%zu\n", obj->name, plan.storage_size) >= 0;
}

/* Production path: every byte, symbol identity and relocation addend is
 * snapped into a neutral ModuleIR before the target sees this definition.
 * Preserve the exact historical x86 GNU section, binding and data spelling;
 * legacy standalone C fixtures still call minic_x86_64_emit_global above. */
static const char *x86_module_default_section(MinicCoreModuleDefaultSection section) {
    switch (section) {
    case MINIC_CORE_MODULE_SECTION_BSS: return ".bss";
    case MINIC_CORE_MODULE_SECTION_DATA: return ".data";
    case MINIC_CORE_MODULE_SECTION_RODATA: return ".section .rodata";
    case MINIC_CORE_MODULE_SECTION_RELRO: return ".section .data.rel.ro";
    default: return NULL;
    }
}

static bool x86_module_explicit_bss_contents_valid(
    const MinicCoreModuleGlobal *global) {
    size_t i;
    const char *name = global->section_name;
    if (name == NULL || strncmp(name, ".bss", 4U) != 0 ||
        (name[4] != '\0' && name[4] != '.')) return true;
    for (i = 0U; i < global->fragment_count; ++i) {
        const MinicCoreModuleFragment *fragment = &global->fragments[i];
        if (fragment->kind == MINIC_CORE_MODULE_FRAGMENT_RELOCATION ||
            (fragment->kind == MINIC_CORE_MODULE_FRAGMENT_BITS &&
             fragment->bits != 0U)) return false;
    }
    return true;
}

bool minic_x86_64_emit_module_global(FILE *out, const MinicTargetInfo *target,
                                      const MinicCoreModule *module, size_t ordinal) {
    const MinicCoreModuleGlobal *global;
    const char *fallback;
    if (out == NULL || target == NULL ||
        target->architecture != MINIC_TARGET_ARCH_X86_64 ||
        target->data_layout == NULL || target->data_layout->pointer_size != 8U ||
        module == NULL || !module->finalized || module->type_context == NULL ||
        module->globals == NULL || ordinal >= module->global_count)
        return false;
    global = &module->globals[ordinal];
    fallback = x86_module_default_section(global->default_section);
    if (global->is_extern || global->is_alias || global->is_thread_local ||
        (global->fragment_count != 0U && global->fragments == NULL) ||
        !global->common_elf_bss_admitted ||
        global->storage_alignment == 0U || fallback == NULL ||
        minic_core_type_get(module->type_context, global->core_type_id) == NULL ||
        !x86_module_explicit_bss_contents_valid(global) ||
        !minic_multi_module_emit_elf_section(out, global, fallback) ||
        !minic_multi_module_emit_elf_binding(out, global) ||
        (!global->is_internal &&
         !minic_multi_module_emit_elf_symbol_visibility(
             out, global->visibility, global->name)))
        return false;
    if (fprintf(out, "  .balign %zu\n.type %s,@object\n%s:\n",
                global->storage_alignment, global->name, global->name) < 0 ||
        !minic_multi_module_emit_global_fragments(
            out, global, x86_data_directive, false, 8U))
        return false;
    return fprintf(out, ".size %s,%zu\n", global->name,
                   global->storage_size) >= 0;
}

