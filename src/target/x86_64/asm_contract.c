#include "target/x86_64/asm_contract.h"
#include "target/x86_64/asm_contract_internal.h"
#include <stdio.h>
#include <string.h>

/* Core's form ID is transport metadata, not a trusted capability. A future
 * pass, serialized IR, or standalone Core client can manufacture it. Check
 * the exact admitted source spelling/variant once at the target boundary;
 * the emitter then consumes the typed semantic form without text dispatch.
 * In particular zero returned by an ISA form matcher never admits a variant. */
bool minic_x86_64_typed_machine_form_matches_template(
    uint32_t form_id, uint32_t variant, const char *text, size_t length) {
    unsigned int form = 0U;
    unsigned int percpu_operation = 0U;
    size_t percpu_width = 0U;
    if (!text) return false;
    switch ((MinicX86MachineAsmForm)form_id) {
    case MINIC_X86_MACHINE_ASM_NOSPEC:
        return (variant == 4U || variant == 8U) &&
            minic_x86_64_asm_nospec_template(text, length);
    case MINIC_X86_MACHINE_ASM_CPUID_MATCHED:
        return (variant == 6U || variant == 8U) && length == 5U &&
            memcmp(text, "cpuid", 5U) == 0;
    case MINIC_X86_MACHINE_ASM_MULACC_MATCHED:
        return variant == 1U &&
            minic_x86_64_asm_mulacc_template(text, length);
    case MINIC_X86_MACHINE_ASM_BITSCAN_MATCHED:
        return variant != 0U && variant ==
            (uint32_t)minic_x86_64_asm_bit_scan_form(text, length);
    case MINIC_X86_MACHINE_ASM_MATCHED_MEMORY:
        return (variant == 2U || variant == 5U || variant == 7U) &&
            minic_x86_64_asm_matched_template(text, length,
                variant == 7U ? 4U : variant == 5U ? 3U : 1U);
    case MINIC_X86_MACHINE_ASM_PREFETCHW_ALT:
        return variant == 1U &&
            minic_x86_64_linux_prefetchw_alt_form(text, length);
    case MINIC_X86_MACHINE_ASM_CMPXCHG128:
        return (variant == 1U || variant == 2U) &&
            variant == minic_x86_64_linux_cmpxchg128_form(text, length);
    case MINIC_X86_MACHINE_ASM_CRASH_REGISTER: {
        const unsigned int gpr =
            minic_x86_64_linux_crash_gpr_snapshot_form(text, length);
        const unsigned int misc =
            minic_x86_64_linux_crash_misc_snapshot_form(text, length);
        return (gpr != 0U || misc != 0U) &&
            variant == (gpr != 0U ? gpr : 16U + misc);
    }
    case MINIC_X86_MACHINE_ASM_FUTEX_EXTABLE:
        return variant >= 1U && variant <= 6U &&
            variant == minic_x86_64_linux_futex_extable_form(text, length);
    case MINIC_X86_MACHINE_ASM_ZSTD_CPUID:
        return variant == 0U && length == 5U &&
            memcmp(text, "cpuid", 5U) == 0;
    case MINIC_X86_MACHINE_ASM_XSETBV:
        return variant == 0U && length == 6U &&
            memcmp(text, "xsetbv", 6U) == 0;
    case MINIC_X86_MACHINE_ASM_MMIO_READ:
        form = minic_x86_64_linux_mmio_read_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_MMIO_WRITE:
        form = minic_x86_64_linux_mmio_write_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_ATOMIC_XCHG:
        form = minic_x86_64_linux_atomic_xchg_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_MONITOR_WAIT:
        form = minic_x86_64_linux_monitor_wait_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_SEGMENT_READ:
        form = minic_x86_64_linux_segment_read_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_FSGSBASE:
        form = minic_x86_64_linux_fsgsbase_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_RANDOM_SEED:
        form = minic_x86_64_linux_random_seed_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_PKRU:
        form = minic_x86_64_linux_pkru_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_MXCSR:
        form = minic_x86_64_linux_mxcsr_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_PORT_IO:
        form = (unsigned int)minic_x86_64_port_io_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_ATOMIC_RMW:
        form = (unsigned int)minic_x86_64_atomic_rmw_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_RAW_CMPXCHG:
        form = (unsigned int)minic_x86_64_raw_cmpxchg_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_TRYCAS:
        form = (unsigned int)minic_x86_64_trycas_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_BITOPS:
        form = (unsigned int)minic_x86_64_linux_bitops_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_ATOMIC_CC:
        form = (unsigned int)minic_x86_64_atomic_cc_form(text, length);
        return form != 0U && variant ==
            (form | (minic_x86_64_atomic_cc_locked_form(text, length)
                ? MINIC_X86_ATOMIC_CC_LOCK_VARIANT : 0U));
    case MINIC_X86_MACHINE_ASM_RAID6_SIMD:
        return minic_x86_64_raid6_simd_template(text, length, variant);
    case MINIC_X86_MACHINE_ASM_KVM_MOVDQA:
        form = minic_x86_64_kvm_movdqa_form(text, length);
        return form != 0U && variant == form;
    case MINIC_X86_MACHINE_ASM_IP_HERE:
        return variant == 1U &&
            minic_x86_64_linux_ip_here_template(text, length);
    case MINIC_X86_MACHINE_ASM_BUG_TABLE_C_IMMEDIATE:
        form = minic_x86_64_linux_bug_table_c_form(text, length);
        /* Core renumbers the named source operands before MachineIR, so
         * variant 2's normalized template also has the numeric spelling. */
        return form != 0U && (variant == form ||
            (variant == 2U && form == 3U));
    case MINIC_X86_MACHINE_ASM_NOP_CONST_I:
        return variant == 1U && length == 3U &&
            memcmp(text,"nop",3U) == 0;
    case MINIC_X86_MACHINE_ASM_ZERO_BARRIER:
        form = minic_x86_64_linux_zero_barrier_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_ZSTD_PADDING:
        form = minic_x86_64_linux_zstd_padding_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_PRIVILEGED_ZERO:
        form = minic_x86_64_linux_privileged_zero_form(text,length);
        return form >= 4U && form <= 10U && variant == form;
    case MINIC_X86_MACHINE_ASM_AMX_TILERELEASE:
        return variant == 1U &&
            minic_x86_64_linux_amx_tilerelease_form(text,length);
    case MINIC_X86_MACHINE_ASM_X87_FWAIT_FAULT:
        return variant == 1U &&
            minic_x86_64_linux_x87_fwait_fault_form(text,length);
    case MINIC_X86_MACHINE_ASM_FXSAVE:
        form = minic_x86_64_linux_fxsave_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_LAR_SELECTOR:
        return variant == 1U &&
            minic_x86_64_linux_lar_selector_form(text, length);
    case MINIC_X86_MACHINE_ASM_IDENTITY_LOAD:
        return variant == 1U &&
            minic_x86_64_linux_identity_load_form(text, length);
    case MINIC_X86_MACHINE_ASM_VERW_CLEAR:
        return variant == 1U &&
            minic_x86_64_linux_verw_clear_form(text, length);
    case MINIC_X86_MACHINE_ASM_TLB_CACHE:
        form = minic_x86_64_linux_tlb_cache_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_DELAY_MUL:
        return variant == 1U &&
            minic_x86_64_linux_delay_mul_form(text, length);
    case MINIC_X86_MACHINE_ASM_DELAY_WAIT:
        form = minic_x86_64_linux_delay_wait_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_LOWLEVEL_SCALAR:
        form = minic_x86_64_linux_lowlevel_scalar_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_BOOT_FLAGS:
        form = minic_x86_64_linux_boot_flags_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_PM_TRACE:
        return variant == 1U &&
            minic_x86_64_linux_pm_trace_form(text, length);
    case MINIC_X86_MACHINE_ASM_I915_MOVNTDQA:
        form = minic_x86_64_linux_i915_movntdqa_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_PVCLOCK_SCALE:
        return variant == 1U &&
            minic_x86_64_linux_pvclock_scale_form(text, length);
    case MINIC_X86_MACHINE_ASM_PREEMPT_STATIC_CALL:
        form = minic_x86_64_linux_preempt_static_call_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_IPV6_CHECKSUM:
        return variant == 1U &&
            minic_x86_64_linux_ipv6_checksum_form(text, length);
    case MINIC_X86_MACHINE_ASM_BOOT_STRING:
        form = minic_x86_64_linux_boot_string_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_VDSO_SYSCALL:
        return variant == 1U &&
            minic_x86_64_linux_vdso_syscall_form(text, length);
    case MINIC_X86_MACHINE_ASM_VDSO_CPUNODE:
        return variant == 1U &&
            minic_x86_64_linux_vdso_cpunode_form(text, length);
    case MINIC_X86_MACHINE_ASM_UNWIND_CONTEXT:
        return variant == 0U &&
            minic_x86_64_linux_unwind_context_form(text, length);
    case MINIC_X86_MACHINE_ASM_FDIV_BUG:
        return variant == 0U &&
            minic_x86_64_linux_fdiv_bug_form(text, length);
    case MINIC_X86_MACHINE_ASM_X87_FILD_PREPARE:
        return variant == 0U &&
            minic_x86_64_linux_x87_fild_prepare_form(text, length);
    case MINIC_X86_MACHINE_ASM_X87_PROBE:
        return variant == 0U &&
            minic_x86_64_linux_x87_probe_form(text, length);
    case MINIC_X86_MACHINE_ASM_VMWARE_HYPERCALL:
        form = (unsigned int)minic_x86_64_linux_vmware_hypercall_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_VMWARE_STEALCLOCK_ALT:
        form = (unsigned int)minic_x86_64_linux_vmware_stealclock_alt_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_KVM_HYPERCALL_ALT:
        form = (unsigned int)((minic_x86_64_linux_kvm_hypercall_alt_form(text, length) || minic_x86_64_linux_kvm_hypercall_direct_form(text, length)) ? 1U : 0U);
        break;
    case MINIC_X86_MACHINE_ASM_EXCEPTION_STACK_CALL:
        form = (unsigned int)minic_x86_64_linux_exception_stack_call_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_LEGACY_FNSAVE:
        form = (unsigned int)minic_x86_64_linux_legacy_fnsave_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_KEXEC_LOAD_SEGMENTS:
        form = (unsigned int)minic_x86_64_linux_kexec_load_segments_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_EFI_KERNEL_TRANSFER:
        form = (unsigned int)minic_x86_64_linux_efi_kernel_transfer_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_PCI_MMCONFIG:
        form = (unsigned int)(minic_x86_64_linux_pci_mmconfig_form(text, length, false) ? minic_x86_64_linux_pci_mmconfig_form(text, length, false) : minic_x86_64_linux_pci_mmconfig_form(text, length, true));
        break;
    case MINIC_X86_MACHINE_ASM_TASK_SIZE_ALT:
        form = (unsigned int)minic_x86_64_linux_task_size_alt_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_IRQ_STACK_CALL:
        form = (unsigned int)minic_x86_64_linux_irq_stack_call_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_PREEMPT_GS_DEC:
        form = (unsigned int)minic_x86_64_linux_preempt_gs_dec_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_GEN_ENDBR:
        form = (unsigned int)minic_x86_64_linux_gen_endbr_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_KASLR_MUL:
        form = (unsigned int)minic_x86_64_linux_kaslr_mul_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_MULDIV_U64:
        form = (unsigned int)minic_x86_64_linux_muldiv_u64_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_VIA_XSTORE:
        form = (unsigned int)minic_x86_64_linux_via_xstore_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_CRASH_SEGMENT_SNAPSHOT:
        form = (unsigned int)minic_x86_64_linux_crash_segment_snapshot_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_TASK_CONTROL:
        form = (unsigned int)minic_x86_64_linux_task_control_form(text, length);
        return form >= 1U && form <= 2U && variant == form;
    case MINIC_X86_MACHINE_ASM_IRET_TO_SELF:
        form = (unsigned int)minic_x86_64_linux_iret_to_self_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_INT3_SELFTEST:
        form = (unsigned int)minic_x86_64_linux_int3_selftest_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_PV_INDIRECT_CALL:
        form = (unsigned int)minic_x86_64_linux_pv_indirect_call_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_ALT_RELOC_SELFTEST:
        form = (unsigned int)minic_x86_64_linux_alt_reloc_selftest_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_REAL_MODE_FAR_JUMP:
        form = (unsigned int)minic_x86_64_linux_real_mode_far_jump_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_REP_STRING_IO:
        form = (unsigned int)minic_x86_64_linux_rep_string_io_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_IOMEM_COPY:
        form = (unsigned int)minic_x86_64_linux_iomem_copy_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_FLUSHCACHE_COPY:
        form = (unsigned int)minic_x86_64_linux_flushcache_copy_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_SEGMENT_LOAD_EXTABLE:
        form = (unsigned int)minic_x86_64_linux_segment_load_extable_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_LKGS_EXTABLE:
        form = (unsigned int)minic_x86_64_linux_lkgs_extable_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_LOAD_UNALIGNED_ZEROPAD:
        form = (unsigned int)minic_x86_64_linux_load_unaligned_zeropad_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_KERNEL_NOFAULT_GOTO:
        form = (unsigned int)minic_x86_64_linux_kernel_nofault_goto_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_REP_MOVSB_ALTERNATIVE:
        form = (unsigned int)minic_x86_64_linux_rep_movsb_alternative_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_REP_STOSB_ALTERNATIVE:
        form = (unsigned int)minic_x86_64_linux_rep_stosb_alternative_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_FPU_USER_FRAME:
        form = (unsigned int)minic_x86_64_linux_fpu_user_frame_form(text, length);
        return form != 0U &&
            (variant == form || variant == (form | 0x100U));
    case MINIC_X86_MACHINE_ASM_FPU_KERNEL_RESTORE:
        form = (unsigned int)minic_x86_64_linux_fpu_kernel_restore_form(text, length);
        return form >= 1U && form <= 3U && variant == form;
    case MINIC_X86_MACHINE_ASM_FXRSTOR_SAFE:
        form = (unsigned int)minic_x86_64_linux_fxrstor_safe_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_XSTATE_FAULT:
        form = (unsigned int)minic_x86_64_linux_xstate_fault_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_XRSTOR_ALTERNATIVE:
        form = (unsigned int)minic_x86_64_linux_xrstor_alternative_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_XSAVE_TRIPLE_ALTERNATIVE:
        form = (unsigned int)minic_x86_64_linux_xsave_triple_alternative_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_SMAP_ALTERNATIVE:
        form = (unsigned int)minic_x86_64_linux_smap_alternative_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_WEAK_WRMSR_FENCE:
        form = (unsigned int)minic_x86_64_linux_weak_wrmsr_fence_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_UNREACHABLE_ANNOTATION:
        form = minic_x86_64_linux_unreachable_label(text,length) != 0U;
        break;
    case MINIC_X86_MACHINE_ASM_LFENCE_ALTERNATIVE:
        form = minic_x86_64_linux_lfence_alternative_form(text,length);
        break;
    case MINIC_X86_MACHINE_ASM_SEGMENT_REGISTER_EXTABLE:
        form = (unsigned int)minic_x86_64_linux_segment_register_extable_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_PUT_USER_CALL:
        form = (unsigned int)minic_x86_64_linux_put_user_call_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_GET_USER_CALL:
        form = (unsigned int)minic_x86_64_linux_get_user_call_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_IDLE_CLFLUSH_ALT:
        form = (unsigned int)minic_x86_64_linux_idle_clflush_alt_form(text, length, true);
        break;
    case MINIC_X86_MACHINE_ASM_PAT_CLFLUSHOPT:
        form = (unsigned int)minic_x86_64_linux_pat_clflushopt_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_DEBUG_REGISTER:
        form = (unsigned int)minic_x86_64_linux_debug_register_form(text, length);
        return form >= 1U && form <= 12U && variant == form;
    case MINIC_X86_MACHINE_ASM_ALTERNATIVE_WRMSR:
        form = (unsigned int)minic_x86_64_linux_alternative_wrmsr_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_CONTROL_REGISTER:
        form = (unsigned int)minic_x86_64_linux_control_register_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_THREE_CALL_ALTERNATIVE:
        form = (unsigned int)minic_x86_64_linux_three_call_alternative_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_BOOT_SEGMENTS:
        form = (unsigned int)minic_x86_64_linux_boot_segments_template(text, length);
        return form == 1U && variant == 1U;
    case MINIC_X86_MACHINE_ASM_LIDT:
        form = (unsigned int)minic_x86_64_linux_descriptor_table_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_GS_TESTCC:
        form = (unsigned int)minic_x86_64_linux_gs_testcc_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_TESTCC:
        form = (unsigned int)minic_x86_64_linux_testcc_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_APIC_AMD_ALT:
        form = (unsigned int)minic_x86_64_linux_apic_amd_alt_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_GS_TIED_ALU:
        form = (unsigned int)minic_x86_64_gs_tied_alu_form(text, length, &percpu_width);
        break;
    case MINIC_X86_MACHINE_ASM_ADD32_CARRY:
        form = (unsigned int)minic_x86_64_add32_carry_template(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_TCPUDP_NOFOLD:
        form = (unsigned int)minic_x86_64_linux_tcpudp_nofold_template(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_PERCPU_GS:
        form = (unsigned int)minic_x86_64_linux_percpu_form(text, length, &percpu_operation, &percpu_width);
        break;
    case MINIC_X86_MACHINE_ASM_IP_FAST_CSUM:
        form = (unsigned int)minic_x86_64_ip_fast_csum_template(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_CHECKSUM:
        form = (unsigned int)minic_x86_64_checksum_fold_template(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_MSR:
        form = (unsigned int)minic_x86_64_linux_msr_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_HWEIGHT:
        form = (unsigned int)minic_x86_64_hweight_alternative_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_COUNTER:
        form = (unsigned int)minic_x86_64_linux_counter_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_GS_ATOMIC:
        form = (unsigned int)(minic_x86_64_gs_atomic_plan(text, length).kind != MINIC_X86_GS_ATOMIC_NONE);
        break;
    case MINIC_X86_MACHINE_ASM_REP_STOS:
        form = (unsigned int)minic_x86_64_linux_rep_stos_width(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_LOCK_BITCC:
        form = (unsigned int)minic_x86_64_linux_lock_bitcc_form(text, length);
        break;
    case MINIC_X86_MACHINE_ASM_CSUM_PARTIAL:
        form = (unsigned int)minic_x86_64_linux_csum_partial_form(text, length);
        break;
    default:
        return false;
    }
    /* The uaccess forms preserve their original source vs normalized Core
     * distinctions: GNU's named %[size] becomes typed %c4 at Core ingress.
     * Both sides must prove the exact normalized private helper form. */
    if (form_id == MINIC_X86_MACHINE_ASM_PUT_USER_CALL ||
        form_id == MINIC_X86_MACHINE_ASM_GET_USER_CALL)
        return form != 0U && variant == form;
    if (form_id == MINIC_X86_MACHINE_ASM_GS_TIED_ALU)
        return form != 0U && variant == form;
    /* The source-qualified descriptor's memory direction must reach MIR. */
    if (form_id == MINIC_X86_MACHINE_ASM_LIDT)
        return form != 0U && variant == form;
    /* Segment-selector exception tables are physically distinct FS/GS/DS/ES/SS
     * writes; keep the admitted selector identity sealed into MachineIR. */
    if (form_id == MINIC_X86_MACHINE_ASM_SEGMENT_LOAD_EXTABLE)
        return form >= 1U && form <= 5U && variant == form;
    if (form_id == MINIC_X86_MACHINE_ASM_SEGMENT_REGISTER_EXTABLE)
        return form >= 1U && form <= 3U && variant == form;
    if (form_id == MINIC_X86_MACHINE_ASM_LKGS_EXTABLE)
        return form != 0U && variant == 1U;
    /* Each crash snapshot reads a different architectural segment register.
     * Preserve that six-way physical identity after Source qualification. */
    if (form_id == MINIC_X86_MACHINE_ASM_CRASH_SEGMENT_SNAPSHOT)
        return form >= 1U && form <= 6U && variant == form;
    /* All eight privileged CRn forms have distinct hardware direction and
     * register identities; carry that precise selection into MachineIR. */
    if (form_id == MINIC_X86_MACHINE_ASM_CONTROL_REGISTER)
        return form != 0U && variant == form;
    /* The APIC alternative has distinct source operand and hardware shapes:
     * MMIO MOV/XCHG or accumulator DIV. Retain that distinction in MIR. */
    if (form_id == MINIC_X86_MACHINE_ASM_APIC_AMD_ALT)
        return form != 0U && variant == form;
    if (form_id == MINIC_X86_MACHINE_ASM_IRQ_STACK_CALL)
        return form >= 1U && form <= 3U && variant == form;
    if (form_id == MINIC_X86_MACHINE_ASM_SMAP_ALTERNATIVE)
        return form >= 1U && form <= 4U && variant == form;
    /* PCI MMCONFIG's exact byte/word/dword accumulator width is a hardware
     * ABI restriction, unlike the width-agnostic source-only form tags. */
    if (form_id == MINIC_X86_MACHINE_ASM_PCI_MMCONFIG)
        return (form == 1U || form == 2U || form == 4U) &&
            variant == form;
    /* The four XSAVE-family ModRM encodings have distinct instructions;
     * preserve their exact authenticated opcode variant into physical MIR. */
    if (form_id == MINIC_X86_MACHINE_ASM_XSTATE_FAULT)
        return form >= 1U && form <= 4U && variant == form;
    /* Most other tagged source families publish variant zero; the explicit
     * privileged/debug families above carry their exact physical variant. */
    if (form_id >= MINIC_X86_MACHINE_ASM_CRASH_REGISTER &&
        form_id <= MINIC_X86_MACHINE_ASM_CSUM_PARTIAL)
        return form != 0U && variant == 0U;
    return form != 0U && variant == form;
}

/* Linux BUG metadata has two finite original GNU grammars.  The %c forms
 * are *typed* source operands, not arbitrary assembler substitution.  All
 * labels, section order, and optional unreachable metadata are part of the
 * matched identity; a machine operand can never authorize extra source ASM. */
/* GCC's original Linux WARN_ON grammar includes whitespace and assembler
 * comments after the four directives. Match the COMPLETE normalized grammar
 * (including sections and local labels), never a source prefix. Comments are
 * not instructions, but GNU `%c` operands remain typed and verified later. */
static bool x86_bug_table_compact(const char *text, size_t length,
    char *compact, size_t capacity) {
    bool comment = false;
    size_t i, n = 0U;
    if (text == NULL || compact == NULL || capacity == 0U ||
        length > 2048U) return false;
    for (i = 0U; i < length; ++i) {
        const char ch = text[i];
        if (ch == '\0') return false;
        if (ch == '\n') {
            comment = false;
            continue;
        }
        if (comment) continue;
        if (ch == '#') {
            comment = true;
            continue;
        }
        if (ch == ' ' || ch == '\t' || ch == '\r') continue;
        if (n + 1U >= capacity) return false;
        compact[n++] = ch;
    }
    compact[n] = '\0';
    return true;
}

unsigned int minic_x86_64_linux_bug_table_c_form(
    const char *text, size_t length) {
    static const char numeric[] =
        "1:\t.byte 0x0f, 0x0b\n"
        ".pushsection __bug_table,\"aw\"\n"
        "2:\t.long 1b - .\n"
        ".long %c0 - .\n"
        ".word %c1\n"
        ".word %c2\n"
        ".org 2b+%c3\n"
        ".popsection\n"
        "998:\n"
        ".pushsection .discard.reachable\n"
        ".long 998b\n"
        ".popsection\n";
    static const char named[] =
        "1:\t.byte 0x0f, 0x0b\n"
        ".pushsection __bug_table,\"aw\"\n"
        "2:\t.long 1b - .\n"
        ".long %c[path] - .\n"
        ".word %c[ln]\n"
        ".word %c[fl]\n"
        ".org 2b+%c[sz]\n"
        ".popsection\n";
    /* Core normalizes validated %[name] references to numeric operand
     * indices; the qualifier already sealed the original named grammar. */
    static const char named_core[] =
        "1:\t.byte 0x0f, 0x0b\n"
        ".pushsection __bug_table,\"aw\"\n"
        "2:\t.long 1b - .\n"
        ".long %c0 - .\n"
        ".word %c1\n"
        ".word %c2\n"
        ".org 2b+%c3\n"
        ".popsection\n";
    char actual[1024], expected[1024];
    if (text == NULL || strlen(text) != length) return 0U;
    if (length == sizeof(numeric)-1U &&
        memcmp(text,numeric,sizeof(numeric)-1U) == 0) return 1U;
    if (length == sizeof(named)-1U &&
        memcmp(text,named,sizeof(named)-1U) == 0) return 2U;
    if (length == sizeof(named_core)-1U &&
        memcmp(text,named_core,sizeof(named_core)-1U) == 0) return 3U;
    if (!x86_bug_table_compact(text,length,actual,sizeof(actual))) return 0U;
    if (x86_bug_table_compact(numeric,sizeof(numeric)-1U,
            expected,sizeof(expected)) && strcmp(actual,expected) == 0)
        return 1U;
    if (x86_bug_table_compact(named,sizeof(named)-1U,
            expected,sizeof(expected)) && strcmp(actual,expected) == 0)
        return 2U;
    if (x86_bug_table_compact(named_core,sizeof(named_core)-1U,
            expected,sizeof(expected)) && strcmp(actual,expected) == 0)
        return 3U;
    return 0U;
}

/* Machine-side verification recognizes exact x86 GNU instruction forms;
 * C operand qualification lives exclusively in asm_contract_source.c. */
unsigned int minic_x86_64_linux_testcc_form(const char *text, size_t length) {
    static const char *const forms[] = {
        "testb%2,%1/*outputconditioncodenz*/",
        "testb%2,%1/*outputconditioncodez*/"
    };
    char compact[120];
    size_t i, n = 0U;
    if (!text || !length || length >= sizeof(compact)) return 0U;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return 0U;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(forms) / sizeof(forms[0]); ++i)
        if (strcmp(compact, forms[i]) == 0) return (unsigned int)i + 1U;
    return 0U;
}
/* The per-CPU bit test reads GS-relative storage. It cannot be lowered
 * through an ordinary C pointer dereference: GS is an architectural base,
 * and the GNU flag output observes CF after BT. Preserve both effects. */
unsigned int minic_x86_64_linux_gs_testcc_form(
    const char *text, size_t length) {
    static const char *const forms[] = {
        "btl%%gs:%2,%1/*outputconditioncodec*/",
        "btq%%gs:%2,%1/*outputconditioncodec*/"
    };
    char compact[120];
    size_t i, n = 0U;
    if (!text || !length || length >= sizeof(compact)) return 0U;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return 0U;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(forms) / sizeof(forms[0]); ++i)
        if (strcmp(compact, forms[i]) == 0) return (unsigned int)i + 1U;
    return 0U;
}


bool minic_x86_64_compact_asm_equals(
    const char *text, size_t length, const char *expected);


/* LGDT/LIDT do not read an integer value: their operand is the actual
 * ten-byte packed descriptor (u16 limit, u64 base). Linux boot and IDT
 * setup use this privileged memory form, not a pointer-valued register.
 * A different struct or width would make the CPU read beyond the operand. */
unsigned int minic_x86_64_linux_descriptor_table_form(
    const char *text, size_t length) {
    static const char *const exact[] = {
        "lidt %0", "lgdt %0", "sgdt %0", "sidt %0"
    };
    size_t i;
    if (!text) return 0U;
    for (i = 0U; i < sizeof(exact) / sizeof(exact[0]); ++i)
        if (length == strlen(exact[i]) &&
            memcmp(text, exact[i], length) == 0)
            return (unsigned int)(i + 1U);
    return 0U;
}

/* Strictly recognize an entire privileged template, not a prefix or an
 * arbitrary assembler string. GNU's whitespace does not change operands. */
bool minic_x86_64_compact_asm_equals(const char *text, size_t length,
                                        const char *expected) {
    size_t i, j = 0U;
    if (!text || !expected) return false;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == '\0') return false;
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (expected[j] != c) return false;
        ++j;
    }
    return expected[j] == '\0';
}

/* Exact arch/x86/include/asm/kexec.h crash_setup_regs GPR snapshots. GNU
 * `=m` stores architectural GPR contents into the addressable pt_regs field;
 * the Core must not turn the memory operand into an arbitrary register. */
unsigned int minic_x86_64_linux_crash_gpr_snapshot_form(
    const char *text, size_t length) {
    static const char *const registers[] = {
        "rbx", "rcx", "rdx", "rsi", "rdi", "rbp", "rax", "rsp",
        "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"
    };
    size_t i;
    for (i = 0U; i < sizeof(registers)/sizeof(registers[0]); ++i) {
        char exact[40];
        const int n = snprintf(exact, sizeof(exact),
                               "movq%%%%%s,%%0", registers[i]);
        if (n > 0 && (size_t)n < sizeof(exact) &&
            minic_x86_64_compact_asm_equals(text, length, exact))
            return (unsigned int)i + 1U;
    }
    return 0U;
}

/* Form 1 preserves hardware RFLAGS via PUSHFQ/POPQ, including all condition
 * codes; form 2 records the machine RIP with LEA, never a C pseudo-address. */
unsigned int minic_x86_64_linux_crash_misc_snapshot_form(
    const char *text, size_t length) {
    if (minic_x86_64_compact_asm_equals(text, length, "pushfq;popq%0")) return 1U;
    if (minic_x86_64_compact_asm_equals(text, length, "lea0(%%rip),%0")) return 2U;
    /* Linux native_save_fl(): the source comment and generic `pushf` spelling
     * are sealed here, then canonicalized to the existing 64-bit FLAGS dump.
     * The =rm/memory-clobber contract is verified separately at C ingress. */
    if (minic_x86_64_compact_asm_equals(
            text, length, "#__raw_save_flagspushf;pop%0")) return 3U;
    return 0U;
}


/* arch/x86/include/asm/pm-trace.h: TRACE_RESUME/TRACE_SUSPEND encode an
 * immutable two-field ELF record in .tracedata, then return its address.
 * `%c2` is a direct symbol for the preprocessed __FILE__ string literal;
 * loading that string's pointer at run time is not GNU `i` semantics. */
bool minic_x86_64_linux_pm_trace_form(const char *text, size_t length) {
    return minic_x86_64_compact_asm_equals(text, length,
        "movq$1f,%0.section.tracedata,\"a\"1:.word%c1.quad%c2.previous");
}


unsigned int minic_x86_64_linux_task_control_form(const char *text, size_t length) {
    if (minic_x86_64_compact_asm_equals(text, length, "ltr%w0")) return 1U;
    if (minic_x86_64_compact_asm_equals(text, length, "str%0")) return 2U;
    return 0U;
}

bool minic_x86_64_linux_iret_to_self_form(const char *text, size_t length) {
    return minic_x86_64_compact_asm_equals(
        text, length, "mov%%ss,%0pushq%q0pushq%%rspaddq$8,(%%rsp)"
                      "pushfqmov%%cs,%0pushq%q0pushq$1firetq1:");
}

bool minic_x86_64_linux_int3_selftest_form(const char *text, size_t length) {
    return minic_x86_64_compact_asm_equals(
        text, length, "int3_selftest_ip:986:.pushsection.discard.noendbr"
                      ".long986b.popsectionint3;nop;nop;nop;nop");
}

/* Paravirtualized Linux IRQ safe halt: executable indirect CALL and retained
 * parainstruction site/length metadata are an indivisible patching ABI. */
bool minic_x86_64_linux_pv_indirect_call_form(const char *text, size_t length) {
    static const char expected[] =
        "771:999:.pushsection.discard.retpoline_safe.long999b.popsection"
        "call*%[paravirt_opptr];772:"
        ".pushsection.parainstructions,\"a\".balign8.quad771b"
        ".byte%c[paravirt_typenum].byte772b-771b.balign8.popsection";
    static const char numeric[] =
        "771:999:.pushsection.discard.retpoline_safe.long999b.popsection"
        "call*%6;772:"
        ".pushsection.parainstructions,\"a\".balign8.quad771b"
        ".byte%c5.byte772b-771b.balign8.popsection";
    return minic_x86_64_compact_asm_equals(text, length, expected) ||
           minic_x86_64_compact_asm_equals(text, length, numeric);
}

/* Real Linux 6.6 KVM's alternative hypercall encoding: Intel VMCALL is the
 * initially executable instruction; AMD VMMCALL is a separate replacement
 * fragment selected by the kernel's alternative patching using the exact
 * X86_FEATURE_VMMCALL (8*32+15) metadata and PC-relative section offsets.
 * A raw VMCALL/VMMCALL with equivalent CPU instruction alone is insufficient. */
bool minic_x86_64_linux_kvm_hypercall_alt_form(
    const char *text, size_t length) {
    static const char expected[] =
        "#ALT:oldnstr661:vmcall662:#ALT:padding"
        ".skip-(((6651f-6641f)-(662b-661b))>0)*"
        "((6651f-6641f)-(662b-661b)),0x90"
        "663:.pushsection.altinstructions,\"a\""
        ".long661b-..long6641f-..4byte(8*32+15)"
        ".byte663b-661b.byte6651f-6641f.popsection"
        ".pushsection.altinstr_replacement,\"ax\""
        "#ALT:replacement16641:vmmcall6651:.popsection";
    return minic_x86_64_compact_asm_equals(text, length, expected);
}

/* VMware legacy I/O backdoor is the boot-time fallback for both Intel and
 * AMD hypercall encodings. Linux emits TWO alternatives with exact feature
 * IDs (8*32+18,8*32+19); replacing the old port I/O by only one bare
 * vmcall would make the image invalid on the other hypervisor path. */
bool minic_x86_64_linux_vmware_stealclock_alt_form(
    const char *text, size_t length) {
    static const char expected[] =
        "#ALT:oldinstr2661:movw$0x5658,%%dx;inl(%%dx),%%eax"
        "662:#ALT:padding2.skip-((((6651f-6641f)^"
        "(((6651f-6641f)^(6652f-6642f))&"
        "-(-((6651f-6641f)<(6652f-6642f)))))-(662b-661b))>0)*"
        "(((6651f-6641f)^(((6651f-6641f)^(6652f-6642f))&"
        "-(-((6651f-6641f)<(6652f-6642f)))))-(662b-661b)),0x90"
        "663:.pushsection.altinstructions,\"a\""
        ".long661b-..long6641f-..4byte(8*32+18)"
        ".byte663b-661b.byte6651f-6641f"
        ".long661b-..long6642f-..4byte(8*32+19)"
        ".byte663b-661b.byte6652f-6642f.popsection"
        ".pushsection.altinstr_replacement,\"ax\""
        "#ALT:replacement16641:vmcall6651:"
        "#ALT:replacement26642:vmmcall6652:.popsection";
    return minic_x86_64_compact_asm_equals(text, length, expected);
}


/* The SEV fallback emits AMD VMMCALL directly; unlike the alternative form,
 * no ELF patch record is produced. Both use the same Linux register ABI. */
bool minic_x86_64_linux_kvm_hypercall_direct_form(
    const char *text, size_t length) {
    return text != NULL && length == 7U &&
           memcmp(text, "vmmcall", 7U) == 0;
}


/* Exceptional double-fault-safe x86 stack call (Linux page_fault_oops):
 * the outgoing function receives 3 fixed SysV arguments on a special
 * per-CPU exception stack; the old RSP is stored at the new stack top and
 * restored by POP %rsp. The executable call target must remain a direct
 * function-symbol PC-relative relocation and the objtool reachable record
 * must point at the instruction after that call. */
bool minic_x86_64_linux_exception_stack_call_form(
    const char *text, size_t length) {
    static const char named[] =
        "movq%%rsp,(%[tos])movq%[tos],%%rsp"
        "movq%[arg3],%%rdxmovq%[arg2],%%rsimovq%[arg1],%%rdi"
        "call%c[__func]998:.pushsection.discard.reachable"
        ".long998b.popsectionpopq%%rsp";
    static const char numeric[] =
        "movq%%rsp,(%3)movq%3,%%rsp"
        "movq%6,%%rdxmovq%5,%%rsimovq%4,%%rdi"
        "call%c2998:.pushsection.discard.reachable"
        ".long998b.popsectionpopq%%rsp";
    /* Named %[tos] is GNU operand zero (a tied +r output), not the
     * duplicate input %[top] at index three. Core normalizes accordingly. */
    static const char core_numeric[] =
        "movq%%rsp,(%0)movq%0,%%rsp"
        "movq%6,%%rdxmovq%5,%%rsimovq%4,%%rdi"
        "call%c2998:.pushsection.discard.reachable"
        ".long998b.popsectionpopq%%rsp";
    return minic_x86_64_compact_asm_equals(text, length, named) ||
           minic_x86_64_compact_asm_equals(text, length, numeric) ||
           minic_x86_64_compact_asm_equals(text, length, core_numeric);
}


/* Text-poking selftest: retain an empty baseline (NOP padded to replacement
 * width) and a relocatable LEA+CALL in the alternative replacement section. */
bool minic_x86_64_linux_alt_reloc_selftest_form(const char *text, size_t length) {
    static const char expected[] =
        "#ALT:oldnstr661:662:#ALT:padding"
        ".skip-(((6651f-6641f)-(662b-661b))>0)*"
        "((6651f-6641f)-(662b-661b)),0x90"
        "663:.pushsection.altinstructions,\"a\""
        ".long661b-..long6641f-..4byte(3*32+21)"
        ".byte663b-661b.byte6651f-6641f.popsection"
        ".pushsection.altinstr_replacement,\"ax\""
        "#ALT:replacement16641:lea%0,%%rdi;call__alt_reloc_selftest;"
        "6651:.popsection";
    static const char named[] =
        "#ALT:oldnstr661:662:#ALT:padding"
        ".skip-(((6651f-6641f)-(662b-661b))>0)*"
        "((6651f-6641f)-(662b-661b)),0x90"
        "663:.pushsection.altinstructions,\"a\""
        ".long661b-..long6641f-..4byte(3*32+21)"
        ".byte663b-661b.byte6651f-6641f.popsection"
        ".pushsection.altinstr_replacement,\"ax\""
        "#ALT:replacement16641:lea%[mem],%%rdi;call__alt_reloc_selftest;"
        "6651:.popsection";
    return minic_x86_64_compact_asm_equals(text, length, expected) ||
           minic_x86_64_compact_asm_equals(text, length, named);
}


/* machine_real_restart transfers out of long mode through the real-mode
 * trampoline's six-byte far pointer. The GNU D constraint is the boot
 * restart type: keeping it physically in EDI is part of the ABI. */
bool minic_x86_64_linux_real_mode_far_jump_form(const char *text,
                                                size_t length) {
    return minic_x86_64_compact_asm_equals(text, length, "ljmpl*%0");
}

/* Linux annotate_unreachable() emits a single reference to the current
 * numeric label in .discard.unreachable. Verify the complete section and
 * label relation; do not allow arbitrary multi-instruction opaque text. */
bool minic_x86_64_linux_unreachable_opaque(const char *text, size_t length) {
    char compact[256], expected[256];
    size_t i, n = 0U, digits = 0U;
    unsigned int label = 0U;
    if (!text || !length || length >= sizeof(compact) || strlen(text) != length)
        return false;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return false;
        compact[n++] = c;
    }
    compact[n] = '\0';
    while (digits < n && compact[digits] >= '0' && compact[digits] <= '9') {
        if (digits >= 4U) return false;
        label = label * 10U + (unsigned int)(compact[digits] - '0');
        ++digits;
    }
    if (!digits || !label || compact[digits] != ':') return false;
    return snprintf(expected, sizeof(expected),
        "%u:.pushsection.discard.unreachable.long%ub-..popsection",
        label, label) > 0 && strcmp(compact, expected) == 0;
}

