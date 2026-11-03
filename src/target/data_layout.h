#ifndef MINIC_TARGET_DATA_LAYOUT_H
#define MINIC_TARGET_DATA_LAYOUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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
    size_t float_size;
    size_t float_alignment;
    size_t double_size;
    size_t double_alignment;
    size_t long_double_size;
    size_t long_double_alignment;
    /* Separate IEEE binary128 identity; 0 means unsupported target. */
    size_t float128_size;
    size_t float128_alignment;
    /* Fixed-width vector object storage. Zero for all targets without an
     * explicitly implemented source vector layout; no register ABI implied. */
    size_t fixed_vector128_size;
    size_t fixed_vector128_alignment;
    /* Maximum GNU vector_size object width admitted for storage. This is
     * separate from target builtin NEON declarations and SIMD value ABI. */
    size_t gnu_fixed_vector_max_storage_bytes;
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

#endif
