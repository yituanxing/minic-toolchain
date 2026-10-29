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
    loongarch64:loongarch64_linux_trap_state
do
    arch=${record%%:*}
    fixture=${record#*:}
    "$host_cc" -E -P "$source_root/tests/target/$fixture.c" \
        -o "$workdir/$fixture.i"
    "$minic_cc" -S "$workdir/$fixture.i" --target="$arch-linux" \
        -fshort-wchar -funsigned-char -fno-PIE \
        -o "$workdir/$fixture.s"
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

printf 'PASS: original Linux arm64 TPIDR/AT/PAR alternatives and LoongArch IRQ stack/trap CSR contracts\n'
