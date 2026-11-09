#include "target/machine_register.h"
#include "target/target_info.h"

#include <string.h>

static bool aarch_machine_match(const char *name, size_t length, const char *expected) {
    return strlen(expected) == length && memcmp(name, expected, length) == 0;
}

bool minic_aarch64_machine_fixed_register_read(const char *name, size_t length) {
    return aarch_machine_match(name, length, "sp");
}

bool minic_aarch64_machine_local_fixed_register(const char *name, size_t length) {
    unsigned int number;
    /* Current SP and x30 have explicitly admitted native paths. */
    if (aarch_machine_match(name, length, "sp") ||
        aarch_machine_match(name, length, "x30")) return true;
    /* Admit x/r0..17 only; x18 and callee-saved state remain excluded. */
    if (length < 2U || length > 3U || (name[0] != 'x' && name[0] != 'r') ||
        name[1] < '0' || name[1] > '9') return false;
    number = (unsigned int)(name[1] - '0');
    if (length == 3U) {
        if (name[2] < '0' || name[2] > '9') return false;
        number = number * 10U + (unsigned int)(name[2] - '0');
    }
    return number <= 17U;
}

/* Machine identity is qualified and closed. The source policy independently
 * owns the C type signatures; only these exact four forms have memory effects. */
MinicMachineBuiltinEffect minic_aarch64_machine_builtin_effect(
    const struct MinicTargetInfo *target, const char *name, size_t length) {
    static const char prefix[] = "__minic_target_builtin_aarch64_";
    const char *suffix;
    if (target == NULL || target->architecture != MINIC_TARGET_ARCH_AARCH64 ||
        name == NULL || length <= sizeof(prefix) - 1U ||
        memcmp(name, prefix, sizeof(prefix) - 1U) != 0)
        return MINIC_MACHINE_BUILTIN_EFFECT_NONE;
    suffix = name + sizeof(prefix) - 1U;
    length -= sizeof(prefix) - 1U;
    if (aarch_machine_match(suffix, length, "ld1v16qi_us") ||
        aarch_machine_match(suffix, length, "ld1v2di_us"))
        return MINIC_MACHINE_BUILTIN_EFFECT_READ;
    if (aarch_machine_match(suffix, length, "st1v16qi_su") ||
        aarch_machine_match(suffix, length, "st1v2di_su"))
        return MINIC_MACHINE_BUILTIN_EFFECT_WRITE;
    return MINIC_MACHINE_BUILTIN_EFFECT_NONE;
}

bool minic_aarch64_machine_builtin_form(
    const struct MinicTargetInfo *target, const char *name, size_t length,
    size_t *argument_count, const char **mnemonic, size_t *immediate_index) {
    MinicMachineBuiltinEffect effect =
        minic_aarch64_machine_builtin_effect(target, name, length);
    if (effect == MINIC_MACHINE_BUILTIN_EFFECT_NONE || argument_count == NULL ||
        mnemonic == NULL || immediate_index == NULL)
        return false;
    *argument_count = effect == MINIC_MACHINE_BUILTIN_EFFECT_READ ? 1U : 2U;
    *mnemonic = effect == MINIC_MACHINE_BUILTIN_EFFECT_READ ? "ldr" : "str";
    *immediate_index = SIZE_MAX;
    return true;
}
