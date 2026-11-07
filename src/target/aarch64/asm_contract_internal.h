#ifndef MINIC_TARGET_AARCH64_ASM_CONTRACT_INTERNAL_H
#define MINIC_TARGET_AARCH64_ASM_CONTRACT_INTERNAL_H
#include "target/aarch64/asm_contract.h"
#include "target/common/lp64_scalar.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

/* Form identifiers are sealed at C admission and consumed by the machine
 * emitter. They therefore live with the shared AArch64 machine contract. */
enum Aarch64MachineAsmForm {
    AARCH64_MACHINE_ASM_LDAR = 1,
    AARCH64_MACHINE_ASM_STLR,
    AARCH64_MACHINE_ASM_RAW_IO_READ,
    AARCH64_MACHINE_ASM_RAW_IO_STORE,
    AARCH64_MACHINE_ASM_KVM_VA_MASK,
    AARCH64_MACHINE_ASM_PSTATE_ALTERNATIVE,
    AARCH64_MACHINE_ASM_HYP_SPIN,
    AARCH64_MACHINE_ASM_BIT_REVERSE,
    AARCH64_MACHINE_ASM_LSE,
    AARCH64_MACHINE_ASM_LSE_CASP128,
    AARCH64_MACHINE_ASM_LSE_DEC_POSITIVE,
    AARCH64_MACHINE_ASM_KVM_AT_FIXUP,
    AARCH64_MACHINE_ASM_VHE_SYSREG_ALTERNATIVE,
    AARCH64_MACHINE_ASM_AT_S1E1R,
    AARCH64_MACHINE_ASM_BARRIER_ALTERNATIVE,
    AARCH64_MACHINE_ASM_PERCPU_WRITE_ALTERNATIVE,
    AARCH64_MACHINE_ASM_QCOM_RETURN_STACK,
    AARCH64_MACHINE_ASM_RNG_SYSREG,
    AARCH64_MACHINE_ASM_COUNTER_ORDERING,
    AARCH64_MACHINE_ASM_TIMER_ALTERNATIVE,
    AARCH64_MACHINE_ASM_XPACLRI,
    AARCH64_MACHINE_ASM_SMCCC,
    AARCH64_MACHINE_ASM_SVC,
    AARCH64_MACHINE_ASM_ATOMIC_DEC_POSITIVE,
    AARCH64_MACHINE_ASM_CMPXCHG128,
    AARCH64_MACHINE_ASM_FUTEX,
    AARCH64_MACHINE_ASM_DISABLED_HW_PAN,
    AARCH64_MACHINE_ASM_WFXT,
    AARCH64_MACHINE_ASM_UACCESS,
    AARCH64_MACHINE_ASM_ZEROPAD,
    AARCH64_MACHINE_ASM_USER_CACHE_FAULT,
    AARCH64_MACHINE_ASM_CACHE_OP,
    AARCH64_MACHINE_ASM_ENCODED_SYSREG_WRITE,
    AARCH64_MACHINE_ASM_TLBI,
    AARCH64_MACHINE_ASM_CMPWAIT,
    AARCH64_MACHINE_ASM_BARRIER,
    AARCH64_MACHINE_ASM_ENCODED_SYSREG_READ,
    AARCH64_MACHINE_ASM_PREFETCH,
    AARCH64_MACHINE_ASM_MSR,
    AARCH64_MACHINE_ASM_BRK,
    AARCH64_MACHINE_ASM_MRS,
    AARCH64_MACHINE_ASM_IO_DEPENDENCY,
    AARCH64_MACHINE_ASM_PERCPU_ALTERNATIVE,
    AARCH64_MACHINE_ASM_PERCPU_RMW,
    AARCH64_MACHINE_ASM_ATOMIC_RETURN,
    AARCH64_MACHINE_ASM_XCHG,
    AARCH64_MACHINE_ASM_CMPXCHG,
    AARCH64_MACHINE_ASM_ATOMIC_RMW,
    AARCH64_MACHINE_ASM_ATOMIC_FETCH,
    AARCH64_MACHINE_ASM_UACCESS_BIC,
    AARCH64_MACHINE_ASM_MASK,
    AARCH64_MACHINE_ASM_KEXEC_CRASH,
    AARCH64_MACHINE_ASM_SHA3_EOR3
};

