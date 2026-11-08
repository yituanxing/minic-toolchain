/* RV32I/ILP32 Core consumer. Core, DataLayout, symbol policy, RISC-V
 * opcode selection and assembler are shared with RV64. ILP32 owns its
 * two-word scalar ABI and frame layout; those are not interchangeable with
 * RV64 LP64. Qualified: records, direct/indirect and variadic call sites,
 * IEEE binary64 via pinned compiler-rt, and GNU differential ELF32/QEMU.
 * Variadic callee definitions use the ILP32 psABI register-save area.
 * Unqualified two-word va_arg operands continue to fail closed. */
#include "target/riscv32/rv32_emit.h"
#include "target/riscv32/rv32_internal.h"
#include "target/riscv/machine_target_state.h"
#include "target/riscv/machine_core_select.h"
#include "target/riscv/machine_gnu_asm.h"
#include "target/common/machine_production_preflight.h"
#include "target/riscv32/rv32_abi_cursor.h"
#include "target/riscv/function_symbol.h"
#include "target/riscv32/rv32_global_emit.h"
#include "target/common/program_visit.h"
#include "target/common/program_emit.h"
#include "target/common/elf_alias_emit.h"
#include "target/data_layout.h"
#include <stdio.h>
#include <stdlib.h>

static void rv32_diag(MinicDiagnostic *d, const char *path, const char *text) {
    if (d == NULL) return;
    d->path = path;
    d->line = 1U;
    d->column = 1U;
    (void)snprintf(d->message, sizeof(d->message), "%s", text);
}

