#include "target/riscv64/codegen.h"
#include "target/riscv64/global_emit.h"
#include "target/common/elf_alias_emit.h"
#include "target/common/program_visit.h"
#include "target/common/program_emit.h"
#include "target/riscv64/abi.h"
#include "target/riscv64/codegen_internal.h"
#include "target/riscv64/core_codegen.h"
#include "target/riscv/function_symbol.h"
#include "target/riscv64/isa.h"
#include "target/riscv/machine_target_state.h"
#include "target/riscv/machine_core_select.h"
#include "target/riscv/machine_gnu_asm.h"
#include "target/riscv64/frame_layout.h"
#include "target/riscv64/asm_plan.h"
#include "target/riscv64/core_admission.h"
#include "target/riscv64/frame_requirements.h"
#include "target/riscv64/runtime_libcall.h"
#include "target/common/machine_production_preflight.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The five targets now use one program visitor. Keep RISC-V LP64 global
 * relocations, GNU file-asm provenance, symbol syntax and ABI self-contained. */
typedef struct Rv64ProgramContext {
    const char *path;
    MinicDiagnostic *diagnostic;
    const MinicCoreModule *module;
} Rv64ProgramContext;

/* Production always consumes its one finalized LP64 ABI/frame layout.
 * Selection happens before writing a function symbol, and a rejected MIR
 * never falls through to an old FILE* instruction emitter.  LP64D register
 * banks must be qualified separately; borrowing LP64 GPR placements for
 * that ABI would break public C calls despite syntactically valid asm. */
static bool rv64_emit_typed_function(
    FILE *out, const MinicCoreModule *module,
    const MinicTargetInfo *target, const MinicCoreFunction *function,
    const MinicRiscvFunctionSymbol *symbol,
    const MinicCoreFunctionFacts *facts,
    const MinicCoreObjectStoragePlan *storage,
    const MinicRiscvIsa *isa) {
    MinicRiscv64FunctionAbiPlan abi = {0};
    MinicRiscv64CoreFrame old_frame = {0};
    MinicRiscvPhysicalFrame physical = {0};
    MinicRiscvPhysicalCalleeSave callee_saved[12] = {{0}};
    MinicRiscvCoreMachinePlan bridge = {0};
    MinicMachineFunction machine = {0};
    MinicMachineGNUAsmPolicy policy = {0};
    bool okay = false;
    bool frame_ready = false;
    if (out == NULL || module == NULL || target == NULL ||
        function == NULL || symbol == NULL || symbol->symbol_name == NULL ||
        facts == NULL || isa == NULL ||
        isa->abi != MINIC_RISCV_ABI_LP64) goto done;
    if (!minic_riscv64_core_function_admit_profile(
            true, function, &abi, true, facts, storage) ||
        !minic_riscv64_asm_function_plan_build(
            function, &old_frame.asm_plan) ||
        !minic_riscv64_core_frame_initialize_with_storage(
            function, &old_frame, &abi, true,
            minic_riscv64_needs_arithmetic_libcall(isa, function),
            facts, storage)) goto done;
    frame_ready = true;
    if (!minic_riscv64_core_frame_mark_reachable_blocks(
            function, facts, &old_frame) ||
        !minic_riscv64_machine_physical_frame(&old_frame,
            &physical, callee_saved, 12U)) goto done;
    bridge.target = target;
    bridge.module = module;
    bridge.core = function;
    bridge.frame = &physical;
    bridge.semantic_calls64 = old_frame.semantic_calls;
    bridge.value_offsets = old_frame.value_offsets;
    bridge.value_offset_count = old_frame.value_count;
    bridge.object_offsets = old_frame.object_offsets;
    bridge.object_offset_count = old_frame.object_count;
    if (!minic_riscv64_core_frame_abi_view(
            &old_frame, &bridge.abi64_view)) goto done;
    bridge.abi64 = &bridge.abi64_view;
    {
        const MinicMachineProductionSource source = {
            .core = function, .reachable_blocks = old_frame.reachable_blocks,
            .target = target, .symbol = symbol->symbol_name
        };
        if (!minic_riscv_machine_select_core_subset(&machine, &bridge) ||
            !minic_riscv_machine_gnu_asm_policy(target, &policy) ||
            !minic_machine_production_preflight(&source, &machine, &policy))
            goto done;
    }
    okay = minic_riscv_emit_function_symbol_begin(out, symbol) &&
        minic_machine_gnu_asm_emit_body(out, &machine, &policy) &&
        minic_riscv_emit_function_symbol_end(out, symbol);
done:
    if (!okay && getenv("MINIC_RV_MACHINE_TRACE") != NULL)
        (void)fprintf(stderr, "RV64_TYPED_REJECT function=%s frame_ready=%u abi=%u\n",
            symbol != NULL && symbol->symbol_name != NULL ?
                symbol->symbol_name : "<unknown>",
            (unsigned)frame_ready, isa != NULL ? (unsigned)isa->abi : 0U);
    minic_machine_function_destroy(&machine);
    minic_riscv64_core_frame_destroy(&old_frame);
    minic_riscv64_function_abi_plan_destroy(&abi);
    return okay;
}

