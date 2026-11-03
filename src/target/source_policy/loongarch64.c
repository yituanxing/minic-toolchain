#include "target/source_policy/source_policy.h"
#include "target/machine_register.h"
#include "frontend/ast.h"
#include "target/loongarch64/machine_builtin_form.h"

#include <string.h>

static bool loong_register_clobber(const char *name, size_t length) {
    /* $tN/tN, $aN/aN and $s0 recognize GNU syntax only. The Loong machine
     * qualifier independently checks saved s0 and return-address effects. */
    if (length == 3U && name[0] == '$') { ++name; --length; }
    return length == 2U &&
           ((name[0] == 't' && name[1] >= '0' && name[1] <= '8') ||
            (name[0] == 'a' && name[1] >= '0' && name[1] <= '7') ||
            (name[0] == 's' && name[1] == '0'));
}

static bool loong_immediate(const char *constraint, size_t length, int64_t value) {
    return length == 1U && constraint[0] == 'I' && value >= -2048 && value <= 2047;
}

/* GCC LoongArch <larchintrin.h> machine-builtin semantic signatures belong
 * to this target source policy. The native CALL emitter rechecks the same
 * internal name, mnemonic, privilege and immediate effects; this is not an
 * instruction-passthrough permission. */
typedef enum LoongBuiltinScalar {
    LOONG_BUILTIN_VOID = 0,
    LOONG_BUILTIN_CHAR,
    LOONG_BUILTIN_SHORT,
    LOONG_BUILTIN_INT,
    LOONG_BUILTIN_LONG,
    LOONG_BUILTIN_UCHAR,
    LOONG_BUILTIN_USHORT,
    LOONG_BUILTIN_UINT,
    LOONG_BUILTIN_ULONG
} LoongBuiltinScalar;

typedef struct LoongBuiltinDesc {
    const char *suffix;
    const char *mnemonic;
    LoongBuiltinScalar result;
    size_t argc;
    LoongBuiltinScalar arg[3];
    size_t immediate_index;
} LoongBuiltinDesc;

static MinicType loong_builtin_type(const MinicTargetInvocation *target,
                                    LoongBuiltinScalar scalar) {
    switch (scalar) {
    case LOONG_BUILTIN_CHAR: {
        MinicType type;
        /* Invocation-specific -fsigned-char must also affect these
         * synthetic declarations, or the AST verifier rejects the TU. */
        return minic_target_info_plain_char_type(target, &type)
            ? type : minic_type_void();
    }
    case LOONG_BUILTIN_SHORT: return minic_type_short();
    case LOONG_BUILTIN_INT: return minic_type_int();
    case LOONG_BUILTIN_LONG: return minic_type_long();
    case LOONG_BUILTIN_UCHAR: return minic_type_unsigned_char();
    case LOONG_BUILTIN_USHORT: return minic_type_unsigned_short();
    case LOONG_BUILTIN_UINT: return minic_type_unsigned_int();
    case LOONG_BUILTIN_ULONG: return minic_type_unsigned_long();
    default: return minic_type_void();
    }
}