/* Only a fully authenticated .discard.unreachable template may expose its
 * numeric source label. Consumers must also match the GNU `i` witness. */
unsigned int minic_x86_64_linux_unreachable_label(const char *text,
    size_t length) {
    unsigned int label = 0U;
    size_t i = 0U;
    if (!minic_x86_64_linux_unreachable_opaque(text,length)) return 0U;
    while (i < length && text[i] >= '0' && text[i] <= '9') {
        label = label * 10U + (unsigned int)(text[i] - '0');
        ++i;
    }
    return label;
}


/* Read-once volatile MMIO: exact MOVB/MOVW/MOVL/MOVQ width, register result,
 * and an address-backed memory input. The +memory C clobber is optional:
 * Linux's double-underscore raw helpers intentionally omit it. */
unsigned int minic_x86_64_linux_mmio_read_form(const char *text, size_t length) {
    if (minic_x86_64_compact_asm_equals(text, length, "movb%1,%0")) return 1U;
    /* The x86 PCI MMCONFIG byte reader fixes the destination to AL rather
     * than referencing GNU output %0; its =a constraint proves this mode. */
    if (minic_x86_64_compact_asm_equals(text, length, "movb%1,%%al")) return 1U;
    if (minic_x86_64_compact_asm_equals(text, length, "movw%1,%0")) return 2U;
    if (minic_x86_64_compact_asm_equals(text, length, "movl%1,%0")) return 4U;
    if (minic_x86_64_compact_asm_equals(text, length, "movq%1,%0")) return 8U;
    return 0U;
}

unsigned int minic_x86_64_linux_mmio_write_form(const char *text, size_t length) {
    if (minic_x86_64_compact_asm_equals(text, length, "movb%0,%1")) return 1U;
    if (minic_x86_64_compact_asm_equals(text, length, "movw%0,%1")) return 2U;
    if (minic_x86_64_compact_asm_equals(text, length, "movl%0,%1")) return 4U;
    if (minic_x86_64_compact_asm_equals(text, length, "movq%0,%1")) return 8U;
    return 0U;
}


/* PCI MMCONFIG's AMD Fam10h restriction is stronger than normal MMIO:
 * the *hardware* memory access must use AL/AX/EAX, and GNU's `r` input
 * is the address itself. Never accept a general `m` form under this ABI. */
unsigned int minic_x86_64_linux_pci_mmconfig_form(
    const char *text, size_t length, bool write) {
    static const char *const reads[] = {
        "movb(%1),%%al", "movw(%1),%%ax", "movl(%1),%%eax"
    };
    static const char *const writes[] = {
        "movb%%al,(%1)", "movw%%ax,(%1)", "movl%%eax,(%1)"
    };
    size_t i;
    for (i = 0U; i < 3U; ++i)
        if (minic_x86_64_compact_asm_equals(text, length,
                write ? writes[i] : reads[i]))
            return (unsigned int)1U << i;
    return 0U;
}


/* GNU Linux inline `rep; outs{b,w,l}` / `rep; ins{b,w,l}` has two
 * read/write C lvalues (+S/+D address and +c count), and a DX input port.
 * Matching only the exact instruction and register constraints is crucial:
 * a source-level read/write I/O operation may never become an empty asm. */
unsigned int minic_x86_64_linux_rep_string_io_form(const char *text,
                                                     size_t length) {
    if (minic_x86_64_compact_asm_equals(text, length, "rep;outsb")) return 1U;
    if (minic_x86_64_compact_asm_equals(text, length, "rep;outsw")) return 2U;
    if (minic_x86_64_compact_asm_equals(text, length, "rep;outsl")) return 3U;
    if (minic_x86_64_compact_asm_equals(text, length, "rep;insb")) return 4U;
    if (minic_x86_64_compact_asm_equals(text, length, "rep;insw")) return 5U;
    if (minic_x86_64_compact_asm_equals(text, length, "rep;insl")) return 6U;
    return 0U;
}


unsigned int minic_x86_64_linux_iomem_copy_form(const char *text, size_t length) {
    if (minic_x86_64_compact_asm_equals(text, length, "movsb")) return 1U;
    if (minic_x86_64_compact_asm_equals(text, length, "movsw")) return 2U;
    if (minic_x86_64_compact_asm_equals(text, length,
        "rep;movsltestb$2,%b4je1fmovsw1:testb$1,%b4je2fmovsb2:")) return 3U;
    return 0U;
}

/* drivers/gpu/drm/i915/i915_memcpy.c: all four real memcpy_from_wc
 * `movntdqa` blocks. The source explicitly has no XMM clobber constraints;
 * kernel_fpu_begin() and Linux -mgeneral-regs-only own that environment.
 * Exact complete multi-instruction matching prevents arbitrary raw ASM. */
unsigned int minic_x86_64_linux_i915_movntdqa_form(
    const char *text, size_t length) {
    static const char *const prefix4 =
        "movntdqa(%0),%%xmm0"
        "movntdqa16(%0),%%xmm1"
        "movntdqa32(%0),%%xmm2"
        "movntdqa48(%0),%%xmm3";
    static const char *const aligned4 =
        "movaps%%xmm0,(%1)"
        "movaps%%xmm1,16(%1)"
        "movaps%%xmm2,32(%1)"
        "movaps%%xmm3,48(%1)";
    static const char *const unaligned4 =
        "movups%%xmm0,(%1)"
        "movups%%xmm1,16(%1)"
        "movups%%xmm2,32(%1)"
        "movups%%xmm3,48(%1)";
    char normalized[512];
    size_t i, n = 0U;
    if (!text || !length || length >= sizeof(normalized)) return 0U;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || n + 1U >= sizeof(normalized)) return 0U;
        normalized[n++] = c;
    }
    normalized[n] = '\0';
    if (strcmp(normalized, "movntdqa(%0),%%xmm0movaps%%xmm0,(%1)") == 0)
        return 1U;
    if (strcmp(normalized, "movntdqa(%0),%%xmm0movups%%xmm0,(%1)") == 0)
        return 2U;
    if (n == strlen(prefix4) + strlen(aligned4) &&
        memcmp(normalized, prefix4, strlen(prefix4)) == 0 &&
        strcmp(normalized + strlen(prefix4), aligned4) == 0)
        return 3U;
    if (n == strlen(prefix4) + strlen(unaligned4) &&
        memcmp(normalized, prefix4, strlen(prefix4)) == 0 &&
        strcmp(normalized + strlen(prefix4), unaligned4) == 0)
        return 4U;
    return 0U;
}


unsigned int minic_x86_64_kvm_movdqa_form(const char *text, size_t length) {
    unsigned int reg;
    for (reg = 0U; reg < 16U; ++reg) {
        char load[40], store[40];
        const int l = snprintf(load, sizeof(load), "movdqa%%0,%%%%xmm%u", reg);
        const int s = snprintf(store, sizeof(store), "movdqa%%%%xmm%u,%%0", reg);
        if (l <= 0 || s <= 0 || (size_t)l >= sizeof(load) ||
            (size_t)s >= sizeof(store)) return 0U;
        if (minic_x86_64_compact_asm_equals(text, length, load)) return reg + 1U;
        if (minic_x86_64_compact_asm_equals(text, length, store)) return reg + 17U;
    }
    return 0U;
}


