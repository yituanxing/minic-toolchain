#ifndef MINIC_TARGET_RISCV_REGISTER_IDENTITY_H
#define MINIC_TARGET_RISCV_REGISTER_IDENTITY_H

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* Physical GPR identity belongs to the RISC-V target, not to Core's source
 * spelling. GNU accepts x7 and t2 (or x8, s0 and fp) as the SAME register.
 * Comparing string spellings in clobber, fixed-binding or frame scratch
 * allocation silently loses liveness dependencies across these aliases. */
static inline bool minic_riscv_gpr_id(const char *name, size_t length,
                                     unsigned *id) {
    static const char *const abi_names[32] = {
        "zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2",
        "s0", "s1", "a0", "a1", "a2", "a3", "a4", "a5",
        "a6", "a7", "s2", "s3", "s4", "s5", "s6", "s7",
        "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6",
    };
    unsigned index;
    if (name == NULL || id == NULL || length == 0U) return false;
    if (length >= 2U && length <= 3U && name[0] == 'x' &&
        name[1] >= '0' && name[1] <= '9') {
        index = (unsigned)(name[1] - '0');
        if (length == 3U) {
            if (name[2] < '0' || name[2] > '9' || index == 0U)
                return false;
            index = index * 10U + (unsigned)(name[2] - '0');
        }
        if (index > 31U) return false;
        *id = index;
        return true;
    }
    if (length == 2U && memcmp(name, "fp", 2U) == 0) {
        *id = 8U;
        return true;
    }
    for (index = 0U; index < 32U; ++index) {
        const char *const spelling = abi_names[index];
        if (strlen(spelling) == length &&
            memcmp(name, spelling, length) == 0) {
            *id = index;
            return true;
        }
    }
    return false;
}

static inline bool minic_riscv_gpr_name_id(const char *name, unsigned *id) {
    return name != NULL && minic_riscv_gpr_id(name, strlen(name), id);
}

static inline bool minic_riscv_gpr_name_equal(const char *left,
                                               const char *right) {
    unsigned left_id, right_id;
    return minic_riscv_gpr_name_id(left, &left_id) &&
           minic_riscv_gpr_name_id(right, &right_id) && left_id == right_id;
}

static inline bool minic_riscv_gpr_caller_saved(unsigned id) {
    return (id >= 5U && id <= 7U) || (id >= 10U && id <= 17U) ||
           (id >= 28U && id <= 31U);
}

static inline bool minic_riscv_gpr_callee_saved(unsigned id) {
    return id == 8U || id == 9U || (id >= 18U && id <= 27U);
}

#endif
