#ifndef MINIC_TARGET_TARGET_INFO_H
#define MINIC_TARGET_TARGET_INFO_H

#include "common/call_frame_address.h"
#include "common/calling_convention.h"
#include "target/data_layout.h"
#include "target/machine_register.h"
#include "target/riscv/isa_features.h"
#include "target/target_catalog.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Machine ABI transport and source C va_list identity are separate.
 * A new target must explicitly describe its machine cursor transport. */
typedef enum MinicVariadicScalarReadKind {
    MINIC_VARIADIC_SCALAR_READ_UNSUPPORTED = 0,
    MINIC_VARIADIC_SCALAR_READ_LINEAR_SLOT,
    MINIC_VARIADIC_SCALAR_READ_ABI_CURSOR
} MinicVariadicScalarReadKind;

typedef enum MinicVariadicCopyKind {
    MINIC_VARIADIC_COPY_UNSUPPORTED = 0,
    MINIC_VARIADIC_COPY_SCALAR_ASSIGNMENT,
    MINIC_VARIADIC_COPY_ABI_STATE
} MinicVariadicCopyKind;

/* Machine lowering capabilities do not follow from C declaration admission. */
typedef enum MinicTargetCallCapability {
    MINIC_TARGET_CALL_MS_X64_FIXED_SCALAR = 1U << 0
} MinicTargetCallCapability;

struct MinicCoreModule;
struct MinicTargetInfo;

/* Query a sealed machine-internal builtin name, never a C signature. */
typedef bool (*MinicTargetMachineBuiltinFormLookup)(
    const struct MinicTargetInfo *, const char *, size_t,
    size_t *, const char **, size_t *);

typedef struct MinicTargetInfo {
    MinicTargetArchitecture architecture;
    /* Exact machine-recognized fixed-register forms, shared with SourcePolicy
     * through the ISA-owned recognizer, never a pointer to C source state. */
    MinicMachineRegisterRecognizer fixed_register_read;
    MinicMachineRegisterRecognizer fixed_register_write;
    MinicMachineRegisterRecognizer local_fixed_register;
    MinicTargetMachineBuiltinFormLookup machine_builtin_form_lookup;
    const MinicDataLayout *data_layout;
    unsigned int call_lowering_capabilities;
    bool call_frame_return_address_level0;
    bool call_frame_frame_address_level0;
    bool call_frame_frame_address_level1;
    MinicVariadicScalarReadKind variadic_scalar_read_kind;
    size_t variadic_linear_slot_bytes;
    MinicVariadicCopyKind variadic_copy_kind;
    /* Machine ELF/CALL lowering, not C integer model or declaration policy. */
    bool pic_lowering_supported;
    bool position_independent;
    unsigned int x86_codegen_options;
    bool loongarch64_soft_float_abi;
    MinicRiscvIsa riscv_isa;
} MinicTargetInfo;

const MinicTargetInfo *minic_default_target_info(void);
const MinicTargetInfo *minic_target_info_for_architecture(
    MinicTargetArchitecture architecture);
const MinicTargetInfo *minic_target_info_from_name(const char *name);
const MinicDataLayout *minic_target_info_data_layout(const MinicTargetInfo *target);

/* Core-only machine builtin form, after source qualification was sealed. */
bool minic_target_info_machine_builtin_form(
    const MinicTargetInfo *target, const char *internal_callee, size_t length,
    size_t *argument_count,
    const char **mnemonic, size_t *immediate_index);

/* Sealed Module ELF symbol facts; never inferred from unsealed source AST. */
bool minic_target_info_elf_module_object_is_tu_local(
    const struct MinicCoreModule *module, const char *symbol);
bool minic_target_info_elf_module_function_is_tu_local(
    const struct MinicCoreModule *module, const char *symbol);
bool minic_target_info_elf_module_object_is_nonpreemptible(
    const struct MinicCoreModule *module, const char *symbol);
bool minic_target_info_elf_module_function_is_nonpreemptible(
    const struct MinicCoreModule *module, const char *symbol);
bool minic_target_info_elf_module_object_is_undefined_weak(
    const struct MinicCoreModule *module, const char *symbol);
bool minic_target_info_elf_module_function_is_undefined_weak(
    const struct MinicCoreModule *module, const char *symbol);

bool minic_target_info_function_calling_convention_scalar_call_supported(
    const MinicTargetInfo *target, MinicFunctionCallingConvention convention);
/* Sealed Core machine hard-register identity checks. These take only an
 * ISA register spelling; C declaration/lvalue qualification is separate. */
bool minic_target_info_fixed_register_write_supported(
    const MinicTargetInfo *target, const char *name, size_t name_length);
bool minic_target_info_fixed_register_supported(
    const MinicTargetInfo *target, const char *name, size_t name_length);
bool minic_target_info_local_fixed_register_supported(
    const MinicTargetInfo *target, const char *name, size_t name_length);
bool minic_target_info_call_frame_address_supported(
    const MinicTargetInfo *target,
    MinicCallFrameAddressKind kind, unsigned int level);

#endif