static uint64_t x86_linux_template_fingerprint(const char *text, size_t length) {
    uint64_t hash = UINT64_C(14695981039346656037);
    size_t i;
    for (i = 0U; i < length; ++i) {
        hash ^= (unsigned char)text[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

unsigned int minic_x86_64_linux_flushcache_copy_form(const char *text, size_t length) {
    if (minic_x86_64_compact_asm_equals(text, length,
        "movq(%0),%%r8movq8(%0),%%r9movq16(%0),%%r10movq24(%0),%%r11"
        "movnti%%r8,(%1)movnti%%r9,8(%1)movnti%%r10,16(%1)movnti%%r11,24(%1)"))
        return 1U;
    if (minic_x86_64_compact_asm_equals(text, length,
        "movq(%0),%%r8movnti%%r8,(%1)")) return 2U;
    if (minic_x86_64_compact_asm_equals(text, length,
        "movl(%0),%%r8dmovnti%%r8d,(%1)")) return 3U;
    /* The 704/712-byte source and Core-normalized `clwb` alternatives both
       retain two CPU-feature replacement records. Distinct full-text hashes
       identify the spelling; the mandatory section/instruction sentinels
       have the same meaning for both variants. */
    if (text &&
        ((length == 704U && x86_linux_template_fingerprint(text, length) ==
            UINT64_C(0xd4e9ddad388cfc52)) ||
         (length == 712U && x86_linux_template_fingerprint(text, length) ==
            UINT64_C(0xef6513d224b5c286))) &&
        strstr(text, "# ALT: oldinstr2") == text &&
        strstr(text, ".altinstructions") != NULL &&
        strstr(text, ".altinstr_replacement") != NULL &&
        strstr(text, "0x66, 0x0f, 0xae, 0x30") != NULL)
        return 4U;
    return 0U;
}


unsigned int minic_x86_64_linux_atomic_xchg_form(const char *s, size_t len) {
    if (minic_x86_64_compact_asm_equals(s, len, "xchgb%b0,%1")) return 1U;
    if (minic_x86_64_compact_asm_equals(s, len, "xchgw%w0,%1")) return 2U;
    if (minic_x86_64_compact_asm_equals(s, len, "xchgl%0,%1")) return 4U;
    if (minic_x86_64_compact_asm_equals(s, len, "xchgq%q0,%1")) return 8U;
    return 0U;
}


/* Linux pvclock's 64x64->128 multiplication and 32-bit right funnel shift.
 * The RDX:RAX pair is an ABI contract, not a plain 64-bit product. */
unsigned int minic_x86_64_linux_pvclock_scale_form(const char *text, size_t length) {
    if (minic_x86_64_compact_asm_equals(
            text, length, "mulq%[mul_frac];shrd$32,%[hi],%[lo]") ||
        minic_x86_64_compact_asm_equals(text, length, "mulq%3;shrd$32,%1,%0"))
        return 1U;
    return 0U;
}


/* Linux head64 startup_gdt: reload DS/SS/ES with the exact kernel data
 * selector in AX. These are privileged CPU segment-register writes, not
 * ordinary stores. The GNU source uses an `a` input and requires memory
 * sequencing; the machine owner chooses the real physical EAX/AX register. */
bool minic_x86_64_linux_boot_segments_template(
    const char *text, size_t length) {
    static const char expected[] =
        "movl%%eax,%%dsmovl%%eax,%%ssmovl%%eax,%%es";
    char compact[120];
    size_t n = 0U, i;
    if (!text || length == 0U || length >= sizeof(compact)) return false;
    for (i = 0U; i < length; ++i) {
        const char ch = text[i];
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') continue;
        if (ch == '\0' || n + 1U >= sizeof(compact)) return false;
        compact[n++] = ch;
    }
    compact[n] = '\0';
    return strcmp(compact, expected) == 0;
}

/* Linux interrupt flag, halted-CPU state and cache WB/INV transitions. These
 * exact
 * zero-operand GNU templates are privileged machine instructions; their
 * compiler memory sequencing is essential, and the Target must not
 * reinterpret them as ordinary optimizer barriers or omit the instruction. */
unsigned int minic_x86_64_linux_privileged_zero_form(
    const char *text, size_t length) {
    static const char *const forms[] = {
        "cli", "sti", "sti; hlt", "hlt", "wbinvd", "swapgs",
        ".byte 0xf, 0x1, 0xe8", "outb %al, $0x80", "outb %al, $0xed",
        "int3"
    };
    size_t i;
    if (!text || length == 0U || length > 32U) return 0U;
    for (i = 0U; i < sizeof(forms) / sizeof(forms[0]); ++i)
        if (length == strlen(forms[i]) &&
            memcmp(text, forms[i], length) == 0)
            return (unsigned int)(i + 1U);
    return 0U;
}

/* TILERELEASE is an architectural AMX tile-register state transition. Linux
 * spells it as raw VEX bytes to support assemblers without the mnemonic;
 * only this exact zero-operand encoding is eligible for native emission. */
bool minic_x86_64_linux_amx_tilerelease_form(const char *text, size_t length) {
    static const char bytes[] = ".byte0xc4,0xe2,0x78,0x49,0xc0";
    char compact[64];
    size_t i, n = 0U;
    if (!text || length == 0U || length >= sizeof(compact)) return false;
    for (i = 0U; i < length; ++i) {
        const char ch = text[i];
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') continue;
        if (ch == '\0' || n + 1U >= sizeof(compact)) return false;
        compact[n++] = ch;
    }
    compact[n] = '\0';
    return strcmp(compact, bytes) == 0;
}

/* `fpu__drop` waits for any pending x87 exception and installs a type-1
 * exception-table recovery entry around FWAIT. The fixup metadata is part
 * of the instruction contract, not a removable assembler annotation. */
bool minic_x86_64_linux_x87_fwait_fault_form(const char *text, size_t length) {
    static const char expected[] =
        "1:fwait2:.pushsection\"__ex_table\",\"a\""
        ".balign4.long(1b)-..long(2b)-..long1.popsection";
    char compact[192];
    size_t i, n = 0U;
    if (!text || length == 0U || length >= sizeof(compact)) return false;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return false;
        compact[n++] = c;
    }
    compact[n] = '\0';
    return strcmp(compact, expected) == 0;
}

/* WRMSR ordering fence with Linux's feature-flagged empty replacement.
 * The two fences and complete .altinstructions record are inseparable: a
 * bare MFENCE/LFENCE is not equivalent on CPUs advertising this feature. */
bool minic_x86_64_linux_weak_wrmsr_fence_form(const char *text, size_t length) {
    static const char expected[] =
        "#ALT:oldnstr661:mfence;lfence662:#ALT:padding"
        ".skip-(((6651f-6641f)-(662b-661b))>0)*"
        "((6651f-6641f)-(662b-661b)),0x90"
        "663:.pushsection.altinstructions,\"a\""
        ".long661b-..long6641f-..4byte(((1<<0)<<16)|((11*32+27)))"
        ".byte663b-661b.byte6651f-6641f.popsection"
        ".pushsection.altinstr_replacement,\"ax\""
        "#ALT:replacement16641:6651:.popsection";
    char compact[512];
    size_t i, n = 0U;
    if (!text || length == 0U || length >= sizeof(compact)) return false;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return false;
        compact[n++] = c;
    }
    compact[n] = '\0';
    return strcmp(compact, expected) == 0;
}


/* This is the complete Linux three-way CALL alternatives grammar, including
 * every relocation and feature number. The C preprocessor expands a lengthy
 * MAX() expression inside .skip; compare the full expression, not a prefix,
 * and never admit unrecognized assembler input through the Core boundary.
 * Named input spelling is normalized to its numeric Core position later. */
#define X86_ALT_CALL_MAX \
    "((6651f-6641f)^(((6651f-6641f)^(6652f-6642f))&-(-((6651f-6641f)<(6652f-6642f)))))"
#define X86_ALT_CALL_SKIP \
    ".skip-((" X86_ALT_CALL_MAX "-(662b-661b))>0)*(" \
    X86_ALT_CALL_MAX "-(662b-661b)),0x90"

bool minic_x86_64_linux_three_call_alternative_form(
    const char *text, size_t length) {
    static const char *const lines[] = {
        "#ALT:oldinstr2", "661:", NULL, "662:",
        "#ALT:padding2", X86_ALT_CALL_SKIP, "663:",
        ".pushsection.altinstructions,\"a\"",
        ".long661b-.", ".long6641f-.", ".4byte(3*32+16)",
        ".byte663b-661b", ".byte6651f-6641f",
        ".long661b-.", ".long6642f-.", ".4byte(9*32+9)",
        ".byte663b-661b", ".byte6652f-6642f",
        ".popsection", ".pushsection.altinstr_replacement,\"ax\"",
        "#ALT:replacement1", "6641:", NULL, "6651:",
        "#ALT:replacement2", "6642:", NULL, "6652:",
        ".popsection"
    };
    const size_t count = sizeof(lines) / sizeof(lines[0]);
    size_t position = 0U, line_index = 0U;
    if (!text || length < 250U || length > 1600U ||
        strlen(text) != length) return false;
    while (position < length) {
        char compact[1000];
        size_t n = 0U;
        while (position < length && text[position] != '\n') {
            const char c = text[position++];
            if (c == ' ' || c == '\t' || c == '\r') continue;
            if (c == '\0' || n + 1U >= sizeof(compact)) return false;
            compact[n++] = c;
        }
        if (position < length) ++position;
        compact[n] = '\0';
        if (n == 0U) continue;
        if (line_index >= count) return false;
        if (lines[line_index]) {
            if (strcmp(compact, lines[line_index]) != 0) return false;
        } else if (line_index == 2U) {
            if (strcmp(compact, "call%c[old]") != 0 &&
                strcmp(compact, "call%c2") != 0) return false;
        } else if (line_index == 22U) {
            if (strcmp(compact, "call%c[new1]") != 0 &&
                strcmp(compact, "call%c3") != 0) return false;
        } else if (line_index == 26U) {
            if (strcmp(compact, "call%c[new2]") != 0 &&
                strcmp(compact, "call%c4") != 0) return false;
        } else return false;
        ++line_index;
    }
    return line_index == count;
}
#undef X86_ALT_CALL_SKIP
#undef X86_ALT_CALL_MAX

/* Linux _static_cpu_has: exactly two C goto targets, three immediate
 * operands, an 8-bit bit test in the auxiliary text section, and two runtime
 * alternative entries. A token-by-token grammar rejects even one extra
 * instruction, change of CPU feature metadata or section-order mutation. */
#define X86_CPU_HAS_MAX \
    "((6651f-6641f)^(((6651f-6641f)^(6652f-6642f))&-(-((6651f-6641f)<(6652f-6642f)))))"
#define X86_CPU_HAS_SKIP \
    ".skip-((" X86_CPU_HAS_MAX "-(662b-661b))>0)*(" \
    X86_CPU_HAS_MAX "-(662b-661b)),0x90"

bool minic_x86_64_linux_cpu_has_goto_form(
    const char *text, size_t length) {
    static const char *const lines[] = {
        "#ALT:oldinstr2", "661:", "jmp6f", "662:",
        "#ALT:padding2", X86_CPU_HAS_SKIP, "663:",
        ".pushsection.altinstructions,\"a\"",
        ".long661b-.", ".long6641f-.", ".4byte(3*32+21)",
        ".byte663b-661b", ".byte6651f-6641f",
        ".long661b-.", ".long6642f-.", ".4byte%c[feature]",
        ".byte663b-661b", ".byte6652f-6642f",
        ".popsection", ".pushsection.altinstr_replacement,\"ax\"",
        "#ALT:replacement1", "6641:", "jmp%l[t_no]", "6651:",
        "#ALT:replacement2", "6642:", "6652:", ".popsection",
        ".pushsection.altinstr_aux,\"ax\"", "6:",
        "testb%[bitnum],%P[cap_byte](%%rip)",
        "jnz%l[t_yes]", "jmp%l[t_no]", ".popsection"
    };
    const size_t count = sizeof(lines) / sizeof(lines[0]);
    size_t cursor = 0U, line_index = 0U;
    if (!text || length < 600U || length > 1100U || strlen(text) != length)
        return false;
    while (cursor < length) {
        char compact[500];
        size_t n = 0U;
        while (cursor < length && text[cursor] != '\n') {
            const char ch = text[cursor++];
            if (ch == ' ' || ch == '\t' || ch == '\r') continue;
            if (ch == '\0' || n + 1U >= sizeof(compact)) return false;
            compact[n++] = ch;
        }
        if (cursor < length) ++cursor;
        compact[n] = '\0';
        if (n == 0U) continue;
        if (line_index >= count || strcmp(compact, lines[line_index]) != 0)
            return false;
        ++line_index;
    }
    return line_index == count;
}
#undef X86_CPU_HAS_SKIP
#undef X86_CPU_HAS_MAX

/* Preemption's ASM_CALL_CONSTRAINT is `+r(current_stack_pointer)`, where the
 * variable is bound to the physical RSP. GNU keeps this binding live across
 * the call; it is neither a register value to copy back nor a C assignment.
 * Validate the full two static-call trampolines, the fixed register and the
 * exact GNU access constraint before admitting a Core effect. */
unsigned int minic_x86_64_linux_preempt_static_call_form(
    const char *text, size_t length) {
    if (minic_x86_64_compact_asm_equals(
            text, length, "call__SCT__preempt_schedule")) return 1U;
    if (minic_x86_64_compact_asm_equals(
            text, length, "call__SCT__preempt_schedule_notrace")) return 2U;
    return 0U;
}

/* Derived byte-for-byte from Linux 6.6.143 arch/x86/include/asm/preempt.h:
 * GEN_UNARY_RMWcc("decl", pcpu_hot.preempt_count, e,
 *                 __percpu_arg([var])). rmwcc.h adds the hardware flag
 * output and memory clobber; percpu.h expands __percpu_arg to %%gs:%[var]. */
bool minic_x86_64_linux_preempt_gs_dec_form(
    const char *text, size_t length) {
    return minic_x86_64_compact_asm_equals(
        text, length, "decl%%gs:%[var]/*outputconditioncodee*/") ||
           /* Core rewrites GNU named %[var] references to ordinal %0
            * before storing a structured template for machine emission. */
           minic_x86_64_compact_asm_equals(
               text, length, "decl%%gs:%0/*outputconditioncodee*/");
}

/* Linux 6.6.143 arch/x86/include/asm/ibt.h: gen_endbr() emits a
 * nonvolatile GNU early-clobber register output. Unlike hand-written
 * instruction bytes, this two-instruction sequence computes a C u32. */
bool minic_x86_64_linux_gen_endbr_form(
    const char *text, size_t length) {
    return minic_x86_64_compact_asm_equals(
        text, length, "mov$~0xfa1e0ff3,%[endbr]not%[endbr]") ||
           minic_x86_64_compact_asm_equals(
               text, length, "mov$~0xfa1e0ff3,%0not%0");
}

/* _ASM_MUL from Linux arch/x86/lib/kaslr.c resolves to mulq on x86_64.
 * GCC gives the RAX:RDX product as two distinct outputs; source operand
 * three accepts register or memory, and we always choose a scratch GPR. */
bool minic_x86_64_linux_kaslr_mul_form(const char *text, size_t length) {
    return minic_x86_64_compact_asm_equals(text, length, "mulq%3");
}

bool minic_x86_64_linux_muldiv_u64_form(const char *text, size_t length) {
    return minic_x86_64_compact_asm_equals(text, length, "mulq%2;divq%3");
}


/* Linux drivers/char/hw_random/via-rng.c: PadLock's XSTORE instruction
 * writes through EDI, returns a count in EAX and advances EDX/EDI. The
 * =m(*addr) alias is not an independent address: the +D output MUST be
 * the same source pointer lvalue or hardware would update the wrong object. */
bool minic_x86_64_linux_via_xstore_form(const char *text, size_t length) {
    return minic_x86_64_compact_asm_equals(
        text, length, ".byte0x0F,0xA7,0xC0/*xstore%%edi(addr=%0)*/");
}

bool minic_x86_64_linux_zstd_p2align_form(const char *text, size_t length) {
    /* Linux lib/zstd/decompress/zstd_decompress_block.c demands a 64-byte
     * instruction alignment at an inner loop entry. Whitelist this exact
     * assembler directive; never transport arbitrary basic-asm text. */
    return minic_x86_64_compact_asm_equals(text, length, ".p2align6");
}

/* Restore the five exact original Linux ZSTD loop-padding forms formerly
 * handled by the direct x86 emitter. They are assembler directives / NOP,
 * not permission to pass arbitrary basic-asm strings to GNU as. */
unsigned int minic_x86_64_linux_zstd_padding_form(
    const char *text, size_t length) {
    static const char *const exact[] = {
        "nop", ".p2align 3", ".p2align 4", ".p2align 5"
    };
    size_t i;
    if (text == NULL) return 0U;
    for (i = 0U; i < sizeof(exact)/sizeof(exact[0]); ++i)
        if (length == strlen(exact[i]) &&
            memcmp(text, exact[i], length) == 0)
            return (unsigned int)(i + 1U);
    return minic_x86_64_linux_zstd_p2align_form(text, length) ? 5U : 0U;
}

/* 6.6 arch/x86/include/asm/kexec.h crash_setup_regs() stores 16-bit CPU
 * segment selectors through GNU's fixed EAX output into unsigned long
 * pt_regs slots. MOVL to EAX must clear the high 32 bits of RAX. */
unsigned int minic_x86_64_linux_crash_segment_snapshot_form(
    const char *text, size_t length) {
    static const char *const selectors[] = {"ss", "cs", "ds", "es", "fs", "gs"};
    char expected[32];
    size_t i;
    for (i = 0U; i < sizeof(selectors)/sizeof(selectors[0]); ++i) {
        int n = snprintf(expected, sizeof(expected),
                         "movl%%%%%s,%%%%eax;", selectors[i]);
        if (n > 0 && (size_t)n < sizeof(expected) &&
            minic_x86_64_compact_asm_equals(text, length, expected))
            return (unsigned int)i + 1U;
    }
    return 0U;
}


/* x86 irq_stack.h call_on_stack(): two original GNU layouts and their
 * Core-numeric equivalents. The mandatory .discard.reachable entry is
 * matched as part of the call contract so objtool's CFG metadata survives. */
unsigned int minic_x86_64_linux_irq_stack_call_form(
    const char *text, size_t length) {
    static const char *const named[] = {
        "movq%%rsp,(%[tos])movq%[tos],%%rsp"
        "callirq_enter_rcumovq%[arg1],%%rdi"
        "call%c[__func]998:.pushsection.discard.reachable"
        ".long998b.popsectioncallirq_exit_rcupopq%%rsp",
        "movq%%rsp,(%[tos])movq%[tos],%%rsp"
        "callirq_enter_rcumovq%[arg2],%%rsi"
        "movq%[arg1],%%rdicall%c[__func]998:"
        ".pushsection.discard.reachable.long998b.popsection"
        "callirq_exit_rcupopq%%rsp",
        "movq%%rsp,(%[tos])movq%[tos],%%rsp"
        "call%c[__func]998:.pushsection.discard.reachable"
        ".long998b.popsectionpopq%%rsp"
    };
    static const char *const numeric[] = {
        "movq%%rsp,(%3)movq%3,%%rsp"
        "callirq_enter_rcumovq%4,%%rdi"
        "call%c2" "998:.pushsection.discard.reachable"
        ".long998b.popsectioncallirq_exit_rcupopq%%rsp",
        "movq%%rsp,(%3)movq%3,%%rsp"
        "callirq_enter_rcumovq%5,%%rsi"
        "movq%4,%%rdicall%c2" "998:"
        ".pushsection.discard.reachable.long998b.popsection"
        "callirq_exit_rcupopq%%rsp",
        "movq%%rsp,(%3)movq%3,%%rsp"
        "call%c2" "998:.pushsection.discard.reachable"
        ".long998b.popsectionpopq%%rsp"
    };
    static const char *const core_numeric[] = {
        "movq%%rsp,(%0)movq%0,%%rsp"
        "callirq_enter_rcumovq%4,%%rdi"
        "call%c2" "998:.pushsection.discard.reachable"
        ".long998b.popsectioncallirq_exit_rcupopq%%rsp",
        "movq%%rsp,(%0)movq%0,%%rsp"
        "callirq_enter_rcumovq%5,%%rsi"
        "movq%4,%%rdicall%c2" "998:"
        ".pushsection.discard.reachable.long998b.popsection"
        "callirq_exit_rcupopq%%rsp",
        "movq%%rsp,(%0)movq%0,%%rsp"
        "call%c2" "998:.pushsection.discard.reachable"
        ".long998b.popsectionpopq%%rsp"
    };
    char compact[512];
    size_t i, n = 0U;
    if (text == NULL || length == 0U || length >= sizeof(compact)) return 0U;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return 0U;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < 3U; ++i)
        if (strcmp(compact, named[i]) == 0 ||
            strcmp(compact, numeric[i]) == 0 ||
            strcmp(compact, core_numeric[i]) == 0)
            return (unsigned int)i + 1U;
    return 0U;
}


/* A read of CR0/CR2/CR3/CR4 is not a C memory load; the dummy GNU `m`
 * operand exists only to constrain reordering of the privileged CPU read.
 * Match both instruction and operand footprint, then preserve that logical
 * dependency as a Core memory effect without dereferencing its fake address.
 * Writes of CR2/CR3 are physical machine state updates and a full compiler
 * barrier, not a normal pointer assignment. */
unsigned int minic_x86_64_linux_control_register_form(
    const char *text, size_t length) {
    static const char *const names[] = {
        "mov%%cr0,%0", "mov%%cr2,%0", "mov%%cr3,%0", "mov%%cr4,%0",
        "mov%0,%%cr2", "mov%0,%%cr3", "mov%0,%%cr0", "mov%0,%%cr4"
    };
    char compact[64];
    size_t i, n = 0U;
    if (!text || length == 0U || length >= sizeof(compact)) return 0U;
    for (i = 0U; i < length; ++i) {
        const char ch = text[i];
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') continue;
        if (ch == '\0' || ch == ';' || n + 1U >= sizeof(compact)) return 0U;
        compact[n++] = ch;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(names) / sizeof(names[0]); ++i)
        if (strcmp(compact, names[i]) == 0) return (unsigned int)i + 1U;
    return 0U;
}


unsigned int minic_x86_64_linux_tlb_cache_form(const char *text, size_t len) {
    if (!text) return 0U;
    if (len == 11U && memcmp(text, "invlpg (%0)", 11U) == 0) return 1U;
    if (len == 10U && memcmp(text, "clflush %0", 10U) == 0) return 2U;
    return 0U;
}


/* Linux writes MSR values through a static feature alternative. Before the
 * capability is enabled, the baseline is two NOP bytes; after patching, it
 * becomes WRMSR with ECX/EAX/EDX holding the caller's exact operands. This
 * metadata carries an assembler-resolved .skip and the CPU feature bit.
 * Keep a closed line grammar so no arbitrary .pushsection or machine opcode
 * can sneak through merely because a template starts with `# ALT`. */
bool minic_x86_64_linux_alternative_wrmsr_form(
    const char *text, size_t length) {
    static const char *const lines[] = {
        "#ALT:oldnstr", "661:", "662:", "#ALT:padding",
        ".skip-(((6651f-6641f)-(662b-661b))>0)*((6651f-6641f)-(662b-661b)),0x90",
        "663:", ".pushsection.altinstructions,\"a\"",
        ".long661b-.", ".long6641f-.", NULL,
        ".byte663b-661b", ".byte6651f-6641f",
        ".popsection", ".pushsection.altinstr_replacement,\"ax\"",
        "#ALT:replacement1", "6641:", "wrmsr", "6651:", ".popsection"
    };
    size_t cursor = 0U, line_index = 0U;
    const size_t count = sizeof(lines) / sizeof(lines[0]);
    if (!text || length < 190U || length > 600U || strlen(text) != length)
        return false;
    while (cursor < length) {
        char compact[250];
        size_t n = 0U;
        while (cursor < length && text[cursor] != '\n') {
            const char c = text[cursor++];
            if (c == ' ' || c == '\t' || c == '\r') continue;
            if (c == '\0' || n + 1U >= sizeof(compact)) return false;
            compact[n++] = c;
        }
        if (cursor < length) ++cursor;
        compact[n] = '\0';
        if (n == 0U) continue;
        if (line_index >= count) return false;
        if (lines[line_index]) {
            if (strcmp(compact, lines[line_index]) != 0) return false;
        } else if (line_index == 9U) {
            if (strcmp(compact, ".4byte%c[feature]") != 0 &&
                strcmp(compact, ".4byte%c3") != 0) return false;
        } else return false;
        ++line_index;
    }
    return line_index == count;
}


/* Exact original Linux cmp/sbb two-instruction target ISA shape. */
bool minic_x86_64_asm_nospec_template(const char *text, size_t len) {
    static const char exact[] = "cmp%1,%2;sbb%0,%0";
    char compact[96];
    size_t i, n = 0U;
    if (!text || !len || len >= sizeof(compact)) return false;
    for (i = 0U; i < len; ++i) {
        const char ch = text[i];
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') continue;
        if (n + 1U >= sizeof(compact)) return false;
        compact[n++] = ch;
    }
    compact[n] = '\0';
    /* Original GNU Linux 6.6.143 barrier.h ends SBB with a semicolon.
     * Permit exactly one trailing delimiter, not extra machine opcodes. */
    return strcmp(compact, exact) == 0 ||
           strcmp(compact, "cmp%1,%2;sbb%0,%0;") == 0;
}


/* Linux _THIS_IP_: one precise RIP-relative LEA with a 64-bit register
 * result, never an arbitrary target inline ASM escape hatch. This reaches
 * Core as an address-backed REGISTER_OUTPUT structured effect, not a new
 * frontend IR opcode or a Core-architecture conditional. */
