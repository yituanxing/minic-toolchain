#!/bin/sh
# Real Linux 6.6 AArch64 / LoongArch64 privileged ABI contracts.
# Cross GNU assemblers are optional here; these source-to-assembly tests
# never execute kernel-mode instructions on the build host.
set -eu

minic_cc=${MINIC_CC:-build/debug/bin/minic-cc}
host_cc=${HOST_CC:-cc}
source_root=${MINIC_SOURCE_ROOT:-.}
workdir=$(mktemp -d)
trap 'rm -rf -- "$workdir"' EXIT HUP INT TERM

for record in \
    aarch64:aarch64_linux_cpu_state_and_fault \
    aarch64:aarch64_linux_kernel_nofault \
    aarch64:aarch64_linux_tpidr2 \
    aarch64:aarch64_linux_user_cache_fault \
    aarch64:linux_large_record_transfer \
    loongarch64:loongarch64_linux_trap_state \
    loongarch64:loongarch64_linux_fpr_transfer \
    loongarch64:linux_large_record_transfer
do
    arch=${record%%:*}
    fixture=${record#*:}
    output="$workdir/$fixture"
    if [ "$fixture" = linux_large_record_transfer ]; then
        output="$workdir/${arch}_$fixture"
    fi
    "$host_cc" -E -P "$source_root/tests/target/$fixture.c" \
        -o "$output.i"
    "$minic_cc" -S "$output.i" --target="$arch-linux" \
        -fshort-wchar -funsigned-char -fno-PIE \
        -o "$output.s"
done

# Kernel-address nofault accesses use ordinary LDR/STR, NOT unprivileged
# LDTR/STTR; the fault fixup is type 3 (type 2 means user-memory access).
arm_nofault="$workdir/aarch64_linux_kernel_nofault.s"
for opcode in \
    'ldrb w10, [x11]' 'ldrh w10, [x11]' \
    'ldr w10, [x11]' 'ldr x10, [x11]' \
    'strb w10, [x11]' 'strh w10, [x11]' \
    'str w10, [x11]' 'str x10, [x11]'
do
    if ! grep -Fq "$opcode" "$arm_nofault"; then
        printf 'FAIL: AArch64 kernel nofault opcode %s absent\n' "$opcode" >&2
        exit 1
    fi
done
if [ "$(grep -c '  .short 3$' "$arm_nofault")" -ne 8 ]; then
    printf 'FAIL: AArch64 kernel nofault type-3 exception count mismatched\n' >&2
    exit 1
fi

loong_fpr="$workdir/loongarch64_linux_fpr_transfer.s"
for reg in $(seq 0 31)
do
    if ! grep -Fq "movfr2gr.d \$t0, \$f$reg" "$loong_fpr" ||
       ! grep -Fq "movgr2fr.d \$f$reg, \$t0" "$loong_fpr"; then
        printf 'FAIL: LoongArch 64-bit FPR%s transfer direction absent\n' "$reg" >&2
        exit 1
    fi
done

# AArch64 SME task TLS is a real encoded MSR S3_3_C13_C0_5; missing
# TPIDR2 formerly stopped the entire arch/arm64/kernel/process.i TU.
if ! grep -Fq '.inst 0xd51bd0a9' "$workdir/aarch64_linux_tpidr2.s"; then
    printf 'FAIL: AArch64 encoded TPIDR2_EL0 write missing\n' >&2
    exit 1
fi
cache_fault="$workdir/aarch64_linux_user_cache_fault.s"
for instruction in 'dc civac, x10' 'sys 3, c7, c13, 1, x10' \
                   'sys 3, c7, c12, 1, x10' 'ic ivau, x10'
do
    if ! grep -Fq "$instruction" "$cache_fault"; then
        printf 'FAIL: Linux arm64 cache-maintenance fault operation missing %s\n' "$instruction" >&2
        exit 1
    fi
done
if [ "$(grep -c '  .short 1001$' "$cache_fault")" -ne 4 ] ||
   [ "$(grep -c '  .short 2$' "$cache_fault")" -ne 4 ]; then
    printf 'FAIL: arm64 cache-maintenance type-2/status-register exception entries lost\n' >&2
    exit 1
fi

# Native long record copies are counted loops, including record snapshots;
# small records keep the short, direct sequence. Test BOTH LP64 backends.
arm_record="$workdir/aarch64_linux_large_record_transfer.s"
for instruction in 'movz x12, #8192' 'ldrb w9, [x11], #1' \
                   'strb w9, [x10], #1' 'subs x12, x12, #1' 'b.ne 1b'
do
    if ! grep -Fq "$instruction" "$arm_record"; then
        printf 'FAIL: AArch64 large record copy/snapshot loop missing %s\n' "$instruction" >&2
        exit 1
    fi
done
loong_record="$workdir/loongarch64_linux_large_record_transfer.s"
for instruction in 'li.d $t3, 8192' 'ld.bu $t0, $t2, 0' \
                   'st.b $t0, $t1, 0' 'addi.d $t3, $t3, -1' \
                   'bnez $t3, 1b'
do
    if ! grep -Fq "$instruction" "$loong_record"; then
        printf 'FAIL: LoongArch large record copy/snapshot loop missing %s\n' "$instruction" >&2
        exit 1
    fi
done

arm="$workdir/aarch64_linux_cpu_state_and_fault.s"
for opcode in \
    'msr tpidr_el1, x9' \
    'msr tpidr_el2, x9' \
    '.hword 47' \
    'at s1e1r, x9' \
    'dmb sy' \
    '.hword 71' \
    '.pushsection .altinstructions,"a"' \
    '.org . - (664b-663b) + (662b-661b)'
do
    if ! grep -Fq "$opcode" "$arm"; then
        printf 'FAIL: arm64 Linux CPU-state opcode/patch %s absent\n' "$opcode" >&2
        exit 1
    fi
done

loong="$workdir/loongarch64_linux_trap_state.s"
for opcode in \
    'st.d $s0, $sp, 0' \
    'st.d $ra, $sp, 8' \
    'move $sp, $t0' \
    'move $a0, $t1' \
    'bl handle_loongarch_irq' \
    'move $sp, $s0' \
    'ld.d $ra, $sp, 8' \
    'ld.d $s0, $sp, 0' \
    'pcaddi $ra, 0' \
    'st.d $r1, $t0, 8' \
    'st.d $r31, $t0, 248' \
    'movgr2fcsr $fcsr0, $t0'
do
    if ! grep -Fq "$opcode" "$loong"; then
        printf 'FAIL: LoongArch Linux IRQ/trap opcode %s absent\n' "$opcode" >&2
        exit 1
    fi
done
for csr in 0 1 2 4 5 7
do
    if ! grep -Fq "csrrd \$t0, $csr" "$loong"; then
        printf 'FAIL: LoongArch Linux trap CSR %s absent\n' "$csr" >&2
        exit 1
    fi
done

# Shape mutations must not cause Target to accept a different privileged
# instruction or to silently replace the kernel's runtime patch record.
sed 's/tpidr_el2/tpidr_el0/g' \
    "$workdir/aarch64_linux_cpu_state_and_fault.i" > "$workdir/bad_tpidr.i"
if "$minic_cc" -S "$workdir/bad_tpidr.i" --target=aarch64-linux \
    -o "$workdir/bad_tpidr.s" >/dev/null 2>&1; then
    printf 'FAIL: mutated AArch64 TPIDR alternative was admitted\n' >&2
    exit 1
fi
sed 's/handle_loongarch_irq/hijack_loongarch_irq/g' \
    "$workdir/loongarch64_linux_trap_state.i" > "$workdir/bad_irq.i"
if "$minic_cc" -S "$workdir/bad_irq.i" --target=loongarch64-linux \
    -o "$workdir/bad_irq.s" >/dev/null 2>&1; then
    printf 'FAIL: mutated LoongArch IRQ stack-switch call was admitted\n' >&2
    exit 1
fi
sed 's/\.short (3)/.short (4)/g' \
    "$workdir/aarch64_linux_kernel_nofault.i" > "$workdir/bad_nofault.i"
if "$minic_cc" -S "$workdir/bad_nofault.i" --target=aarch64-linux \
    -o "$workdir/bad_nofault.s" >/dev/null 2>&1; then
    printf 'FAIL: AArch64 corrupted kernel-nofault exception type admitted\n' >&2
    exit 1
fi
sed 's/"31"/"32"/g' \
    "$workdir/loongarch64_linux_fpr_transfer.i" > "$workdir/bad_fpr.i"
if "$minic_cc" -S "$workdir/bad_fpr.i" --target=loongarch64-linux \
    -o "$workdir/bad_fpr.s" >/dev/null 2>&1; then
    printf 'FAIL: LoongArch out-of-range FPR32 transfer admitted\n' >&2
    exit 1
fi
sed 's/((13) << 12)/((14) << 12)/g' \
    "$workdir/aarch64_linux_tpidr2.i" > "$workdir/bad_tpidr2.i"
if "$minic_cc" -S "$workdir/bad_tpidr2.i" --target=aarch64-linux \
    -o "$workdir/bad_tpidr2.s" >/dev/null 2>&1; then
    printf 'FAIL: unknown AArch64 encoded TPIDR2 sysreg admitted\n' >&2
    exit 1
fi
sed 's/"2"/"3"/g' \
    "$workdir/aarch64_linux_user_cache_fault.i" > "$workdir/bad_cache_fault.i"
if "$minic_cc" -S "$workdir/bad_cache_fault.i" --target=aarch64-linux \
    -o "$workdir/bad_cache_fault.s" >/dev/null 2>&1; then
    printf 'FAIL: invalid cache maintenance exception type-3 admitted\n' >&2
    exit 1
fi

printf 'PASS: Linux arm64 TPIDR2/TPIDR/AT/PAR, cache-maintenance type-2 and kernel nofault type-3; LoongArch IRQ/trap CSR/FPR, both LP64 large record loops\n'
