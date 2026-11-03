#ifndef MINIC_TARGET_LOONGARCH64_ASM_CONTRACT_INTERNAL_H
#define MINIC_TARGET_LOONGARCH64_ASM_CONTRACT_INTERNAL_H

#include "target/loongarch64/asm_contract.h"

/* ISA template forms are machine-owned, but both the C admission pass and
 * the Core emitter consult the very same decoders. No C AST types enter this
 * internal contract. */
typedef enum LoongLinuxClockAsm {
    LOONG_CLOCK_NONE = 0,
    LOONG_CLOCK_RDTIME_NUMERIC,
    LOONG_CLOCK_RDTIME_NAMED,
    LOONG_CLOCK_VDSO_BASE,
    LOONG_CLOCK_CPU_ID,
    LOONG_CLOCK_COUNTER_ONLY
} LoongLinuxClockAsm;

typedef enum MinicLoongPerCpuIndexedForm {
    LOONG_PERCPU_NONE = 0, LOONG_PERCPU_READ_B, LOONG_PERCPU_READ_H,
    LOONG_PERCPU_READ_W, LOONG_PERCPU_READ_D,
    LOONG_PERCPU_WRITE_B, LOONG_PERCPU_WRITE_H,
    LOONG_PERCPU_WRITE_W, LOONG_PERCPU_WRITE_D
} MinicLoongPerCpuIndexedForm;

typedef struct LoongLinuxSubPositive {
    size_t width;
    bool add_negated;
} LoongLinuxSubPositive;

typedef struct LoongLinuxUaccess {
    bool load;
    unsigned int width;
    const char *opcode;
} LoongLinuxUaccess;

typedef enum LoongLinuxFutexRmw {
    LOONG_FUTEX_NONE = 0,
    LOONG_FUTEX_SET,
    LOONG_FUTEX_ADD,
    LOONG_FUTEX_OR,
    LOONG_FUTEX_AND,
    LOONG_FUTEX_XOR
} LoongLinuxFutexRmw;

struct MinicLoongAtomicTemplate {
    const char *text;
    const char *instruction;
    size_t width;
};

/* Stable ordinal contract shared by source admission and machine dispatch. */
enum MinicLoongDescriptorId {
    LOONG_DESC_CACOP = 1,
    LOONG_DESC_VDSO,
    LOONG_DESC_FRAMETRACE,
    LOONG_DESC_FRAMETRACE_CSR,
    LOONG_DESC_CPUCFG,
    LOONG_DESC_FPR_TRANSFER,
    LOONG_DESC_FUTEX_CMPXCHG,
    LOONG_DESC_FCSR0_WRITE,
    LOONG_DESC_PERCPU_INDEXED,
    LOONG_DESC_CMPXCHG,
    LOONG_DESC_XCHG,
    LOONG_DESC_XCHG_SMALL,
    LOONG_DESC_MASK_NOSPEC,
    LOONG_DESC_PTE
};

unsigned int loong_linux_jump_label_form(const char *, size_t);
LoongLinuxClockAsm loong_linux_clock_form(const char *, size_t);
bool loong_linux_vdso_syscall_form(const char *, size_t);
bool loongarch64_dbar_hint(int64_t);
bool loongarch64_dbar_template(const char *, size_t);
bool loong_linux_cacop_form(const char *, size_t);
const char *loongarch64_literal_dbar(const char *, size_t);
extern const struct MinicLoongAtomicTemplate loong_atomic[8];
const struct MinicLoongAtomicTemplate *loong_atomic_template(const char *, size_t, bool);
unsigned int loong_csr_template(const char *, size_t);
bool loong_linux_cpucfg_form(const char *, size_t);
bool loong_pte_form(const char *, size_t);
unsigned loong_linux_cmpxchg_form(const char *, size_t);
unsigned loong_linux_xchg_form(const char *, size_t);
bool loong_linux_xchg_small_form(const char *, size_t);
bool loong_mask_nospec_form(const char *, size_t);
MinicLoongPerCpuIndexedForm loong_percpu_indexed_form(const char *, size_t);
size_t loong_percpu_amo_form(const char *, size_t);
size_t loong_percpu_amo_width(size_t);
bool loong_fcsr0_write_form(const char *, size_t);
unsigned int loong_linux_fpr_transfer_form(const char *, size_t);
unsigned int loong_linux_invtlb_form(const char *, size_t);
unsigned int loong_linux_add_unless_form(const char *, size_t);
bool loong_linux_sub_positive_form(const char *, size_t, LoongLinuxSubPositive *);
LoongLinuxFutexRmw loong_linux_futex_rmw_form(const char *, size_t);
bool loong_linux_futex_cmpxchg_form(const char *, size_t);
bool loong_linux_uaccess_form(const char *, size_t, LoongLinuxUaccess *);
bool loong_linux_irq_switch_template(const char *, size_t, bool);
bool loong_linux_irq_switch_clobber(const char *, size_t, size_t);
bool loong_linux_frametrace_template(const char *, size_t);
unsigned int loong_linux_frametrace_csr_form(const char *, size_t);

#endif