static bool rv32_func(FILE *out, const MinicCoreModule *module,
                       const MinicCoreFunction *f,
                       const MinicCoreModuleFunctionSymbol *module_decl,
                       bool function_sections, const MinicTargetInfo *target,
                       const MinicCoreFunctionFacts *facts,
                       const MinicCoreObjectStoragePlan *storage,
                       MinicDiagnostic *diag) {
    Rv32Frame fr = {0};
    const bool *reachable = NULL;
    bool *owned_reachable = NULL;
    MinicRiscvFunctionSymbol sym;
    bool ok = false;
    /* RV32FunctionAbiPlan checks both return and parameter types once,
     * before constructing the frame. Its placements are the sole ILP32
     * authority for call, prologue and return emission. */
    if (!rv32_frame_init_with_storage(
            &fr, f, target, minic_core_function_is_variadic(f), facts, storage)) {
        if (getenv("MINIC_RV32_FRAME_TRACE") != NULL)
            (void)fprintf(stderr,
                "RV32_FRAME_OWNER function=%s parameters=%zu values=%zu objects=%zu instructions=%zu\n",
                module_decl->name != NULL ? module_decl->name : "<unnamed>",
                minic_core_function_parameter_count(f), f->value_count, f->object_count,
                f->instruction_count);
        rv32_diag(diag, "<rv32-frame>", "rv32i: cannot allocate an ILP32 Core frame");
        rv32_frame_destroy(&fr);
        return false;
    }
    if (minic_core_function_facts_verified(facts, f)) {
        reachable = facts->reachable_blocks;
    } else {
        owned_reachable = calloc(f->block_count == 0U ? 1U : f->block_count,
                                 sizeof(*owned_reachable));
        if (owned_reachable != NULL &&
            minic_core_function_mark_reachable_blocks(
                f, owned_reachable, f->block_count))
            reachable = owned_reachable;
    }
    if (reachable == NULL) {
        rv32_diag(diag, "<rv32-core>", "rv32i: cannot classify Core CFG reachability");
        goto done;
    }
    if (!minic_riscv_function_symbol_from_module(module_decl, &sym)) goto done;
    sym.function_sections = function_sections;
    /* Whole-function production cutover. The already-finalized ILP32 frame
     * and ABI cursor are borrowed, not re-planned; failure is fatal for the
     * entire function, with no instruction-level old FILE* fallback. */
    {
        MinicMachineFunction machine = {0};
        MinicMachineGNUAsmPolicy policy = {0};
        MinicMachineGNUAsmPrepared *prepared_body = NULL;
        MinicRiscvPhysicalFrame physical = {0};
        MinicRiscvCoreMachinePlan bridge = {0};
        const MinicMachineProductionSource source = {
            .core = f, .reachable_blocks = reachable, .target = target,
            .symbol = sym.symbol_name
        };
        const char *reject_stage = "physical_frame";
        bool admitted = rv32_machine_physical_frame(&fr, &physical);
        if (admitted) {
            bridge = (MinicRiscvCoreMachinePlan){
                .target = target, .module = module, .core = f,
                .frame = &physical, .abi32 = &fr.abi,
                .value_offsets = fr.value_offsets,
                .value_offset_count = fr.value_count,
                .object_offsets = fr.object_offsets,
                .object_offset_count = fr.object_count
            };
            reject_stage = "select";
            admitted = minic_riscv_machine_select_core_for_production(
                &machine, &bridge);
            if (admitted) {
                reject_stage = "policy";
                admitted = minic_riscv_machine_gnu_asm_policy(target, &policy);
            }
            if (admitted) {
                reject_stage = "preflight";
                admitted = minic_machine_production_preflight_prepare(
                    &source, &machine, &policy, &prepared_body);
            }
        }
        if (!admitted && getenv("MINIC_RV_MACHINE_TRACE") != NULL)
            (void)fprintf(stderr,
                "RV32_TYPED_REJECT_STAGE=%s function=%s\n", reject_stage,
                module_decl->name != NULL ? module_decl->name : "<unnamed>");
        if (!admitted)
            rv32_diag(diag, "<rv32-machine>",
                "rv32i: complete typed MachineIR function rejected");
        ok = admitted && minic_riscv_emit_function_symbol_begin(out, &sym) &&
            minic_machine_gnu_asm_emit_prepared_body(out, prepared_body) &&
            minic_riscv_emit_function_symbol_end(out, &sym);
        minic_machine_gnu_asm_prepared_destroy(prepared_body);
        minic_machine_function_destroy(&machine);
        goto done;
    }
done:
    free(owned_reachable);
    rv32_frame_destroy(&fr);
    return ok;
}
/* Generic TU traversal lives in Target/common; ELF32 globals, declaration
 * aliases, source asm and the physical ILP32 function ABI remain RV32-owned. */
typedef struct Rv32ProgramContext {
    MinicDiagnostic *diagnostic;
    const MinicCoreModule *module;
} Rv32ProgramContext;


static bool rv32_visit_module_global(
    FILE *out, const MinicCoreModule *module, const MinicTargetInfo *target,
    const MinicCoreModuleGlobal *global, size_t index, void *opaque) {
    Rv32ProgramContext *ctx = opaque;
    if (global->definition_kind == MINIC_CORE_MODULE_DEFINITION_DECLARATION) {
        if (global->alias_target_index != SIZE_MAX)
            return minic_multi_elf_emit_module_object_alias(
                out, module, global, "@object");
        return true;
    }
    if (minic_riscv32_emit_module_global(out, target, module, index))
        return true;
    {
        char message[256];
        (void)snprintf(message, sizeof(message),
            "rv32i: unsupported module global object=%s fragments=%zu",
            global->name != NULL ? global->name : "<unnamed>",
            global->fragment_count);
        rv32_diag(ctx->diagnostic, "<rv32-global>", message);
    }
    return false;
}


