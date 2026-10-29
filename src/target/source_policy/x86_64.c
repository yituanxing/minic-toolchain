#include "target/source_policy/source_policy.h"

#include <string.h>

static bool x86_match(const char *name, size_t length, const char *expected) {
    return strlen(expected) == length && memcmp(name, expected, length) == 0;
}

/* A global current_stack_pointer alias reads the LIVE RSP only. */
static bool x86_fixed_register_read(const char *name, size_t length) {
    return x86_match(name, length, "rsp");
}

/* Linux's tied +r RSP operand is independently qualified by the machine
 * owner; this only admits its GNU source spelling and scalar type. */
static bool x86_readwrite_hardreg(const char *reg, size_t reg_length,
                                 const char *constraint, size_t length,
                                 MinicType type) {
    return x86_match(reg, reg_length, "rsp") &&
           x86_match(constraint, length, "+r") &&
           minic_type_equal(type, minic_type_unsigned_long());
}

static bool x86_register_clobber(const char *name, size_t length) {
    static const char *const caller_scratch[] = {
        "cc", "dx", "rdx", "edx", "rax", "eax", "rcx", "ecx",
        "rsi", "esi", "rdi", "edi", "r8", "r9", "r10", "r11"
    };
    size_t i;
    for (i = 0U; i < sizeof(caller_scratch) / sizeof(caller_scratch[0]); ++i)
        if (x86_match(name, length, caller_scratch[i])) return true;
    return false;
}

static bool x86_local_fixed_register(const char *name, size_t length) {
    /* GNU accepts a leading AT&T percent marker on local declarations.
     * Admitting a local never reserves the physical register. Callee-saved
     * registers remain excluded until machine save/restore is qualified. */
    if (length > 1U && name[0] == '%') { ++name; --length; }
    return !x86_match(name, length, "cc") && x86_register_clobber(name, length);
}

static bool x86_source_clobber(const char *name, size_t length) {
    /* EBX/RBX/flags are syntax-only GNU Linux source names. They are NOT
     * admitted as machine allocator clobbers or fixed-register bindings. */
    return x86_match(name, length, "ebx") || x86_match(name, length, "rbx") ||
           x86_match(name, length, "flags") || x86_register_clobber(name, length);
}

static bool x86_immediate(const char *constraint, size_t length, int64_t value) {
    /* GCC i386 'I': shift count 0..31, not the signed RISC-V imm12. */
    return length == 1U && constraint[0] == 'I' && value >= 0 && value <= 31;
}

const MinicTargetAsmSourcePolicy minic_x86_64_asm_source_policy = {
    .fixed_register_read = x86_fixed_register_read,
    .local_fixed_register = x86_local_fixed_register,
    .readwrite_hardreg = x86_readwrite_hardreg,
    .register_clobber = x86_register_clobber,
    .source_clobber = x86_source_clobber,
    .immediate_constraint = x86_immediate,
    .immediate_may_defer = true,
    .immediate_diagnostic = "GNU asm 'I' input requires an x86 shift count 0..31",
};
