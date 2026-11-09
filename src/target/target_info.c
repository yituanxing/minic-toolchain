#include "target/target_info.h"
#include "target/loongarch64/machine_builtin_form.h"
#include "target/riscv/machine_target_state.h"

#include <stdint.h>

/* Physical ISA capabilities and byte layout belong to machine TargetInfo.
 * The immutable C-language registry is owned separately by SourcePolicy. */
typedef struct MinicTargetMachineDescriptor {
    MinicTargetInfo target;
    const MinicDataLayout *(*layout)(void);
    bool (*initialize_machine_state)(MinicTargetInfo *);
} MinicTargetMachineDescriptor;

static const MinicTargetMachineDescriptor minic_machine_riscv32 = {
    .target = {
        .fixed_register_read = minic_riscv32_machine_fixed_register_read,
        .local_fixed_register = minic_riscv32_machine_local_fixed_register,
        .variadic_scalar_read_kind = MINIC_VARIADIC_SCALAR_READ_LINEAR_SLOT,
        .variadic_linear_slot_bytes = 4U,
        .variadic_copy_kind = MINIC_VARIADIC_COPY_SCALAR_ASSIGNMENT,
        .pic_lowering_supported = true,
        .call_frame_return_address_level0 = true,
        .call_frame_frame_address_level0 = true,
    },
    .layout = minic_riscv32_linux_data_layout,
    .initialize_machine_state = minic_riscv32_target_state_initialize,
};
static const MinicTargetMachineDescriptor minic_machine_riscv64 = {
    .target = {
        .fixed_register_read = minic_riscv64_machine_fixed_register_read,
        .local_fixed_register = minic_riscv64_machine_local_fixed_register,
        .variadic_scalar_read_kind = MINIC_VARIADIC_SCALAR_READ_LINEAR_SLOT,
        .variadic_linear_slot_bytes = 8U,
        .variadic_copy_kind = MINIC_VARIADIC_COPY_SCALAR_ASSIGNMENT,
        .pic_lowering_supported = true,
        .call_frame_return_address_level0 = true,
        .call_frame_frame_address_level0 = true,
    },
    .layout = minic_default_data_layout,
    .initialize_machine_state = minic_riscv64_target_state_initialize,
};
static const MinicTargetMachineDescriptor minic_machine_x86_64 = {
    .target = {
        .fixed_register_read = minic_x86_64_machine_fixed_register_read,
        .local_fixed_register = minic_x86_64_machine_local_fixed_register,
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
        .fixed_register_read = minic_aarch64_machine_fixed_register_read,
        .local_fixed_register = minic_aarch64_machine_local_fixed_register,
        .machine_builtin_form_lookup = minic_aarch64_machine_builtin_form,
        .machine_builtin_effect_lookup = minic_aarch64_machine_builtin_effect,
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
        .fixed_register_read = minic_loongarch64_machine_fixed_register_read,
        .fixed_register_write = minic_loongarch64_machine_fixed_register_write,
        .local_fixed_register = minic_loongarch64_machine_local_fixed_register,
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
            if (initialized.data_layout == NULL ||
                (descriptor->initialize_machine_state != NULL &&
                 !descriptor->initialize_machine_state(&initialized)))
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
 * Machine TargetInfo never stores a source-language descriptor. */
const MinicTargetInfo *minic_target_info_from_name(const char *name) {
    const MinicTargetCatalogEntry *entry = minic_target_catalog_find(name);
    return entry == NULL ? NULL :
           minic_target_info_for_architecture(entry->architecture);
}

const MinicDataLayout *minic_target_info_data_layout(const MinicTargetInfo *target) {
    return target == NULL ? NULL : target->data_layout;
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

bool minic_target_info_fixed_register_supported(
    const MinicTargetInfo *target, const char *name, size_t name_length) {
    return target != NULL && name != NULL &&
           target->fixed_register_read != NULL &&
           target->fixed_register_read(name, name_length);
}

bool minic_target_info_fixed_register_write_supported(
    const MinicTargetInfo *target, const char *name, size_t name_length) {
    return target != NULL && name != NULL &&
           target->fixed_register_write != NULL &&
           target->fixed_register_write(name, name_length);
}

bool minic_target_info_local_fixed_register_supported(
    const MinicTargetInfo *target, const char *name, size_t name_length) {
    return target != NULL && name != NULL &&
           target->local_fixed_register != NULL &&
           target->local_fixed_register(name, name_length);
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

MinicMachineBuiltinEffect minic_target_info_machine_builtin_effect(
    const MinicTargetInfo *target, const char *internal_callee, size_t length) {
    return target != NULL && target->machine_builtin_effect_lookup != NULL
        ? target->machine_builtin_effect_lookup(target, internal_callee, length)
        : MINIC_MACHINE_BUILTIN_EFFECT_NONE;
}
