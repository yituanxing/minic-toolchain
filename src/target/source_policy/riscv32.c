#include "target/source_policy/source_policy.h"

#include <string.h>

static bool rv32_fixed_register_read(const char *name, size_t length) {
    return length == 2U && (memcmp(name, "sp", 2U) == 0 ||
                            memcmp(name, "tp", 2U) == 0);
}

static bool rv32_local_fixed_register(const char *name, size_t length) {
    /* RV32 ILP32 source local bindings are separately qualified from RV64.
     * Do not import RV64's physical alias support without native proof. */
    return length == 2U && name[0] == 'a' && name[1] >= '0' && name[1] <= '7';
}

static bool rv32_register_clobber(const char *name, size_t length) {
    /* Source admission does not reserve operand registers. */
    return length == 2U &&
           ((name[0] == 'a' && name[1] >= '0' && name[1] <= '7') ||
            (name[0] == 't' && name[1] >= '0' && name[1] <= '6'));
}

static bool rv32_immediate(const char *constraint, size_t length, int64_t value) {
    return length == 1U && constraint[0] == 'I' && value >= -2048 && value <= 2047;
}

const MinicTargetAsmSourcePolicy minic_riscv32_asm_source_policy = {
    .fixed_register_read = rv32_fixed_register_read,
    .local_fixed_register = rv32_local_fixed_register,
    .register_clobber = rv32_register_clobber,
    .immediate_constraint = rv32_immediate,
    .immediate_diagnostic = "GNU asm 'I' input requires a signed 12-bit integer constant",
};