static bool rv32_visit_function(
    FILE *out, const MinicCoreModule *module, const MinicTargetInfo *target,
    const MinicCoreModuleFunctionSymbol *module_decl,
    const MinicCoreFunction *fn,
    const MinicCoreFunctionFacts *facts,
    const MinicCoreObjectStoragePlan *storage, size_t index,
    bool function_sections, void *opaque) {
    Rv32ProgramContext *ctx = opaque;
    const char *name;
    (void)index;
    if (!rv32_func(out, module, fn, module_decl, function_sections,
                   target, facts, storage, ctx->diagnostic))
        return false;
    if (!module_decl->is_constructor) return true;
    /* GNU ELF32 startup traverses four-byte .init_array function pointers.
     * The function body is already emitted; registration is a separate
     * target-owned ELF record, including for static constructors. */
    name = module_decl->name;
    return name != NULL && name[0] != '\0' &&
           fprintf(out, ".section .init_array,\"aw\",@init_array\n"
                        ".p2align 2\n  .4byte %s\n.text\n", name) >= 0;
}

static bool rv32_prefix(FILE *out, const MinicCoreModule *module,
                        const MinicTargetInfo *target,
                        const MinicCoreFunction *functions, size_t count,
                        bool function_sections, void *opaque) {
    (void)module;
    (void)target;
    (void)functions;
    (void)count;
    (void)function_sections;
    (void)opaque;
    return fprintf(out, ".option norvc\n") >= 0;
}

static bool rv32_between(FILE *out, const MinicCoreModule *module,
                         const MinicTargetInfo *target,
                         const MinicCoreFunction *functions, size_t count,
                         bool function_sections, void *opaque) {
    size_t i;
    (void)target;
    (void)functions;
    (void)count;
    (void)function_sections;
    (void)opaque;
    if (module != NULL) {
        if (module->file_asm_count != 0U && fprintf(out, ".text\n") < 0)
            return false;
        for (i = 0U; i < module->file_asm_count; ++i) {
            const MinicCoreModuleFileAsm *source = &module->file_asms[i];
            if (source->text == NULL ||
                fprintf(out, "# MINIC_SOURCE_FILE_ASM_BEGIN\n") < 0 ||
                fwrite(source->text, 1U, source->length, out) != source->length ||
                fprintf(out, "\n# MINIC_SOURCE_FILE_ASM_END\n") < 0)
                return false;
        }
        return minic_multi_elf_emit_module_function_aliases(
                   out, module, "@function") && fprintf(out, ".text\n") >= 0;
    }
    return false;
}

static bool rv32_write_core_program_impl(const char *path,
                                      const MinicTargetInfo *target,
                                      const MinicCoreFunction *functions,
                                      size_t function_count,
                                      bool function_sections,
                                      const MinicCoreModule *module,
                                      MinicDiagnostic *diagnostic) {
    static const MinicProgramEmitConfig config = {
        rv32_visit_function, rv32_prefix, rv32_between,
        NULL, NULL, MINIC_PROGRAM_FUNCTION_LIVE_DEFINITIONS, false
    };
    bool ok;
    Rv32ProgramContext context = { diagnostic, module };
    if (path == NULL || module == NULL || target == NULL ||
        minic_riscv_target_isa(target).xlen != 32U ||
        minic_riscv_target_isa(target).abi != MINIC_RISCV_ABI_ILP32 ||
        module->function_count != function_count ||
        (function_count != 0U && functions == NULL)) {
        rv32_diag(diagnostic, "<rv32-abi>",
                  "rv32i: unsupported file-scope inline ASM or invalid Core function map");
        return false;
    }
    ok = minic_program_emit_write_module(
        path, module, target, functions, function_count,
        function_sections, &config, &context,
        rv32_visit_module_global);
    if (!ok && diagnostic != NULL && diagnostic->message[0] == '\0')
        rv32_diag(diagnostic, path, "rv32i: emission failed");
    return ok;
}

bool minic_riscv32_write_core_module(const char *path,
                                      const MinicTargetInfo *target,
                                      const MinicCoreFunction *functions,
                                      size_t function_count,
                                      bool function_sections,
                                      const MinicCoreModule *module,
                                      MinicDiagnostic *diagnostic) {
    return minic_program_emit_module_preflight(
               module, functions, function_count) &&
           rv32_write_core_program_impl(
        path, target, functions, function_count,
        function_sections, module, diagnostic);
}