bool minic_x86_64_linux_ip_here_template(const char *text, size_t length) {
    static const char exact[] = "lea0(%%rip),%0";
    char compact[48];
    size_t i, n = 0U;
    if (text == NULL || length == 0U || length >= sizeof(compact))
        return false;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t') continue;
        if (c == '\n' || c == '\r' || c == ';' || c == '\0')
            return false;
        compact[n++] = c;
    }
    compact[n] = '\0';
    return strcmp(compact, exact) == 0;
}


/* Original x86 Linux this_cpu_read_stable_8 is a nonvolatile GNU p
 * address computation followed by a REAL GS-segment hardware read.
 * Only the literal 64-bit instruction / two exact operands are admitted.
 * A Core address SSA value represents P[var]; x86 still owns the GS ISA. */
size_t minic_x86_64_asm_percpu_stable_width(const char *s, size_t length) {
    static const struct { const char *name; size_t width; } forms[] = {
        {"movb%%gs:%P[var],%[val]",1U}, {"movb%%gs:%P1,%0",1U},
        {"movw%%gs:%P[var],%[val]",2U}, {"movw%%gs:%P1,%0",2U},
        {"movl%%gs:%P[var],%[val]",4U}, {"movl%%gs:%P1,%0",4U},
        {"movq%%gs:%P[var],%[val]",8U}, {"movq%%gs:%P1,%0",8U}
    };
    char compact[96]; size_t i, n = 0U;
    if (!s || length == 0U || length >= sizeof(compact)) return 0U;
    for (i = 0U; i < length; ++i) {
        const char c = s[i];
        if (c == ';' || c == '\r' || c == '\n') return 0U;
        if (c == ' ' || c == '\t') continue;
        if (n + 1U >= sizeof(compact)) return 0U;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(forms) / sizeof(forms[0]); ++i)
        if (strcmp(forms[i].name, compact) == 0) return forms[i].width;
    return 0U;
}
/* Whitespace is irrelevant for these four specific instructions, whose
 * operand constraints and exact instruction sequence are checked together. */
/* Only integer operands of the same real x86 width; never silently
 * convert a user-supplied operand or drop the hardware condition flags. */

/* x86 Linux zero-GNU-operand hardware ordering / processor-state class.
 * One ISA descriptor handles source admission and final machine emission:
 * 1=locked stack read-write barrier; 2/3/4=full/load/store fence;
 * 5=pause, 6=rep;nop, 7=x87 FNINIT. No unrestricted ASM text is allowed. */
unsigned int minic_x86_64_linux_zero_barrier_form(
    const char *text, size_t length) {
    return minic_x86_64_zero_barrier_form_exact(text, length);
}


/* Exact original GNU Linux 6.6.143 rdtsc_ordered() hardware clock:
 * the baseline instruction and two alternative patch records are integral
 * to the Linux ELF object. Qualify only the entire original 652-byte
 * source template, never general-purpose GNU ASM passthrough. */
static bool x86_ordered_clock_form(const char *s, size_t len) {
    static const char expected[] =
        "# ALT: oldinstr2\n661:\n\trdtsc\n662:\n# ALT: padding2\n.skip -((((6651"
        "f-6641f) ^ (((6651f-6641f) ^ (6652f-6642f)) & -(-((6651f-6641f) <"
        " (6652f-6642f))))) - (662b-661b)) > 0) * (((6651f-6641f) ^ (((665"
        "1f-6641f) ^ (6652f-6642f)) & -(-((6651f-6641f) < (6652f-6642f))))"
        ") - (662b-661b)), 0x90\n663:\n.pushsection .altinstructions,\"a\"\n .l"
        "ong 661b - .\n .long 6641f - .\n .4byte (20*32+ 2)\n .byte 663b-661b"
        "\n .byte 6651f-6641f\n .long 661b - .\n .long 6642f - .\n .4byte ( 1*"
        "32+27)\n .byte 663b-661b\n .byte 6652f-6642f\n.popsection\n.pushsecti"
        "on .altinstr_replacement, \"ax\"\n# ALT: replacement 1\n6641:\n\tlfence"
        "; rdtsc\n6651:\n# ALT: replacement 2\n6642:\n\trdtscp\n6652:\n.popsectio"
        "n\n";
    return s != NULL && len == sizeof(expected) - 1U &&
           memcmp(s, expected, sizeof(expected) - 1U) == 0;
}
/* Complete x86 EDX:EAX counter family, GNU 32/64-bit output carriers. */
unsigned int minic_x86_64_linux_counter_form(const char *s,size_t len) {
    static const char *const names[]={"rdtsc","rdpmc","rdtscp","xgetbv"};
    char buf[32];size_t i,n=0U;
    if (x86_ordered_clock_form(s,len)) return 4U;
    if(!s||!len||len>=sizeof(buf))return 0U;
    for(i=0U;i<len;++i) {
        const char c=s[i];
        if(c==' '||c==9||c==10||c==13)continue;
        if(n+1U>=sizeof(buf))return 0U;
        buf[n++]=c;
    }
    buf[n]=0;
    for(i=0U;i<4U;++i)
        if(strcmp(buf,names[i])==0)
            return i==3U?5U:(unsigned int)i+1U;
    return 0U;
}


/* FXSAVE uses a 16-byte-aligned, 512-byte architectural memory image.
 * Linux has both the original 32-bit pointer form (`fxsave %0`) and the
 * 64-bit pointer form (`fxsaveq %[fx]`). A structured memory output records
 * exactly which C object is overwritten; no scalar imitation is permitted. */
unsigned int minic_x86_64_linux_fxsave_form(
    const char *text, size_t length) {
    if (!text) return 0U;
    if (length == 9U && memcmp(text, "fxsave %0", 9U) == 0)
        return 1U;
    if (length == 13U && memcmp(text, "fxsaveq %[fx]", 13U) == 0)
        return 2U;
    /* Core canonicalizes a named GNU output to its numeric binding. Only
     * the source qualifier can admit the named form in the first place. */
    if (length == 10U && memcmp(text, "fxsaveq %0", 10U) == 0)
        return 2U;
    return 0U;
}


/* Early boot probes the x87 execution unit by FNINIT and captures its
 * 16-bit status/control words in two independent writable C memory objects.
 * GNU `+m` makes both locations read/write effects, not register outputs. */
bool minic_x86_64_linux_x87_probe_form(
    const char *text, size_t length) {
    static const char expected[] = "fninit ; fnstsw %0 ; fnstcw %1";
    return text && length == sizeof(expected) - 1U &&
           memcmp(text, expected, sizeof(expected) - 1U) == 0;
}


/* Original Linux 6.6 ALTERNATIVE(call __sw_hweight{32,64}, POPCNT).
 * This source parser checks the COMPLETE GNU alternative assembly,
 * including patch metadata/feature=151, not a prefix/suffix substring.
 * Real kernel software fallback and ALT ELF records must be preserved. */
unsigned int minic_x86_64_hweight_alternative_form(const char *s,size_t len) {
    static const char *const begin="661:call__sw_hweight";
    static const char *const middle=
        "662:.skip-(((6651f-6641f)-(662b-661b))>0)*"
        "((6651f-6641f)-(662b-661b)),0x90"
        "663:.pushsection.altinstructions,\"a\""
        ".long661b-..long6641f-."
        ".4byte(4*32+23)"
        ".byte663b-661b.byte6651f-6641f.popsection"
        ".pushsection.altinstr_replacement,\"ax\""
        "6641:popcnt";
    char compact[1024],expect[1024];size_t i,n=0U,w;
    if(!s||!len||len>=sizeof(compact))return 0U;
    for(i=0U;i<len;++i){
        if(s[i]=='#'){while(i<len&&s[i]!=10)++i;continue;}
        if(s[i]==' '||s[i]==9||s[i]==10||s[i]==13)continue;
        if(n+1U>=sizeof(compact))return 0U;
        compact[n++]=s[i];
    }
    compact[n]=0;
    for(w=4U;w<=8U;w+=4U) {
        const int written=snprintf(expect,sizeof(expect),
           "%s%s%s%s%%1,%%0""6651:.popsection",
           begin,w==4U?"32":"64",middle,w==4U?"l":"q");
        if(written>0&&(size_t)written<sizeof(expect)&&
           strcmp(compact,expect)==0)return (unsigned int)w;
    }
    return 0U;
}


/* VMware vmcall/vmmcall GNU ABI has four independent 32-bit inputs and four
 * 32-bit outputs in EAX/ECX/EDX/EBX. These hypervisor opcodes are NEVER
 * interchangeable with CPUID or with an ordinary C function call. */
unsigned int minic_x86_64_linux_vmware_hypercall_form(
    const char *text, size_t length) {
    if (text && length == 6U && memcmp(text, "vmcall", 6U) == 0) return 1U;
    if (text && length == 7U && memcmp(text, "vmmcall", 7U) == 0) return 2U;
    if (minic_x86_64_compact_asm_equals(text, length, "inl(%%dx),%%eax")) return 3U;
    return 0U;
}


/* 64-bit FNSAVE writes the 108-byte legacy x87 image and resets x87 state;
 * FWAIT must follow it to expose deferred faults before FRSTOR. */
bool minic_x86_64_linux_legacy_fnsave_form(
    const char *text, size_t length) {
    return minic_x86_64_compact_asm_equals(text, length, "fnsave%[fp];fwait") ||
           minic_x86_64_compact_asm_equals(text, length, "fnsave%0;fwait");
}


/* x86 Linux kexec resets DS/ES/SS/FS/GS together, from the same GNU 'a'
 * selector input, on the identity-mapped boot path. */
bool minic_x86_64_linux_kexec_load_segments_form(
    const char *text, size_t length) {
    return minic_x86_64_compact_asm_equals(text, length,
        "movl%0,%%dsmovl%0,%%esmovl%0,%%ssmovl%0,%%fsmovl%0,%%gs");
}


/* EFI's final transition is a real indirect JMP, NOT an indirect call;
 * firmware's boot_params value must be in RSI on entry to the kernel image.
 * Accept no other indirect branch or ABI register assignment. */
bool minic_x86_64_linux_efi_kernel_transfer_form(
    const char *text, size_t length) {
    return minic_x86_64_compact_asm_equals(text, length, "jmp*%0");
}


/* Original Linux asm/msr.h Linux 6.6 __rdmsr/__wrmsr:
 * Only the EXACT 1: RDMSR/WRMSR; 2: original GNU exception table
 * (two PC-relative exception entries, fixup type 9/8) may be executed.
 * This is privileged machine ISA, never emulated by a plain C load. */
unsigned int minic_x86_64_linux_msr_form(const char *text,size_t len) {
    static const char *const exact[]={
      "1:rdmsr2:.pushsection\"__ex_table\",\"a\".balign4"
      ".long(1b)-..long(2b)-..long9.popsection",
      "1:wrmsr2:.pushsection\"__ex_table\",\"a\".balign4"
      ".long(1b)-..long(2b)-..long8.popsection",
      /* mce_rdmsrl: architectural-violation #MC fixup kind 13. */
      "1:rdmsr2:.pushsection\"__ex_table\",\"a\".balign4"
      ".long(1b)-..long(2b)-..long13.popsection",
      /* Linux 6.6 EX_TYPE_WRMSR_IN_MCE is 12; RDMSR_IN_MCE is 13. */
      "1:wrmsr2:.pushsection\"__ex_table\",\"a\".balign4"
      ".long(1b)-..long(2b)-..long12.popsection"
    };
    /* Source named operands are canonicalized to numeric %0 by Core.
     * Exact matching of both representations preserves the hardware and
     * full GNU exception-register fixup contract. */
    static const char safe_read[] = "1: rdmsr ; xor %[err],%[err]\n2:\n\t .pushsection \"__ex_table\",\"a\"\n .balign 4\n .long (1b) - .\n .long (2b) - .\n.macro extable_type_reg type:req reg:req\n.set .Lfound, 0\n.set .Lregnr, 0\n.irp rs,rax,rcx,rdx,rbx,rsp,rbp,rsi,rdi,r8,r9,r10,r11,r12,r13,r14,r15\n.ifc \\reg, %%\\rs\n.set .Lfound, .Lfound+1\n.long \\type + (.Lregnr << 8)\n.endif\n.set .Lregnr, .Lregnr+1\n.endr\n.set .Lregnr, 0\n.irp rs,eax,ecx,edx,ebx,esp,ebp,esi,edi,r8d,r9d,r10d,r11d,r12d,r13d,r14d,r15d\n.ifc \\reg, %%\\rs\n.set .Lfound, .Lfound+1\n.long \\type + (.Lregnr << 8)\n.endif\n.set .Lregnr, .Lregnr+1\n.endr\n.if (.Lfound != 1)\n.error \"extable_type_reg: bad register argument\"\n.endif\n.endm\nextable_type_reg reg=%[err], type=11 \n.purgem extable_type_reg\n .popsection\n";
    static const char safe_write[] = "1: wrmsr ; xor %[err],%[err]\n2:\n\t .pushsection \"__ex_table\",\"a\"\n .balign 4\n .long (1b) - .\n .long (2b) - .\n.macro extable_type_reg type:req reg:req\n.set .Lfound, 0\n.set .Lregnr, 0\n.irp rs,rax,rcx,rdx,rbx,rsp,rbp,rsi,rdi,r8,r9,r10,r11,r12,r13,r14,r15\n.ifc \\reg, %%\\rs\n.set .Lfound, .Lfound+1\n.long \\type + (.Lregnr << 8)\n.endif\n.set .Lregnr, .Lregnr+1\n.endr\n.set .Lregnr, 0\n.irp rs,eax,ecx,edx,ebx,esp,ebp,esi,edi,r8d,r9d,r10d,r11d,r12d,r13d,r14d,r15d\n.ifc \\reg, %%\\rs\n.set .Lfound, .Lfound+1\n.long \\type + (.Lregnr << 8)\n.endif\n.set .Lregnr, .Lregnr+1\n.endr\n.if (.Lfound != 1)\n.error \"extable_type_reg: bad register argument\"\n.endif\n.endm\nextable_type_reg reg=%[err], type=10 \n.purgem extable_type_reg\n .popsection\n";
    static const char canonical_read[] = "1: rdmsr ; xor %0,%0\n2:\n\t .pushsection \"__ex_table\",\"a\"\n .balign 4\n .long (1b) - .\n .long (2b) - .\n.macro extable_type_reg type:req reg:req\n.set .Lfound, 0\n.set .Lregnr, 0\n.irp rs,rax,rcx,rdx,rbx,rsp,rbp,rsi,rdi,r8,r9,r10,r11,r12,r13,r14,r15\n.ifc \\reg, %%\\rs\n.set .Lfound, .Lfound+1\n.long \\type + (.Lregnr << 8)\n.endif\n.set .Lregnr, .Lregnr+1\n.endr\n.set .Lregnr, 0\n.irp rs,eax,ecx,edx,ebx,esp,ebp,esi,edi,r8d,r9d,r10d,r11d,r12d,r13d,r14d,r15d\n.ifc \\reg, %%\\rs\n.set .Lfound, .Lfound+1\n.long \\type + (.Lregnr << 8)\n.endif\n.set .Lregnr, .Lregnr+1\n.endr\n.if (.Lfound != 1)\n.error \"extable_type_reg: bad register argument\"\n.endif\n.endm\nextable_type_reg reg=%0, type=11 \n.purgem extable_type_reg\n .popsection\n";
    static const char canonical_write[] = "1: wrmsr ; xor %0,%0\n2:\n\t .pushsection \"__ex_table\",\"a\"\n .balign 4\n .long (1b) - .\n .long (2b) - .\n.macro extable_type_reg type:req reg:req\n.set .Lfound, 0\n.set .Lregnr, 0\n.irp rs,rax,rcx,rdx,rbx,rsp,rbp,rsi,rdi,r8,r9,r10,r11,r12,r13,r14,r15\n.ifc \\reg, %%\\rs\n.set .Lfound, .Lfound+1\n.long \\type + (.Lregnr << 8)\n.endif\n.set .Lregnr, .Lregnr+1\n.endr\n.set .Lregnr, 0\n.irp rs,eax,ecx,edx,ebx,esp,ebp,esi,edi,r8d,r9d,r10d,r11d,r12d,r13d,r14d,r15d\n.ifc \\reg, %%\\rs\n.set .Lfound, .Lfound+1\n.long \\type + (.Lregnr << 8)\n.endif\n.set .Lregnr, .Lregnr+1\n.endr\n.if (.Lfound != 1)\n.error \"extable_type_reg: bad register argument\"\n.endif\n.endm\nextable_type_reg reg=%0, type=10 \n.purgem extable_type_reg\n .popsection\n";
    if(text && len==sizeof(safe_read)-1U &&
       memcmp(text,safe_read,sizeof(safe_read)-1U)==0)return 3U;
    if(text && len==sizeof(safe_write)-1U &&
       memcmp(text,safe_write,sizeof(safe_write)-1U)==0)return 4U;
    if(text && len==sizeof(canonical_read)-1U &&
       memcmp(text,canonical_read,sizeof(canonical_read)-1U)==0)return 3U;
    if(text && len==sizeof(canonical_write)-1U &&
       memcmp(text,canonical_write,sizeof(canonical_write)-1U)==0)return 4U;
    char norm[256];size_t i,n=0U;
    if(!text||!len||len>=sizeof(norm))return 0U;
    for(i=0U;i<len;++i){
       const char c=text[i];
       if(c==' '||c==9||c==10||c==13)continue;
       if(n+1U>=sizeof(norm))return 0U;
       norm[n++]=c;
    }
    norm[n]=0;
    for(i=0U;i<4U;++i)
        if(strcmp(norm,exact[i])==0)return i==2U?5U:i==3U?6U:(unsigned int)i+1U;
    return 0U;
}


/* Linux checksum_64.h ip_fast_csum: full original add/adc/loop/CF chain.
 * This machine form changes output pointer and header-length registers too;
 * it is not the simpler two-input csum_fold contract. */
bool minic_x86_64_ip_fast_csum_template(const char *text, size_t length) {
    static const char expected[] =
        "movl(%1),%0subl$4,%2jbe2faddl4(%1),%0adcl8(%1),%0"
        "adcl12(%1),%01:adcl16(%1),%0lea4(%1),%1decl%2jne1b"
        "adcl$0,%0movl%0,%2shrl$16,%0addw%w2,%w0adcl$0,%0notl%02:";
    char compact[256]; size_t i, n = 0U;
    if (!text || !length || length >= sizeof(compact)) return false;
    for (i=0U;i<length;++i) {
        const char c=text[i];
        if (c==' ' || c=='\n' || c=='\t' || c=='\r') continue;
        if (n+1U>=sizeof(compact)) return false;
        compact[n++]=c;
    }
    compact[n]='\0';
    return strcmp(compact, expected)==0;
}


/* Original Linux x86 checksum_64.h csum_fold: 32-bit ADD then ADC
 * with a tied initialization operand; the real CF hardware effect is
 * indivisible. This strict ISA contract handles the function family. */
bool minic_x86_64_checksum_fold_template(const char *text,size_t len){
    static const char *const expected="addl%1,%0adcl$0xffff,%0";
    char compact[80];size_t i,n=0U;
    if(!text||!len||len>=sizeof(compact))return false;
    for(i=0U;i<len;++i){
        const char c=text[i];
        if(c==' '||c==9||c==10||c==13)continue;
        if(c==';'||n+1U>=sizeof(compact))return false;
        compact[n++]=c;
    }
    compact[n]=0;
    return strcmp(compact,expected)==0;
}


/* One typed Linux x86 per-CPU GS instruction catalog.
 * Matches the original arch/x86/include/asm/percpu.h GNU operand modes.
 * Only the target owns the GS segment and width/ALU instruction names. */
typedef struct {
    const char *name;
    unsigned int op;  /* read=0 write=1 inc=2 dec=3 add=4 sub=5
                         and=6 or=7 xor=8 */
    size_t width;
} MinicX86PerCpuForm;
static const MinicX86PerCpuForm x86_percpu_forms[]={
    {"mov",0U,1U},{"mov",0U,2U},{"mov",0U,4U},{"mov",0U,8U},
    {"mov",1U,1U},{"mov",1U,2U},{"mov",1U,4U},{"mov",1U,8U},
    {"inc",2U,1U},{"inc",2U,2U},{"inc",2U,4U},{"inc",2U,8U},
    {"dec",3U,1U},{"dec",3U,2U},{"dec",3U,4U},{"dec",3U,8U},
    {"add",4U,1U},{"add",4U,2U},{"add",4U,4U},{"add",4U,8U},
    {"sub",5U,1U},{"sub",5U,2U},{"sub",5U,4U},{"sub",5U,8U},
    {"and",6U,1U},{"and",6U,2U},{"and",6U,4U},{"and",6U,8U},
    {"or",7U,1U},{"or",7U,2U},{"or",7U,4U},{"or",7U,8U},
    {"xor",8U,1U},{"xor",8U,2U},{"xor",8U,4U},{"xor",8U,8U}
};
bool minic_x86_64_linux_percpu_form(const char *text,size_t length,
    unsigned int *op,size_t *width){
    char compact[128],exact[128];size_t i,j=0U;
    if(!text||!length||length>=sizeof(compact)||!op||!width)
        return false;
    for(i=0U;i<length;++i){
        const char c=text[i];
        if(c==' '||c==9||c==10||c==13)continue;
        if(c==';'||j+1U>=sizeof(compact))return false;
        compact[j++]=c;
    }
    compact[j]=0;
    for(i=0U;i<sizeof(x86_percpu_forms)/sizeof(x86_percpu_forms[0]);++i){
        const MinicX86PerCpuForm *f=&x86_percpu_forms[i];
        const char suffix=f->width==1U?'b':f->width==2U?'w':
                          f->width==4U?'l':'q';
        int n;
        if(f->op==0U)
            n=snprintf(exact,sizeof(exact),"%s%c%%%%gs:%%1,%%0",
                       f->name,suffix);
        else if(f->op==2U||f->op==3U)
            n=snprintf(exact,sizeof(exact),"%s%c%%%%gs:%%0",
                       f->name,suffix);
        else
            n=snprintf(exact,sizeof(exact),"%s%c%%1,%%%%gs:%%0",
                       f->name,suffix);
        if(n>0&&(size_t)n<sizeof(exact)&&strcmp(compact,exact)==0){
            *op=f->op;*width=f->width;return true;
        }
        /* Linux original C source uses named %[var]/%[val].
         * Numeric templates also arise in exact preprocessed .i.
         * Check the complete source shape, not an instruction prefix. */
        if(f->op==0U)
            n=snprintf(exact,sizeof(exact),
                       "%s%c%%%%gs:%%[var],%%[val]",f->name,suffix);
        else if(f->op==2U||f->op==3U)
            n=snprintf(exact,sizeof(exact),
                       "%s%c%%%%gs:%%[var]",f->name,suffix);
        else
            n=snprintf(exact,sizeof(exact),
                       "%s%c%%[val],%%%%gs:%%[var]",f->name,suffix);
        if(n>0&&(size_t)n<sizeof(exact)&&strcmp(compact,exact)==0){
            *op=f->op;*width=f->width;return true;
        }
    }
    /* percpu_stable_op() names the address with GNU %P and constrains it
     * with "p", not "m": it is a pointer-register input, not a memory
     * lvalue. Its exact text is already validated by the stable catalog. */
    *width = minic_x86_64_asm_percpu_stable_width(text,length);
    if (*width != 0U) {
        *op = 0U;
        return true;
    }
    return false;
}


/* Kernel this_cpu_ptr: a live register seed combined with the GS-relative
 * per-CPU displacement, using GNU tied output 0 / memory input 1. Unlike
 * the 36 direct GS lvalue operations, the output is an ordinary C scalar.
 * One exact semantic descriptor covers 5 ALU ops x 32/64-bit slots and
 * optionally their explicit size suffix; no generic GS passthrough. */
