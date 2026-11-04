#include "target/x86_64/global_emit.h"
#include "target/common/module_global_emit.h"
#include <stdio.h>
#include <stdint.h>

/* Target-owned x86 ELF scalar/relocation spelling. Global payload and
 * section classification belong to common GlobalEmissionPlan. */
static const char *x86_data_directive(size_t size) {
    return size == 1U ? ".byte" : size == 2U ? ".word" :
           size == 4U ? ".long" : size == 8U ? ".quad" : NULL;
}

bool minic_x86_64_emit_module_global(FILE *out, const MinicTargetInfo *target,
                                      const MinicCoreModule *module, size_t ordinal) {
    const MinicCoreModuleGlobal *global;
    MinicModuleElfGlobalPlan plan;
    if (out == NULL || target == NULL ||
        target->architecture != MINIC_TARGET_ARCH_X86_64 ||
        target->data_layout == NULL || target->data_layout->pointer_size != 8U ||
        module == NULL || !minic_core_module_verify_global(module, ordinal) ||
        module->type_context == NULL ||
        module->globals == NULL || ordinal >= module->global_count)
        return false;
    global = &module->globals[ordinal];
    if (!global->emittable ||
        global->definition_kind == MINIC_CORE_MODULE_DEFINITION_DECLARATION ||
        global->alias_target_index != SIZE_MAX ||
        global->is_thread_local ||
        (global->fragment_count != 0U && global->fragments == NULL) ||
        global->storage_alignment == 0U ||
        minic_core_type_get(module->type_context, global->core_type_id) == NULL ||
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