/* Linux's AArch64 __jump_table key is a minimum 8-byte-aligned static_key
 * address plus optional bit-0 branch flag. Inline specialization can fold
 * &key_array[i] into symbol + a nonzero byte offset; it must not silently
 * discard the offset or admit other low bits into the branch flag field. */
static inline bool aarch64_linux_jump_key_addend(int64_t addend) {
    return addend >= 0 && (addend & INT64_C(7)) <= 1;
}

/* Pure ISA recognition: no C AST, GNU operands, or Core instruction types.
 * `normalized` only changes those GNU patterns that use numeric operand
 * placeholders on the emitted machine side. 0 means not a sealed form. */
bool aarch64_machine_asm_identify(const char *text, size_t length,
                                  bool normalized, uint32_t *id,
                                  uint32_t *variant);

/* Exact GNU arm64 Linux crash_setup_regs() 675-byte GPR/SP/PSTATE/PC snapshot.
 * Whitespace may differ after Core normalization; every opcode, register,
 * local label and GNU operand placeholder must still match. */
bool aarch64_linux_kexec_crash_template(const char *text, size_t length);

/* Pure Core execution-type checks shared by the legacy Linux asm emitter and
 * the typed Machine selector. No source-language enum or C AST can enter. */
static inline const MinicCoreType *aarch64_asm_core_type(
    const MinicCoreFunction *fn, MinicCoreValueId value, bool pointee) {
    MinicCoreTypeId id;
    if (!fn || !fn->type_context || !fn->values || value >= fn->value_count)
        return NULL;
    id = fn->values[value].core_type_id;
    if (pointee && !minic_lp64_core_pointer_pointee(
            fn->type_context, id, &id)) return NULL;
    id = minic_core_type_unqualified(fn->type_context, id);
    return minic_core_type_get(fn->type_context, id);
}

static inline bool aarch64_asm_core_integer(
    const MinicCoreFunction *fn, MinicCoreValueId value, bool pointee,
    size_t width, int sign) {
    const MinicCoreType *type = aarch64_asm_core_type(fn, value, pointee);
    return type && type->has_layout && type->kind == MINIC_CORE_TYPE_INTEGER &&
        !type->value.integer.is_bool && type->size == width &&
        type->value.integer.bit_width == width * 8U &&
        (sign < 0 || type->value.integer.is_signed == (sign != 0));
}

static inline bool aarch64_asm_core_bool(
    const MinicCoreFunction *fn, MinicCoreValueId value, bool pointee) {
    const MinicCoreType *type = aarch64_asm_core_type(fn, value, pointee);
    return type && type->has_layout && type->kind == MINIC_CORE_TYPE_INTEGER &&
        type->size == 1U && type->value.integer.is_bool &&
        type->value.integer.bit_width == 1U;
}

static inline bool aarch64_asm_core_pointer(
    const MinicCoreFunction *fn, MinicCoreValueId value, bool pointee) {
    const MinicCoreType *type = aarch64_asm_core_type(fn, value, pointee);
    return type && type->has_layout && type->kind == MINIC_CORE_TYPE_POINTER &&
        type->size == 8U;
}

static inline bool aarch64_stlr_scalar_core(
    const MinicCoreFunction *fn, MinicCoreValueId value,
    bool pointee, size_t width) {
    return aarch64_asm_core_integer(fn, value, pointee, width, -1);
}

static inline bool aarch64_stlr_memory_carrier_core(
    const MinicCoreFunction *fn, MinicCoreValueId address, size_t width) {
    const MinicCoreType *object = aarch64_asm_core_type(fn, address, true);
    if (!object || !object->has_layout) return false;
    if (aarch64_stlr_scalar_core(fn, address, true, width) ||
        (width == 1U && aarch64_asm_core_bool(fn, address, true)) ||
        (width == 8U && aarch64_asm_core_pointer(fn, address, true)))
        return true;
    return object->kind == MINIC_CORE_TYPE_RECORD &&
        object->has_layout && object->size == width &&
        object->alignment <= width;
}

/* Complete original arm64 barrier.h source ISA fingerprints. These shared
 * pure recognizers reject multiline/extra-instruction text, and revalidate
 * the width that the source C admission sealed into the Core form ID. */