#define V LOONG_BUILTIN_VOID
#define C LOONG_BUILTIN_CHAR
#define H LOONG_BUILTIN_SHORT
#define I LOONG_BUILTIN_INT
#define L LOONG_BUILTIN_LONG
#define UC LOONG_BUILTIN_UCHAR
#define UH LOONG_BUILTIN_USHORT
#define UI LOONG_BUILTIN_UINT
#define UL LOONG_BUILTIN_ULONG
static const LoongBuiltinDesc loong_builtins[] = {
    {"cpucfg", "cpucfg", UI, 1U, {UI,V,V}, SIZE_MAX},
    {"asrtle_d", "asrtle.d", V, 2U, {L,L,V}, SIZE_MAX},
    {"asrtgt_d", "asrtgt.d", V, 2U, {L,L,V}, SIZE_MAX},
    {"crc_w_b_w", "crc.w.b.w", I, 2U, {C,I,V}, SIZE_MAX},
    {"crc_w_h_w", "crc.w.h.w", I, 2U, {H,I,V}, SIZE_MAX},
    {"crc_w_w_w", "crc.w.w.w", I, 2U, {I,I,V}, SIZE_MAX},
    {"crc_w_d_w", "crc.w.d.w", I, 2U, {L,I,V}, SIZE_MAX},
    {"crcc_w_b_w", "crcc.w.b.w", I, 2U, {C,I,V}, SIZE_MAX},
    {"crcc_w_h_w", "crcc.w.h.w", I, 2U, {H,I,V}, SIZE_MAX},
    {"crcc_w_w_w", "crcc.w.w.w", I, 2U, {I,I,V}, SIZE_MAX},
    {"crcc_w_d_w", "crcc.w.d.w", I, 2U, {L,I,V}, SIZE_MAX},
    {"iocsrrd_b", "iocsrrd.b", UC, 1U, {UI,V,V}, SIZE_MAX},
    {"iocsrrd_h", "iocsrrd.h", UH, 1U, {UI,V,V}, SIZE_MAX},
    {"iocsrrd_w", "iocsrrd.w", UI, 1U, {UI,V,V}, SIZE_MAX},
    {"iocsrrd_d", "iocsrrd.d", UL, 1U, {UI,V,V}, SIZE_MAX},
    {"iocsrwr_b", "iocsrwr.b", V, 2U, {UC,UI,V}, SIZE_MAX},
    {"iocsrwr_h", "iocsrwr.h", V, 2U, {UH,UI,V}, SIZE_MAX},
    {"iocsrwr_w", "iocsrwr.w", V, 2U, {UI,UI,V}, SIZE_MAX},
    {"iocsrwr_d", "iocsrwr.d", V, 2U, {UL,UI,V}, SIZE_MAX},
    {"csrrd_w", "csrrd", UI, 1U, {UI,V,V}, 0U},
    {"csrrd_d", "csrrd", UL, 1U, {UI,V,V}, 0U},
    {"csrwr_w", "csrwr", UI, 2U, {UI,UI,V}, 1U},
    {"csrwr_d", "csrwr", UL, 2U, {UL,UI,V}, 1U},
    {"csrxchg_w", "csrxchg", UI, 3U, {UI,UI,UI}, 2U},
    {"csrxchg_d", "csrxchg", UL, 3U, {UL,UL,UI}, 2U},
};
#undef V
#undef C
#undef H
#undef I
#undef L
#undef UC
#undef UH
#undef UI
#undef UL

/* Machine and Source lookup share only this immutable ISA descriptor index.
 * In particular, the native path never reconstructs a GNU C identifier. */
static const LoongBuiltinDesc *loong_builtin_descriptor_suffix(
    const char *suffix, size_t suffix_length) {
    size_t i;
    if (suffix == NULL || suffix_length == 0U) return NULL;
    for (i = 0U; i < sizeof(loong_builtins) / sizeof(loong_builtins[0]); ++i) {
        const LoongBuiltinDesc *descriptor = &loong_builtins[i];
        if (strlen(descriptor->suffix) == suffix_length &&
            memcmp(suffix, descriptor->suffix, suffix_length) == 0)
            return descriptor;
    }
    return NULL;
}

/* Original GNU source spelling and a few admitted header wrappers map to
 * one unspellable internal Core callee. Source alone performs this mapping. */
