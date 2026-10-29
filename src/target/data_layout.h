#ifndef MINIC_TARGET_DATA_LAYOUT_H
#define MINIC_TARGET_DATA_LAYOUT_H

#include "frontend/ast.h"

#include <stdbool.h>
#include <stddef.h>

typedef enum MinicByteOrder {
    MINIC_BYTE_ORDER_UNKNOWN = 0,
    MINIC_BYTE_ORDER_LITTLE,
    MINIC_BYTE_ORDER_BIG
} MinicByteOrder;

typedef struct MinicDataLayout {
    /* An ABI property, not an architecture test in Frontend or Core.
     * A bytewise bit-field lowering path must reject an unknown order. */
    MinicByteOrder byte_order;
    size_t pointer_size;
    size_t pointer_alignment;
    size_t integer_size[MINIC_INTEGER_RANK_INT128 + 1U];
    size_t integer_alignment[MINIC_INTEGER_RANK_INT128 + 1U];
    size_t float_size;
    size_t float_alignment;
    size_t double_size;
    size_t double_alignment;
    size_t long_double_size;
    size_t long_double_alignment;
    /* Separate IEEE binary128 identity; 0 means unsupported target. */
    size_t float128_size;
    size_t float128_alignment;
} MinicDataLayout;

bool minic_data_layout_is_little_endian(const MinicDataLayout *layout);
const MinicDataLayout *minic_default_data_layout(void);
/* ILP32 data model for RV32; semantic widths and storage are target-owned. */
const MinicDataLayout *minic_riscv32_linux_data_layout(void);
/* x86-64 Linux SysV has a distinct target identity even when sizes coincide. */
const MinicDataLayout *minic_x86_64_linux_data_layout(void);
/* AAPCS64 LP64 and LoongArch ELF lp64d: separate target-owned layouts. */
const MinicDataLayout *minic_aarch64_linux_data_layout(void);
const MinicDataLayout *minic_loongarch64_linux_data_layout(void);
bool minic_data_layout_type(const MinicDataLayout *layout,
                            const MinicC0Program *program,
                            MinicType type,
                            size_t *size,
                            size_t *alignment);
/* Machine-neutral validation of the complete C0 object and record layout.
 * Physical frame allocation, value slots, and target ABI are NOT computed.
 * This is the unique translation-unit layout admission for all five targets. */
typedef enum MinicDataLayoutProgramCheck {
    MINIC_DATA_LAYOUT_PROGRAM_OK = 0,
    MINIC_DATA_LAYOUT_PROGRAM_INVALID_INPUT,
    MINIC_DATA_LAYOUT_PROGRAM_INVALID_RECORD,
    MINIC_DATA_LAYOUT_PROGRAM_INVALID_GLOBAL
} MinicDataLayoutProgramCheck;
MinicDataLayoutProgramCheck minic_data_layout_validate_program(
    const MinicDataLayout *, const MinicC0Program *);

bool minic_data_layout_global_object(const MinicDataLayout *layout,
                                     const MinicC0Program *program,
                                     const MinicGlobalObject *object,
                                     size_t *size,
                                     size_t *alignment);
bool minic_data_layout_record_field_alignment(const MinicDataLayout *layout,
                                              const MinicC0Program *program,
                                              const MinicRecord *record,
                                              size_t field_index,
                                              size_t *alignment);
bool minic_data_layout_record_field_layout(const MinicDataLayout *layout,
                                           const MinicC0Program *program,
                                           const MinicRecord *record,
                                           size_t field_index,
                                           size_t *offset,
                                           size_t *bit_offset);
bool minic_data_layout_record_field_offset(const MinicDataLayout *layout,
                                           const MinicC0Program *program,
                                           const MinicRecord *record,
                                           size_t field_index,
                                           size_t *offset);
bool minic_data_layout_global_relocation_offset(const MinicDataLayout *layout,
                                                const MinicC0Program *program,
                                                const MinicGlobalObject *object,
                                                const MinicGlobalRelocation *relocation,
                                                size_t *offset);
bool minic_data_layout_global_relocation_target_addend(const MinicDataLayout *layout,
                                                       const MinicC0Program *program,
                                                       const MinicGlobalRelocation *relocation,
                                                       int64_t *addend);

#endif
