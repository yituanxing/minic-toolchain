#include "target/target_info.h"

#include <limits.h>
#include <string.h>

const char *const minic_riscv64_argument_registers[8] = {
    "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7"};

const MinicTargetInfo *minic_default_target_info(void) {
    static MinicTargetInfo target;

    if (target.data_layout == NULL) {
        target.data_layout = minic_default_data_layout();
        target.wide_character_type = minic_type_unsigned_short();
        target.gnu_sizeof_void_is_one = true;
        target.gnu_sizeof_function_is_one = true;
        target.call_frame_return_address_level0 = true;
        target.call_frame_frame_address_level0 = true;
    }
    return &target;
}

const MinicDataLayout *minic_target_info_data_layout(const MinicTargetInfo *target) {
    return target == NULL ? NULL : target->data_layout;
}

bool minic_target_info_wide_character_type(const MinicTargetInfo *target, MinicType *type) {
    if (target == NULL || type == NULL || !minic_type_is_integer(target->wide_character_type)) {
        return false;
    }
    *type = target->wide_character_type;
    return true;
}

bool minic_target_info_sizeof_type(const MinicTargetInfo *target,
                                   const MinicC0Program *program,
                                   MinicType type,
                                   size_t *size) {
    size_t alignment;

    if (target == NULL || program == NULL || size == NULL) {
        return false;
    }
    if (minic_type_is_void(type)) {
        if (!target->gnu_sizeof_void_is_one) {
            return false;
        }
        *size = 1U;
        return true;
    }
    if (minic_type_is_function(type)) {
        if (!target->gnu_sizeof_function_is_one) {
            return false;
        }
        *size = 1U;
        return true;
    }
    return minic_data_layout_type(target->data_layout, program, type, size, &alignment);
}

bool minic_target_info_integer_width(const MinicTargetInfo *target,
                                     const MinicC0Program *program,
                                     MinicType type,
                                     unsigned int *bits) {
    size_t size;
    size_t alignment;

    if (target == NULL || program == NULL || bits == NULL || !minic_type_is_integer(type) ||
        !minic_data_layout_type(target->data_layout, program, type, &size, &alignment) ||
        size == 0U || size > (size_t)(UINT_MAX / CHAR_BIT)) {
        return false;
    }
    (void)alignment;
    *bits = (unsigned int)(size * CHAR_BIT);
    return true;
}

bool minic_target_info_call_frame_address_supported(const MinicTargetInfo *target,
                                                    MinicCallFrameAddressKind kind,
                                                    unsigned int level) {
    if (target == NULL || level != 0U) {
        return false;
    }
    switch (kind) {
    case MINIC_CALL_FRAME_ADDRESS_RETURN:
        return target->call_frame_return_address_level0;
    case MINIC_CALL_FRAME_ADDRESS_FRAME:
        return target->call_frame_frame_address_level0;
    }
    return false;
}

bool minic_target_info_fixed_register_supported(const MinicTargetInfo *target,
                                                const char *name,
                                                size_t name_length) {
    if (target == NULL || name == NULL) {
        return false;
    }
    /* The current RV64 backend uses most caller/scratch registers internally.
     * Only the architectural stack/thread registers observed in unchanged Linux
     * are safe fixed bindings until register allocation becomes target-aware. */
    return (name_length == 2U && memcmp(name, "tp", 2U) == 0) ||
           (name_length == 2U && memcmp(name, "sp", 2U) == 0);
}

bool minic_target_info_inline_asm_register_clobber_supported(const MinicTargetInfo *target,
                                                             const char *name,
                                                             size_t name_length) {
    if (target == NULL || name == NULL) {
        return false;
    }
    /* TargetConstraint v0 exposes the RV64 temporary-register class used by
     * unchanged Linux. Broader physical-register classes remain fail-closed. */
    return name_length == 2U && name[0] == 't' && name[1] >= '0' && name[1] <= '6';
}

bool minic_target_info_inline_asm_immediate_constraint_supported(const MinicTargetInfo *target,
                                                                 const char *constraint,
                                                                 size_t constraint_length,
                                                                 int64_t value) {
    if (target == NULL || constraint == NULL) {
        return false;
    }
    /* RISC-V GCC constraint I is a signed 12-bit integer immediate. */
    return constraint_length == 1U && constraint[0] == 'I' && value >= -2048 && value <= 2047;
}
