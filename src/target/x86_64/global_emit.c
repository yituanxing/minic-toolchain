#include "target/x86_64/global_emit.h"
#include "target/common/module_global_emit.h"
#include <stdio.h>
#include <string.h>

/* Target-owned x86 ELF scalar/relocation spelling. Global payload and
 * section classification belong to common GlobalEmissionPlan. */
static const char *x86_data_directive(size_t size) {
    return size == 1U ? ".byte" : size == 2U ? ".word" :
           size == 4U ? ".long" : size == 8U ? ".quad" : NULL;
}

/* Every byte, symbol identity and relocation addend is
 * snapped into a neutral ModuleIR before the target sees this definition.
 * Preserve the exact x86 GNU section, binding and data spelling. */
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

