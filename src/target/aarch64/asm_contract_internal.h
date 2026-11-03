#ifndef MINIC_TARGET_AARCH64_ASM_CONTRACT_INTERNAL_H
#define MINIC_TARGET_AARCH64_ASM_CONTRACT_INTERNAL_H
#include "target/aarch64/asm_contract.h"
#include <stddef.h>
#include <stdbool.h>

/* Form identifiers are sealed at C admission and consumed by the machine
 * emitter. They therefore live with the shared AArch64 machine contract. */
enum Aarch64MachineAsmForm {
    AARCH64_MACHINE_ASM_LDAR = 1,
    AARCH64_MACHINE_ASM_STLR,
    AARCH64_MACHINE_ASM_RAW_IO_READ,
    AARCH64_MACHINE_ASM_RAW_IO_STORE
};

enum { AARCH64_LINUX_TLBI_ADDR_FORM_COUNT = 13U };

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
bool aarch64_linux_at_s1e1r_template(
    const char *text, size_t length);
bool aarch64_linux_atomic_dec_positive_form(
    const char *text, size_t length);
const Aarch64LinuxBarrierAlternative *aarch64_linux_barrier_alternative(
    const char *text, size_t length);
bool aarch64_linux_brk_template(const char *text, size_t length);
bool aarch64_linux_brk_value(int64_t value);
const char *aarch64_linux_cache_op_form(
    const char *text, size_t length);
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
bool aarch64_linux_efi_unclobbered_cache_form(const char *mnemonic);
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
const char *aarch64_linux_prefetch_template(const char *text, size_t length);
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
unsigned int aarch64_linux_tlbi_form(const char *text,size_t length);
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
size_t aarch64_load_acquire_width(const char *s,size_t len);
bool aarch64_lse_form(const char *text, size_t length,
                            bool numeric, Aarch64LseForm *result);
const Aarch64RawIoRead *aarch64_raw_io_read(
    const char *text, size_t length);
const Aarch64RawIoStore *aarch64_raw_io_store(
    const char *text, size_t length);
size_t aarch64_stlr_width(const char *s, size_t len);
const char *aarch64_linux_mrs_register(const char *text, size_t len);
bool mask_template(const char *text, size_t len);
#endif