unsigned int minic_x86_64_gs_tied_alu_form(
    const char *text, size_t length, size_t *width) {
    static const char *const forms[]={"add","sub","and","or","xor"};
    char compact[80], expected[80];
    size_t i, n = 0U, op, w;
    if (!text || !length || length >= sizeof(compact) || !width)
        return 0U;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t') continue;
        if (c == '\n' || c == '\r' || c == ';' || c == '\0' ||
            n + 1U >= sizeof(compact))
            return 0U;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (op = 0U; op < 5U; ++op)
        for (w = 4U; w <= 8U; w += 4U) {
            const char suffix = w == 4U ? 'l' : 'q';
            int size = snprintf(expected,sizeof(expected),
                                "%s%%%%gs:%%1,%%0",forms[op]);
            if (size > 0 && (size_t)size < sizeof(expected) &&
                strcmp(compact,expected)==0) {
                /* Unsuffixed GNU ALU operand width is selected by the
                 * output register class, validated independently below. */
                *width=0U;
                return (unsigned int)(op+1U);
            }
            size=snprintf(expected,sizeof(expected),
                          "%s%c%%%%gs:%%1,%%0",forms[op],suffix);
            if(size>0&&(size_t)size<sizeof(expected)&&
               strcmp(compact,expected)==0) {
                *width=w;
                return (unsigned int)(op+1U);
            }
        }
    return 0U;
}


/* GNU Linux PREFETCHW ALTERNATIVE: preserve the original 6.6.143
 * .altinstructions plus .altinstr_replacement ELF patching contract,
 * including both fallback and replacement native machine opcodes. */
/* x86 Linux 6.6 native APIC MMIO and AMD divider-clear CPU alternatives.
 * The source's ELF alternatives are NOT equivalent to executing either
 * machine instruction unconditionally: the first is fallback code, and
 * the second lives only in .altinstr_replacement. Match the entire GNU
 * alternative (feature keys, padding, sections, labels) without accepting
 * a nearby but semantically different asm template. */
unsigned int minic_x86_64_linux_apic_amd_alt_form(const char *s,size_t len) {
    static const struct { const char *first,*replacement,*feature; } modes[] = {
        {"661:movl%0,%P1662:", "6641:xchgl%0,%P16651:.popsection", "(22*32+(5))"},
        {"661:662:", "6641:div%26651:.popsection", "(22*32+(1*32+1))"}
    };
    static const char body[] =
        ".skip-(((6651f-6641f)-(662b-661b))>0)*"
        "((6651f-6641f)-(662b-661b)),0x90"
        "663:.pushsection.altinstructions,\"a\""
        ".long661b-..long6641f-."
        ".4byte%s"
        ".byte663b-661b.byte6651f-6641f.popsection"
        ".pushsection.altinstr_replacement,\"ax\"";
    char compact[1024],expected[1024];size_t i,n=0U;
    if(!s||!len||len>=sizeof(compact))return 0U;
    for(i=0U;i<len;++i){
        const char ch=s[i];
        if(ch=='#'){while(i<len&&s[i]!='\n')++i;continue;}
        if(ch==' '||ch=='\t'||ch=='\r'||ch=='\n')continue;
        if(ch=='\0'||n+1U>=sizeof(compact))return 0U;
        compact[n++]=ch;
    }
    compact[n]='\0';
    for(i=0U;i<sizeof(modes)/sizeof(modes[0]);++i){
        char metadata[800];
        const int metadata_count=snprintf(metadata,sizeof(metadata),body,
                                           modes[i].feature);
        const int count=metadata_count>0&&(size_t)metadata_count<sizeof(metadata)
            ? snprintf(expected,sizeof(expected),"%s%s%s",
                       modes[i].first,metadata,modes[i].replacement) : -1;
        if(count>0&&(size_t)count<sizeof(expected)&&
           strcmp(compact,expected)==0)return (unsigned int)(i+1U);
    }
    return 0U;
}


bool minic_x86_64_linux_prefetchw_alt_form(const char *s,size_t length) {
    static const char expected[]=
        "661:prefetcht0%P1662:"
        ".skip-(((6651f-6641f)-(662b-661b))>0)*"
        "((6651f-6641f)-(662b-661b)),0x90"
        "663:.pushsection.altinstructions,\"a\""
        ".long661b-..long6641f-."
        ".4byte(6*32+8)"
        ".byte663b-661b.byte6651f-6641f.popsection"
        ".pushsection.altinstr_replacement,\"ax\""
        "6641:prefetchw%P16651:.popsection";
    char text[1024];size_t i,n=0U;
    if(!s||!length||length>=sizeof(text))return false;
    for(i=0U;i<length;++i) {
        const char c=s[i];
        if(c=='#'){while(i<length&&s[i]!='\n')++i;continue;}
        if(c==' '||c=='\t'||c=='\r'||c=='\n')continue;
        if(c=='\0'||n+1U>=sizeof(text))return false;
        text[n++]=c;
    }
    text[n]='\0';
    return strcmp(text,expected)==0;
}


/* Original Linux checksum_64.h add32_with_carry GNU tied "0" input
 * and second register-or-memory operand. Same shared arithmetic effect
 * family as csum_fold, but a DIFFERENT exact GNU constraint contract:
 *   =r output, 0 initial, rm added; ADD then ADC with live CF. */
bool minic_x86_64_add32_carry_template(const char *s,size_t len){
    static const char text[]="addl%2,%0adcl$0,%0";
    char norm[80];size_t i,n=0U;
    if(!s||!len||len>=sizeof(norm))return false;
    for(i=0U;i<len;++i){
        const char c=s[i];
        if(c==' '||c==9||c==10||c==13)continue;
        if(c==';'||n+1U>=sizeof(norm))return false;
        norm[n++]=c;
    }
    norm[n]=0;
    return strcmp(norm,text)==0;
}

/* TCP/UDP pseudoheader addition requires the complete carry chain, not four
 * independent additions. This is a distinct GNU contract from csum_add:
 * three g inputs and a tied fourth input initializing the output register. */
bool minic_x86_64_linux_tcpudp_nofold_template(const char *s, size_t len) {
    return minic_x86_64_compact_asm_equals(
        s, len, "addl%1,%0adcl%2,%0adcl%3,%0adcl$0,%0");
}


bool minic_x86_64_asm_mulacc_template(const char *template_text,
                                       size_t template_length) {
    static const char expected[] = "mulq%7addq%%rax,%0adcq%%rdx,%1adcq$0,%2";
    char text[128];
    size_t i, j = 0U;
    if (template_length >= sizeof(text)) return false;
    for (i = 0U; i < template_length; ++i) {
        const char c = template_text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        text[j++] = c;
    }
    text[j] = '\0';
    return strcmp(text, expected) == 0;
}

/* Original Linux mul_u64_u64_div_u64:
 *   asm("mulq %2; divq %3" : "=a"(q)
 *       : "a"(a), "rm"(mul), "rm"(div) : "rdx");
 * Admit one exact machine sequence, never generic executable template
 * passthrough with unchecked physical clobbers. The machine emitter chooses
 * the register alternative for both rm inputs and keeps RDX out of inputs. */
bool minic_x86_64_asm_muldiv_template(const char *text, size_t length) {
    static const char expected[] = "mulq%2;divq%3";
    char normalized[64];
    size_t i, j = 0U;
    if (!text || length >= sizeof(normalized)) return false;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        normalized[j++] = c;
    }
    normalized[j] = '\0';
    return strcmp(normalized, expected) == 0;
}

/* Exact original GNU Linux __raw_try_cmpxchg 8-case source.
 * Linux CC_SET(z) must remain a real hardware flag output, not C logic. */
static const MinicX86TryCasDesc x86_trycas_forms[] = {
    {"cmpxchgb", "%r10b", 1U, true},
    {"cmpxchgw", "%r10w", 2U, true},
    {"cmpxchgl", "%r10d", 4U, true},
    {"cmpxchgq", "%r10", 8U, true},
    {"cmpxchgb", "%r10b", 1U, false},
    {"cmpxchgw", "%r10w", 2U, false},
    {"cmpxchgl", "%r10d", 4U, false},
    {"cmpxchgq", "%r10", 8U, false}
};
const MinicX86TryCasDesc *minic_x86_64_trycas_desc(MinicX86TryCas form) {
    return form>=MINIC_X86_TRYCAS_B_LOCK&&form<=MINIC_X86_TRYCAS_Q_LOCAL ?
        &x86_trycas_forms[(size_t)form-1U]:NULL;
}
MinicX86TryCas minic_x86_64_trycas_form(const char *text,size_t len) {
    static const char smp[]=
        ".pushsection.smp_locks,\"a\".balign4.long671f-..popsection671:lock;";
    char compact[256],expected[256];
    size_t i,n=0U,mode,variant;
    if(!text||!len||len>=sizeof(compact))return MINIC_X86_TRYCAS_NONE;
    for(i=0U;i<len;++i){
        const char c=text[i];
        if(c==' '||c=='\t'||c=='\n'||c=='\r')continue;
        if(n+1U>=sizeof(compact))return MINIC_X86_TRYCAS_NONE;
        compact[n++]=c;
    }
    compact[n]='\0';
    for(mode=0U;mode<sizeof(x86_trycas_forms)/sizeof(x86_trycas_forms[0]);++mode)
        for(variant=0U;variant<2U;++variant) {
            const MinicX86TryCasDesc *d=&x86_trycas_forms[mode];
            const int written=snprintf(expected,sizeof(expected),
                "%s%s%s,%s/*outputconditioncodez*/",
                d->locked?smp:"",d->op,
                variant?"%3":"%[new]",variant?"%1":"%[ptr]");
            if(written>0&&(size_t)written<sizeof(expected)&&
               strcmp(compact,expected)==0)
                return (MinicX86TryCas)(mode+1U);
        }
    return MINIC_X86_TRYCAS_NONE;
}

/* Genuine Linux 6.6.143 arch_cmpxchg (not try_cmpxchg):
 * .smp_locks metadata; lock cmpxchgb/w/l/q %2,%1;
 * outputs =a and +m, input new q/r, tied old "0". One physical ISA
 * descriptor shared with the pre-existing flag-return trycas family. */
MinicX86TryCas minic_x86_64_raw_cmpxchg_form(
    const char *text, size_t length) {
    static const char smp[] =
        ".pushsection.smp_locks,\"a\".balign4.long671f-..popsection671:lock;";
    char compact[256], expected[256];
    size_t i, n = 0U, form;
    if (text == NULL || length == 0U || length >= sizeof(compact))
        return MINIC_X86_TRYCAS_NONE;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (n + 1U >= sizeof(compact)) return MINIC_X86_TRYCAS_NONE;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (form = 0U; form < sizeof(x86_trycas_forms) /
                            sizeof(x86_trycas_forms[0]); ++form) {
        const MinicX86TryCasDesc *d = &x86_trycas_forms[form];
        int written = snprintf(expected, sizeof(expected), "%s%s%%2,%%1",
                               d->locked ? smp : "", d->op);
        if (written > 0 && (size_t)written < sizeof(expected) &&
            strcmp(expected, compact) == 0)
            return (MinicX86TryCas)(form + 1U);
    }
    return MINIC_X86_TRYCAS_NONE;
}


/* Original Linux asm/shared/io.h: target-owned 1/2/4 byte privileged port
 * input and output. Unknown suffixes never flow into the GNU assembler. */
static const MinicX86PortIoDesc x86_port_io[]={
    {"outb",1U,false},{"outw",2U,false},{"outl",4U,false},
    {"inb",1U,true},{"inw",2U,true},{"inl",4U,true}
};
const MinicX86PortIoDesc *minic_x86_64_port_io_description(
    MinicX86PortIo form){
    const size_t n=(size_t)form;
    return n>0U&&n<=sizeof(x86_port_io)/sizeof(x86_port_io[0])?
        &x86_port_io[n-1U]:NULL;
}
MinicX86PortIo minic_x86_64_port_io_form(const char *s,size_t len){
    static const char *const names[]={
        "outb%b0,%w1","outw%w0,%w1","outl%0,%w1",
        "inb%w1,%b0","inw%w1,%w0","inl%w1,%0"
    };
    char compact[64];size_t i,j=0U;
    if(!s||!len||len>=sizeof(compact))return MINIC_X86_PORT_IO_NONE;
    for(i=0U;i<len;++i){
        const char c=s[i];
        if(c==' '||c==9||c==10||c==13)continue;
        if(j+1U>=sizeof(compact))return MINIC_X86_PORT_IO_NONE;
        compact[j++]=c;
    }
    compact[j]=0;
    for(i=0U;i<sizeof(names)/sizeof(names[0]);++i)
        if(strcmp(compact,names[i])==0)return (MinicX86PortIo)(i+1U);
    return MINIC_X86_PORT_IO_NONE;
}


/* Original Linux 6.6.x x86 atomic RMW: one ISA owner for binary
 * ADD/SUB/AND/OR/XOR (32/64, SMP/local), unary INC/DEC (32/64), and
 * the 64-bit =m output + separate m-input form of ADD/SUB/INC/DEC.
 * The latter is a REAL hardware read/write even though GNU spells the
 * output write-only; the explicit m-input is an ordering dependency. */
static const MinicX86AtomicRmwDesc x86_linux_atomic_rmw[] = {
    {"addl",4U,false,false}, {"subl",4U,false,false},
    {"andl",4U,false,false}, {"orl",4U,false,false},
    {"xorl",4U,false,false},
    {"addq",8U,false,false}, {"subq",8U,false,false},
    {"andq",8U,false,false}, {"orq",8U,false,false},
    {"xorq",8U,false,false},
    {"addl",4U,true,false}, {"subl",4U,true,false},
    {"andl",4U,true,false}, {"orl",4U,true,false},
    {"xorl",4U,true,false},
    {"addq",8U,true,false}, {"subq",8U,true,false},
    {"andq",8U,true,false}, {"orq",8U,true,false},
    {"xorq",8U,true,false},
    {"incl",4U,true,true}, {"decl",4U,true,true},
    {"incq",8U,true,true}, {"decq",8U,true,true},
    {"incl",4U,false,true}, {"decl",4U,false,true},
    {"incq",8U,false,true}, {"decq",8U,false,true}
};
const MinicX86AtomicRmwDesc *minic_x86_64_atomic_rmw_description(
    MinicX86AtomicRmw form) {
    const size_t index=(size_t)form;
    return index>0U &&
           index<=sizeof(x86_linux_atomic_rmw)/sizeof(x86_linux_atomic_rmw[0])
        ? &x86_linux_atomic_rmw[index-1U] : NULL;
}
MinicX86AtomicRmw minic_x86_64_atomic_rmw_form(const char *text,size_t len) {
    static const char smp[]=
        ".pushsection.smp_locks,\"a\".balign4.long671f-..popsection671:lock;";
    char compact[256], expected[256];
    size_t i,j=0U,k,variant;
    if(!text||!len||len>=sizeof(compact))return MINIC_X86_ATOMIC_RMW_NONE;
    for(i=0U;i<len;++i){
        const char c=text[i];
        if(c==' '||c==9||c==10||c==13)continue;
        if(j+1U>=sizeof(compact))return MINIC_X86_ATOMIC_RMW_NONE;
        compact[j++]=c;
    }
    compact[j]=0;
    for(k=0U;k<sizeof(x86_linux_atomic_rmw)/
                    sizeof(x86_linux_atomic_rmw[0]);++k){
        const MinicX86AtomicRmwDesc *d=&x86_linux_atomic_rmw[k];
        for(variant=0U;variant<2U;++variant){
            const int n=d->unary?
                snprintf(expected,sizeof(expected),"%s%s%s",
                    d->locked?smp:"",d->opcode,variant?"%[var]":"%0"):
                snprintf(expected,sizeof(expected),"%s%s%s,%s",
                    d->locked?smp:"",d->opcode,
                    variant?"%[val]":"%1",variant?"%[var]":"%0");
            if(n>0&&(size_t)n<sizeof(expected) &&
               strcmp(compact,expected)==0)
                return (MinicX86AtomicRmw)(k+1U);
        }
    }
    return MINIC_X86_ATOMIC_RMW_NONE;
}


/* Original GNU Linux SMP xadd (1/2/4/8): 671 patch section plus LOCK.
 * This ABI/ISA contract is separate from non-LOCK local_add_return. */
static size_t x86_linux_smp_xadd_width(const char *text,size_t len) {
    static const char *const forms[] = {
        ".pushsection.smp_locks,\"a\".balign4.long671f-..popsection671:"
        "lock;xaddb%b0,%1",
        ".pushsection.smp_locks,\"a\".balign4.long671f-..popsection671:"
        "lock;xaddw%w0,%1",
        ".pushsection.smp_locks,\"a\".balign4.long671f-..popsection671:"
        "lock;xaddl%0,%1",
        ".pushsection.smp_locks,\"a\".balign4.long671f-..popsection671:"
        "lock;xaddq%q0,%1"
    };
    char compact[256]; size_t i,n=0U;
    if(!text||!len||len>=sizeof(compact))return 0U;
    for(i=0U;i<len;++i) {
        const char c=text[i];
        if(c==' '||c=='\n'||c=='\t'||c=='\r')continue;
        if(n+1U>=sizeof(compact))return 0U;
        compact[n++]=c;
    }
    compact[n]='\0';
    for(i=0U;i<4U;++i)
        if(strcmp(compact,forms[i])==0)return (size_t)1U<<i;
    return 0U;
}


/* Linux 6.6.143 arch/x86/include/asm/local.h local_add_return:
 * register +r receives the PREVIOUS memory value after bare xaddq; +m
 * is the true hardware RMW lvalue. Core uses existing typed operand roles.
 * This target-owned ISA contract must never widen into generic ASM text. */
static size_t x86_linux_local_xadd_width(const char *text, size_t len) {
    char compact[64];
    size_t i, n = 0U;
    if (text == NULL || len == 0U || len >= sizeof(compact)) return 0U;
    for (i = 0U; i < len; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (n + 1U >= sizeof(compact)) return 0U;
        compact[n++] = c;
    }
    compact[n] = '\0';
    if (strcmp(compact, "xaddq%0,%1;") == 0) return 8U;
    if (strcmp(compact, "xaddl%0,%1;") == 0) return 4U;
    return 0U;
}


/* XADD against GS-relative per-CPU memory returns the previous value in the
 * register lvalue and updates the per-CPU slot, exactly as the direct-memory
 * Linux local XADD does. The GS segment belongs solely to this x86 target. */
static size_t x86_linux_percpu_xadd_width(const char *text, size_t length) {
    static const char *const patterns[] = {
        "xaddb%0,%%gs:%1", "xaddw%0,%%gs:%1",
        "xaddl%0,%%gs:%1", "xaddq%0,%%gs:%1",
        "xaddb%[tmp],%%gs:%[var]", "xaddw%[tmp],%%gs:%[var]",
        "xaddl%[tmp],%%gs:%[var]", "xaddq%[tmp],%%gs:%[var]"
    };
    static const size_t widths[] = {1U, 2U, 4U, 8U, 1U, 2U, 4U, 8U};
    char compact[80];
    size_t i, n = 0U;
    if (!text || length == 0U || length >= sizeof(compact)) return 0U;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return 0U;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(widths)/sizeof(widths[0]); ++i)
        if (strcmp(compact, patterns[i]) == 0) return widths[i];
    return 0U;
}


/* GNU __this_cpu_cmpxchg: the +a carrier starts with the expected value;
 * cmpxchg updates GS-relative memory only on equality and returns the
 * observed previous memory value in that same carrier. Keep the memory
 * operand separate from an ordinary C dereference: %gs is architectural. */
static size_t x86_linux_gs_cmpxchg_loop_width(
    const char *text, size_t length);
static size_t x86_linux_gs_cmpxchg_width(const char *text, size_t length) {
    static const char *const symbolic[] = {
        "cmpxchgb%[nval],%%gs:%[var]", "cmpxchgw%[nval],%%gs:%[var]",
        "cmpxchgl%[nval],%%gs:%[var]", "cmpxchgq%[nval],%%gs:%[var]"
    };
    static const char *const numeric[] = {
        "cmpxchgb%2,%%gs:%1", "cmpxchgw%2,%%gs:%1",
        "cmpxchgl%2,%%gs:%1", "cmpxchgq%2,%%gs:%1"
    };
    static const size_t widths[] = {1U, 2U, 4U, 8U};
    size_t i;
    for (i = 0U; i < 4U; ++i)
        if (minic_x86_64_compact_asm_equals(text, length, symbolic[i]) ||
            minic_x86_64_compact_asm_equals(text, length, numeric[i]))
            return widths[i];
    return 0U;
}

static size_t x86_linux_gs_cmpxchg_loop_width(
    const char *text, size_t length) {
    static const char *const opcodes[] = {
        "cmpxchgb", "cmpxchgw", "cmpxchgl", "cmpxchgq"
    };
    size_t i;
    for (i = 0U; i < 4U; ++i) {
        char expected[128];
        int n = snprintf(expected, sizeof(expected),
            "mov%c%%%%gs:%%[var],%%[oval]1:%s%%[nval],%%%%gs:%%[var]jnz1b",
            "bwlq"[i], opcodes[i]);
        if (n > 0 && (size_t)n < sizeof(expected) &&
            minic_x86_64_compact_asm_equals(text, length, expected))
            return (size_t)(1U << i);
        n = snprintf(expected, sizeof(expected),
            "mov%c%%%%gs:%%1,%%01:%s%%2,%%%%gs:%%1jnz1b",
            "bwlq"[i], opcodes[i]);
        if (n > 0 && (size_t)n < sizeof(expected) &&
            minic_x86_64_compact_asm_equals(text, length, expected))
            return (size_t)(1U << i);
    }
    return 0U;
}

/* Source qualification and machine lowering converge on this one typed
 * decision; the variant also owns lock metadata, segment override, and the
 * retry-loop distinction. Unknown templates stay unadmitted. */
MinicX86GsAtomicPlan minic_x86_64_gs_atomic_plan(
    const char *text, size_t length) {
    MinicX86GsAtomicPlan plan = {MINIC_X86_GS_ATOMIC_NONE, 0U};
    if (!text || !length) return plan;
    if ((plan.width = x86_linux_smp_xadd_width(text, length)) != 0U)
        plan.kind = MINIC_X86_GS_ATOMIC_XADD_SMP;
    else if ((plan.width = x86_linux_local_xadd_width(text, length)) != 0U)
        plan.kind = MINIC_X86_GS_ATOMIC_XADD_LOCAL;
    else if ((plan.width = x86_linux_percpu_xadd_width(text, length)) != 0U)
        plan.kind = MINIC_X86_GS_ATOMIC_XADD_PERCPU;
    else if ((plan.width = x86_linux_gs_cmpxchg_width(text, length)) != 0U)
        plan.kind = MINIC_X86_GS_ATOMIC_CMPXCHG;
    else if ((plan.width = x86_linux_gs_cmpxchg_loop_width(text, length)) != 0U)
        plan.kind = MINIC_X86_GS_ATOMIC_CMPXCHG_RETRY;
    else plan.width = 0U;
    return plan;
}


/* Linux memset16/32/64: two early-clobbered fixed register results and
 * three GNU operands (A, tied D, tied C). A REP string operation mutates
 * both RDI and RCX, so a cosmetic "rep stos" emission is not sufficient. */
size_t minic_x86_64_linux_rep_stos_width(const char *text, size_t length) {
    if (minic_x86_64_compact_asm_equals(text, length, "repstosw")) return 2U;
    if (minic_x86_64_compact_asm_equals(text, length, "repstosl")) return 4U;
    if (minic_x86_64_compact_asm_equals(text, length, "repstosq")) return 8U;
    return 0U;
}


/* One exact x86 target table for independent Linux privileged/atomic and
 * non-temporal memory forms. Only instruction and GNU operand contracts are
 * arch-owned; Core continues carrying generic memory/scalar effects. */
unsigned int minic_x86_64_linux_lowlevel_scalar_form(
    const char *text, size_t length) {
    static const char *const exact[] = {
        "hlt", "lldt%w0", "sldt%0", "mov%0,%%fs",
        "invpcid%[desc],%[type]", "btcq%1,%0",
        "movntil%1,%0", "movntiq%1,%0"
    };
    size_t i;
    for (i = 0U; i < sizeof(exact)/sizeof(exact[0]); ++i)
        if (minic_x86_64_compact_asm_equals(text, length, exact[i]))
            return (unsigned int)(i + 1U);
    if (minic_x86_64_compact_asm_equals(text, length, "invpcid%0,%1"))
        return 5U;
    return 0U;
}


/* Complete compressed-boot cpuflags grammar: CF bit test, bit set, CPUID
 * register ties, and hardware EFLAGS probing. Do not silently lower these
 * architecture side effects as ordinary C integer operators. */
