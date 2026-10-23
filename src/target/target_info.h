#ifndef MINIC_TARGET_TARGET_INFO_H
#define MINIC_TARGET_TARGET_INFO_H

#include "target/data_layout.h"
#include "target/riscv64/isa.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum MinicTargetArchitecture {
    MINIC_TARGET_ARCH_RISCV64 = 0,
    MINIC_TARGET_ARCH_X86_64,
    MINIC_TARGET_ARCH_AARCH64,
    MINIC_TARGET_ARCH_LOONGARCH64
} MinicTargetArchitecture;

/* Zero is invalid: a newly registered target must specify its cursor. */
typedef enum MinicVariadicScalarReadKind {
    MINIC_VARIADIC_SCALAR_READ_UNSUPPORTED = 0,
    MINIC_VARIADIC_SCALAR_READ_LINEAR_SLOT,
    MINIC_VARIADIC_SCALAR_READ_ABI_CURSOR
} MinicVariadicScalarReadKind;

/* ABI policy, not a Core architecture switch. Copying a structured cursor
 * differs from assigning a scalar cursor; unsupported is always explicit. */
typedef enum MinicVariadicCopyKind {
    MINIC_VARIADIC_COPY_UNSUPPORTED = 0,
    MINIC_VARIADIC_COPY_SCALAR_ASSIGNMENT,
    MINIC_VARIADIC_COPY_ABI_STATE
} MinicVariadicCopyKind;

typedef enum MinicIntegerLiteralBase {
    MINIC_INTEGER_LITERAL_BASE_BINARY = 2,
    MINIC_INTEGER_LITERAL_BASE_OCTAL = 8,
    MINIC_INTEGER_LITERAL_BASE_DECIMAL = 10,
    MINIC_INTEGER_LITERAL_BASE_HEXADECIMAL = 16
} MinicIntegerLiteralBase;

typedef struct MinicTargetIntegerModel {
    MinicIntegerSign plain_char_sign;
    /* Target semantic width used for integer ranges and conversions. Signed
       widths include the sign bit; this is intentionally independent of object
       storage size/alignment and therefore _Bool is one semantic bit on RV64. */
    unsigned int semantic_width_bits[MINIC_INTEGER_RANK_INT128 + 1];
} MinicTargetIntegerModel;

typedef struct MinicTargetInfo {
    MinicTargetArchitecture architecture;
    const MinicDataLayout *data_layout;
    const MinicTargetIntegerModel *integer_model;
    MinicType wide_character_type;
    MinicIntegerRank word_integer_rank;
    bool gnu_sizeof_void_is_one;
    bool gnu_sizeof_function_is_one;
    bool call_frame_return_address_level0;
    bool call_frame_frame_address_level0;
    MinicVariadicScalarReadKind variadic_scalar_read_kind;
    size_t variadic_linear_slot_bytes;
    MinicVariadicCopyKind variadic_copy_kind;
    /* Per-translation-unit RV64 GNU PIC/PIE lowering selection.
     * The canonical target descriptors default to static PC-relative.
     * A CLI invocation copies the descriptor before setting this bit. */
    bool position_independent;
    /* RISC-V-specific, per-invocation subtarget. Legacy default preserved. */
    MinicRiscvIsa riscv_isa;
} MinicTargetInfo;

extern const char *const minic_riscv64_argument_registers[8];

const MinicTargetInfo *minic_default_target_info(void);
const MinicTargetInfo *minic_target_info_for_architecture(MinicTargetArchitecture architecture);
const MinicTargetInfo *minic_target_info_from_name(const char *name);
const MinicDataLayout *minic_target_info_data_layout(const MinicTargetInfo *target);
const MinicTargetIntegerModel *minic_target_info_integer_model(const MinicTargetInfo *target);
bool minic_target_info_variadic_state_copy_type_supported(
    const MinicTargetInfo *, MinicType);
bool minic_target_info_plain_char_sign(const MinicTargetInfo *target, MinicIntegerSign *sign);
bool minic_target_info_plain_char_type(const MinicTargetInfo *target, MinicType *type);
bool minic_target_info_wide_character_type(const MinicTargetInfo *target, MinicType *type);
bool minic_target_info_word_integer_type(const MinicTargetInfo *target,
                                         MinicIntegerSign sign,
                                         MinicType *type);
bool minic_target_info_sizeof_type(const MinicTargetInfo *target,
                                   const MinicC0Program *program,
                                   MinicType type,
                                   size_t *size);
bool minic_target_info_integer_width(const MinicTargetInfo *target,
                                     const MinicC0Program *program,
                                     MinicType type,
                                     unsigned int *bits);
bool minic_target_info_integer_promotion(const MinicTargetInfo *target,
                                         MinicType type,
                                         MinicType *result);
bool minic_target_info_integer_promotion_for_program(const MinicTargetInfo *target,
                                                     const MinicC0Program *program,
                                                     MinicType type,
                                                     MinicType *result);
bool minic_target_info_integer_common(const MinicTargetInfo *target,
                                      MinicType left,
                                      MinicType right,
                                      MinicType *result);
bool minic_target_info_integer_common_for_program(const MinicTargetInfo *target,
                                                  const MinicC0Program *program,
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
bool minic_target_info_local_fixed_register_supported(const MinicTargetInfo *target,
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
