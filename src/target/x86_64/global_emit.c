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
    MinicModuleElfGlobalPlan plan;
    if (out == NULL || target == NULL ||
        target->architecture != MINIC_TARGET_ARCH_X86_64 ||
        target->data_layout == NULL || target->data_layout->pointer_size != 8U ||
        module == NULL || !module->finalized || module->type_context == NULL ||
        module->globals == NULL || ordinal >= module->global_count)
        return false;
    global = &module->globals[ordinal];
    if (global->is_extern || global->alias_target_index != SIZE_MAX ||
        global->is_thread_local ||
        (global->fragment_count != 0U && global->fragments == NULL) ||
        !global->common_elf_bss_admitted ||
        global->storage_alignment == 0U ||
        minic_core_type_get(module->type_context, global->core_type_id) == NULL ||
        !x86_module_explicit_bss_contents_valid(global) ||
        !minic_multi_module_elf_global_plan_build(
            global, NULL, true, x86_data_directive, false, 8U, &plan) ||
        !minic_multi_module_elf_global_emit_section(out, &plan) ||
        !minic_multi_module_elf_global_emit_binding(out, &plan) ||
        (!global->is_internal &&
         !minic_multi_module_elf_global_emit_visibility(out, &plan)))
        return false;
    if (fprintf(out, "  .balign %zu\n.type %s,@object\n%s:\n",
                global->storage_alignment, global->name, global->name) < 0 ||
        !minic_multi_module_elf_global_emit_fragments(out, &plan))
        return false;
    return fprintf(out, ".size %s,%zu\n", global->name,
                   global->storage_size) >= 0;
}

