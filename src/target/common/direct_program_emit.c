#include "target/common/direct_program_emit.h"

#include "target/backend_core_usage.h"
#include "target/common/module_global_emit.h"
#include "target/common/elf_alias_emit.h"
#include "target/common/program_visit.h"
#include "target/common/program_emit.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The complete-output and no-partial-assembly policy used by the two
 * original GNU/QEMU-validated LP64 direct emitters. Keep ISA-specific
 * function instructions and call ABI private; typed frames need no text rewrite. */
static bool minic_multi_direct_fail(MinicDiagnostic *diag, const char *path,
                                    const char *display, const char *why) {
    if (diag != NULL) {
        diag->path = path;
        diag->line = 1U;
        diag->column = 1U;
        (void)snprintf(diag->message, sizeof(diag->message), "%s: %s", display, why);
    }
    return false;
}

/* Opt-in trace is emitted using the original two-target format verbatim. */
bool minic_multi_direct_machine_reject(const MinicCoreFunction *fn, const char *arch,
                                const char *name, const char *stage,
                                size_t block_id, size_t instruction_id,
                                unsigned int backend_line) {
    if (getenv("MINIC_M1_TRACE") != NULL) {
        const int kind = (fn != NULL && instruction_id < fn->instruction_count)
                             ? (int)fn->instructions[instruction_id].kind : -1;
        (void)fprintf(stderr,
                      "MINIC_BACKEND_REJECT arch=%s function=%s stage=%s "
                      "block=%lld instruction=%lld kind=%d backend_line=%u\n",
                      arch, name != NULL ? name : "<unnamed>", stage,
                      block_id == SIZE_MAX ? -1LL : (long long)block_id,
                      instruction_id == SIZE_MAX ? -1LL : (long long)instruction_id,
                      kind, backend_line);
    }
    return false;
}

/* This adapter retains the M1 diagnostic and census behavior while the
 * program-wide walk is shared with x86/RV32/RV64. */
typedef struct MinicDirectVisitContext {
    const MinicMultiDirectModuleConfig *config;
    const MinicCoreModule *module;
    size_t scanned_functions, rejected_functions;
    bool census;
} MinicDirectVisitContext;

static const char *direct_module_data_directive(size_t width) {
    return width == 1U ? ".byte" : width == 2U ? ".2byte" :
           width == 4U ? ".4byte" : width == 8U ? ".8byte" : NULL;
}

static bool direct_emit_module_global(FILE *out,
                                      const MinicCoreModuleGlobal *obj) {
    MinicModuleElfGlobalPlan plan;
    if (out == NULL || obj == NULL || obj->name == NULL ||
        obj->name[0] == '\0' || obj->is_extern ||
        obj->alias_target_index != SIZE_MAX ||
        !obj->common_elf_bss_admitted ||
        !minic_multi_module_elf_global_plan_build(
            obj, NULL, true, direct_module_data_directive, false, 8U, &plan))
        return false;
    return (obj->is_internal ||
            minic_multi_module_elf_global_emit_visibility(out, &plan)) &&
           minic_multi_module_elf_global_emit_section(out, &plan) &&
           minic_multi_module_elf_global_emit_binding(out, &plan) &&
           fprintf(out, "  .balign %zu\n.type %s,@object\n%s:\n",
                   obj->storage_alignment, obj->name, obj->name) >= 0 &&
           minic_multi_module_elf_global_emit_fragments(out, &plan) &&
           fprintf(out, ".size %s,%zu\n", obj->name,
                   obj->storage_size) >= 0;
}

static bool direct_visit_module_global(
    FILE *out, const MinicCoreModule *module, const MinicTargetInfo *target,
    const MinicCoreModuleGlobal *obj, size_t index, void *opaque) {
    const MinicDirectVisitContext *ctx = opaque;
    bool success = true;
    (void)target;
    (void)index;
    if (out == NULL || module == NULL || ctx == NULL || obj == NULL ||
        ctx->config == NULL || !obj->emittable) return false;
    if (!obj->is_extern)
        success = direct_emit_module_global(out, obj);
    else if (obj->alias_target_index != SIZE_MAX)
        success = minic_multi_elf_emit_module_object_alias(
            out, module, obj, ctx->config->elf_object_type);
    if (!success && getenv("MINIC_M1_TRACE") != NULL)
        (void)fprintf(stderr,
            "MINIC_M4_GLOBAL_FRONTIER arch=%s name=%s "
            "storage=%zu fragments=%zu reloc=%zu\n",
            ctx->config->architecture_name,
            obj->name != NULL ? obj->name : "<unnamed>",
            obj->storage_size, obj->fragment_count, obj->reference_count);
    return success;
}

