#include "target/source_policy/source_policy.h"

#include <string.h>

static bool aarch_register_is(const char *name, size_t length, const char *expected) {
    return strlen(expected) == length && memcmp(name, expected, length) == 0;
}

static bool aarch_fixed_register_read(const char *name, size_t length) {
    return aarch_register_is(name, length, "sp");
}

static bool aarch_local_fixed_register(const char *name, size_t length) {
    unsigned int number;
    /* Linux current_stack_pointer and PAC XPACLRI's tied x30 link-register
     * operand have separately qualified native machine paths. */
    if (aarch_register_is(name, length, "sp") ||
        aarch_register_is(name, length, "x30")) return true;
    /* Source-only AAPCS64 argument and scratch bindings x/r0..17. x18 and
     * all callee-saved registers remain fail-closed. */
    if (length < 2U || length > 3U || (name[0] != 'x' && name[0] != 'r') ||
        name[1] < '0' || name[1] > '9') return false;
    number = (unsigned int)(name[1] - '0');
    if (length == 3U) {
        if (name[2] < '0' || name[2] > '9') return false;
        number = number * 10U + (unsigned int)(name[2] - '0');
    }
    return number <= 17U;
}

static bool aarch_register_clobber(const char *name, size_t length) {
    /* Recognizing NZCV 'cc' never authorizes arbitrary machine asm. */
    return aarch_register_is(name, length, "cc");
}

static bool aarch_memory_input(const char *constraint, size_t length) {
    /* GCC AArch64 Q: one base-register addressing mode, no displacement. */
    return length == 1U && constraint[0] == 'Q';
}

static bool aarch_immediate(const char *constraint, size_t length, int64_t value) {
    /* GCC AArch64 'I': unsigned imm12, optionally left shifted by 12. */
    return length == 1U && constraint[0] == 'I' &&
           ((value >= 0 && value <= 4095) ||
            (value >= 4096 && value <= 0xfff000 && (value & 4095) == 0));
}

const MinicTargetAsmSourcePolicy minic_aarch64_asm_source_policy = {
    .fixed_register_read = aarch_fixed_register_read,
    .local_fixed_register = aarch_local_fixed_register,
    .register_clobber = aarch_register_clobber,
    .memory_input_constraint = aarch_memory_input,
    .immediate_constraint = aarch_immediate,
    .immediate_may_defer = true,
    .immediate_diagnostic =
        "GNU asm 'I' input requires an AArch64 unsigned imm12 (optional shift 12)",
};
