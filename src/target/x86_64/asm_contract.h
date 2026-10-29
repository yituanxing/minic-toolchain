#ifndef MINIC_TARGET_X86_64_ASM_CONTRACT_H
#define MINIC_TARGET_X86_64_ASM_CONTRACT_H
#include "target/backend.h"
bool minic_x86_64_requires_qualified_input_asm(const MinicInlineAsm *);
/* Linux constant bit-test with GNU flag output (@ccnz/@ccz). */
unsigned int minic_x86_64_linux_testcc_form(const char *, size_t);
unsigned int minic_x86_64_linux_gs_testcc_form(const char *, size_t);
/* x86 original Linux _THIS_IP_ RIP-relative register-result hardware ABI */
bool minic_x86_64_linux_ip_here_template(const char *, size_t);
/* Real 10-byte x86-64 GDTR/IDTR descriptor memory operands. */
unsigned int minic_x86_64_linux_descriptor_table_form(const char *, size_t);
unsigned int minic_x86_64_linux_task_control_form(const char *, size_t);
bool minic_x86_64_linux_iret_to_self_form(const char *, size_t);
bool minic_x86_64_linux_int3_selftest_form(const char *, size_t);
bool minic_x86_64_linux_pv_indirect_call_form(const char *, size_t);
bool minic_x86_64_linux_kvm_hypercall_alt_form(const char *, size_t);
bool minic_x86_64_linux_kvm_hypercall_direct_form(const char *, size_t);
bool minic_x86_64_linux_exception_stack_call_form(const char *, size_t);
bool minic_x86_64_linux_alt_reloc_selftest_form(const char *, size_t);
bool minic_x86_64_linux_real_mode_far_jump_form(const char *, size_t);
bool minic_x86_64_linux_unreachable_opaque(const char *, size_t);
unsigned int minic_x86_64_linux_mmio_read_form(const char *, size_t);
unsigned int minic_x86_64_linux_mmio_write_form(const char *, size_t);
bool minic_x86_64_linux_mmio_type_matches(MinicType, unsigned int);
/* Fixed-SI/DI, fixed-CX repeated string I/O with a DX port operand. */
unsigned int minic_x86_64_linux_rep_string_io_form(const char *, size_t);
/* Fixed RCX/RDI/RSI kernel iomem MOVSB/MOVSW/REP MOVSL with tied inputs. */
unsigned int minic_x86_64_linux_iomem_copy_form(const char *, size_t);
/* Linux i915 WC/SSE4.1 copy: exact aligned/unaligned 1x and 4x blocks. */
unsigned int minic_x86_64_linux_i915_movntdqa_form(const char *, size_t);
/* Non-temporal memcpy and CLWB alternative with exact Linux machine ABI. */
unsigned int minic_x86_64_linux_flushcache_copy_form(const char *, size_t);
/* Atomic register/memory exchange with the GNU +r/+m two-output ABI. */
unsigned int minic_x86_64_linux_atomic_xchg_form(const char *, size_t);
unsigned int minic_x86_64_linux_pvclock_scale_form(const char *, size_t);
bool minic_x86_64_linux_boot_segments_template(const char *, size_t);
unsigned int minic_x86_64_linux_privileged_zero_form(const char *, size_t);
bool minic_x86_64_linux_amx_tilerelease_form(const char *, size_t);
bool minic_x86_64_linux_x87_fwait_fault_form(const char *, size_t);
bool minic_x86_64_linux_weak_wrmsr_fence_form(const char *, size_t);
/* Linux three-way CALL patching: exact alternatives/relocations and RSP ABI. */
bool minic_x86_64_linux_three_call_alternative_form(const char *, size_t);
/* CR0/2/3/4 reads and CR0/2/3 writes, including GNU dummy-memory ordering. */
unsigned int minic_x86_64_linux_control_register_form(const char *, size_t);
/* Physical TLB invalidation and cacheline writeback, exact GNU operands. */
unsigned int minic_x86_64_linux_tlb_cache_form(const char *, size_t);
/* Linux ALTERNATIVE() with an empty baseline and WRMSR replacement. */
bool minic_x86_64_linux_alternative_wrmsr_form(const char *, size_t);
/* xAPIC MMIO MOV/XCHG and AMD divider-clear exact ELF alternatives. */
unsigned int minic_x86_64_linux_apic_amd_alt_form(const char *, size_t);
/* Linux native MONITOR/MWAIT and interrupt-enabling MWAIT byte ISA family. */
unsigned int minic_x86_64_linux_monitor_wait_form(const char *, size_t);
/* Read the live 16-bit DS/ES segment selector, not the segment base. */
unsigned int minic_x86_64_linux_segment_read_form(const char *, size_t);
/* Linux uaccess put-user privileged helper CALL with size-specialized ABI. */
unsigned int minic_x86_64_linux_put_user_call_form(const char *, size_t);
/* Checked and nocheck __get_user_N helpers, RAX input/result and RDX value. */
unsigned int minic_x86_64_linux_get_user_call_form(const char *, size_t);
/* Linux x86 MWAIT idle CLFLUSH alternatives with feature bit 711. */
bool minic_x86_64_linux_idle_clflush_alt_form(const char *, size_t, bool);
bool minic_x86_64_linux_pat_clflushopt_form(const char *, size_t);
/* Exact x86 futex atomic ISA + GNU __ex_table register recovery templates. */
unsigned int minic_x86_64_linux_futex_extable_form(const char *, size_t);
/* MDS buffer-clearing VERW selector read, with architectural CC effects. */
bool minic_x86_64_linux_verw_clear_form(const char *, size_t);
/* Linux debug register DR0/1/2/3/6/7 physical read/write hardware family. */
unsigned int minic_x86_64_linux_debug_register_form(const char *, size_t);
unsigned int minic_x86_64_linux_segment_load_extable_form(const char *, size_t);
bool minic_x86_64_linux_lkgs_extable_form(const char *, size_t);
unsigned int minic_x86_64_linux_fsgsbase_form(const char *, size_t);
unsigned int minic_x86_64_linux_segment_register_extable_form(const char *, size_t);
unsigned int minic_x86_64_linux_kernel_nofault_goto_form(const char *, size_t);
bool minic_x86_64_linux_rep_movsb_alternative_form(const char *, size_t);
bool minic_x86_64_linux_rep_stosb_alternative_form(const char *, size_t);
/* FPU user frame state save/restore with SMAP patching and type-15 fixup. */
unsigned int minic_x86_64_linux_fpu_user_frame_form(const char *, size_t);
/* FXRSTOR/FXRSTORQ protected by Linux type-17 register exception fixup. */
unsigned int minic_x86_64_linux_fxrstor_safe_form(const char *, size_t);
/* Kernel FPU state restore, with type-6 exception recovery for faults. */
unsigned int minic_x86_64_linux_fpu_kernel_restore_form(const char *, size_t);
/* x87 legacy state prepare: FNCLEX; EMMS; FILD DWORD PTR [state]. */
bool minic_x86_64_linux_x87_fild_prepare_form(const char *, size_t);
unsigned int minic_x86_64_linux_mxcsr_form(const char *, size_t);
size_t minic_x86_64_linux_percpu_xadd_width(const char *, size_t);
size_t minic_x86_64_linux_gs_cmpxchg_width(const char *, size_t);
bool minic_x86_64_linux_gs_cmpxchg_loop(const char *, size_t);
size_t minic_x86_64_linux_rep_stos_width(const char *, size_t);
unsigned int minic_x86_64_linux_lowlevel_scalar_form(const char *, size_t);
unsigned int minic_x86_64_linux_boot_flags_form(const char *, size_t);
bool minic_x86_64_linux_delay_mul_form(const char *, size_t);
unsigned int minic_x86_64_linux_delay_wait_form(const char *, size_t);
bool minic_x86_64_linux_identity_load_form(const char *, size_t);
bool minic_x86_64_linux_ipv6_checksum_form(const char *, size_t);
unsigned int minic_x86_64_linux_csum_partial_form(const char *, size_t);
bool minic_x86_64_linux_unwind_context_form(const char *, size_t);
bool minic_x86_64_linux_task_size_alt_form(const char *, size_t);
/* Call the hardirq handler on the per-CPU IRQ stack with exact RSP save. */
unsigned int minic_x86_64_linux_irq_stack_call_form(const char *, size_t);
/* Linux CONFIG_PREEMPT_DYNAMIC static-call trampoline with a fixed RSP live
 * GNU +r output. This is a real CALL, not a register-producing expression. */
