#include "riscv_elf_abi.h"

#include <stddef.h>

enum {
    MINILD_EF_RISCV_RVC = 0x0001U,
    MINILD_EF_RISCV_FLOAT_ABI = 0x0006U,
    MINILD_EF_RISCV_RVE = 0x0008U,
    MINILD_EF_RISCV_TSO = 0x0010U
};

bool minild_riscv_elf_abi_merge(uint32_t current,
                               uint32_t incoming,
                               bool have_current,
                               bool incoming_has_code,
                               uint32_t *merged_out,
                               const char **reason_out) {
    if (merged_out == NULL) {
        if (reason_out != NULL) {
            *reason_out = "invalid-elf-flags-output";
        }
        return false;
    }
    if (reason_out != NULL) {
        *reason_out = NULL;
    }
    if (!have_current) {
        *merged_out = incoming;
        return true;
    }
    if (!incoming_has_code) {
        *merged_out = current;
        return true;
    }

    if ((current & MINILD_EF_RISCV_FLOAT_ABI) !=
        (incoming & MINILD_EF_RISCV_FLOAT_ABI)) {
        if (reason_out != NULL) {
            *reason_out = "float-abi-mismatch";
        }
        return false;
    }
    if ((current & MINILD_EF_RISCV_RVE) !=
        (incoming & MINILD_EF_RISCV_RVE)) {
        if (reason_out != NULL) {
            *reason_out = "embedded-abi-mismatch";
        }
        return false;
    }
    if ((current & ~(uint32_t)(MINILD_EF_RISCV_RVC | MINILD_EF_RISCV_FLOAT_ABI |
                               MINILD_EF_RISCV_RVE | MINILD_EF_RISCV_TSO)) !=
        (incoming & ~(uint32_t)(MINILD_EF_RISCV_RVC | MINILD_EF_RISCV_FLOAT_ABI |
                                MINILD_EF_RISCV_RVE | MINILD_EF_RISCV_TSO))) {
        if (reason_out != NULL) {
            *reason_out = "processor-elf-flags-mismatch";
        }
        return false;
    }

    *merged_out = current | incoming;
    return true;
}