static inline size_t aarch64_load_acquire_width(const char *s, size_t len) {
    static const char *const forms[] = {
        "ldarb%w0,%1", "ldarh%w0,%1", "ldar%w0,%1", "ldar%0,%1"
    };
    char compact[64];
    size_t i, count = 0U;
    if (!s || !len || len >= sizeof(compact)) return 0U;
    for (i = 0U; i < len; ++i) {
        if (s[i] == '\n' || s[i] == '\r' || s[i] == ';') return 0U;
        if (s[i] == ' ' || s[i] == '\t') continue;
        if (count + 1U >= sizeof(compact)) return 0U;
        compact[count++] = s[i];
    }
    compact[count] = '\0';
    for (i = 0U; i < 4U; ++i)
        if (strcmp(compact, forms[i]) == 0) return (size_t)1U << i;
    return 0U;
}

static inline size_t aarch64_stlr_width(const char *s, size_t len) {
    static const char *const forms[] = {
        NULL, "stlrb%w1,%0", "stlrh%w1,%0", NULL,
        "stlr%w1,%0", NULL, NULL, NULL, "stlr%x1,%0"
    };
    char compact[64];
    size_t i, count = 0U;
    if (!s || !len || len >= sizeof(compact)) return 0U;
    for (i = 0U; i < len; ++i) {
        if (s[i] == '\n' || s[i] == '\r' || s[i] == ';') return 0U;
        if (s[i] == ' ' || s[i] == '\t') continue;
        if (count + 1U >= sizeof(compact)) return 0U;
        compact[count++] = s[i];
    }
    compact[count] = '\0';
    for (i = 1U; i <= 8U; ++i)
        if (forms[i] && strcmp(compact, forms[i]) == 0) return i;
    return 0U;
}

enum { AARCH64_LINUX_TLBI_ADDR_FORM_COUNT = 13U };

/* Exact Linux 6.6 arm64 TLBI v8.5 source forms. The source qualifier and
 * Machine SOURCE_ASM backend share one whitelist; GNU assembles the original
 * `.arch` directive and privileged TLBI instruction without reinterpretation. */
static const char *const aarch64_linux_tlbi_names[] = {
    "vale1is", "vale2is", "aside1is", "rvale1is",
    "vae1is", "rvae1is", "vaale1is", "vaae1is",
    "ipas2e1is", "ipas2e1", "vae2is", "ripas2e1is", "ripas2e1",
    "vmalle1", "vmalle1is", "alle1is", "vmalls12e1is", "vmalls12e1"
};

static inline unsigned int aarch64_linux_tlbi_form(
    const char *text, size_t length) {
    char compact[128], expected[128];
    size_t i, count = 0U;
    if (!text || !length || length >= sizeof(compact)) return 0U;
    for (i = 0U; i < length; ++i) {
        const char ch = text[i];
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') continue;
        if (ch == '\0' || count + 1U >= sizeof(compact)) return 0U;
        compact[count++] = ch;
    }
    compact[count] = '\0';
    for (i = 0U; i < sizeof(aarch64_linux_tlbi_names) /
            sizeof(aarch64_linux_tlbi_names[0]); ++i) {
        const int written = snprintf(expected, sizeof(expected),
            ".archarmv8.5-atlbi%s%s", aarch64_linux_tlbi_names[i],
            i < AARCH64_LINUX_TLBI_ADDR_FORM_COUNT ? ",%x0" : "");
        if (written > 0 && (size_t)written < sizeof(expected) &&
            strcmp(compact, expected) == 0) return (unsigned int)i + 1U;
    }
    return 0U;
}

/* Source-owned one-input privileged/cache/hint instructions. These exact
 * target forms are shared by C admission and Machine SOURCE_ASM validation. */
static inline bool aarch64_linux_at_s1e1r_template(
    const char *text, size_t length) {
    static const char exact[] = "ats1e1r,%0";
    char compact[64];
    size_t i, count = 0U;
    if (!text || !length || length >= sizeof(compact)) return false;
    for (i = 0U; i < length; ++i) {
        const char ch = text[i];
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') continue;
        if (ch == '\0' || count + 1U >= sizeof(compact)) return false;
        compact[count++] = ch;
    }
    compact[count] = '\0';
    return strcmp(compact, exact) == 0;
}

