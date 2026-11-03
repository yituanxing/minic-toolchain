#include "target/target_info.h"
#include "core/module_ir.h"
#include "target/loongarch64/machine_builtin_form.h"
#include "target/source_policy/target_source_info.h"

#include <stdint.h>
#include <string.h>

/* Physical ISA capabilities and byte layout belong to machine TargetInfo.
 * The immutable C-language registry is owned separately by SourcePolicy. */
typedef struct MinicTargetMachineDescriptor {
    MinicTargetInfo target;
    const MinicDataLayout *(*layout)(void);
} MinicTargetMachineDescriptor;

static const MinicTargetMachineDescriptor minic_machine_riscv32 = {
    .target = {
        .variadic_scalar_read_kind = MINIC_VARIADIC_SCALAR_READ_LINEAR_SLOT,
        .variadic_linear_slot_bytes = 4U,
        .variadic_copy_kind = MINIC_VARIADIC_COPY_SCALAR_ASSIGNMENT,
        .pic_lowering_supported = true,
        .call_frame_return_address_level0 = true,
        .call_frame_frame_address_level0 = true,
        .riscv_isa = {.xlen = 32U, .abi = MINIC_RISCV_ABI_ILP32,
                      .strict_isa = true},
    },
    .layout = minic_riscv32_linux_data_layout,
};
static const MinicTargetMachineDescriptor minic_machine_riscv64 = {
    .target = {
        .variadic_scalar_read_kind = MINIC_VARIADIC_SCALAR_READ_LINEAR_SLOT,
        .variadic_linear_slot_bytes = 8U,
        .variadic_copy_kind = MINIC_VARIADIC_COPY_SCALAR_ASSIGNMENT,
        .pic_lowering_supported = true,
        .call_frame_return_address_level0 = true,
        .call_frame_frame_address_level0 = true,
        .riscv_isa = {.xlen = 64U, .abi = MINIC_RISCV_ABI_LP64D,
                      .strict_isa = false},
    },
    .layout = minic_default_data_layout,
};
static const MinicTargetMachineDescriptor minic_machine_x86_64 = {
    .target = {
        .call_lowering_capabilities = MINIC_TARGET_CALL_MS_X64_FIXED_SCALAR,
        .variadic_scalar_read_kind = MINIC_VARIADIC_SCALAR_READ_ABI_CURSOR,
        .variadic_copy_kind = MINIC_VARIADIC_COPY_ABI_STATE,
        .pic_lowering_supported = true,
        .call_frame_return_address_level0 = true,
        .call_frame_frame_address_level0 = true,
    },
    .layout = minic_x86_64_linux_data_layout,
};
static const MinicTargetMachineDescriptor minic_machine_aarch64 = {
    .target = {
        .variadic_scalar_read_kind = MINIC_VARIADIC_SCALAR_READ_ABI_CURSOR,
        .variadic_copy_kind = MINIC_VARIADIC_COPY_ABI_STATE,
        .pic_lowering_supported = true,
        .call_frame_return_address_level0 = true,
        .call_frame_frame_address_level0 = true,
        .call_frame_frame_address_level1 = true,
    },
    .layout = minic_aarch64_linux_data_layout,
};
static const MinicTargetMachineDescriptor minic_machine_loongarch64 = {
    .target = {
        .machine_builtin_form_lookup = minic_loongarch64_machine_builtin_form,
        .variadic_scalar_read_kind = MINIC_VARIADIC_SCALAR_READ_LINEAR_SLOT,
        .variadic_linear_slot_bytes = 8U,
        .variadic_copy_kind = MINIC_VARIADIC_COPY_SCALAR_ASSIGNMENT,
        .pic_lowering_supported = true,
        .call_frame_return_address_level0 = true,
        .call_frame_frame_address_level0 = true,
    },
    .layout = minic_loongarch64_linux_data_layout,
};

/* This is the one machine registration order, shared with the driver and
 * independently maintained source-language registry through the catalog. */