unsigned int minic_x86_64_linux_preempt_static_call_form(const char *, size_t);
/* Linux __preempt_count_dec_and_test: GS-relative decrement + ZF output. */
bool minic_x86_64_linux_preempt_gs_dec_form(const char *, size_t);
/* Linux IBT gen_endbr: mov complement / not, named early-clobber u32. */
bool minic_x86_64_linux_gen_endbr_form(const char *, size_t);
/* Circular 64x64->128 multiply from arch/x86/lib/kaslr.c. */
bool minic_x86_64_linux_kaslr_mul_form(const char *, size_t);
bool minic_x86_64_linux_muldiv_u64_form(const char *, size_t);
/* VIA PadLock XSTORE four-output (memory, eax, edx, edi) contract. */
bool minic_x86_64_linux_via_xstore_form(const char *, size_t);
/* ZSTD basic GNU ASM alignment directive, with no operand side effects. */
bool minic_x86_64_linux_zstd_p2align_form(const char *, size_t);
unsigned int minic_x86_64_linux_crash_segment_snapshot_form(const char *, size_t);
unsigned int minic_x86_64_linux_xstate_fault_form(const char *, size_t);
bool minic_x86_64_linux_xrstor_alternative_form(const char *, size_t);
bool minic_x86_64_linux_xsave_triple_alternative_form(const char *, size_t);
unsigned int minic_x86_64_linux_pkru_form(const char *, size_t);
bool minic_x86_64_linux_fdiv_bug_form(const char *, size_t);
unsigned int minic_x86_64_linux_jump_label_form(const char *, size_t);
/* Linux SMAP ALTERNATIVE: empty baseline, STAC/CLAC or flags save/restore. */
unsigned int minic_x86_64_linux_smap_alternative_form(const char *, size_t);
/* Linux harden_usercopy speculative barrier ALTERNATIVE, feature 642. */
bool minic_x86_64_linux_lfence_alternative_form(const char *, size_t);
/* Linux fast string load with type-20 partial-page zero-pad exception. */
bool minic_x86_64_linux_load_unaligned_zeropad_form(const char *, size_t);
/* Linux force_valid_ss LAR selector-rights read with hardware ZF fallback. */
bool minic_x86_64_linux_lar_selector_form(const char *, size_t);
/* Linux hardware RNG with real CF flag-output and 64-bit result storage. */
unsigned int minic_x86_64_linux_random_seed_form(const char *, size_t);
unsigned int minic_x86_64_linux_zero_barrier_form(const char *, size_t);
unsigned int minic_x86_64_linux_counter_form(const char *,size_t);
/* FXSAVE/FXSAVE64 exact 512-byte aligned architectural memory images. */
unsigned int minic_x86_64_linux_fxsave_form(const char *, size_t);
bool minic_x86_64_linux_x87_probe_form(const char *, size_t);
unsigned int minic_x86_64_hweight_alternative_form(const char *,size_t);
unsigned int minic_x86_64_linux_msr_form(const char *,size_t);
/* Exact GNU four-register VMware hypercall, legacy FNSAVE, and kexec
 * architectural segment reload. Unknown templates remain fail-closed. */
