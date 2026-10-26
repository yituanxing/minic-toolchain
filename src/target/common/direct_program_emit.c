#include "target/common/direct_program_emit.h"

#include "target/backend_core_usage.h"
#include "target/common/global_emit.h"
#include "target/common/program_visit.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The complete-output and no-partial-assembly policy used by the two
 * original GNU/QEMU-validated LP64 direct emitters. Keep ISA-specific
 * function instructions, call ABI and optional final legalization private. */
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
    const MinicMultiDirectProgramConfig *config;
    size_t scanned_functions, rejected_functions;
    bool census;
} MinicDirectVisitContext;

static bool direct_visit_global(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicGlobalObject *obj, size_t index, void *opaque) {
    const MinicDirectVisitContext *ctx = opaque;
    bool success = true;
    (void)index;
    if (!obj->is_extern)
        success = minic_multi_emit_simple_global(
            out, program, target, obj, minic_multi_emit_elf_relocation);
    else if (obj->alias_target != MINIC_GLOBAL_OBJECT_INVALID)
        success = false;
    if (!success && getenv("MINIC_M1_TRACE") != NULL) {
        const MinicArrayType *array = minic_type_is_array(obj->type)
            ? minic_c0_program_array_type(program, obj->type.array_type_id) : NULL;
        const MinicRecord *record = array != NULL &&
            minic_type_is_record(array->element_type)
            ? minic_c0_program_record(program, array->element_type.record_id) : NULL;
        (void)fprintf(stderr,
            "MINIC_M4_GLOBAL_FRONTIER arch=%s name=%s init=%zu reloc=%zu type_kind=%d element_kind=%d element_count=%zu fields=%zu\n",
            ctx->config->architecture_name,
            obj->name != NULL ? obj->name : "<unnamed>",
            obj->initializer_count, obj->relocation_count,
            (int)obj->type.base_kind,
            array != NULL ? (int)array->element_type.base_kind : -1,
            array != NULL ? array->element_count : 0U,
            record != NULL ? record->field_count : 0U);
    }
    return success;
}

static bool direct_visit_function(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicFunction *decl, const MinicCoreFunction *fn,
    size_t index, bool function_sections, void *opaque) {
    MinicDirectVisitContext *ctx = opaque;
    ++ctx->scanned_functions;
    if (ctx->config->emit_function(out, program, target, decl, fn,
                                   index, function_sections))
        return true;
    ++ctx->rejected_functions;
    if (getenv("MINIC_M1_TRACE") != NULL) {
        (void)fprintf(stderr,
            "MINIC_M1_FRONTIER arch=%s function=%s "
            "core_blocks=%zu core_objects=%zu parameters=%zu "
            "values=%zu instructions=%zu\n",
            ctx->config->architecture_name,
            fn->name != NULL ? fn->name : "<unnamed>",
            fn->block_count, fn->object_count, fn->parameter_count,
            fn->value_count, fn->instruction_count);
        if (!ctx->census) (void)minic_core_function_dump(stderr, fn);
    }
    return false;
}

bool minic_multi_direct_write_program(
    const char *path, const MinicC0Program *program,
    const MinicTargetInfo *target, const MinicCoreFunction *core_functions,
    size_t core_function_count, bool function_sections,
    const MinicMultiDirectProgramConfig *config,
    MinicDiagnostic *diagnostic) {
    FILE *out;
    bool success = true;
    const bool census = getenv("MINIC_BACKEND_CENSUS") != NULL;
    MinicDirectVisitContext visits = { config, 0U, 0U, census };

    if (config == NULL || config->architecture_name == NULL ||
        config->display_name == NULL || config->gnu_stack_note == NULL ||
        config->emit_function == NULL)
        return false;
    if (path == NULL || program == NULL || target == NULL ||
        target->architecture != config->architecture ||
        (core_function_count > 0U && core_functions == NULL) ||
        core_function_count != program->function_count)
        return minic_multi_direct_fail(diagnostic, path, config->display_name,
                                       "invalid Core or target configuration");
    if (program->file_asm_count != 0U)
        return minic_multi_direct_fail(diagnostic, path, config->display_name,
                                       "file asm not supported");
    out = fopen(path, "wb");
    if (out == NULL)
        return minic_multi_direct_fail(diagnostic, path, config->display_name,
                                       "cannot open GNU assembly output");
    success = minic_program_visit_globals(
        out, program, target, direct_visit_global, &visits);
    /* An opt-in census still inspects all defined Core functions if a
     * global failed, but the partial .s is always removed. */
    if (success || census) {
        const bool functions_ok = minic_program_visit_functions(
            out, program, target, core_functions, core_function_count,
            function_sections, MINIC_PROGRAM_FUNCTION_LIVE_DEFINITIONS,
            census, direct_visit_function, &visits);
        success = success && functions_ok;
    }
    if (census) {
        minic_backend_core_usage_dump(config->architecture_name,
                                      program, target, core_functions,
                                      core_function_count);
        (void)fprintf(stderr,
            "MINIC_BACKEND_CENSUS arch=%s scanned=%zu rejected=%zu\n",
            config->architecture_name, visits.scanned_functions, visits.rejected_functions);
    }
    if (success && fputs(config->gnu_stack_note, out) == EOF)
        success = false;
    if (fclose(out) != 0) success = false;
    if (success && config->finalize_output != NULL &&
        !config->finalize_output(path))
        success = false;
    if (!success) {
        (void)remove(path);
        return minic_multi_direct_fail(
            diagnostic, path, config->display_name,
            "Core instruction/ABI not yet supported by M1 emitter");
    }
    return true;
}
