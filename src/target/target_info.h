#ifndef MINIC_TARGET_TARGET_INFO_H
#define MINIC_TARGET_TARGET_INFO_H

#include "target/data_layout.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum MinicIntegerLiteralBase {
    MINIC_INTEGER_LITERAL_BASE_DECIMAL = 10,
    MINIC_INTEGER_LITERAL_BASE_OCTAL = 8,
    MINIC_INTEGER_LITERAL_BASE_HEXADECIMAL = 16
} MinicIntegerLiteralBase;

typedef struct MinicTargetIntegerModel {
    MinicIntegerSign plain_char_sign;
    unsigned int value_bits[MINIC_INTEGER_RANK_INT128 + 1];
} MinicTargetIntegerModel;

typedef struct MinicTargetInfo {
    const MinicDataLayout *data_layout;
    const MinicTargetIntegerModel *integer_model;
    MinicType wide_character_type;
    bool gnu_sizeof_void_is_one;
    bool gnu_sizeof_function_is_one;
    bool call_frame_return_address_level0;
    bool call_frame_frame_address_level0;
} MinicTargetInfo;

extern const char *const minic_riscv64_argument_registers[8];

const MinicTargetInfo *minic_default_target_info(void);
const MinicDataLayout *minic_target_info_data_layout(const MinicTargetInfo *target);
const MinicTargetIntegerModel *minic_target_info_integer_model(const MinicTargetInfo *target);
bool minic_target_info_plain_char_sign(const MinicTargetInfo *target, MinicIntegerSign *sign);
bool minic_target_info_wide_character_type(const MinicTargetInfo *target, MinicType *type);
bool minic_target_info_sizeof_type(const MinicTargetInfo *target,
                                   const MinicC0Program *program,
                                   MinicType type,
                                   size_t *size);
bool minic_target_info_integer_value_width(const MinicTargetInfo *target,
                                           MinicType type,
                                           unsigned int *bits);
bool minic_target_info_integer_width(const MinicTargetInfo *target,
                                     const MinicC0Program *program,
                                     MinicType type,
                                     unsigned int *bits);
bool minic_target_info_integer_promotion(const MinicTargetInfo *target,
                                         MinicType type,
                                         MinicType *result);
bool minic_target_info_integer_common(const MinicTargetInfo *target,
                                      MinicType left,
                                      MinicType right,
                                      MinicType *result);
bool minic_target_info_integer_literal_type(const MinicTargetInfo *target,
                                            MinicIntegerLiteralBase base,
                                            bool has_unsigned_suffix,
                                            unsigned int long_count,
                                            uint64_t value,
                                            MinicType *result);
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