unsigned int minic_x86_64_linux_boot_flags_form(
    const char *text, size_t length) {
    if (minic_x86_64_compact_asm_equals(
            text, length, "btl%2,%1/*outputconditioncodec*/")) return 1U;
    if (minic_x86_64_compact_asm_equals(text, length, "btsl%1,%0")) return 2U;
    if (minic_x86_64_compact_asm_equals(text, length, "cpuid")) return 3U;
    if (minic_x86_64_compact_asm_equals(text, length,
            "pushfqpushfqpop%0mov%0,%1xor%2,%1push%1popfq"
            "pushfqpop%1popfq")) return 4U;
    return 0U;
}

/* `mull %%edx` in x86 __const_udelay uses two tied implicit operands:
 * GNU input "1" supplies EAX, input "0" supplies EDX.  The 32x32 unsigned
 * multiply yields the high 32 bits in EDX, which are written to the 64-bit
 * delay counter, and the low bits in the early-clobber EAX scratch output. */
bool minic_x86_64_linux_delay_mul_form(const char *text, size_t length) {
    return text && length == sizeof("mull %%edx") - 1U &&
           memcmp(text, "mull %%edx", length) == 0;
}

unsigned int minic_x86_64_linux_delay_wait_form(
    const char *text, size_t length) {
    if (minic_x86_64_compact_asm_equals(text, length,
        "test%0,%0jz3fjmp1f.align161:jmp2f.align16"
        "2:dec%0jnz2b3:dec%0")) return 1U;
    if (minic_x86_64_compact_asm_equals(text, length, "tpause%%ecx")) return 2U;
    return 0U;
}


/* The identity-mapping bootstrap intentionally dereferences the physical
 * address and consumes the result in R9 while declaring R9 clobbered.  GNU
 * 'g' must not be modeled as an address-only input or as a no-op barrier. */
bool minic_x86_64_linux_identity_load_form(const char *text, size_t length) {
    return minic_x86_64_compact_asm_equals(
               text, length, "mov%[address],%%r9") ||
           minic_x86_64_compact_asm_equals(text, length, "mov%0,%%r9");
}


bool minic_x86_64_linux_ipv6_checksum_form(const char *text, size_t length) {
    return minic_x86_64_compact_asm_equals(text, length,
             "addq(%[saddr]),%[sum]adcq8(%[saddr]),%[sum]"
             "adcq(%[daddr]),%[sum]adcq8(%[daddr]),%[sum]"
             "adcq$0,%[sum]") ||
           minic_x86_64_compact_asm_equals(text, length,
             "addq(%2),%0adcq8(%2),%0adcq(%3),%0"
             "adcq8(%3),%0adcq$0,%0");
}


/* Linux csum_partial has three related CF-preserving x86-64 operations:
 * register+register, five independent qword memory operands, and an explicit
 * base pointer with a GNU "m" alias region of 8/16/32 bytes.  Each complete
 * template is matched; a generic ADD/ADC prefix is not sufficient because
 * the final adcq $0 must consume the preceding carry bit. */
unsigned int minic_x86_64_linux_csum_partial_form(
    const char *text, size_t length) {
    static const char *const forms[] = {
        "addq%1,%0adcq$0,%0",
        "addq%1,%0adcq%2,%0adcq%3,%0adcq%4,%0adcq%5,%0adcq$0,%0",
        "addq0*8(%[src]),%[res]adcq1*8(%[src]),%[res]"
            "adcq2*8(%[src]),%[res]adcq3*8(%[src]),%[res]adcq$0,%[res]",
        "addq0*8(%[src]),%[res]adcq1*8(%[src]),%[res]adcq$0,%[res]",
        "addq0*8(%[src]),%[res]adcq$0,%[res]",
        "addq%[trail],%[res]adcq$0,%[res]",
        /* Core safely canonicalizes named GNU operands into positionals;
         * emit-time ISA matching must recognize the same exact instruction
         * stream without permitting additional templates at source. */
        "addq0*8(%1),%0adcq1*8(%1),%0adcq2*8(%1),%0"
            "adcq3*8(%1),%0adcq$0,%0",
        "addq0*8(%1),%0adcq1*8(%1),%0adcq$0,%0",
        "addq0*8(%1),%0adcq$0,%0"
    };
    size_t i;
    for (i = 0U; i < sizeof(forms) / sizeof(forms[0]); ++i) {
        if (minic_x86_64_compact_asm_equals(text, length, forms[i]))
            return (unsigned int)(i < 6U ? i + 1U : i - 3U);
    }
    return 0U;
}


/* Target-only descriptor for all 8 GNU Linux atomic RMWcc forms.
 * This is the only source/machine ALU and flag-sign table. */
static const struct {
    MinicX86AtomicCCDesc desc;
    const char *flag;
} x86_atomic_cc_forms[] = {
    {{"decl", 4U, false, false}, "e"},
    {{"incl", 4U, false, false}, "e"},
    {{"decq", 8U, false, false}, "e"},
    {{"incq", 8U, false, false}, "e"},
    {{"subl", 4U, true, false}, "e"},
    {{"addl", 4U, true, true}, "s"},
    {{"subq", 8U, true, false}, "e"},
    {{"addq", 8U, true, true}, "s"}
};
const MinicX86AtomicCCDesc *minic_x86_64_atomic_cc_description(
    MinicX86AtomicCC form) {
    const size_t id = (size_t)form;
    return id >= MINIC_X86_ATOMIC_CC_DEC32 &&
           id <= MINIC_X86_ATOMIC_CC_ADD64_SIGN ?
        &x86_atomic_cc_forms[id - 1U].desc : NULL;
}
/* Exact original GNU SMP patchtable + LOCK + operand + CC_SET text,
 * whether source-named or numeric after Common inline ASM normalization.
 * Never forward source ASM or any unrecognized suffix to GAS. */
MinicX86AtomicCC minic_x86_64_atomic_cc_form(const char *text, size_t len) {
    static const char prefix[] =
        ".pushsection.smp_locks,\"a\".balign4.long671f-..popsection671:lock;";
    char compact[256], expected[256];
    size_t i, n = 0U, id, variant, lock_variant;
    if (!text || len == 0U || len >= sizeof(compact))
        return MINIC_X86_ATOMIC_CC_NONE;
    for (i = 0U; i < len; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (n + 1U >= sizeof(compact)) return MINIC_X86_ATOMIC_CC_NONE;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (id = 0U; id < sizeof(x86_atomic_cc_forms) /
                       sizeof(x86_atomic_cc_forms[0]); ++id) {
        const MinicX86AtomicCCDesc *d = &x86_atomic_cc_forms[id].desc;
        for (variant = 0U; variant < 2U; ++variant)
          for (lock_variant = 0U; lock_variant < 2U; ++lock_variant) {
            const char *mem = variant ? "%0" : "%[var]";
            const char *value = variant ? "%2" : "%[val]";
            const char *prefix_text = lock_variant ? prefix : "";
            const int written = d->has_value ?
                snprintf(expected, sizeof(expected),
                    "%s%s%s,%s/*outputconditioncode%s*/",
                    prefix_text, d->opcode, value, mem,
                    x86_atomic_cc_forms[id].flag) :
                snprintf(expected, sizeof(expected),
                    "%s%s%s/*outputconditioncode%s*/",
                    prefix_text, d->opcode, mem,
                    x86_atomic_cc_forms[id].flag);
            if (written > 0 && (size_t)written < sizeof(expected) &&
                strcmp(compact, expected) == 0)
                return (MinicX86AtomicCC)(id + 1U);
        }
    }
    return MINIC_X86_ATOMIC_CC_NONE;
}

bool minic_x86_64_atomic_cc_locked_form(const char *text, size_t len) {
    static const char marker[] = ".smp_locks";
    size_t i;
    if (!text || minic_x86_64_atomic_cc_form(text, len) ==
                     MINIC_X86_ATOMIC_CC_NONE) return false;
    for (i = 0U; i + sizeof(marker) - 1U <= len; ++i)
        if (memcmp(text + i, marker, sizeof(marker) - 1U) == 0)
            return true;
    return false;
}

/* Queued spinlock bit-test and unlock-byte flag probes preserve both
 * SMP patch records and x86 carry/sign flag outputs. */
unsigned int minic_x86_64_linux_lock_bitcc_form(
    const char *text, size_t len) {
    static const char prefix[] =
        ".pushsection.smp_locks,\"a\".balign4.long671f-..popsection671:lock;";
    char compact[256];
    size_t i, n = 0U;
    if (!text || !len || len >= sizeof(compact)) return 0U;
    for (i = 0U; i < len; ++i) {
        if (text[i] == ' ' || text[i] == '\t' ||
            text[i] == '\n' || text[i] == '\r') continue;
        if (n + 1U >= sizeof(compact)) return 0U;
        compact[n++] = text[i];
    }
    compact[n] = '\0';
    if (strncmp(compact, prefix, sizeof(prefix)-1U) != 0) return 0U;
    if (strcmp(compact + sizeof(prefix)-1U,
        "btsl%[val],%[var]/*outputconditioncodec*/") == 0 ||
        strcmp(compact + sizeof(prefix)-1U,
        "btsl%2,%0/*outputconditioncodec*/") == 0) return 1U;
    if (strcmp(compact + sizeof(prefix)-1U,
        "andb%2,%1/*outputconditioncodes*/") == 0) return 2U;
    return 0U;
}


/* REPE CMPSB exposes ZF and modified pointers/count, DIVL consumes implicit
 * EDX:EAX, REP MOVSQ/MOVSB exports all three fixed-register outputs. */
unsigned int minic_x86_64_linux_boot_string_form(
    const char *text, size_t length) {
    if (minic_x86_64_compact_asm_equals(text, length,
        "repe;cmpsb/*outputconditioncodenz*/")) return 1U;
    if (minic_x86_64_compact_asm_equals(text, length, "divl%2")) return 2U;
    if (minic_x86_64_compact_asm_equals(text, length,
        "rep;movsqmovq%4,%%rcxrep;movsb")) return 3U;
    return 0U;
}


/* vDSO fallback syscall reads three fixed Linux ABI input registers and
 * produces RAX. Clock queries expose an additional =m timespec lvalue; the
 * gettimeofday variant carries a "memory" compiler barrier instead. */
bool minic_x86_64_linux_vdso_syscall_form(
    const char *text, size_t length) {
    return text && length == sizeof("syscall") - 1U &&
           memcmp(text, "syscall", length) == 0;
}


/* vDSO getcpu switches from its original LSL selector-limit read to RDPID
 * using the Linux alternative-instruction patch table (CPUID 16*32+22).
 * Match the whole table, lengths and opcode bytes, never a leading mnemonic. */
bool minic_x86_64_linux_vdso_cpunode_form(
    const char *text, size_t length) {
    static const char *const prefix =
        "#ALT:oldnstr661:";
    static const char *const suffix =
        "662:#ALT:padding"
        ".skip-(((6651f-6641f)-(662b-661b))>0)*"
        "((6651f-6641f)-(662b-661b)),0x90"
        "663:.pushsection.altinstructions,\"a\""
        ".long661b-..long6641f-..4byte(16*32+22)"
        ".byte663b-661b.byte6651f-6641f"
        ".popsection.pushsection.altinstr_replacement,\"ax\""
        "#ALT:replacement16641:.byte0xf3,0x0f,0xc7,0xf8"
        "6651:.popsection";
    char *expected;
    char compact[640];
    size_t i,n=0U;
    if (!text || !length || length>=sizeof(compact)) return false;
    for (i=0U;i<length;++i) {
        if (text[i]==' ' || text[i]=='\t' ||
            text[i]=='\r' || text[i]=='\n') continue;
        if (n+1U>=sizeof(compact)) return false;
        compact[n++]=text[i];
    }
    compact[n]='\0';
    if (strncmp(compact,prefix,strlen(prefix))!=0) return false;
    expected=compact+strlen(prefix);
    if (strncmp(expected,"lsl%[seg],%[p]",strlen("lsl%[seg],%[p]"))==0)
        expected+=strlen("lsl%[seg],%[p]");
    else if (strncmp(expected,"lsl%2,%0",strlen("lsl%2,%0"))==0)
        expected+=strlen("lsl%2,%0");
    else return false;
    return strcmp(expected,suffix)==0;
}


/* Original GNU Linux 6.6.143 x86 bitops: exact source templates, not
 * a generic inline assembly passthrough. The same table is the ONLY
 * authoritative ISA/memory/flag descriptor used by source and machine.
 * Names become canonical numeric operands in shared Core. */
static const struct {
    const char *literal;
    const char *canonical_alias;
    MinicX86BitopsDesc desc;
} x86_linux_bitops[] = {
    {"btq%2,%1/*outputconditioncodec*/", NULL, {"btq",0,1,0,0}},
    {"btsq%2,%1/*outputconditioncodec*/", NULL, {"btsq",0,1,0,1}},
    {"btrq%2,%1/*outputconditioncodec*/", NULL, {"btrq",0,1,0,1}},
    {"btcq%2,%1/*outputconditioncodec*/", NULL, {"btcq",0,1,0,1}},
    {"btsq%1,%0", NULL, {"btsq",0,0,0,1}},
    {"btrq%1,%0", NULL, {"btrq",0,0,0,1}},
    {".pushsection.smp_locks,\"a\".balign4.long671f-..popsection671:lock;btsq%1,%0",
     NULL,
     {"btsq",1,0,0,1}},
    {".pushsection.smp_locks,\"a\".balign4.long671f-..popsection671:lock;btrq%1,%0",
     NULL,
     {"btrq",1,0,0,1}},
    {".pushsection.smp_locks,\"a\".balign4.long671f-..popsection671:lock;btsq%[val],%[var]/*outputconditioncodec*/",
     ".pushsection.smp_locks,\"a\".balign4.long671f-..popsection671:lock;btsq%2,%0/*outputconditioncodec*/",
     {"btsq",1,1,1,1}},
    {".pushsection.smp_locks,\"a\".balign4.long671f-..popsection671:lock;btrq%[val],%[var]/*outputconditioncodec*/",
     ".pushsection.smp_locks,\"a\".balign4.long671f-..popsection671:lock;btrq%2,%0/*outputconditioncodec*/",
     {"btrq",1,1,1,1}},
    {".pushsection.smp_locks,\"a\".balign4.long671f-..popsection671:lock;orb%b1,%0",
     NULL,
     {"orb",1,0,1,1}},
    {".pushsection.smp_locks,\"a\".balign4.long671f-..popsection671:lock;andb%b1,%0",
     NULL,
     {"andb",1,0,1,1}},
    {".pushsection.smp_locks,\"a\".balign4.long671f-..popsection671:lock;xorb%b1,%0",
     NULL,
     {"xorb",1,0,1,1}},
    {".pushsection.smp_locks,\"a\".balign4.long671f-..popsection671:lock;btcq%1,%0",
     NULL,
     {"btcq",1,0,0,1}}
};
MinicX86BitopsForm minic_x86_64_linux_bitops_form(
    const char *text,size_t length) {
    char compact[256];
    size_t i,j=0U;
    if (!text||!length||length>=sizeof(compact))
        return MINIC_X86_BITOPS_NONE;
    for(i=0U;i<length;++i) {
        const char ch=text[i];
        if (ch==' '||ch=='\t'||ch=='\n'||ch=='\r')continue;
        if(j+1U>=sizeof(compact))return MINIC_X86_BITOPS_NONE;
        compact[j++]=ch;
    }
    compact[j]='\0';
    for(i=0U;i<sizeof(x86_linux_bitops)/sizeof(x86_linux_bitops[0]);++i)
        if(strcmp(compact,x86_linux_bitops[i].literal)==0||
           (x86_linux_bitops[i].canonical_alias &&
            strcmp(compact,x86_linux_bitops[i].canonical_alias)==0))
            return (MinicX86BitopsForm)(i+1U);
    return MINIC_X86_BITOPS_NONE;
}
const MinicX86BitopsDesc *minic_x86_64_bitops_description(
    MinicX86BitopsForm form) {
    const size_t index = (size_t)form;
    if(index==0U||index>sizeof(x86_linux_bitops)/sizeof(x86_linux_bitops[0]))
        return NULL;
    return &x86_linux_bitops[index-1U].desc;
}

/* Exact x86 Linux word-sized GNU BSF/BSR spelling. The REP prefix on
 * Linux __ffs is deliberate: it becomes TZCNT where available, and
 * preserves BSF bit-index semantics for nonzero inputs on older CPUs.
 * An all-zero input has no defined output; callers must not depend on it. */
/* Decode complete GNU source/machine bit-scan templates once.
 * Tied input #2 is materialized explicitly as the pre-BSR destination,
 * so a zero source is not silently changed to an uninitialized register. */
MinicX86BitScanForm minic_x86_64_asm_bit_scan_form(
    const char *text, size_t length) {
    static const struct { const char *text; MinicX86BitScanForm form; } forms[] = {
        {"rep;bsf%1,%0", MINIC_X86_BIT_SCAN_BSF64},
        {"bsr%1,%0", MINIC_X86_BIT_SCAN_BSR64},
        {"bsfl%1,%0", MINIC_X86_BIT_SCAN_BSF32_TIED},
        {"bsrl%1,%0", MINIC_X86_BIT_SCAN_BSR32_TIED},
        {"bsrq%1,%q0", MINIC_X86_BIT_SCAN_BSR64_READWRITE}
    };
    char compact[64];
    size_t i, n = 0U;
    if (!text || length == 0U || length >= sizeof(compact))
        return MINIC_X86_BIT_SCAN_NONE;
    for (i = 0U; i < length; ++i) {
        const char ch = text[i];
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') continue;
        if (n + 1U >= sizeof(compact)) return MINIC_X86_BIT_SCAN_NONE;
        compact[n++] = ch;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(forms) / sizeof(forms[0]); ++i)
        if (strcmp(compact, forms[i].text) == 0)
            return forms[i].form;
    return MINIC_X86_BIT_SCAN_NONE;
}
/* The GNU =r + matched-digit family owns only explicit register operands.
 * No hidden target machine registers, jumps, labels, function calls or
 * directives. Every template instruction is made of a conservative x86
 * scalar carry/move mnemonic and references only %0..%{outputs-1}. */
/* An explicitly bounded grammar for the BusyBox/TLS P-256 register +
 * pointer-memory inline ASM. No source bytes may reach GNU AS except:
 *   %N, (%N), K*8(%N) for K=1..3, $0 and $1.
 * Core/machine independently verify operand roles and physical registers.
 * We retain the already-supported pointer forms; rejecting them as
 * register-only would cause a regression, and ignoring unchecked suffixes
 * permitted an injected second instruction. */
static bool x86_matched_space(char c) {
    return c == ' ' || c == '\t' || c == '\r';
}
static bool x86_matched_ref(const char *text, size_t len, size_t *cursor,
                            size_t outputs, size_t *references) {
    size_t i = *cursor;
    if (i >= len || text[i] != '%' || i + 1U >= len ||
        text[i + 1U] < '0' ||
        (size_t)(text[i + 1U] - '0') >= outputs)
        return false;
    *cursor = i + 2U;
    ++*references;
    return true;
}
static bool x86_matched_operand(const char *text, size_t len, size_t *cursor,
                                size_t outputs, size_t *refs) {
    size_t i = *cursor;
    if (i >= len) return false;
    if (text[i] == '$') {
        ++i;
        if (i >= len || (text[i] != '0' && text[i] != '1'))
            return false;
        ++i;
    } else {
        bool memory = false;
        if (i + 4U < len &&
            text[i] >= '1' && text[i] <= '3' &&
            text[i + 1U] == '*' && text[i + 2U] == '8' &&
            text[i + 3U] == '(') {
            i += 4U;
            memory = true;
        } else if (text[i] == '(') {
            ++i;
            memory = true;
        }
        if (!x86_matched_ref(text, len, &i, outputs, refs))
            return false;
        if (memory) {
            if (i >= len || text[i] != ')')
                return false;
            ++i;
        }
    }
    *cursor = i;
    return true;
}
bool minic_x86_64_asm_matched_template(const char *text,
                                        size_t length, size_t outputs) {
    static const char *const binary_ops[] = {
        "movq", "addq", "adcq", "subq", "sbbq"
    };
    size_t i = 0U, lines = 0U, refs = 0U, k;
    if (text == NULL || length == 0U || length > 1024U ||
        outputs == 0U || outputs > 4U)
        return false;
    while (i < length) {
        bool matched = false;
        while (i < length &&
               (x86_matched_space(text[i]) || text[i] == '\n' ||
                text[i] == ';'))
            ++i;
        if (i == length) break;
        if (i + 3U <= length && memcmp(text + i, "cmc", 3U) == 0) {
            i += 3U;
        } else {
            for (k = 0U; k < sizeof(binary_ops) / sizeof(binary_ops[0]); ++k) {
                if (i + 4U <= length &&
                    memcmp(text + i, binary_ops[k], 4U) == 0) {
                    matched = true;
                    i += 4U;
                    break;
                }
            }
            if (!matched || i >= length || !x86_matched_space(text[i]))
                return false;
            while (i < length && x86_matched_space(text[i])) ++i;
            if (!x86_matched_operand(text, length, &i, outputs, &refs))
                return false;
            while (i < length && x86_matched_space(text[i])) ++i;
            if (i == length || text[i] != ',') return false;
            ++i;
            while (i < length && x86_matched_space(text[i])) ++i;
            if (!x86_matched_operand(text, length, &i, outputs, &refs))
                return false;
        }
        while (i < length && x86_matched_space(text[i])) ++i;
        if (i < length && text[i] != '\n' && text[i] != ';')
            return false;
        ++lines;
        if (i < length) ++i;
    }
    return lines != 0U && refs != 0U;
}


/* Linux 6.6 x86/include/asm/mwait.h deliberately spells MONITOR, MONITORX,
 * MWAIT and MWAITX as raw opcode bytes to compile with older assemblers.
 * Parse the exact instruction sequence and fixed A/C/D or A/B/C operand
 * assignments. The STI+MWAIT form enables interrupts for the sleeping
 * instruction; silently treating STI as an ordinary compiler fence would
 * race the idle wakeup path. The return value is a target-owned ISA ID. */
unsigned int minic_x86_64_linux_monitor_wait_form(
    const char *text, size_t length) {
    static const char *const forms[] = {
        ".byte0x0f,0x01,0xc8;",       /* MONITOR */
        ".byte0x0f,0x01,0xfa;",       /* MONITORX */
        ".byte0x0f,0x01,0xc9;",       /* MWAIT */
        ".byte0x0f,0x01,0xfb;",       /* MWAITX */
        "sti;.byte0x0f,0x01,0xc9;"    /* STI; MWAIT */
    };
    char compact[96];
    size_t i, n = 0U;
    if (!text || !length || length >= sizeof(compact)) return 0U;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return 0U;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(forms)/sizeof(forms[0]); ++i)
        if (strcmp(compact, forms[i]) == 0) return (unsigned int)i + 1U;
    return 0U;
}


/* copy_thread() and ptrace() snapshot segment selectors from live CPU state.
 * DS/ES/FS/GS are 16-bit architectural registers, even in long mode. A C memory
 * load is not equivalent, and the ordinary fixed-register allocator cannot
 * manufacture their values. Support both the short selector snapshot
 * (memory ordered) and the Linux four 32-bit zero-extended reads. */
unsigned int minic_x86_64_linux_segment_read_form(
    const char *text, size_t length) {
    static const char *const allowed[] = {
        "mov%%es,%0", "mov%%ds,%0",
        "movl%%fs,%0", "movl%%gs,%0", "movl%%ds,%0", "movl%%es,%0",
        "mov%%ss,%0", "mov%%cs,%0", "mov%%fs,%0", "mov%%gs,%0"
    };
    char normalized[48];
    size_t i, n = 0U;
    if (!text || !length || length >= sizeof(normalized)) return 0U;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
            continue;
        if (c == '\0' || c == ';' || n + 1U >= sizeof(normalized))
            return 0U;
        normalized[n++] = c;
    }
    normalized[n] = '\0';
    for (i = 0U; i < sizeof(allowed)/sizeof(allowed[0]); ++i)
        if (strcmp(normalized, allowed[i]) == 0) return (unsigned int)i + 1U;
    return 0U;
}


