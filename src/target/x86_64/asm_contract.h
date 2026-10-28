#ifndef MINIC_TARGET_X86_64_ASM_CONTRACT_H
#define MINIC_TARGET_X86_64_ASM_CONTRACT_H
#include "target/backend.h"
bool minic_x86_64_requires_qualified_input_asm(const MinicInlineAsm *);
/* x86 original Linux _THIS_IP_ RIP-relative register-result hardware ABI */
bool minic_x86_64_linux_ip_here_template(const char *, size_t);
unsigned int minic_x86_64_linux_counter_form(const char *,size_t);
unsigned int minic_x86_64_hweight_alternative_form(const char *,size_t);
unsigned int minic_x86_64_linux_msr_form(const char *,size_t);
bool minic_x86_64_checksum_fold_template(const char *,size_t);
bool minic_x86_64_add32_carry_template(const char *,size_t);
bool minic_x86_64_linux_percpu_form(const char *,size_t,unsigned int *,size_t *);

/* Post-Core immediate-specialized GNU Linux BUG/WARN metadata format.
 * Accept directive-only side-table source, never arbitrary opaque machine ASM. */
bool minic_x86_64_linux_bug_table_opaque(const char *, size_t);
/* Register allocation, GNU constraints and native instruction templates
 * belong to x86. The shared compiler sees only Core operand roles. */
bool minic_x86_64_qualify_inline_asm(
    const MinicC0Program *, const MinicInlineAsm *, MinicTargetInlineAsmContract *);
/* Original Linux barrier.h CMP/SBB mask, strictly qualified. */
bool minic_x86_64_asm_nospec_template(const char *, size_t);
/* Qualified x86 per-cpu stable GS segment read, exact GNU p input. */
bool minic_x86_64_asm_percpu_stable_template(const char *, size_t);
bool minic_x86_64_asm_mulacc_template(const char *, size_t);
bool minic_x86_64_asm_muldiv_template(const char *, size_t);
/* The one x86 GNU Linux bitops ISA family: native BT/BTS/BTR,
 * hardware CF output, real 671 SMP lock table and typed Core roles. */
typedef enum MinicX86BitopsForm {
    MINIC_X86_BITOPS_NONE = 0,
    MINIC_X86_BITOPS_TEST,
    MINIC_X86_BITOPS_TEST_SET,
    MINIC_X86_BITOPS_TEST_CLEAR,
    MINIC_X86_BITOPS_TEST_CHANGE,
    MINIC_X86_BITOPS_SET,
    MINIC_X86_BITOPS_CLEAR,
    MINIC_X86_BITOPS_LOCK_SET,
    MINIC_X86_BITOPS_LOCK_CLEAR,
    MINIC_X86_BITOPS_LOCK_TEST_SET,
    MINIC_X86_BITOPS_LOCK_TEST_CLEAR,
    /* Constant-bit original Linux arch_set_bit/arch_clear_bit: 8-bit
     * RMW cannot be widened without corrupting neighboring bytes. */
    MINIC_X86_BITOPS_LOCK_OR_BYTE,
    MINIC_X86_BITOPS_LOCK_AND_BYTE
} MinicX86BitopsForm;
typedef struct MinicX86BitopsDesc {
    const char *opcode;
    unsigned int is_locked;
    unsigned int has_flag;
    unsigned int memory_output;
    unsigned int writes_memory;
} MinicX86BitopsDesc;
MinicX86BitopsForm minic_x86_64_linux_bitops_form(const char *, size_t);
const MinicX86BitopsDesc *minic_x86_64_bitops_description(MinicX86BitopsForm);

/* Linux cmpxchg.h 1/2/4/8-byte true try-CAS, SMP and local.
 * Same ISA descriptor is used by both GNU C admission and emitter. */
typedef enum MinicX86TryCas {
    MINIC_X86_TRYCAS_NONE=0,
    MINIC_X86_TRYCAS_B_LOCK,MINIC_X86_TRYCAS_W_LOCK,
    MINIC_X86_TRYCAS_L_LOCK,MINIC_X86_TRYCAS_Q_LOCK,
    MINIC_X86_TRYCAS_B_LOCAL,MINIC_X86_TRYCAS_W_LOCAL,
    MINIC_X86_TRYCAS_L_LOCAL,MINIC_X86_TRYCAS_Q_LOCAL
} MinicX86TryCas;
typedef struct MinicX86TryCasDesc {
    const char *op,*new_reg,*load_old,*store_old;
    size_t width;
    bool locked;
} MinicX86TryCasDesc;
MinicX86TryCas minic_x86_64_trycas_form(const char *,size_t);
const MinicX86TryCasDesc *minic_x86_64_trycas_desc(MinicX86TryCas);


/* Original Linux privileged x86 port I/O, six exact ISA forms. */
typedef enum MinicX86PortIo {
    MINIC_X86_PORT_IO_NONE=0,
    MINIC_X86_PORT_IO_OUTB,MINIC_X86_PORT_IO_OUTW,MINIC_X86_PORT_IO_OUTL,
    MINIC_X86_PORT_IO_INB,MINIC_X86_PORT_IO_INW,MINIC_X86_PORT_IO_INL
} MinicX86PortIo;
typedef struct MinicX86PortIoDesc {
    const char *opcode;
    size_t width;
    bool is_input;
} MinicX86PortIoDesc;
MinicX86PortIo minic_x86_64_port_io_form(const char *,size_t);
const MinicX86PortIoDesc *minic_x86_64_port_io_description(MinicX86PortIo);