static const MinicTargetMachineDescriptor *const minic_target_machine_descriptors[] = {
#define MINIC_TARGET_REGISTRATION(arch, id, spelling, stages, isa) \
    &minic_machine_##id,
#include "target/target_registration.def"
#undef MINIC_TARGET_REGISTRATION
};

const MinicTargetInfo *minic_target_info_for_architecture(
    MinicTargetArchitecture architecture) {
    static MinicTargetInfo canonical_targets[
        sizeof(minic_target_machine_descriptors) /
        sizeof(minic_target_machine_descriptors[0])];
    const MinicTargetCatalogEntry *catalog;
    size_t count;
    size_t index;

    catalog = minic_target_catalog_all(&count);
    if (count != sizeof(minic_target_machine_descriptors) /
                 sizeof(minic_target_machine_descriptors[0])) return NULL;
    for (index = 0U; index < count; ++index) {
        const MinicTargetMachineDescriptor *descriptor =
            minic_target_machine_descriptors[index];
        MinicTargetInfo *target = &canonical_targets[index];
        if (catalog[index].architecture != architecture) continue;
        if (target->data_layout == NULL) {
            MinicTargetInfo initialized = descriptor->target;
            initialized.architecture = catalog[index].architecture;
            initialized.data_layout = descriptor->layout();
            initialized.source_info = minic_target_source_info_for_architecture(
                catalog[index].architecture);
            if (initialized.data_layout == NULL ||
                initialized.source_info == NULL)
                return NULL;
            *target = initialized;
        }
        return target;
    }
    return NULL;
}

const MinicTargetInfo *minic_default_target_info(void) {
    return minic_target_info_for_architecture(MINIC_TARGET_ARCH_RISCV64);
}

/* Target spellings are owned by one catalog shared with the staged driver.
 * TargetInfo remains the authority for C widths, language facts and ABI. */
const MinicTargetInfo *minic_target_info_from_name(const char *name) {
    const MinicTargetCatalogEntry *entry = minic_target_catalog_find(name);
    return entry == NULL ? NULL :
           minic_target_info_for_architecture(entry->architecture);
}

const MinicDataLayout *minic_target_info_data_layout(const MinicTargetInfo *target) {
    return target == NULL ? NULL : target->data_layout;
}

static bool minic_elf_module_visibility_is_nonpreemptible(
    MinicCoreModuleVisibility visibility) {
    return visibility == MINIC_CORE_MODULE_VISIBILITY_HIDDEN ||
           visibility == MINIC_CORE_MODULE_VISIBILITY_INTERNAL;
}

bool minic_target_info_elf_module_object_is_tu_local(
    const MinicCoreModule *module, const char *symbol) {
    size_t index;
    if (module == NULL || !module->finalized || symbol == NULL ||
        (module->global_count != 0U && module->globals == NULL)) return false;
    for (index = 0U; index < module->global_count; ++index) {
        const MinicCoreModuleGlobal *object = &module->globals[index];
        if (object->name != NULL && strcmp(object->name, symbol) == 0 &&
            object->is_internal && !object->is_extern &&
            !object->is_block_scope_extern_only) return true;
    }
    return false;
}

bool minic_target_info_elf_module_function_is_tu_local(
    const MinicCoreModule *module, const char *symbol) {
    size_t index;
    if (module == NULL || !module->finalized || symbol == NULL ||
        (module->function_count != 0U && module->function_symbols == NULL)) return false;
    for (index = 0U; index < module->function_count; ++index) {
        const MinicCoreModuleFunctionSymbol *function =
            &module->function_symbols[index];
        if (function->name != NULL && strcmp(function->name, symbol) == 0 &&
            function->is_defined && function->is_internal) return true;
    }
    return false;
}