/* Linux loadsegment() is not the bare MOV to a segment register: its 1:/2:
 * fixup record uses exception type 5 and must survive as a three-word
 * __ex_table entry. Admit exactly that complete GNU spelling (including the
 * relocation base and exception handler type) for the 16-bit selector ISA. */
unsigned int minic_x86_64_linux_segment_load_extable_form(
    const char *text, size_t length) {
    static const char *const segment_names[] = {
        "fs", "gs", "ds", "es", "ss"
    };
    static const char tail[] =
        "2:.pushsection\"__ex_table\",\"a\".balign4"
        ".long(1b)-..long(2b)-..long5.popsection";
    char compact[180], expected[180];
    size_t i, n = 0U;
    if (!text || !length || length >= sizeof(compact)) return 0U;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return 0U;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(segment_names)/sizeof(segment_names[0]); ++i) {
        int written = snprintf(expected, sizeof(expected),
            "1:movw%%0,%%%%%s%s", segment_names[i], tail);
        if (written > 0 && (size_t)written < sizeof(expected) &&
            strcmp(compact, expected) == 0) return (unsigned int)i + 1U;
    }
    return 0U;
}


/* Linux 6.6 LKGS uses the fixed DI-register ISA encoding, with a full
 * __ex_table type-17 register fixup encoded by its GNU local macro. The
 * original 731-byte template is recognized whole: a modified macro,
 * relocated fault site or register number must not silently compile. */
bool minic_x86_64_linux_lkgs_extable_form(const char *text, size_t length) {
    static const char original[] =
        "1:  .byte 0xf2,0x0f,0x00,0xf7 ; "
        " .pushsection \"__ex_table\",\"a\"\n"
        " .balign 4\n"
        " .long (1b) - .\n"
        " .long (1b) - .\n"
        ".macro extable_type_reg type:req reg:req\n"
        ".set .Lfound, 0\n"
        ".set .Lregnr, 0\n"
        ".irp rs,rax,rcx,rdx,rbx,rsp,rbp,rsi,rdi,r8,r9,r10,r11,r12,r13,r14,r15\n"
        ".ifc \\reg, %%\\rs\n"
        ".set .Lfound, .Lfound+1\n"
        ".long \\type + (.Lregnr << 8)\n"
        ".endif\n"
        ".set .Lregnr, .Lregnr+1\n"
        ".endr\n"
        ".set .Lregnr, 0\n"
        ".irp rs,eax,ecx,edx,ebx,esp,ebp,esi,edi,r8d,r9d,r10d,r11d,r12d,r13d,r14d,r15d\n"
        ".ifc \\reg, %%\\rs\n"
        ".set .Lfound, .Lfound+1\n"
        ".long \\type + (.Lregnr << 8)\n"
        ".endif\n"
        ".set .Lregnr, .Lregnr+1\n"
        ".endr\n"
        ".if (.Lfound != 1)\n"
        ".error \"extable_type_reg: bad register argument\"\n"
        ".endif\n"
        ".endm\n"
        "extable_type_reg reg=%k[sel], type=(17 | ((0) << 16)) \n"
        ".purgem extable_type_reg\n"
        " .popsection\n";
    if (!text) return false;
    if (length == sizeof(original)-1U &&
        memcmp(text, original, sizeof(original)-1U) == 0)
        return true;
    /* Common Core resolves %[sel] to %0 while preserving the GNU %k
     * 32-bit register view and the rest of the exact exception macro. */
    if (length == sizeof(original)-5U) {
        static const char named[] = "%k[sel]";
        const char *slot = strstr(original, named);
        size_t prefix;
        if (!slot) return false;
        prefix = (size_t)(slot - original);
        return memcmp(text, original, prefix) == 0 &&
               memcmp(text + prefix, "%k0", 3U) == 0 &&
               memcmp(text + prefix + 3U, slot + sizeof(named)-1U,
                   (sizeof(original)-1U) - prefix - (sizeof(named)-1U)) == 0;
    }
    return false;
}


/* FSGSBASE has four operand-bearing privileged forms; the read and write
 * halves have opposite C lvalue roles despite sharing a general GPR ISA
 * encoding. Preserve 64-bit width, memory sequencing and exact mnemonic. */
unsigned int minic_x86_64_linux_fsgsbase_form(
    const char *text, size_t length) {
    static const char *const exact[] = {
        "rdfsbase%0", "rdgsbase%0", "wrfsbase%0", "wrgsbase%0"
    };
    char compact[40];
    size_t n = 0U, i;
    if (!text || !length || length >= sizeof(compact)) return 0U;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return 0U;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(exact)/sizeof(exact[0]); ++i)
        if (strcmp(compact, exact[i]) == 0) return (unsigned int)i + 1U;
    return 0U;
}


/* Linux kernel nofault MOV accesses have a three-word __ex_table record
 * whose second PREL32 word points to the C asm-goto fault label. They are
 * different from ordinary MOV and from uaccess __get_user() helpers.
 * Return 1..4 for load b/w/l/q and 5..8 for store b/w/l/q. */
unsigned int minic_x86_64_linux_kernel_nofault_goto_form(
    const char *text, size_t length) {
    static const char suffix[] =
        ".pushsection\"__ex_table\",\"a\".balign4"
        ".long(1b)-..long(%l2)-..long3.popsection";
    char compact[200], expected[200];
    size_t i, n = 0U, variant;
    static const char widths[] = "bwlq";
    if (!text || !length || length >= sizeof(compact)) return 0U;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return 0U;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (variant = 0U; variant < 8U; ++variant) {
        const bool load = variant < 4U;
        int written = snprintf(expected, sizeof(expected),
            "1:mov%c%s%s", widths[variant % 4U],
            load ? "%[umem],%[output]" : "%0,%1", suffix);
        if (written > 0 && (size_t)written < sizeof(expected) &&
            strcmp(compact, expected) == 0)
            return (unsigned int)variant + 1U;
        /* Core replaces GNU named operands with ordinal indices, but
         * retains the %l2 label placeholder for machine relocation. */
        if (load) {
            written = snprintf(expected, sizeof(expected),
                "1:mov%c%%1,%%0%s", widths[variant], suffix);
            if (written > 0 && (size_t)written < sizeof(expected) &&
                strcmp(compact, expected) == 0)
                return (unsigned int)variant + 1U;
        }
    }
    return 0U;
}


/* Linux copy_user_generic couples REP MOVSB with the FSRM alternative CALL
 * and an exception-table recovery entry. The four GNU read/write operands
 * are RCX/RDI/RSI plus a fixed live RSP dependency; the replacement call
 * is not a normal C SysV call, and all relocations must survive intact. */
bool minic_x86_64_linux_rep_movsb_alternative_form(
    const char *text, size_t length) {
    static const char expected[] =
        "1:#ALT:oldnstr661:repmovsb662:#ALT:padding"
        ".skip-(((6651f-6641f)-(662b-661b))>0)*"
        "((6651f-6641f)-(662b-661b)),0x90"
        "663:.pushsection.altinstructions,\"a\""
        ".long661b-..long6641f-..4byte(((1<<0)<<16)|((18*32+4)))"
        ".byte663b-661b.byte6651f-6641f.popsection"
        ".pushsection.altinstr_replacement,\"ax\""
        "#ALT:replacement16641:callrep_movs_alternative6651:.popsection"
        "2:.pushsection\"__ex_table\",\"a\".balign4"
        ".long(1b)-..long(2b)-..long3.popsection";
    char compact[560];
    size_t i, n = 0U;
    if (!text || length != 506U) return false;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return false;
        compact[n++] = c;
    }
    compact[n] = '\0';
    return strcmp(compact, expected) == 0;
}

/* __clear_user() must preserve both the REP STOSB baseline and the feature-
 * guarded REP STOS helper. The RAX zero input and RSP fixed dependency are
 * part of this privileged ABI, as are the three __ex_table PREL32 words. */
bool minic_x86_64_linux_rep_stosb_alternative_form(
    const char *text, size_t length) {
    static const char expected[] =
        "1:#ALT:oldnstr661:repstosb662:#ALT:padding"
        ".skip-(((6651f-6641f)-(662b-661b))>0)*"
        "((6651f-6641f)-(662b-661b)),0x90"
        "663:.pushsection.altinstructions,\"a\""
        ".long661b-..long6641f-..4byte(((1<<0)<<16)|((12*32+11)))"
        ".byte663b-661b.byte6651f-6641f.popsection"
        ".pushsection.altinstr_replacement,\"ax\""
        "#ALT:replacement16641:callrep_stos_alternative6651:.popsection"
        "2:.pushsection\"__ex_table\",\"a\".balign4"
        ".long(1b)-..long(2b)-..long3.popsection";
    char compact[560];
    size_t i, n = 0U;
    if (!text || length != 506U) return false;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return false;
        compact[n++] = c;
    }
    compact[n] = '\0';
    return strcmp(compact, expected) == 0;
}


/* FNSAVE/FWAIT, FXSAVE, FXSAVEQ, FXRSTOR, FXRSTORQ and FRSTOR in a user
 * signal frame are protected by STAC/CLAC alternative records as well as a
 * type-15 exception entry. An instruction-only matcher would make user
 * faults fatal in kernel mode; verify the two complete patch records. */
unsigned int minic_x86_64_linux_fpu_user_frame_form(
    const char *text, size_t length) {
    static const char alt_prefix[] =
        "#ALT:oldnstr661:662:#ALT:padding"
        ".skip-(((6651f-6641f)-(662b-661b))>0)*"
        "((6651f-6641f)-(662b-661b)),0x90"
        "663:.pushsection.altinstructions,\"a\""
        ".long661b-..long6641f-..4byte(9*32+20)"
        ".byte663b-661b.byte6651f-6641f.popsection"
        ".pushsection.altinstr_replacement,\"ax\""
        "#ALT:replacement16641:";
    static const char *const instructions[] = {
        "fnsave%[fx];fwait", "fxsave%[fx]", "fxsaveq%[fx]",
        "fxrstor%[fx]", "fxrstorq%[fx]", "frstor%[fx]",
        "fnsave%1;fwait", "fxsave%1", "fxsaveq%1",
        "fxrstor%1", "fxrstorq%1", "frstor%1",
        "fnsave%3;fwait", "fxsave%3", "fxsaveq%3",
        "fxrstor%3", "fxrstorq%3", "frstor%3"
    };
    static const char tail[] =
        ".pushsection\"__ex_table\",\"a\".balign4"
        ".long(1b)-..long(2b)-..long15.popsection";
    char compact[1100], expected[1100];
    size_t i, n = 0U;
    if (!text || !length || length >= sizeof(compact)) return 0U;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return 0U;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(instructions)/sizeof(instructions[0]); ++i) {
        const int wanted = snprintf(expected, sizeof(expected),
            "%s%s1:%s2:%s%s%s",
            alt_prefix, ".byte0x0f,0x01,0xcb6651:.popsection",
            instructions[i], alt_prefix,
            ".byte0x0f,0x01,0xca6651:.popsection", tail);
        if (wanted > 0 && (size_t)wanted < sizeof(expected) &&
            strcmp(compact, expected) == 0)
            return (unsigned int)(i % 6U) + 1U;
    }
    return 0U;
}


unsigned int minic_x86_64_linux_fxrstor_safe_form(
    const char *text, size_t length) {
    static const char extable[] =
        " .pushsection \"__ex_table\",\"a\"\n"
        " .balign 4\n"
        " .long (1b) - .\n"
        " .long (2b) - .\n"
        ".macro extable_type_reg type:req reg:req\n"
        ".set .Lfound, 0\n"
        ".set .Lregnr, 0\n"
        ".irp rs,rax,rcx,rdx,rbx,rsp,rbp,rsi,rdi,r8,r9,r10,r11,r12,r13,r14,r15\n"
        ".ifc \\reg, %%\\rs\n"
        ".set .Lfound, .Lfound+1\n"
        ".long \\type + (.Lregnr << 8)\n"
        ".endif\n"
        ".set .Lregnr, .Lregnr+1\n"
        ".endr\n"
        ".set .Lregnr, 0\n"
        ".irp rs,eax,ecx,edx,ebx,esp,ebp,esi,edi,r8d,r9d,r10d,r11d,r12d,r13d,r14d,r15d\n"
        ".ifc \\reg, %%\\rs\n"
        ".set .Lfound, .Lfound+1\n"
        ".long \\type + (.Lregnr << 8)\n"
        ".endif\n"
        ".set .Lregnr, .Lregnr+1\n"
        ".endr\n"
        ".if (.Lfound != 1)\n"
        ".error \"extable_type_reg: bad register argument\"\n"
        ".endif\n"
        ".endm\n";
    static const char *const opcodes[] = {"fxrstor", "fxrstorq"};
    char expected[800];
    size_t i;
    if (!text || length >= sizeof(expected)) return 0U;
    for (i = 0U; i < 4U; ++i) {
        const int written = snprintf(expected, sizeof(expected),
            "1:%s %s\n\t2:\n%s"
            "extable_type_reg reg=%s, type=(17 | ((-14) << 16)) \n"
            ".purgem extable_type_reg\n .popsection\n",
            opcodes[i % 2U], i < 2U ? "%[fx]" : "%3", extable,
            i < 2U ? "%[err]" : "%0");
        if (written > 0 && (size_t)written == length &&
            memcmp(text, expected, length) == 0)
            return (unsigned int)(i % 2U) + 1U;
    }
    return 0U;
}

bool minic_x86_64_linux_x87_fild_prepare_form(const char *text, size_t length) {
    static const char *const forms[] = {
        "fnclexemmsfildl%[addr]", "fnclexemmsfildl%0"
    };
    char compact[80];
    size_t i, n = 0U;
    if (!text || length == 0U || length >= sizeof(compact)) return false;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return false;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(forms)/sizeof(forms[0]); ++i)
        if (strcmp(compact, forms[i]) == 0) return true;
    return false;
}

/* MXCSR is a 32-bit memory control-state operand. Both the source's numeric
 * and named GNU operand spellings denote the same typed machine operation. */
unsigned int minic_x86_64_linux_mxcsr_form(const char *text, size_t length) {
    static const char *const forms[] = {
        "ldmxcsr%0", "ldmxcsr%[val]", "stmxcsr%0", "stmxcsr%[val]"
    };
    char compact[80];
    size_t i, n = 0U;
    if (!text || length == 0U || length >= sizeof(compact)) return 0U;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return 0U;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(forms)/sizeof(forms[0]); ++i)
        if (strcmp(compact, forms[i]) == 0) return i < 2U ? 1U : 2U;
    return 0U;
}

/* ORC unwinder snapshots the instruction pointer, current stack pointer and
 * frame pointer into three independent 64-bit GNU register outputs. These
 * are real hardware register reads; no fake C-call-frame addresses. */
bool minic_x86_64_linux_unwind_context_form(const char *text, size_t length) {
    static const char expected[] = "lea(%%rip),%0mov%%rsp,%1mov%%rbp,%2";
    char compact[128];
    size_t i, n = 0U;
    if (!text || length == 0U || length >= sizeof(compact)) return false;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return false;
        compact[n++] = c;
    }
    compact[n] = '\0';
    return strcmp(compact, expected) == 0;
}


/* Linux TASK_SIZE_MAX's CPU-feature alternative selects a 47-bit or 56-bit
 * user address ceiling. The replacement site and .altinstructions entry are
 * a single ABI contract. Literal `i` inputs are verified again against Core
 * integer constants at machine emission; arbitrary alternatives are denied. */
bool minic_x86_64_linux_task_size_alt_form(const char *text, size_t length) {
    static const char expected[] =
        "#ALT:oldnstr661:movq%[small],%0662:#ALT:padding"
        ".skip-(((6651f-6641f)-(662b-661b))>0)*"
        "((6651f-6641f)-(662b-661b)),0x90"
        "663:.pushsection.altinstructions,\"a\""
        ".long661b-..long6641f-..4byte(16*32+16)"
        ".byte663b-661b.byte6651f-6641f.popsection"
        ".pushsection.altinstr_replacement,\"ax\""
        "#ALT:replacement16641:movq%[large],%06651:.popsection";
    static const char expected_numeric[] =
        "#ALT:oldnstr661:movq%2,%0662:#ALT:padding"
        ".skip-(((6651f-6641f)-(662b-661b))>0)*"
        "((6651f-6641f)-(662b-661b)),0x90"
        "663:.pushsection.altinstructions,\"a\""
        ".long661b-..long6641f-..4byte(16*32+16)"
        ".byte663b-661b.byte6651f-6641f.popsection"
        ".pushsection.altinstr_replacement,\"ax\""
        "#ALT:replacement16641:movq%3,%06651:.popsection";
    char compact[512];
    size_t i, n = 0U;
    if (!text || length == 0U || length >= sizeof(compact)) return false;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return false;
        compact[n++] = c;
    }
    compact[n] = '\0';
    return strcmp(compact, expected) == 0 ||
           strcmp(compact, expected_numeric) == 0;
}


/* The kernel-state restore path is not the user-frame alternative and has
 * no destination error register.  The Linux exception table records type 6
 * (EX_TYPE_FPU_RESTORE); treating it as a naked memory read would remove the
 * kernel's recovery edge when a corrupt state is restored. */
unsigned int minic_x86_64_linux_fpu_kernel_restore_form(
    const char *text, size_t length) {
    static const char *const names[] = {"fxrstor", "fxrstorq", "frstor"};
    static const char *const operands[] = {"%[fx]", "%1"};
    static const char suffix[] =
        "2:.pushsection\"__ex_table\",\"a\".balign4"
        ".long(1b)-..long(2b)-..long6.popsection";
    char compact[256], expected[256];
    size_t i, j, n = 0U;
    if (text == NULL || length == 0U || length >= sizeof(compact))
        return 0U;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return 0U;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(names)/sizeof(names[0]); ++i) {
        for (j = 0U; j < sizeof(operands)/sizeof(operands[0]); ++j) {
            const int size = snprintf(expected, sizeof(expected),
                                      "1:%s%s%s", names[i], operands[j], suffix);
            if (size > 0 && (size_t)size < sizeof(expected) &&
                strcmp(expected, compact) == 0) return (unsigned int)i + 1U;
        }
    }
    return 0U;
}


/* The XSAVE/XSAVES and XRSTOR/XRSTORS families use an explicit %rdi memory
 * address, two fixed mask registers, and an EAX fault return. The exception
 * entry belongs to the instruction, not to the following XOR; preserving its
 * exact pair of PC-relative relocations is required for -EFAULT recovery. */
unsigned int minic_x86_64_linux_xstate_fault_form(
    const char *text, size_t length) {
    static const char *const opcodes[] = {
        "0x48,0x0f,0xae,0x27", /* XSAVE64 */
        "0x48,0x0f,0xae,0x2f", /* XRSTOR64 */
        "0x48,0x0f,0xc7,0x1f", /* XRSTORS64 */
        "0x48,0x0f,0xc7,0x2f"  /* XSAVES64 */
    };
    static const char suffix[] =
        "2:"
        ".pushsection\"__ex_table\",\"a\""
        ".balign4.long(1b)-..long(2b)-..long15.popsection";
    static const char *const error_operands[] = {
        "%[err]", "%0"
    };
    char compact[256], expected[256];
    size_t i, j, n = 0U;
    if (!text || length == 0U || length >= sizeof(compact)) return 0U;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return 0U;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(opcodes)/sizeof(opcodes[0]); ++i) {
        for (j = 0U; j < sizeof(error_operands)/
                           sizeof(error_operands[0]); ++j) {
            const int written = snprintf(expected, sizeof(expected),
                "1:.byte%sxor%s,%s%s", opcodes[i],
                error_operands[j], error_operands[j], suffix);
            if (written > 0 && (size_t)written < sizeof(expected) &&
                strcmp(compact, expected) == 0)
                return (unsigned int)i + 1U;
        }
    }
    return 0U;
}


/* XRSTOR's kernel variant is ALTERNATIVE(XRSTOR64, XRSTORS64,
 * X86_FEATURE_XSAVES) plus a type-6 exception record. Both opcodes carry
 * an explicit fixed-RDI ModRM, and the patch section must be reproducible
 * exactly; otherwise the CPU can execute unsupported instructions. */
bool minic_x86_64_linux_xrstor_alternative_form(
    const char *text, size_t length) {
    static const char expected[] =
        "#ALT:oldnstr661:.byte0x48,0x0f,0xae,0x2f662:"
        "#ALT:padding.skip-(((6651f-6641f)-(662b-661b))>0)*"
        "((6651f-6641f)-(662b-661b)),0x90"
        "663:.pushsection.altinstructions,\"a\""
        ".long661b-..long6641f-..4byte(10*32+3)"
        ".byte663b-661b.byte6651f-6641f.popsection"
        ".pushsection.altinstr_replacement,\"ax\""
        "#ALT:replacement16641:.byte0x48,0x0f,0xc7,0x1f"
        "6651:.popsection3:"
        ".pushsection\"__ex_table\",\"a\".balign4"
        ".long(661b)-..long(3b)-..long6.popsection";
    char compact[570];
    size_t i, n = 0U;
    if (!text || length != 500U) return false;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return false;
        compact[n++] = c;
    }
    compact[n] = '\0';
    return strcmp(compact, expected) == 0;
}


/* The kernel XSAVE ALTERNATIVE_3 is a 2048-byte GNU macro expansion with
 * three nested maximum-length expressions, three CPU patch records, and the
 * full type-17 register-recovery .irp macro. Comparing a *complete* compact
 * fingerprint, rather than selected opcodes, prevents a malformed .skip or
 * wrong CPU feature from being silently accepted. FNV-1a is used only as a
 * frozen exact-template identity check, never on untrusted executable data.
 * The second form is the Core canonicalization of all three %[err] uses to
 * %0. Both variants represent the same GNU template and operands. */
bool minic_x86_64_linux_xsave_triple_alternative_form(
    const char *text, size_t length) {
    unsigned long long digest = 14695981039346656037ULL;
    size_t i, compact = 0U;
    if (!text || (length != 2048U && length != 2036U)) return false;
    for (i = 0U; i < length; ++i) {
        const unsigned char c = (unsigned char)text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || c > 127U) return false;
        digest = (digest ^ c) * 1099511628211ULL;
        ++compact;
    }
    return (compact == 1768U && digest == 0x25454d8338fd3d59ULL) ||
           (compact == 1756U && digest == 0xcc61d8e371e750ccULL);
}


/* jump_label's patch site is *not* merely a JMP: Linux scans a writable
 * __jump_table of (site PREL32, target PREL32, key+flags PREL64). The bit
 * test in the third word must be emitted at assembly time, never evaluated
 * through a runtime pointer. Keep both GNU asm-goto spellings and their
 * named/numeric label placeholders as complete templates. */
unsigned int minic_x86_64_linux_jump_label_form(
    const char *text, size_t length) {
    static const char shared[] =
        ".pushsection__jump_table,\"aw\".balign8"
        ".long1b-..long%s-..quad%%c0+%%c1-..popsection";
    static const char *const jump_spellings[] = {
        "jmp%l[l_yes]#objtoolNOPsthis", "jmp%l[l_yes]",
        "jmp%l2#objtoolNOPsthis", "jmp%l2"
    };
    char compact[220], expected[220], table[180];
    size_t i, n = 0U;
    if (!text || !length || length >= sizeof(compact)) return 0U;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return 0U;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < 4U; ++i) {
        const char *label = i < 2U ? "%l[l_yes]" : "%l2";
        const int table_len = snprintf(table, sizeof(table), shared, label);
        const int wanted = snprintf(expected, sizeof(expected),
                                    "1:%s%s", jump_spellings[i], table);
        if (table_len > 0 && (size_t)table_len < sizeof(table) &&
            wanted > 0 && (size_t)wanted < sizeof(expected) &&
            strcmp(compact, expected) == 0)
            return (unsigned int)(i % 2U) + 1U;
    }
    return 0U;
}


/* Protection Keys form a two-instruction hardware interface, not a textual
 * GNU passthrough. Linux's special_insns.h uses EAX/ECX/EDX for WRPKRU and
 * two distinct write-only EAX/EDX results for RDPKRU. An incorrect operand
 * order or size would change the active userspace page-access permissions;
 * admit only the exact kernel byte encodings and fixed GPR constraints. */