/* Full Linux atomic RMW: hardware add/sub/and/or/xor x 32/64 x SMP/local.
 * Descriptor owns ISA selection, memory effect and stable machine width. */
typedef enum MinicX86AtomicRmw {
    MINIC_X86_ATOMIC_RMW_NONE = 0,
    MINIC_X86_ATOMIC_RMW_ADD32,
    MINIC_X86_ATOMIC_RMW_SUB32,
    MINIC_X86_ATOMIC_RMW_AND32,
    MINIC_X86_ATOMIC_RMW_OR32,
    MINIC_X86_ATOMIC_RMW_XOR32,
    MINIC_X86_ATOMIC_RMW_ADD64,
    MINIC_X86_ATOMIC_RMW_SUB64,
    MINIC_X86_ATOMIC_RMW_AND64,
    MINIC_X86_ATOMIC_RMW_OR64,
    MINIC_X86_ATOMIC_RMW_XOR64,
    MINIC_X86_ATOMIC_RMW_LOCK_ADD32,
    MINIC_X86_ATOMIC_RMW_LOCK_SUB32,
    MINIC_X86_ATOMIC_RMW_LOCK_AND32,
    MINIC_X86_ATOMIC_RMW_LOCK_OR32,
    MINIC_X86_ATOMIC_RMW_LOCK_XOR32,
    MINIC_X86_ATOMIC_RMW_LOCK_ADD64,
    MINIC_X86_ATOMIC_RMW_LOCK_SUB64,
    MINIC_X86_ATOMIC_RMW_LOCK_AND64,
    MINIC_X86_ATOMIC_RMW_LOCK_OR64,
    MINIC_X86_ATOMIC_RMW_LOCK_XOR64,
    MINIC_X86_ATOMIC_RMW_LOCK_INC32,
    MINIC_X86_ATOMIC_RMW_LOCK_DEC32,
    MINIC_X86_ATOMIC_RMW_LOCK_INC64,
    MINIC_X86_ATOMIC_RMW_LOCK_DEC64
} MinicX86AtomicRmw;
typedef struct MinicX86AtomicRmwDesc {
    const char *opcode;
    size_t width;
    bool locked;
    bool unary;
} MinicX86AtomicRmwDesc;
MinicX86AtomicRmw minic_x86_64_atomic_rmw_form(const char *,size_t);
const MinicX86AtomicRmwDesc *minic_x86_64_atomic_rmw_description(
    MinicX86AtomicRmw);

/* Original Linux 6.6.143 x86 LOCK INC/DEC + =@cce output.
 * SOURCE admission and physical emission use the same exact form enum. */
typedef enum MinicX86AtomicCC {
    MINIC_X86_ATOMIC_CC_NONE=0,
    MINIC_X86_ATOMIC_CC_DEC32,
    MINIC_X86_ATOMIC_CC_INC32,
    MINIC_X86_ATOMIC_CC_DEC64,
    MINIC_X86_ATOMIC_CC_INC64,
    MINIC_X86_ATOMIC_CC_SUB32_ZERO,
    MINIC_X86_ATOMIC_CC_ADD32_SIGN,
    MINIC_X86_ATOMIC_CC_SUB64_ZERO,
    MINIC_X86_ATOMIC_CC_ADD64_SIGN
} MinicX86AtomicCC;
typedef struct MinicX86AtomicCCDesc {
    const char *opcode;
    size_t width;
    bool has_value;
    bool flag_sign;
} MinicX86AtomicCCDesc;
MinicX86AtomicCC minic_x86_64_atomic_cc_form(const char *, size_t);
const MinicX86AtomicCCDesc *minic_x86_64_atomic_cc_description(MinicX86AtomicCC);
/* One x86 ISA descriptor distinguishes word and tied-seed Linux BSR/BSF.
 * The GNU output/input contract is target-owned; Common Core sees only
 * scalar/input and address-backed output roles. */
typedef enum MinicX86BitScanForm {
    MINIC_X86_BIT_SCAN_NONE = 0,
    MINIC_X86_BIT_SCAN_BSF64,
    MINIC_X86_BIT_SCAN_BSR64,
    MINIC_X86_BIT_SCAN_BSF32_TIED,
    MINIC_X86_BIT_SCAN_BSR32_TIED,
    MINIC_X86_BIT_SCAN_BSR64_READWRITE
} MinicX86BitScanForm;
MinicX86BitScanForm minic_x86_64_asm_bit_scan_form(const char *, size_t);
/* The Linux __ffs / __fls 64-bit GNU bit-scan family is ISA-owned.
 * No raw arbitrary-template passthrough or global ASM Core opcode. */
bool minic_x86_64_asm_bit_scan_template(
    const char *text, size_t length, bool *is_reverse);
bool minic_x86_64_asm_matched_template(const char *, size_t, size_t);
#endif