static const LoongBuiltinDesc *loong_source_builtin_descriptor(
    const MinicTargetInvocation *target, const char *spelling, size_t length,
    char *callee, size_t callee_capacity) {
    static const char gnu_prefix[] = "__builtin_loongarch_";
    static const char internal_prefix[] = "__minic_target_builtin_loongarch_";
    static const char cpucfg_intrinsic[] = "__cpucfg";
    /* GNU <larchintrin.h> defines these four functions as extern always-
     * inline wrappers of the matching machine builtins. Our parser retains
     * GNU extern-inline linkage without an out-of-line body, so resolve
     * these exact reserved spellings through the already validated ISA
     * builtin contract rather than leaving unresolved ELF libcalls. */
    static const char *const iocsr_wrappers[] = {
        "__iocsrrd_d", "__iocsrrd_w", "__iocsrwr_d", "__iocsrwr_w"
    };
    const bool cpucfg_alias =
        spelling != NULL && length == sizeof(cpucfg_intrinsic) - 1U &&
        memcmp(spelling, cpucfg_intrinsic,
               sizeof(cpucfg_intrinsic) - 1U) == 0;
    bool iocsr_alias = false;
    const char *suffix;
    const LoongBuiltinDesc *descriptor;
    size_t i, suffix_length, prefix_length;
    if (spelling != NULL) {
        for (i = 0U; i < sizeof(iocsr_wrappers) / sizeof(iocsr_wrappers[0]); ++i)
            if (strlen(iocsr_wrappers[i]) == length &&
                memcmp(spelling, iocsr_wrappers[i], length) == 0) {
                iocsr_alias = true;
                break;
            }
    }
    if (target == NULL || spelling == NULL ||
        callee == NULL || callee_capacity == 0U ||
        (!cpucfg_alias && !iocsr_alias &&
         (length <= sizeof(gnu_prefix) - 1U ||
          memcmp(spelling, gnu_prefix, sizeof(gnu_prefix) - 1U) != 0)))
        return NULL;
    suffix = cpucfg_alias || iocsr_alias
        ? spelling + 2U : spelling + sizeof(gnu_prefix) - 1U;
    suffix_length = cpucfg_alias || iocsr_alias
        ? length - 2U : length - (sizeof(gnu_prefix) - 1U);
    prefix_length = sizeof(internal_prefix) - 1U;
    if (suffix_length > SIZE_MAX - prefix_length ||
        prefix_length + suffix_length >= callee_capacity) return NULL;
    descriptor = loong_builtin_descriptor_suffix(suffix, suffix_length);
    if (descriptor == NULL) return NULL;
    (void)memcpy(callee, internal_prefix, prefix_length);
    (void)memcpy(callee + prefix_length, descriptor->suffix, suffix_length);
    callee[prefix_length + suffix_length] = '\0';
    return descriptor;
}

/* Core machine identity is the qualified internal name, not the original
 * GNU source spelling. It admits no source aliases or arbitrary CALL names. */
bool minic_loongarch64_machine_builtin_form(
    const MinicTargetInfo *target, const char *internal_callee, size_t length,
    size_t *argument_count,
    const char **mnemonic, size_t *immediate_index) {
    static const char internal_prefix[] =
        "__minic_target_builtin_loongarch_";
    const size_t prefix_length = sizeof(internal_prefix) - 1U;
    const LoongBuiltinDesc *descriptor;
    if (target == NULL || internal_callee == NULL ||
        argument_count == NULL || mnemonic == NULL || immediate_index == NULL ||
        length <= prefix_length ||
        memcmp(internal_callee, internal_prefix, prefix_length) != 0)
        return false;
    descriptor = loong_builtin_descriptor_suffix(
        internal_callee + prefix_length, length - prefix_length);
    if (descriptor == NULL) return false;
    *argument_count = descriptor->argc;
    *mnemonic = descriptor->mnemonic;
    *immediate_index = descriptor->immediate_index;
    return true;
}

static bool loong_machine_builtin_signature(
    const MinicTargetInvocation *target, const char *spelling, size_t length,
    char *callee, size_t callee_capacity, MinicType *result,
    MinicType arguments[3], size_t *argument_count,
    const char **mnemonic, size_t *immediate_index) {
    const LoongBuiltinDesc *descriptor;
    size_t j;
    if (result == NULL || arguments == NULL || argument_count == NULL ||
        mnemonic == NULL || immediate_index == NULL)
        return false;
    descriptor = loong_source_builtin_descriptor(
        target, spelling, length, callee, callee_capacity);
    if (descriptor == NULL) return false;
    *result = loong_builtin_type(target, descriptor->result);
    for (j = 0U; j < descriptor->argc; ++j)
        arguments[j] = loong_builtin_type(target, descriptor->arg[j]);
    *argument_count = descriptor->argc;
    *mnemonic = descriptor->mnemonic;
    *immediate_index = descriptor->immediate_index;
    return true;
}

const MinicTargetAsmSourcePolicy minic_loongarch64_asm_source_policy = {
    .fixed_register_read = minic_loongarch64_machine_fixed_register_read,
    .fixed_register_write = minic_loongarch64_machine_fixed_register_write,
    .local_fixed_register = minic_loongarch64_machine_local_fixed_register,
    .register_clobber = loong_register_clobber,
    .immediate_constraint = loong_immediate,
    .immediate_may_defer = true,
    .immediate_diagnostic = "GNU asm 'I' input requires a signed 12-bit integer constant",
};