unsigned int minic_x86_64_linux_pkru_form(
    const char *text, size_t length) {
    char compact[96];
    size_t i, n = 0U;
    if (!text || length < 12U || length >= sizeof(compact)) return 0U;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return 0U;
        compact[n++] = c;
    }
    compact[n] = '\0';
    if (strcmp(compact, ".byte0x0f,0x01,0xee") == 0) return 1U;
    if (strcmp(compact, ".byte0x0f,0x01,0xef") == 0) return 2U;
    return 0U;
}


/* The historic Pentium FDIV probe is still built by Linux 6.6.143. It
 * combines three C memory objects and x87 operations that cannot be
 * modelled as GPR arithmetic. Admitting the exact multi-operation template
 * as a target-owned contract preserves FPU state resets, rounding of FISTP
 * to an int destination, and the fwait ordering barrier. */
bool minic_x86_64_linux_fdiv_bug_form(
    const char *text, size_t length) {
    static const char expected[] =
        "fninitfldl%1fdivl%2fmull%2fldl%1"
        "fsubp%%st,%%st(1)fistpl%0fwaitfninit";
    char normalized[200];
    size_t i, n = 0U;
    if (!text || length < 40U || length >= sizeof(normalized)) return false;
    for (i = 0U; i < length; ++i) {
        const char ch = text[i];
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') continue;
        if (ch == '\0' || n + 1U >= sizeof(normalized)) return false;
        normalized[n++] = ch;
    }
    normalized[n] = '\0';
    return strcmp(normalized, expected) == 0;
}


/* Four appearances of Linux's SMAP ALTERNATIVE macro share the exact
 * static-key feature and relocation layout, but have distinct observable
 * state changes. A template is admissible only with the complete section
 * metadata, feature 9*32+20, and precisely one of the four replacement
 * instruction bodies. Removing the patch metadata must never silently
 * turn a conditional STAC/CLAC into an unconditional privileged opcode. */
unsigned int minic_x86_64_linux_smap_alternative_form(
    const char *text, size_t length) {
    static const char shared[] =
        "#ALT:oldnstr661:662:#ALT:padding"
        ".skip-(((6651f-6641f)-(662b-661b))>0)*"
        "((6651f-6641f)-(662b-661b)),0x90"
        "663:.pushsection.altinstructions,\"a\""
        ".long661b-..long6641f-..4byte(9*32+20)"
        ".byte663b-661b.byte6651f-6641f.popsection"
        ".pushsection.altinstr_replacement,\"ax\""
        "#ALT:replacement16641:";
    static const char *const prefixes[] = {
        "", "", "#smap_save", "#smap_restore"
    };
    static const char *const bodies[] = {
        ".byte0x0f,0x01,0xca", ".byte0x0f,0x01,0xcb",
        "pushf;pop%0;.byte0x0f,0x01,0xca", "push%0;popf"
    };
    char compact[700], expected[700];
    size_t i, n = 0U;
    unsigned int form;
    if (!text || length > 680U || length < 300U) return 0U;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return 0U;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (form = 1U; form <= 4U; ++form) {
        const int required = snprintf(expected, sizeof(expected), "%s%s%s%s",
            prefixes[form - 1U], shared, bodies[form - 1U],
            "6651:.popsection");
        if (required > 0 && (size_t)required < sizeof(expected) &&
            strcmp(compact, expected) == 0)
            return form;
    }
    return 0U;
}

/* Mitigation switch in user_access_begin(): an empty sequence is replaced
 * with LFENCE iff X86_FEATURE_USERCOPY_FENCE (20*32+2) is present. The
 * alternative lengths, feature, labels and ELF sections are indivisible. */
bool minic_x86_64_linux_lfence_alternative_form(
    const char *text, size_t length) {
    static const char expected[] =
        "#ALT:oldnstr661:662:#ALT:padding"
        ".skip-(((6651f-6641f)-(662b-661b))>0)*"
        "((6651f-6641f)-(662b-661b)),0x90"
        "663:.pushsection.altinstructions,\"a\""
        ".long661b-..long6641f-..4byte(20*32+2)"
        ".byte663b-661b.byte6651f-6641f.popsection"
        ".pushsection.altinstr_replacement,\"ax\""
        "#ALT:replacement16641:lfence6651:.popsection";
    char compact[420];
    size_t i, count = 0U;
    if (!text || length != 351U) return false;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || count + 1U >= sizeof(compact)) return false;
        compact[count++] = c;
    }
    compact[count] = '\0';
    return strcmp(compact, expected) == 0;
}

/* Linux strnlen/strscpy fast path: faulting word read, followed by a
 * type-20 exception-table record. This is NOT the generic nofault handler
 * (type 3): the exception handler completes the part-before-page-boundary
 * word and zeros the remaining inaccessible bytes in the result register. */
bool minic_x86_64_linux_load_unaligned_zeropad_form(
    const char *text, size_t length) {
    static const char expected[] =
        "1:\tmov %[mem], %[ret]\n"
        "2:\n"
        " .pushsection \"__ex_table\",\"a\"\n"
        " .balign 4\n"
        " .long (1b) - .\n"
        " .long (2b) - .\n"
        " .long 20 \n"
        " .popsection\n";
    static const char numeric[] =
        "1:\tmov %1, %0\n"
        "2:\n"
        " .pushsection \"__ex_table\",\"a\"\n"
        " .balign 4\n"
        " .long (1b) - .\n"
        " .long (2b) - .\n"
        " .long 20 \n"
        " .popsection\n";
    return text &&
        ((length == sizeof(expected) - 1U &&
          memcmp(text, expected, length) == 0) ||
         (length == sizeof(numeric) - 1U &&
          memcmp(text, numeric, length) == 0));
}


/* Linux x86-64 signal frame sanitization uses LAR (Load Access Rights)
 * to inspect a user SS selector. An invalid selector clears ZF; the C
 * output must then be zero, while a valid selector exposes its access
 * rights. Model the instruction and its ZF branch together. */
bool minic_x86_64_linux_lar_selector_form(
    const char *text, size_t length) {
    static const char source[] =
        "lar %[old_ss], %[ar]\n\t"
        "jz 1f\n\t"
        "xorl %[ar], %[ar]\n\t"
        "1:";
    static const char numeric[] =
        "lar %1, %0\n\t"
        "jz 1f\n\t"
        "xorl %0, %0\n\t"
        "1:";
    return text &&
        ((length == sizeof(source) - 1U &&
          memcmp(text, source, length) == 0) ||
         (length == sizeof(numeric) - 1U &&
          memcmp(text, numeric, length) == 0));
}


/* GNU flag output =@ccc observes the hardware Carry Flag, not whether
 * the generated random word happens to be nonzero. RDRAND and RDSEED
 * share the same operand ABI but advertise distinct CPU feature bits. */
unsigned int minic_x86_64_linux_random_seed_form(
    const char *text, size_t length) {
    static const char *const originals[] = {
        "rdrand %[out]\n\t/* output condition code c*/\n",
        "rdseed %[out]\n\t/* output condition code c*/\n",
        "rdrand %1\n\t/* output condition code c*/\n",
        "rdseed %1\n\t/* output condition code c*/\n"
    };
    size_t i;
    if (!text) return 0U;
    for (i = 0U; i < sizeof(originals) / sizeof(originals[0]); ++i)
        if (length == strlen(originals[i]) &&
            memcmp(text, originals[i], length) == 0)
            return (unsigned int)(i % 2U) + 1U;
    return 0U;
}


/* The exception-table register helper in Linux loadsegment(ES/DS/SS) is
 * distinct from the short-selector FS fixup. Its type-17 entry names the
 * GPR that held the selector so the exception handler can rewrite it.
 * Check the whole expanded GNU macro, including its register-number table. */
unsigned int minic_x86_64_linux_segment_register_extable_form(
    const char *text, size_t length) {
    static const char body[] =
        " .pushsection \"__ex_table\",\"a\"\n"
        " .balign 4\n"
        " .long (1b) - .\n"
        " .long (1b) - .\n"
        ".macro extable_type_reg type:req reg:req\n"
        ".set .Lfound, 0\n"
        ".set .Lregnr, 0\n"
        ".irp rs,rax,rcx,rdx,rbx,rsp,rbp,rsi,rdi,r8,r9,r10,r11,r12,r13,r14,r15\n"
        ".ifc \\reg, %%\\rs\n"
        ".set .Lfound, .Lfound+1\n"
        ".long \\type + (.Lregnr << 8)\n"
        ".endif\n"
        ".set .Lregnr, .Lregnr+1\n"
        ".endr\n"
        ".set .Lregnr, 0\n"
        ".irp rs,eax,ecx,edx,ebx,esp,ebp,esi,edi,r8d,r9d,r10d,r11d,r12d,r13d,r14d,r15d\n"
        ".ifc \\reg, %%\\rs\n"
        ".set .Lfound, .Lfound+1\n"
        ".long \\type + (.Lregnr << 8)\n"
        ".endif\n"
        ".set .Lregnr, .Lregnr+1\n"
        ".endr\n"
        ".if (.Lfound != 1)\n"
        ".error \"extable_type_reg: bad register argument\"\n"
        ".endif\n"
        ".endm\n"
        "extable_type_reg reg=%k0, type=(17 | ((0) << 16)) \n"
        ".purgem extable_type_reg\n"
        " .popsection\n";
    static const char *const names[] = {"es", "ds", "ss"};
    char exact[780];
    size_t i;
    if (!text || length != 721U) return 0U;
    for (i = 0U; i < sizeof(names)/sizeof(names[0]); ++i) {
        const int written = snprintf(exact, sizeof(exact),
            "\t\t\t\t\t\t\n1:\tmovl %%k0,%%%%%s\t\t\n%s",
            names[i], body);
        if (written > 0 && (size_t)written == length &&
            memcmp(text, exact, length) == 0)
            return (unsigned int)i + 1U;
    }
    return 0U;
}


/* Native 128-bit CMPXCHG is a single 16-byte atomic read/conditional write;
 * the ZF output and SMP lock-site relocation are observable Linux effects.
 * Match complete source and numbered-Core templates, not a substring that
 * might accidentally admit an unrelated assembly program. */
unsigned int minic_x86_64_linux_cmpxchg128_form(const char *text, size_t length) {
    static const char head[] =
        ".pushsection .smp_locks,\"a\"\n"
        ".balign 4\n"
        ".long 671f - .\n"
        ".popsection\n"
        "671:\n\tlock; cmpxchg16b ";
    static const char tail[] = "\n\t/* output condition code e*/\n";
    static const char *const operand[] = {"%[ptr]", "%1"};
    static const char alt_head[] =
        "# ALT: oldnstr\n"
        "661:\n\tleaq ";
    static const char alt_middle[] =
        ", %%rsi; call this_cpu_cmpxchg16b_emu\n"
        "662:\n"
        "# ALT: padding\n"
        ".skip -(((6651f-6641f)-(662b-661b)) > 0) * "
            "((6651f-6641f)-(662b-661b)),0x90\n"
        "663:\n"
        ".pushsection .altinstructions,\"a\"\n"
        " .long 661b - .\n"
        " .long 6641f - .\n"
        " .4byte ( 4*32+13)\n"
        " .byte 663b-661b\n"
        " .byte 6651f-6641f\n"
        ".popsection\n"
        ".pushsection .altinstr_replacement, \"ax\"\n"
        "# ALT: replacement 1\n"
        "6641:\n\tcmpxchg16b %%gs:";
    static const char alt_tail[] = "\n6651:\n.popsection\n";
    static const char *const alt_address[][2] = {
        {"%P[var]", "%[var]"}, {"%P0", "%0"}
    };
    size_t i;
    if (!text || !length || length > 512U || strlen(text) != length) return 0U;
    for (i = 0U; i < sizeof(operand)/sizeof(operand[0]); ++i) {
        const size_t p = sizeof(head)-1U, o = strlen(operand[i]);
        const size_t t = sizeof(tail)-1U;
        if (length == p+o+t && memcmp(text, head, p)==0 &&
            memcmp(text+p, operand[i], o)==0 &&
            memcmp(text+p+o, tail, t)==0)
            return 1U;
    }
    for (i=0U;i<sizeof(alt_address)/sizeof(alt_address[0]);++i) {
        const size_t h=sizeof(alt_head)-1U, m=sizeof(alt_middle)-1U;
        const size_t t=sizeof(alt_tail)-1U;
        const size_t a=strlen(alt_address[i][0]), b=strlen(alt_address[i][1]);
        if (length == h+a+m+b+t && memcmp(text,alt_head,h)==0 &&
            memcmp(text+h,alt_address[i][0],a)==0 &&
            memcmp(text+h+a,alt_middle,m)==0 &&
            memcmp(text+h+a+m,alt_address[i][1],b)==0 &&
            memcmp(text+h+a+m+b,alt_tail,t)==0)
            return 2U;
    }
    return 0U;
}


/* __put_user_size() is a Linux kernel exception-table-aware helper CALL,
 * not a C store: its input ABI is RCX=user address, RAX=scalar value,
 * RSP=current live stack, ECX=error result. The GNU EBX clobber is part of
 * this private ABI and cannot be dropped just because SysV usually saves
 * RBX. Match the full source constraint footprint and original named size
 * placeholder; Core resolves that size to one integer at a call site. */
unsigned int minic_x86_64_linux_put_user_call_form(
    const char *text, size_t length) {
    static const char original[] = "call__put_user_%c[size]";
    static const char *const lowered[] = {
        "call__put_user_1", "call__put_user_2",
        "call__put_user_4", "call__put_user_8"
    };
    char normalized[96];
    size_t i, n = 0U;
    if (!text || !length || length >= sizeof(normalized)) return 0U;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || c == ';' || n + 1U >= sizeof(normalized))
            return 0U;
        normalized[n++] = c;
    }
    normalized[n] = '\0';
    if (strcmp(normalized, original) == 0) return 0x100U;
    if (strcmp(normalized, "call__put_user_nocheck_%c[size]") == 0)
        return 0x400U;
    /* Common Core resolves the named %[size] to a numbered immediate
     * binding but deliberately leaves GNU %c for the target to encode. */
    if (strcmp(normalized, "call__put_user_%c4") == 0) return 0x200U;
    if (strcmp(normalized, "call__put_user_nocheck_%c4") == 0)
        return 0x800U;
    for (i = 0U; i < sizeof(lowered)/sizeof(lowered[0]); ++i)
        if (strcmp(normalized, lowered[i]) == 0)
            return (unsigned int)(1U << i);
    return 0U;
}


/* Linux __get_user_size() / __get_user_nocheck_size() are native uaccess
 * helpers with error in EAX and read value in RDX, opposite to put_user's
 * RCX pointer convention. RSP remains a live fixed-register tied operand.
 * The body contains its own exception-table fixup; no plain load may replace
 * the fault-aware CALL. Preserve the named GNU immediate until Core resolves
 * the actual width at the call site. */
unsigned int minic_x86_64_linux_get_user_call_form(
    const char *text, size_t length) {
    static const char *const names[] = {
        "call__get_user_%c[size]",
        "call__get_user_nocheck_%c[size]",
        "call__get_user_%c4",
        "call__get_user_nocheck_%c4"
    };
    char compact[96];
    size_t i, n = 0U;
    if (!text || !length || length >= sizeof(compact)) return 0U;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            continue;
        if (c == '\0' || c == ';' || n + 1U >= sizeof(compact))
            return 0U;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(names)/sizeof(names[0]); ++i)
        if (strcmp(compact, names[i]) == 0)
            return (unsigned int)i + 1U;
    return 0U;
}


/* Linux x86 idle.c CLFLUSH alternative: the ordinary instruction stream
 * contains only NOP padding, while .altinstr_replacement holds a CLFLUSH
 * against a physical RAX pointer. Feature 22*32+7 selects this replacement.
 * Preserve the EXACT Linux local labels, MAX-length .skip arithmetic,
 * PREL32 records and instruction-size bytes. This is not a plain prefetch,
 * and it is not safe to drop the alternative when the feature is absent. */
bool minic_x86_64_linux_idle_clflush_alt_form(
    const char *text, size_t length, bool core_numeric) {
    static const char prefix[] =
        "#ALT:oldnstr661:662:#ALT:padding"
        ".skip-(((6651f-6641f)-(662b-661b))>0)*"
        "((6651f-6641f)-(662b-661b)),0x90"
        "663:.pushsection.altinstructions,\"a\""
        ".long661b-..long6641f-..4byte(22*32+(7))"
        ".byte663b-661b.byte6651f-6641f"
        ".popsection.pushsection.altinstr_replacement,\"ax\""
        "#ALT:replacement16641:clflush(";
    static const char suffix[] = ")6651:.popsection";
    static const char *const operand[] = {"%[addr]", "%1"};
    char compact[512];
    char expected[512];
    size_t i, n = 0U;
    const int count = snprintf(expected, sizeof(expected), "%s%s%s",
        prefix, operand[core_numeric ? 1U : 0U], suffix);
    if (!text || !length || length >= sizeof(compact) ||
        count <= 0 || (size_t)count >= sizeof(expected))
        return false;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return false;
        compact[n++] = c;
    }
    compact[n] = '\0';
    return strcmp(compact, expected) == 0;
}

/* x86 PAT cache maintenance uses a real CLFLUSH (3E prefix) until the
 * kernel alternatives pass replaces it with 66 CLFLUSHOPT. Keep the full
 * ALT metadata and the writable memory operand; no plain CLFLUSH fallback. */
bool minic_x86_64_linux_pat_clflushopt_form(
    const char *text, size_t length) {
    static const char expected[] =
        "#ALT:oldnstr661:.byte0x3e;clflush%P0"
        "662:#ALT:padding"
        ".skip-(((6651f-6641f)-(662b-661b))>0)*"
        "((6651f-6641f)-(662b-661b)),0x90"
        "663:.pushsection.altinstructions,\"a\""
        ".long661b-..long6641f-..4byte(9*32+23)"
        ".byte663b-661b.byte6651f-6641f"
        ".popsection.pushsection.altinstr_replacement,\"ax\""
        "#ALT:replacement16641:.byte0x66;clflush%P0"
        "6651:.popsection";
    char compact[512];
    size_t i, n = 0U;
    if (text == NULL || length == 0U || length >= sizeof(compact))
        return false;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return false;
        compact[n++] = c;
    }
    compact[n] = '\0';
    return strcmp(compact, expected) == 0;
}


/* GNU futex's type-17 exception handler writes -EFAULT into a specific
 * physical register. Its `.macro extable_type_reg` is not decoration: an
 * ordinary LOCK/CMPXCHG alone is wrong if a user-address access faults.
 * Recognize the WHOLE macro and both exception records in the retry-loop
 * variants, then let the x86 emitter select and encode a real register. */
unsigned int minic_x86_64_linux_futex_extable_form(
    const char *text, size_t length) {
    static const char macro[] =
        ".macroextable_type_regtype:reqreg:req"
        ".set.Lfound,0.set.Lregnr,0"
        ".irprs,rax,rcx,rdx,rbx,rsp,rbp,rsi,rdi,r8,r9,r10,r11,r12,r13,r14,r15"
        ".ifc\\reg,%%\\rs"
        ".set.Lfound,.Lfound+1.long\\type+(.Lregnr<<8)"
        ".endif.set.Lregnr,.Lregnr+1.endr"
        ".set.Lregnr,0"
        ".irprs,eax,ecx,edx,ebx,esp,ebp,esi,edi,r8d,r9d,r10d,r11d,r12d,r13d,r14d,r15d"
        ".ifc\\reg,%%\\rs"
        ".set.Lfound,.Lfound+1.long\\type+(.Lregnr<<8)"
        ".endif.set.Lregnr,.Lregnr+1.endr"
        ".if(.Lfound!=1)"
        ".error\"extable_type_reg:badregisterargument\""
        ".endif.endm";
    static const char *const prefix[] = {
        "1:.pushsection.smp_locks,\"a\".balign4.long671f-..popsection"
        "671:lock;cmpxchgl%3,%22:",
        "1:xchgl%0,%22:",
        "1:.pushsection.smp_locks,\"a\".balign4.long671f-..popsection"
        "671:lock;xaddl%0,%22:",
        "1:movl%2,%02:movl%0,%3orl%4,%33:.pushsection.smp_locks,\"a\""
        ".balign4.long671f-..popsection671:lock;cmpxchgl%3,%2jnz2b4:",
        "1:movl%2,%02:movl%0,%3andl%4,%33:.pushsection.smp_locks,\"a\""
        ".balign4.long671f-..popsection671:lock;cmpxchgl%3,%2jnz2b4:",
        "1:movl%2,%02:movl%0,%3xorl%4,%33:.pushsection.smp_locks,\"a\""
        ".balign4.long671f-..popsection671:lock;cmpxchgl%3,%2jnz2b4:"
    };
    static const char begin[] =
        ".pushsection\"__ex_table\",\"a\".balign4.long(";
    static const char middle[] = ")-..long(";
    static const char before_macro[] = ")-.";
    char compact[1900], expected[1900];
    size_t i, n = 0U;
    if (text == NULL || length == 0U || length >= sizeof(compact))
        return 0U;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return 0U;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(prefix)/sizeof(prefix[0]); ++i) {
        const bool loop = i >= 3U;
        const char *reg = i == 0U ? "%0" : "%1";
        const char *first_to = loop ? "4b" : "2b";
        int written;

        written = snprintf(expected, sizeof(expected),
            "%s%s1b%s%s%s%s"
            "extable_type_regreg=%s,type=(17|((-14)<<16))"
            ".purgemextable_type_reg.popsection"
            "%s",
            prefix[i], begin, middle, first_to, before_macro, macro,
            reg, loop ? "__SECOND__" : "");
        if (written <= 0 || (size_t)written >= sizeof(expected))
            continue;
        if (loop) {
            char *second = strstr(expected, "__SECOND__");
            written = second == NULL ? -1 : snprintf(second,
                sizeof(expected) - (size_t)(second - expected),
                "%s3b%s4b%s%s"
                "extable_type_regreg=%%1,type=(17|((-14)<<16))"
                ".purgemextable_type_reg.popsection",
                begin, middle, before_macro, macro);
            if (written <= 0 ||
                (size_t)written >= sizeof(expected) - (size_t)(second - expected))
                continue;
        }
        if (strcmp(compact, expected) == 0)
            return (unsigned int)i + 1U;
    }
    return 0U;
}


/* Intel MDS/TAA idle mitigation clears CPU internal buffers through VERW.
 * Its selector is an actual 16-bit memory operand; the instruction has
 * hardware effects even when the input selector is a compile-time constant.
 * An arbitrary `m` GNU input is not permission for other privileged opcodes. */
bool minic_x86_64_linux_verw_clear_form(
    const char *text, size_t length) {
    static const char *const forms[] = {"verw%[ds]", "verw%0"};
    char compact[64];
    size_t i, n = 0U;
    if (!text || !length || length >= sizeof(compact)) return false;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
        if (c == '\0' || c == ';' || n + 1U >= sizeof(compact))
            return false;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(forms)/sizeof(forms[0]); ++i)
        if (strcmp(compact, forms[i]) == 0) return true;
    return false;
}


/* Linux debugreg.h admits DR0/DR1/DR2/DR3/DR6 and DR7 only. DR7 uses
 * a fake `m(*(u32 *)0x1000)` compiler dependency to block reordering of
 * the privileged read/write; dereferencing the fake address would be a
 * kernel fault. Transport that dependency as a memory role in Core and
 * validate it again in Target, without emitting a load/store at 0x1000. */
unsigned int minic_x86_64_linux_debug_register_form(
    const char *text, size_t length) {
    static const unsigned int names[] = {0U, 1U, 2U, 3U, 6U, 7U};
    char compact[64];
    char expected[64];
    size_t i, n = 0U;
    if (!text || !length || length >= sizeof(compact)) return 0U;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || c == ';' || n + 1U >= sizeof(compact))
            return 0U;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < 6U; ++i) {
        int count = snprintf(expected, sizeof(expected),
            "mov%%%%db%u,%%0", names[i]);
        if (count > 0 && (size_t)count < sizeof(expected) &&
            strcmp(compact, expected) == 0) return (unsigned int)i + 1U;
        count = snprintf(expected, sizeof(expected),
            "mov%%0,%%%%db%u", names[i]);
        if (count > 0 && (size_t)count < sizeof(expected) &&
            strcmp(compact, expected) == 0) return (unsigned int)i + 7U;
    }
    return 0U;
}