bool minic_target_info_elf_module_object_is_nonpreemptible(
    const MinicCoreModule *module, const char *symbol) {
    size_t index;
    if (module == NULL || !module->finalized || symbol == NULL ||
        (module->global_count != 0U && module->globals == NULL)) return false;
    for (index = 0U; index < module->global_count; ++index) {
        const MinicCoreModuleGlobal *object = &module->globals[index];
        if (object->name != NULL && strcmp(object->name, symbol) == 0 &&
            ((object->is_internal && !object->is_extern &&
              !object->is_block_scope_extern_only) ||
             minic_elf_module_visibility_is_nonpreemptible(object->visibility)))
            return true;
    }
    return false;
}

bool minic_target_info_elf_module_function_is_nonpreemptible(
    const MinicCoreModule *module, const char *symbol) {
    size_t index;
    if (module == NULL || !module->finalized || symbol == NULL ||
        (module->function_count != 0U && module->function_symbols == NULL)) return false;
    for (index = 0U; index < module->function_count; ++index) {
        const MinicCoreModuleFunctionSymbol *function =
            &module->function_symbols[index];
        if (function->name != NULL && strcmp(function->name, symbol) == 0 &&
            ((function->is_defined && function->is_internal) ||
             minic_elf_module_visibility_is_nonpreemptible(function->visibility)))
            return true;
    }
    return false;
}

bool minic_target_info_elf_module_object_is_undefined_weak(
    const MinicCoreModule *module, const char *symbol) {
    size_t index;
    if (module == NULL || !module->finalized || symbol == NULL ||
        (module->global_count != 0U && module->globals == NULL)) return false;
    for (index = 0U; index < module->global_count; ++index) {
        const MinicCoreModuleGlobal *object = &module->globals[index];
        if (object->name != NULL && strcmp(object->name, symbol) == 0)
            return object->is_weak && object->is_extern &&
                   !object->is_tentative;
    }
    return false;
}

bool minic_target_info_elf_module_function_is_undefined_weak(
    const MinicCoreModule *module, const char *symbol) {
    size_t index;
    if (module == NULL || !module->finalized || symbol == NULL ||
        (module->function_count != 0U && module->function_symbols == NULL)) return false;
    for (index = 0U; index < module->function_count; ++index) {
        const MinicCoreModuleFunctionSymbol *function =
            &module->function_symbols[index];
        if (function->name != NULL && strcmp(function->name, symbol) == 0)
            return function->is_weak && !function->is_defined;
    }
    return false;
}

bool minic_target_info_function_calling_convention_scalar_call_supported(
    const MinicTargetInfo *target, MinicFunctionCallingConvention convention) {
    if (target == NULL || !minic_function_calling_convention_valid(convention))
        return false;
    if (convention == MINIC_CALLING_CONVENTION_DEFAULT)
        return true;
    return convention == MINIC_CALLING_CONVENTION_MS_X64 &&
           (target->call_lowering_capabilities &
            MINIC_TARGET_CALL_MS_X64_FIXED_SCALAR) != 0U;
}
bool minic_target_info_call_frame_address_supported(const MinicTargetInfo *target,
                                                    MinicCallFrameAddressKind kind,
                                                    unsigned int level) {
    if (target == NULL || level > 1U) {
        return false;
    }
    switch (kind) {
    case MINIC_CALL_FRAME_ADDRESS_RETURN:
        return level == 0U && target->call_frame_return_address_level0;
    case MINIC_CALL_FRAME_ADDRESS_FRAME:
        return level == 0U ? target->call_frame_frame_address_level0 :
                             target->call_frame_frame_address_level1;
    }
    return false;
}

bool minic_target_info_machine_builtin_form(
    const MinicTargetInfo *target, const char *internal_callee, size_t length,
    size_t *argument_count,
    const char **mnemonic, size_t *immediate_index) {
    return target != NULL && target->machine_builtin_form_lookup != NULL &&
           target->machine_builtin_form_lookup(
               target, internal_callee, length, argument_count,
               mnemonic, immediate_index);
}