const MinicTargetMachineBuiltinPolicy minic_loongarch64_machine_builtin_policy = {
    .signature = loong_machine_builtin_signature,
};

/* Linux 6.6 __SYSCALL_DEFINEx constructs exactly this named symbol family:
 * sys_SUFFIX is a GNU alias of __se_sys_SUFFIX. The latter receives full
 * signed long (or signed long long for __SC_LONG's 64-bit exceptional case)
 * register words, then performs the source-C type conversions internally.
 * This exception is NOT an implicit C cast and must never admit arbitrary
 * 8-byte aliases simply because their pointers/integers fit in a GPR. */
static bool loong_syscall_alias_names_match(const MinicFunction *alias,
                                            const MinicFunction *owner) {
    static const char public_prefix[] = "sys_";
    static const char sanitizer_prefix[] = "__se_sys_";
    const char *name, *sanitizer;
    if (alias == NULL || owner == NULL || alias->name == NULL || owner->name == NULL)
        return false;
    name = alias->name;
    sanitizer = owner->name;
    if (strncmp(name, public_prefix, sizeof(public_prefix) - 1U) != 0 ||
        strncmp(sanitizer, sanitizer_prefix,
                sizeof(sanitizer_prefix) - 1U) != 0 ||
        name[sizeof(public_prefix) - 1U] == '\0' ||
        strcmp(name + sizeof(public_prefix) - 1U,
               sanitizer + sizeof(sanitizer_prefix) - 1U) != 0)
        return false;
    /* GNU's source names and ELF symbol identities must agree. Do not let
     * unrelated C declarations fake a kernel syscall family through an
     * asm-label override that would escape this restricted ABI exception. */
    return (alias->assembler_name == NULL ||
            strcmp(alias->assembler_name, name) == 0) &&
           (owner->assembler_name == NULL ||
            strcmp(owner->assembler_name, sanitizer) == 0);
}

static bool loong_syscall_alias_word_pair(const MinicTargetInvocation *target,
                                          const MinicC0Program *program,
                                          MinicType public_type,
                                          MinicType sanitizer_type) {
    size_t public_size, sanitizer_size;
    if ((!minic_type_is_integer(public_type) &&
         !minic_type_is_pointer(public_type)) ||
        !minic_type_is_integer(sanitizer_type) ||
        sanitizer_type.integer_sign != MINIC_INTEGER_SIGN_SIGNED ||
        (sanitizer_type.integer_rank != MINIC_INTEGER_RANK_LONG &&
         sanitizer_type.integer_rank != MINIC_INTEGER_RANK_LONG_LONG) ||
        !minic_target_info_sizeof_type(target, program, public_type,
                                       &public_size) ||
        !minic_target_info_sizeof_type(target, program, sanitizer_type,
                                       &sanitizer_size))
        return false;
    return public_size != 0U && public_size <= 8U && sanitizer_size == 8U;
}

static bool loong_syscall_alias_abi_compatible(
    const MinicTargetInvocation *target, const MinicC0Program *program,
    const MinicFunction *alias, const MinicFunction *owner) {
    size_t i;
    if (target == NULL || target->machine.architecture != MINIC_TARGET_ARCH_LOONGARCH64 ||
        program == NULL || alias == NULL || owner == NULL ||
        alias->parameter_count == 0U || alias->parameter_count > 6U ||
        alias->parameter_count != owner->parameter_count ||
        alias->is_defined || !owner->is_defined ||
        alias->is_variadic || owner->is_variadic ||
        alias->is_internal || owner->is_internal ||
        !loong_syscall_alias_names_match(alias, owner) ||
        !minic_type_equal(alias->return_type, minic_type_long()) ||
        !minic_type_equal(owner->return_type, minic_type_long()))
        return false;
    for (i = 0U; i < alias->parameter_count; ++i) {
        if (minic_c0_types_compatible(
                program, alias->parameter_types[i], owner->parameter_types[i]))
            continue;
        if (!loong_syscall_alias_word_pair(target, program,
                                           alias->parameter_types[i],
                                           owner->parameter_types[i]))
            return false;
    }
    return true;
}

const MinicTargetAliasAbiPolicy minic_loongarch64_alias_abi_policy = {
    .allow_incompatible_prototypes = loong_syscall_alias_abi_compatible,
};