static inline const char *aarch64_linux_cache_op_form(
    const char *text, size_t length) {
    static const struct { const char *compact; const char *mnemonic; } forms[] = {
        {"dcivac,%0", "dc ivac"}, {"dccvac,%0", "dc cvac"},
        {"dccvau,%0", "dc cvau"}, {"dccivac,%0", "dc civac"},
        {"icivau,%0", "ic ivau"}
    };
    char compact[80];
    size_t i, count = 0U;
    if (!text || !length || length >= sizeof(compact)) return NULL;
    for (i = 0U; i < length; ++i) {
        const char ch = text[i];
        if (ch == ' ' || ch == '\t') continue;
        if (ch == '\0' || ch == '\n' || ch == '\r' || ch == ';' ||
            count + 1U >= sizeof(compact)) return NULL;
        compact[count++] = ch;
    }
    compact[count] = '\0';
    for (i = 0U; i < sizeof(forms) / sizeof(forms[0]); ++i)
        if (strcmp(compact, forms[i].compact) == 0)
            return forms[i].mnemonic;
    return NULL;
}

static inline bool aarch64_linux_efi_unclobbered_cache_form(
    const char *mnemonic) {
    return mnemonic != NULL &&
        (strcmp(mnemonic, "dc civac") == 0 ||
         strcmp(mnemonic, "dc cvau") == 0);
}

static inline const char *aarch64_linux_prefetch_template(
    const char *text, size_t length) {
    char compact[64];
    size_t i, count = 0U;
    if (!text || !length || length >= sizeof(compact)) return NULL;
    for (i = 0U; i < length; ++i) {
        const char ch = text[i];
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') continue;
        if (ch == ';' || count + 1U >= sizeof(compact)) return NULL;
        compact[count++] = ch;
    }
    compact[count] = '\0';
    if (strcmp(compact, "prfmpldl1keep,%a0") == 0) return "pldl1keep";
    if (strcmp(compact, "prfmpstl1keep,%a0") == 0) return "pstl1keep";
    return NULL;
}

typedef struct {
    const char *old_source, *new_source;
    const char *old_asm, *new_asm, *store_asm;
    size_t width;
} Aarch64RawIoRead;
typedef struct Aarch64RawIoStore {
    const char *source;
    const char *instruction;
    size_t width;
} Aarch64RawIoStore;
typedef struct {
    const char *name;
    const char *alu;
    const char *rmw_constraint;
    const char *fetch32_constraint;
    const char *fetch64_constraint;
} Aarch64AtomicOperation;

typedef struct {
    const Aarch64AtomicOperation *op;
    size_t width;
    unsigned int mode; /* 0=full, 1=relaxed, 2=acquire, 3=release */
} Aarch64AtomicForm;

typedef enum {
    AARCH64_LSE_STORE, AARCH64_LSE_FETCH, AARCH64_LSE_CAS
} Aarch64LseKind;

typedef struct {
    Aarch64LseKind kind;
    const char *mnemonic;
    size_t width;
    unsigned int mode; /* RMW 0=relaxed, 1=acquire, 2=release, 3=acq_rel */
} Aarch64LseForm;

typedef struct Aarch64LinuxPstateAlternative {
    unsigned int cap, opcode, enabled;
} Aarch64LinuxPstateAlternative;
typedef struct Aarch64PerCpuForm {
    const Aarch64AtomicOperation *op;
    size_t width;
    bool returns_new;
} Aarch64PerCpuForm;
typedef struct Aarch64XchgForm {
    size_t width;
    unsigned int mode;
} Aarch64XchgForm;
typedef struct Aarch64LinuxBarrierAlternative {
    unsigned int cap;
    const char *old_compact;
    const char *new_compact;
    const char *old_asm;
    const char *new_asm;
} Aarch64LinuxBarrierAlternative;
typedef struct Aarch64LinuxWriteSysreg {
    unsigned int op0, op1, crn, crm, op2;
} Aarch64LinuxWriteSysreg;

/* One target-owned finite SYS_* catalog shared by source qualification,
 * typed Machine selection and the remaining legacy-only ASM families. */
extern const Aarch64LinuxWriteSysreg aarch64_linux_write_sysregs[];
extern const size_t aarch64_linux_write_sysreg_count;

typedef struct Aarch64LinuxVheSysregAlternative {
    bool is_read;
    unsigned int capability;
    unsigned int old_opcode, new_opcode;
} Aarch64LinuxVheSysregAlternative;
typedef enum Aarch64LinuxHypSpinForm {
    AARCH64_LINUX_HYP_SPIN_NONE,
    AARCH64_LINUX_HYP_SPIN_LOCK,
    AARCH64_LINUX_HYP_SPIN_UNLOCK
} Aarch64LinuxHypSpinForm;
typedef struct Aarch64UaccessForm {
    bool is_load;
    unsigned int width;
    const char *mnemonic;
    unsigned int exception_type;
} Aarch64UaccessForm;


