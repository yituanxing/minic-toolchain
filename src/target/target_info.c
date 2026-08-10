#include "target/target_info.h"

#include <limits.h>
#include <string.h>

const MinicTargetInfo *minic_default_target_info(void) {
    static MinicTargetInfo target;

    if (target.data_layout == NULL) {
        target.data_layout = minic_default_data_layout();
        target.gnu_sizeof_void_is_one = true;
        target.gnu_sizeof_function_is_one = true;
    }
    return &target;
}

const MinicDataLayout *minic_target_info_data_layout(const MinicTargetInfo *target) {
    return target == NULL ? NULL : target->data_layout;
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