static bool direct_visit_function(
    FILE *out, const MinicCoreModule *module, const MinicTargetInfo *target,
    const MinicCoreModuleFunctionSymbol *module_decl,
    const MinicCoreFunction *fn,
    const MinicCoreFunctionFacts *facts,
    const MinicCoreObjectStoragePlan *storage, size_t index,
    bool function_sections, void *opaque) {
    MinicDirectVisitContext *ctx = opaque;
    ++ctx->scanned_functions;
    if (ctx->config->emit_function(out, target, module, module_decl, fn,
                                   facts, storage, index, function_sections))
        return true;
    ++ctx->rejected_functions;
    if (getenv("MINIC_M1_TRACE") != NULL) {
        (void)fprintf(stderr,
            "MINIC_M1_FRONTIER arch=%s function=%s "
            "core_blocks=%zu core_objects=%zu parameters=%zu "
            "values=%zu instructions=%zu\n",
            ctx->config->architecture_name,
            fn->name != NULL ? fn->name : "<unnamed>",
            fn->block_count, fn->object_count, minic_core_function_parameter_count(fn),
            fn->value_count, fn->instruction_count);
        if (!ctx->census) (void)minic_core_function_dump(stderr, fn);
    }
    return false;
}

static bool direct_between(
    FILE *out, const MinicCoreModule *module, const MinicTargetInfo *target,
    const MinicCoreFunction *functions, size_t function_count,
    bool function_sections, void *opaque) {
    const MinicDirectVisitContext *ctx = opaque;
    if (ctx == NULL || ctx->config == NULL)
        return false;
    (void)target;
    (void)functions;
    (void)function_count;
    (void)function_sections;
    return minic_program_emit_source_file_asm_module(out, module) &&
           minic_multi_elf_emit_module_function_aliases(
               out, module, ctx->config->elf_function_type);
}

bool minic_multi_direct_write_module(
    const char *path,
    const MinicTargetInfo *target, const MinicCoreFunction *core_functions,
    size_t core_function_count, bool function_sections,
    const MinicCoreModule *module, const MinicMultiDirectModuleConfig *config,
    MinicDiagnostic *diagnostic) {
    bool success;
    const bool census = getenv("MINIC_BACKEND_CENSUS") != NULL;
    MinicDirectVisitContext visits = { config, module, 0U, 0U, census };

    if (config == NULL || config->architecture_name == NULL ||
        config->display_name == NULL || config->gnu_stack_note == NULL ||
        config->elf_function_type == NULL || config->elf_object_type == NULL ||
        config->emit_function == NULL)
        return false;
    if (path == NULL || module == NULL || target == NULL ||
        target->architecture != config->architecture ||
        (core_function_count > 0U && core_functions == NULL) ||
        !minic_core_module_verify(module) || !module->finalized ||
        module->function_count != core_function_count)
        return minic_multi_direct_fail(diagnostic, path, config->display_name,
                                       "invalid Core or target configuration");
    {
        const MinicProgramEmitConfig emit = {
            direct_visit_function,
            NULL, direct_between,
            config->gnu_stack_note, NULL,
            MINIC_PROGRAM_FUNCTION_LIVE_DEFINITIONS, census
        };
        success = minic_program_emit_write_module(
            path, module, target, core_functions, core_function_count,
            function_sections, &emit, &visits,
            direct_visit_module_global);
    }
    if (census) {
        minic_backend_core_usage_dump(config->architecture_name,
                                      module, core_functions,
                                      core_function_count);
        (void)fprintf(stderr,
            "MINIC_BACKEND_CENSUS arch=%s scanned=%zu rejected=%zu\\n",
            config->architecture_name, visits.scanned_functions,
            visits.rejected_functions);
    }
    if (!success)
        return minic_multi_direct_fail(diagnostic, path, config->display_name,
                                       "Core instruction/ABI not yet supported by M1 emitter");
    return true;
}