unsigned int minic_x86_64_linux_vmware_hypercall_form(const char *, size_t);
bool minic_x86_64_linux_legacy_fnsave_form(const char *, size_t);
bool minic_x86_64_linux_kexec_load_segments_form(const char *, size_t);
bool minic_x86_64_linux_efi_kernel_transfer_form(const char *, size_t);
/* AMD Fam10h PCI MMCONFIG requires AL/AX/EAX specifically, with a separate
 * address-register operand rather than a generic GNU `m` memory operand. */
unsigned int minic_x86_64_linux_pci_mmconfig_form(const char *, size_t, bool);
bool minic_x86_64_ip_fast_csum_template(const char *,size_t);
bool minic_x86_64_checksum_fold_template(const char *,size_t);
bool minic_x86_64_add32_carry_template(const char *,size_t);
bool minic_x86_64_linux_tcpudp_nofold_template(const char *, size_t);
bool minic_x86_64_linux_percpu_form(const char *,size_t,unsigned int *,size_t *);
unsigned int minic_x86_64_gs_tied_alu_form(const char *,size_t,size_t *);
bool minic_x86_64_linux_prefetchw_alt_form(const char *,size_t);

/* Post-Core immediate-specialized GNU Linux BUG/WARN metadata format.
 * Accept directive-only side-table source, never arbitrary opaque machine ASM. */