extern const Aarch64AtomicOperation aarch64_atomic_operations[6];

/* Private machine-owned ISA form recognizers shared with C admission.
 * No C type or AST representation belongs in this header. */
bool aarch64_atomic_form(const char *text, size_t len, bool fetch,
                                Aarch64AtomicForm *result);
bool aarch64_atomic_return_form(
    const char *text, size_t length, Aarch64AtomicForm *result);
const char *aarch64_barrier_template(const char *text, size_t len);
bool aarch64_linux_atomic_dec_positive_form(
    const char *text, size_t length);
const Aarch64LinuxBarrierAlternative *aarch64_linux_barrier_alternative(
    const char *text, size_t length);
bool aarch64_linux_brk_template(const char *text, size_t length);
bool aarch64_linux_brk_value(int64_t value);
bool aarch64_linux_cap_goto_text(const char *s, size_t len,
                                        bool source_form, bool callback_form);
unsigned int aarch64_linux_cmpwait_form(const char *s, size_t length);
unsigned int aarch64_linux_cmpxchg128_form(
    const char *text, size_t length);
unsigned int aarch64_linux_cmpxchg_form(
    const char *text, size_t length, bool numeric);
size_t aarch64_linux_cmpxchg_width(unsigned int form);
bool aarch64_linux_counter_ordering_form(const char *text, size_t len);
unsigned int aarch64_linux_disabled_hw_pan_form(
    const char *text, size_t length);
unsigned int aarch64_linux_encoded_sysreg_form(
    const char *text, size_t length);
unsigned int aarch64_linux_encoded_sysreg_write_form(
    const char *text, size_t length);
unsigned int aarch64_linux_futex_form(
    const char *text, size_t length);
Aarch64LinuxHypSpinForm aarch64_linux_hyp_spin_form(
    const char *text, size_t length);
bool aarch64_linux_io_dependency_form(const char *text,size_t length);
unsigned int aarch64_linux_jump_label_form(
    const char *source, size_t length);
bool aarch64_linux_kvm_at_fixup_template(
    const char *text, size_t length);
bool aarch64_linux_kvm_va_mask_form(
    const char *text, size_t length);
unsigned int aarch64_linux_lse_casp128_form(
    const char *text, size_t length);
bool aarch64_linux_lse_dec_positive_form(
    const char *text, size_t length);
const char *aarch64_linux_msr_register(const char *text, size_t len);
bool aarch64_linux_percpu_alternative_template(
    const char *text, size_t length);
bool aarch64_linux_percpu_rmw_form(
    const char *text, size_t length, Aarch64PerCpuForm *result);
bool aarch64_linux_percpu_write_alternative_template(
    const char *text, size_t length);
Aarch64LinuxPstateAlternative aarch64_linux_pstate_alternative(
    const char *text, size_t length);
bool aarch64_linux_qcom_return_stack_form(
    const char *text, size_t length);
unsigned int aarch64_linux_rng_sysreg_form(
    const char *text, size_t length);
unsigned int aarch64_linux_smccc_form(const char *s, size_t n);
bool aarch64_linux_svc_form(const char *s, size_t n);
unsigned int aarch64_linux_timer_alternative_form(
    const char *text, size_t length);
bool aarch64_linux_uaccess_bic_template(
    const char *text, size_t length);
bool aarch64_linux_uaccess_form(const char *text, size_t length,
                                      Aarch64UaccessForm *result);
const char *aarch64_linux_user_cache_fault_form(
    const char *text, size_t length);
bool aarch64_linux_vhe_sysreg_alternative_form(
    const char *text, size_t length,
    Aarch64LinuxVheSysregAlternative *result);
unsigned int aarch64_linux_wfxt_form(const char *text, size_t length);
bool aarch64_linux_xchg_form(
    const char *text, size_t len, Aarch64XchgForm *result);
bool aarch64_linux_xpaclri_form(const char *text, size_t length);
bool aarch64_linux_zeropad_form(const char *s, size_t length);
bool aarch64_lse_form(const char *text, size_t length,
                            bool numeric, Aarch64LseForm *result);
const Aarch64RawIoRead *aarch64_raw_io_read(
    const char *text, size_t length);
const Aarch64RawIoStore *aarch64_raw_io_store(
    const char *text, size_t length);
const char *aarch64_linux_mrs_register(const char *text, size_t len);
bool mask_template(const char *text, size_t len);
#endif
