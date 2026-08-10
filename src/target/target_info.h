#ifndef MINIC_TARGET_TARGET_INFO_H
#define MINIC_TARGET_TARGET_INFO_H

#include "target/data_layout.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct MinicTargetInfo {
    const MinicDataLayout *data_layout;
    bool gnu_sizeof_void_is_one;
    bool gnu_sizeof_function_is_one;
} MinicTargetInfo;

const MinicTargetInfo *minic_default_target_info(void);
const MinicDataLayout *minic_target_info_data_layout(const MinicTargetInfo *target);
bool minic_target_info_sizeof_type(const MinicTargetInfo *target,
                                   const MinicC0Program *program,
                                   MinicType type,
                                   size_t *size);
bool minic_target_info_integer_width(const MinicTargetInfo *target,
                                     const MinicC0Program *program,
                                     MinicType type,
                                     unsigned int *bits);

#endif