static bool rv64_visit_module_global(
    FILE *file, const MinicCoreModule *module, const MinicTargetInfo *target,
    const MinicCoreModuleGlobal *global, size_t index, void *opaque) {
    Rv64ProgramContext *ctx = opaque;
    if (global->definition_kind == MINIC_CORE_MODULE_DEFINITION_DECLARATION) {
        if (global->alias_target_index != SIZE_MAX)
            return minic_multi_elf_emit_module_object_alias(
                file, module, global, "@object");
        return true;
    }
    if (minic_riscv64_emit_module_global(file, target, module, index))
        return true;
    {
        char message[256];
        (void)snprintf(message, sizeof(message),
            "cannot emit RISC-V Core module global '%s' (index=%zu fragments=%zu)",
            global->name != NULL ? global->name : "<unnamed>",
            index, global->fragment_count);
        minic_riscv64_set_diagnostic(ctx->diagnostic, ctx->path, message);
    }
    return false;
}

static bool rv64_visit_function(
    FILE *file, const MinicCoreModule *module, const MinicTargetInfo *target,
    const MinicCoreModuleFunctionSymbol *module_decl,
    const MinicCoreFunction *core_function,
    const MinicCoreFunctionFacts *facts,
    const MinicCoreObjectStoragePlan *storage, size_t index,
    bool function_sections, void *opaque) {
    Rv64ProgramContext *ctx = opaque;
    const char *name = module_decl->name;
    const bool is_defined = module_decl->is_defined;
    const bool is_constructor = module_decl->is_constructor;
    bool success = true;
    if (getenv("MINIC_BOOTSTRAP_TRACE") != NULL && is_defined) {
        (void)fprintf(stderr,
            "MINIC_BOOTSTRAP_TRACE stage=codegen-function state=begin "
            "index=%zu function=%s\n",
            index, name != NULL ? name : "?");
        (void)fflush(stderr);
    }
    if (!is_defined) {
        if (module_decl->alias_target_index != SIZE_MAX)
            return minic_multi_elf_emit_module_function_alias(
                file, module, index, "@function");
        return true;
    }
    {
        MinicRiscvFunctionSymbol symbol;
        MinicRiscvIsa isa;
        /* The machine emitter owns capability admission, not this visitor. */
        success = minic_riscv_function_symbol_from_module(module_decl, &symbol);
        symbol.function_sections = function_sections;
        success = success && minic_riscv_target_isa_load(target, &isa);
        if (success && isa.abi == MINIC_RISCV_ABI_LP64) {
            /* LP64 is fully GPR/soft-float. The complete selected function
             * owns production emission without a failed-selection fallback. */
            success = rv64_emit_typed_function(file, module, target,
                core_function, &symbol, facts, storage, &isa);
        } else if (success && isa.abi == MINIC_RISCV_ABI_LP64D) {
            /* Default LP64D has a physically different FP argument bank.
             * Keep that existing ABI owner until a typed FPR contract is
             * built; treating it as LP64 here would silently break Linux. */
            success =
                minic_riscv64_emit_core_function_with_symbol_with_storage_module_trusted(
                    file, module, core_function, &symbol, facts, storage,
                    target != NULL && target->position_independent,
                    &isa, ctx->diagnostic);
        }
        if (success && is_constructor) {
            success = name != NULL && name[0] != '\0' &&
                      fprintf(file, ".section .init_array,\"aw\",@init_array\n"
                                    ".p2align 3\n  .dword %s\n.text\n", name) >= 0;
        }
    }
    if (getenv("MINIC_BOOTSTRAP_TRACE") != NULL) {
        (void)fprintf(stderr,
            "MINIC_BOOTSTRAP_TRACE stage=codegen-function state=end "
            "index=%zu function=%s success=%d blocks=%zu instructions=%zu\n",
            index, name != NULL ? name : "?", success ? 1 : 0,
            core_function->block_count, core_function->instruction_count);
        (void)fflush(stderr);
    }
    if (!success &&
        ctx->diagnostic != NULL && ctx->diagnostic->message[0] == '\0') {
        char message[256];
        (void)snprintf(message, sizeof(message),
                       "cannot emit RISC-V function '%s' (index=%zu)",
                       name != NULL ? name : "<unnamed>", index);
        minic_riscv64_set_diagnostic(ctx->diagnostic, ctx->path, message);
    }
    return success;
}