bool minic_x86_64_linux_bug_table_opaque(const char *, size_t);
/* Register allocation, GNU constraints and native instruction templates
 * belong to x86. The shared compiler sees only Core operand roles. */
bool minic_x86_64_qualify_inline_asm(
    const MinicC0Program *, const MinicInlineAsm *, MinicTargetInlineAsmContract *);
bool minic_x86_64_qualify_inline_asm_goto(
    const MinicC0Program *, const MinicInlineAsm *);
bool minic_x86_64_linux_cpu_has_goto_form(const char *, size_t);
/* Original Linux barrier.h CMP/SBB mask, strictly qualified. */
bool minic_x86_64_asm_nospec_template(const char *, size_t);
/* Qualified x86 per-cpu stable GS segment read, exact GNU p input. */
bool minic_x86_64_asm_percpu_stable_template(const char *, size_t);
size_t minic_x86_64_asm_percpu_stable_width(const char *, size_t);
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
/* Linux original cmpxchg returning OLD memory, not CC_SET trycmpxchg. */
MinicX86TryCas minic_x86_64_raw_cmpxchg_form(const char *, size_t);
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
    MINIC_X86_ATOMIC_RMW_LOCK_DEC64,
    MINIC_X86_ATOMIC_RMW_LOCAL_INC32,
    MINIC_X86_ATOMIC_RMW_LOCAL_DEC32,
    MINIC_X86_ATOMIC_RMW_LOCAL_INC64,
    MINIC_X86_ATOMIC_RMW_LOCAL_DEC64
} MinicX86AtomicRmw;
typedef struct MinicX86AtomicRmwDesc {
    const char *opcode;
    size_t width;
    bool locked;
    bool unary;
} MinicX86AtomicRmwDesc;
MinicX86AtomicRmw minic_x86_64_atomic_rmw_form(const char *,size_t);
/* Original Linux local_add_return: bare XADD, not LOCK-serialized SMP. */
size_t minic_x86_64_linux_smp_xadd_width(const char *,size_t);
size_t minic_x86_64_linux_local_xadd_width(const char *, size_t);
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
bool minic_x86_64_atomic_cc_locked_form(const char *, size_t);
unsigned int minic_x86_64_linux_lock_bitcc_form(const char *, size_t);
unsigned int minic_x86_64_linux_boot_string_form(const char *, size_t);
bool minic_x86_64_linux_vdso_syscall_form(const char *, size_t);
bool minic_x86_64_linux_vdso_cpunode_form(const char *, size_t);
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
/* Linux locked CMPXCHG16B with ZF output, and the per-CPU emulator/GS
 * alternative. Exact original GNU templates and numbered Core forms only. */
unsigned int minic_x86_64_linux_cmpxchg128_form(const char *, size_t);
/* The Linux __ffs / __fls 64-bit GNU bit-scan family is ISA-owned.
 * No raw arbitrary-template passthrough or global ASM Core opcode. */
bool minic_x86_64_asm_bit_scan_template(
    const char *text, size_t length, bool *is_reverse);
bool minic_x86_64_asm_matched_template(const char *, size_t, size_t);
#endif
