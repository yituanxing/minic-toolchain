#ifndef MINIC_TARGET_X86_64_ASM_CONTRACT_H
#define MINIC_TARGET_X86_64_ASM_CONTRACT_H
#include "target/backend.h"
bool minic_x86_64_requires_qualified_input_asm(const MinicInlineAsm *);

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
    MINIC_X86_BITOPS_LOCK_TEST_CLEAR
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

/* Original Linux 6.6.143 x86 LOCK INC/DEC + =@cce output.
 * SOURCE admission and physical emission use the same exact form enum. */
typedef enum MinicX86AtomicCC {
    MINIC_X86_ATOMIC_CC_NONE=0,
    MINIC_X86_ATOMIC_CC_DEC32,
    MINIC_X86_ATOMIC_CC_INC32,
    MINIC_X86_ATOMIC_CC_DEC64,
    MINIC_X86_ATOMIC_CC_INC64
} MinicX86AtomicCC;
MinicX86AtomicCC minic_x86_64_atomic_cc_form(const char *, size_t);
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
