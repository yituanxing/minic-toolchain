#include "target/backend.h"
#include "core/module_ir.h"
#include "target/common/program_emit.h"

#include "target/riscv64/codegen.h"
#include "target/riscv32/rv32_emit.h"
#include "target/x86_64/codegen.h"
#include "target/aarch64/codegen.h"
#include "target/loongarch64/codegen.h"

static bool emit_riscv64_module(
    const char *output_path, const MinicTargetInfo *target,
    const MinicCoreFunction *functions,
    size_t function_count, bool function_sections,
    const MinicCoreModule *module, MinicDiagnostic *diagnostic) {
    return minic_riscv64_write_core_module(
        output_path, functions, function_count,
        function_sections, target, module, diagnostic);
}

/* Machine emission is registered only against the canonical target catalog.
 * No C source admission or GNU operand qualifier is present in this object.
 * A new ISA cannot fall through to another target's code generator. */
static const MinicBackend riscv64_backend = {
    .emit_module = emit_riscv64_module,
    .emit_reachable_core_blocks_only = true,
};
static const MinicBackend riscv32_backend = {
    .emit_module = minic_riscv32_write_core_module,
    .emit_reachable_core_blocks_only = true,
};
static const MinicBackend x86_64_backend = {
    .emit_module = minic_x86_64_write_core_module,
    .emit_reachable_core_blocks_only = true,
};
static const MinicBackend aarch64_backend = {
    .emit_module = minic_aarch64_write_core_module,
    .emit_reachable_core_blocks_only = true,
};
static const MinicBackend loongarch64_backend = {
    .emit_module = minic_loongarch64_write_core_module,
    .emit_reachable_core_blocks_only = true,
};

static const MinicBackend *const minic_registered_backends[] = {
#define MINIC_TARGET_REGISTRATION(arch, id, spelling, stages, isa) \
    &id##_backend,
#include "target/target_registration.def"
#undef MINIC_TARGET_REGISTRATION
};

const MinicBackend *minic_backend_for_target(const MinicTargetInfo *target) {
    const MinicTargetCatalogEntry *catalog;
    size_t count, index;
    if (target == NULL) return NULL;
    catalog = minic_target_catalog_all(&count);
    if (count != sizeof(minic_registered_backends) /
                 sizeof(minic_registered_backends[0])) return NULL;
    for (index = 0U; index < count; ++index)
        if (catalog[index].architecture == target->architecture)
            return minic_registered_backends[index];
    return NULL;
}

bool minic_backend_emit_assembly_with_module(
    const MinicBackend *backend, const char *output_path,
    const MinicTargetInfo *target,
    const MinicCoreFunction *functions, size_t function_count,
    bool function_sections, const MinicCoreModule *module,
    MinicDiagnostic *diagnostic) {
    if (backend == NULL || target == NULL || module == NULL ||
        backend != minic_backend_for_target(target) ||
        backend->emit_module == NULL ||
        !minic_program_emit_module_preflight(module, functions, function_count))
        return false;
    return backend->emit_module(output_path, target,
        functions, function_count, function_sections, module, diagnostic);
}
