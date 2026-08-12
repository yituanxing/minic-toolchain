#ifndef MINIC_TARGET_TARGET_INFO_H
#define MINIC_TARGET_TARGET_INFO_H

#include "target/data_layout.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct MinicTargetInfo {
    const MinicDataLayout *data_layout;
    MinicType wide_character_type;
    bool gnu_sizeof_void_is_one;
    bool gnu_sizeof_function_is_one;
    bool call_frame_return_address_level0;
    bool call_frame_frame_address_level0;
} MinicTargetInfo;

const MinicTargetInfo *minic_default_target_info(void);
const MinicDataLayout *minic_target_info_data_layout(const MinicTargetInfo *target);
bool minic_target_info_wide_character_type(const MinicTargetInfo *target, MinicType *type);
bool minic_target_info_sizeof_type(const MinicTargetInfo *target,
                                   const MinicC0Program *program,
                                   MinicType type,
                                   size_t *size);
bool minic_target_info_integer_width(const MinicTargetInfo *target,
                                     const MinicC0Program *program,
                                     MinicType type,
                                     unsigned int *bits);
bool minic_target_info_fixed_register_supported(const MinicTargetInfo *target,
                                                const char *name,
                                                size_t name_length);
bool minic_target_info_inline_asm_register_clobber_supported(const MinicTargetInfo *target,
                                                             const char *name,
                                                             size_t name_length);
bool minic_target_info_inline_asm_immediate_constraint_supported(const MinicTargetInfo *target,
                                                                 const char *constraint,
                                                                 size_t constraint_length,
                                                                 int64_t value);
bool minic_target_info_call_frame_address_supported(const MinicTargetInfo *target,
                                                    MinicCallFrameAddressKind kind,
                                                    unsigned int level);

#endif