static bool rv64_between(FILE *file, const MinicCoreModule *module,
                         const MinicTargetInfo *target,
                         const MinicCoreFunction *functions, size_t count,
                         bool function_sections, void *opaque) {
    size_t i;
    Rv64ProgramContext *ctx = opaque;
    (void)target;
    (void)functions;
    (void)count;
    (void)function_sections;
    if (ctx != NULL && module != NULL) {
        if (module->file_asm_count != 0U && fprintf(file, ".text\n") < 0)
            return false;
        for (i = 0U; i < module->file_asm_count; ++i) {
            const MinicCoreModuleFileAsm *source = &module->file_asms[i];
            if (!minic_riscv64_source_asm_marker_safe(source->text,
                                                       source->length) ||
                !minic_riscv64_asm_audit_marker(file, true) ||
                !minic_riscv64_emit_file_asm_bytes(file, source->text,
                                                    source->length) ||
                fputc('\n', file) == EOF ||
                !minic_riscv64_asm_audit_marker(file, false))
                return false;
        }
        /* GNU file asm may leave GAS in an arbitrary section. */
        return module->file_asm_count == 0U || fprintf(file, ".text\n") >= 0;
    }
    return false;
}

static bool rv64_finalize(const char *path, const MinicTargetInfo *target,
                          void *opaque) {
    Rv64ProgramContext *ctx = opaque;
    char isa_error[240];
    MinicRiscvIsa isa;
    if (target == NULL) return true;
    if (!minic_riscv_target_isa_load(target, &isa)) return false;
    if (!isa.strict_isa) return true;
    if (minic_riscv64_check_isa_assembly(
            path, &isa, isa_error, sizeof(isa_error)))
        return true;
    minic_riscv64_set_diagnostic(ctx->diagnostic, path, isa_error);
    return false;
}

static bool rv64_write_core_program_impl(const char *path,
                                                        const MinicCoreFunction *core_functions,
                                                        size_t core_function_count,
                                                        bool function_sections,
                                                        const MinicTargetInfo *target,
                                                        const MinicCoreModule *module,
                                                        MinicDiagnostic *diagnostic) {
    static const MinicProgramEmitConfig config = {
        rv64_visit_function, NULL, rv64_between,
        NULL, rv64_finalize, MINIC_PROGRAM_FUNCTION_ALL, false
    };
    Rv64ProgramContext context = { path, diagnostic, module };
    bool success;
    if (module == NULL || target == NULL || path == NULL) {
        minic_riscv64_set_diagnostic(diagnostic, path, "module/target/path is required");
        return false;
    }
    if (module->function_count != core_function_count ||
        (core_function_count != 0U && core_functions == NULL)) {
        minic_riscv64_set_diagnostic(diagnostic, path,
            "Core function map does not match program functions");
        return false;
    }
    /* GNU source-asm provenance and strict-I audits remain target-owned.
     * The common transaction removes partial output in ALL ISA profiles. */
    success = minic_program_emit_write_module(
        path, module, target, core_functions, core_function_count,
        function_sections, &config, &context, rv64_visit_module_global);
    if (!success && diagnostic != NULL && diagnostic->message[0] == '\0')
        minic_riscv64_set_diagnostic(diagnostic, path, "cannot write RISC-V assembly");
    return success;
}

bool minic_riscv64_write_core_module(
    const char *path,
    const MinicCoreFunction *core_functions, size_t core_function_count,
    bool function_sections, const MinicTargetInfo *target,
    const MinicCoreModule *module, MinicDiagnostic *diagnostic) {
    return minic_program_emit_module_preflight(
               module, core_functions, core_function_count) &&
           rv64_write_core_program_impl(path,
        core_functions, core_function_count, function_sections,
        target, module, diagnostic);
}
