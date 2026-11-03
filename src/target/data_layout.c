#include "target/data_layout.h"

static const MinicDataLayout minic_rv64_data_layout = {
    .byte_order = MINIC_BYTE_ORDER_LITTLE,
    .pointer_size = 8U,
    .pointer_alignment = 8U,
    .float_size = 4U,
    .float_alignment = 4U,
    .double_size = 8U,
    .double_alignment = 8U,
    .long_double_size = 16U,
    .long_double_alignment = 16U,
    /* RV64 Linux lp64d uses IEEE binary128 for long double and _Float128.
     * These storage facts do not imply support for binary128 operations. */
    .float128_size = 16U,
    .float128_alignment = 16U,
};

/* RV32I ILP32: 32-bit pointers/long, 64-bit long long, soft float.
 * Keep this descriptor distinct from the RV64 one. Backends must consult
 * the target descriptor rather than assuming the host word size. */
static const MinicDataLayout minic_rv32_data_layout = {
    .byte_order = MINIC_BYTE_ORDER_LITTLE,
    .pointer_size = 4U,
    .pointer_alignment = 4U,
    .float_size = 4U,
    .float_alignment = 4U,
    .double_size = 8U,
    .double_alignment = 8U,
    .long_double_size = 16U,
    .long_double_alignment = 16U,
    .float128_size = 16U,
    .float128_alignment = 16U,
};
const MinicDataLayout *minic_riscv32_linux_data_layout(void) {
    return &minic_rv32_data_layout;
}

/* Keep the x86-64 layout separate: size equality does not imply identical
 * scalar representation, record ABI classification, or long-double encoding.
 * The backend remains responsible for those target-specific rules. */
static const MinicDataLayout minic_x86_64_linux_data_layout_value = {
    .byte_order = MINIC_BYTE_ORDER_LITTLE,
    .pointer_size = 8U,
    .pointer_alignment = 8U,
    .float_size = 4U,
    .float_alignment = 4U,
    .double_size = 8U,
    .double_alignment = 8U,
    .long_double_size = 16U,
    .long_double_alignment = 16U,
    .float128_size = 16U,
    .float128_alignment = 16U,
    .gnu_fixed_vector_max_storage_bytes = 16U,
};

/* AAPCS64 LP64, unsigned plain char, 16-byte IEEE quad long double.
 * Pointer and scalar widths alone DO NOT determine AAPCS64 HFA/HVA ABI.
 * This descriptor only covers C object layout, not call classification. */
static const MinicDataLayout minic_aarch64_linux_layout = {
    .byte_order = MINIC_BYTE_ORDER_LITTLE,
    .pointer_size = 8U,
    .pointer_alignment = 8U,
    .float_size = 4U,
    .float_alignment = 4U,
    .double_size = 8U,
    .double_alignment = 8U,
    .long_double_size = 16U,
    .long_double_alignment = 16U,
    .fixed_vector128_size = 16U,
    .fixed_vector128_alignment = 16U,
};

/* LoongArch Linux ELF lp64d: a distinct target despite matching LP64
 * scalar sizes. Its register and stack calling rules are NOT RV64's. */
static const MinicDataLayout minic_loongarch64_linux_layout = {
    .byte_order = MINIC_BYTE_ORDER_LITTLE,
    .pointer_size = 8U,
    .pointer_alignment = 8U,
    .float_size = 4U,
    .float_alignment = 4U,
    .double_size = 8U,
    .double_alignment = 8U,
    .long_double_size = 16U,
    .long_double_alignment = 16U,
};

const MinicDataLayout *minic_aarch64_linux_data_layout(void) {
    return &minic_aarch64_linux_layout;
}

const MinicDataLayout *minic_loongarch64_linux_data_layout(void) {
    return &minic_loongarch64_linux_layout;
}

const MinicDataLayout *minic_default_data_layout(void) {
    return &minic_rv64_data_layout;
}

bool minic_data_layout_is_little_endian(const MinicDataLayout *layout) {
    return layout != NULL && layout->byte_order == MINIC_BYTE_ORDER_LITTLE;
}

const MinicDataLayout *minic_x86_64_linux_data_layout(void) {
    return &minic_x86_64_linux_data_layout_value;
}
