#!/bin/sh
# Target probe families share exact build setup; each case retains its original oracle.
set -eu
if [ "$#" -lt 1 ]; then
    echo "usage: $0 CASE" >&2; exit 2
fi
_probe_case=$1
shift
case "$_probe_case" in
    aarch64-linux-apple-aic-sysregs) _probe_family=1; _probe_src='tests/compiler/c0/aarch64_linux_apple_aic_sysregs.c'; _probe_i='positive'; _probe_s='positive' ;;
    aarch64-linux-bpf-enum-acqrel) _probe_family=1; _probe_src='tests/compiler/c0/aarch64_linux_bpf_enum_acqrel.c'; _probe_i='positive'; _probe_s='positive' ;;
    aarch64-linux-lse-atomic-full) _probe_family=1; _probe_src='tests/compiler/c0/aarch64_linux_lse_atomic_full.c'; _probe_i='positive'; _probe_s='positive' ;;
    aarch64-linux-debug-pmu-mrs-banks) _probe_family=2; _probe_src='tests/compiler/c0/aarch64_linux_debug_pmu_mrs_banks.c'; _probe_i='banks'; _probe_s='banks' ;;
    aarch64-linux-hyp-debug-qcom-sysregs) _probe_family=2; _probe_src='tests/compiler/c0/aarch64_linux_hyp_debug_qcom_sysregs.c'; _probe_i='debug'; _probe_s='debug' ;;
    aarch64-linux-hyp-spinlock-lse) _probe_family=2; _probe_src='tests/compiler/c0/aarch64_linux_hyp_spinlock_lse.c'; _probe_i='hyp'; _probe_s='hyp' ;;
    aarch64-linux-kvm-gic-registers) _probe_family=2; _probe_src='tests/compiler/c0/aarch64_linux_kvm_gic_registers.c'; _probe_i='input'; _probe_s='gic' ;;
    aarch64-linux-kvm-kern-hyp-va-callback) _probe_family=2; _probe_src='tests/compiler/c0/aarch64_linux_kvm_kern_hyp_va_callback.c'; _probe_i='mask'; _probe_s='mask' ;;
    aarch64-linux-kvm-tlbi-range) _probe_family=2; _probe_src='tests/compiler/c0/aarch64_linux_kvm_tlbi_range.c'; _probe_i='tlbi'; _probe_s='tlbi' ;;
    aarch64-linux-kvm-tlbi) _probe_family=2; _probe_src='tests/compiler/c0/aarch64_linux_kvm_tlbi.c'; _probe_i='input'; _probe_s='tlbi' ;;
    aarch64-linux-pstate-pan-tco-alternatives) _probe_family=2; _probe_src='tests/compiler/c0/aarch64_linux_pstate_pan_tco_alternatives.c'; _probe_i='pstate'; _probe_s='pstate' ;;
    aarch64-linux-rcu-record-acquire-release) _probe_family=2; _probe_src='tests/compiler/c0/aarch64_linux_rcu_record_acquire_release.c'; _probe_i='rcu'; _probe_s='rcu' ;;
    aarch64-linux-efi-implicit-cache) _probe_family=3; _probe_src='tests/compiler/c0/aarch64_linux_efi_implicit_cache.c'; _probe_i='efi'; _probe_s='efi' ;;
    aarch64-linux-smmu-dynamic-dmb) _probe_family=3; _probe_src='tests/compiler/c0/aarch64_linux_smmu_dynamic_dmb.c'; _probe_i='real'; _probe_s='real' ;;
    aarch64-linux-mrs-read-family) _probe_family=4; _probe_src='tests/compiler/c0/aarch64_linux_mrs_read_family.c'; _probe_i='read'; _probe_s='read' ;;
    aarch64-linux-smccc-v11-full) _probe_family=4; _probe_src='tests/compiler/c0/aarch64_linux_smccc_v11_full.c'; _probe_i='real'; _probe_s='real' ;;
    aarch64-linux-tegra-ndiv-sysreg) _probe_family=4; _probe_src='tests/compiler/c0/aarch64_linux_tegra_ndiv_sysreg.c'; _probe_i='real'; _probe_s='real' ;;
    float32-const-eval) _probe_family=5; _probe_src='tests/compiler/c0/float32_const_eval.c'; _probe_i='source'; _probe_s='source' ;;
    runtime-designated-positional) _probe_family=5; _probe_src='tests/compiler/c0/runtime_designated_positional.c'; _probe_i='source'; _probe_s='source' ;;
    gnu-linux-nf-conntrack-size) _probe_family=6; _probe_src='tests/compiler/c0/gnu_linux_nf_conntrack_total_size.c'; _probe_i='positive'; _probe_s='gnu' ;;
    gnu-linux-task-mmu-swp-pfn-guard) _probe_family=6; _probe_src='tests/compiler/c0/gnu_linux_task_mmu_swp_pfn_guard.c'; _probe_i='task_mmu'; _probe_s='gnu' ;;
    *) echo "unknown target probe: $_probe_case" >&2; exit 2 ;;
esac
case "$_probe_family" in
    1)
        root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
        mini=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
        src="$root/$_probe_src"
        work=$(mktemp -d)
        trap 'rm -rf "$work"' EXIT HUP INT TERM
        "${HOST_CC:-cc}" -std=gnu11 -E -P "$src" -o "$work/$_probe_i.i"
        "$mini" --target=aarch64-linux -S "$work/$_probe_i.i" -o "$work/$_probe_s.s"
        ;;
    2)
        root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
        minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
        work=$(mktemp -d)
        trap 'rm -rf "$work"' EXIT HUP INT TERM
        "${HOST_CC:-cc}" -E -P -std=gnu11 -x c "$root/$_probe_src" -o "$work/$_probe_i.i"
        "$minic" --target=aarch64-linux -S "$work/$_probe_i.i" -o "$work/$_probe_s.s"
        ;;
    3)
        root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
        mini=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
        host=${HOST_CC:-cc}
        work=$(mktemp -d)
        trap 'rm -rf "$work"' EXIT HUP INT TERM
        src="$root/$_probe_src"
        "$host" -E -P -std=gnu11 "$src" -o "$work/$_probe_i.i"
        "$mini" --target=aarch64-linux -S "$work/$_probe_i.i" -o "$work/$_probe_s.s"
        ;;
    4)
        root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
        mini=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
        work=$(mktemp -d)
        trap 'rm -rf "$work"' EXIT HUP INT TERM
        src="$root/$_probe_src"
        "${HOST_CC:-cc}" -E -P -std=gnu11 "$src" -o "$work/$_probe_i.i"
        "$mini" --target=aarch64-linux -S "$work/$_probe_i.i" -o "$work/$_probe_s.s"
        ;;
    5)
        : "${MINIC_CC:?MINIC_CC path required}"
        root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
        work=$(mktemp -d)
        trap 'rm -rf "$work"' EXIT HUP INT TERM
        ${CC:-cc} -E -P -x c "$root/$_probe_src" -o "$work/$_probe_i.i"
        "$MINIC_CC" --target=x86_64-linux -S "$work/$_probe_i.i" -o "$work/$_probe_s.s"
        ;;
    6)
        root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
        mini=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
        host=${HOST_CC:-cc}
        source="$root/$_probe_src"
        work=$(mktemp -d)
        trap 'rm -r -- "$work"' EXIT HUP INT TERM
        "$host" -E -P -std=gnu11 "$source" -o "$work/$_probe_i.i"
        "$host" -std=gnu11 -O2 -S "$work/$_probe_i.i" -o "$work/$_probe_s.s"
        "$mini" --target=x86_64-linux -S "$work/$_probe_i.i" -o "$work/minic.s"
        ;;
esac
case "$_probe_case" in
    aarch64-linux-apple-aic-sysregs)
# Exact Apple M1 AIC SYS_* encodings; strict source/role and unknown-tuple gates.
test "$(grep -Ec '^  \.inst 0xd5' "$work/positive.s")" -eq 6
for target in x86_64 loongarch64; do
    if "$mini" --target="$target-linux" -S "$work/positive.i" \
        -o "$work/foreign.s" >/dev/null 2>&1; then
        echo "APPLE_AIC_SYSREG=FAIL foreign=$target" >&2; exit 1
    fi
done
sed 's/WRITE_AIC_REG(ipi_rr_local,   5, 15, 0, 0)/WRITE_AIC_REG(ipi_rr_local,   5, 15, 7, 7)/' \
    "$src" > "$work/bad-tuple.c"
sed 's/"rZ" (value)/"m" (value)/' "$src" > "$work/bad-constraint.c"
sed 's/msr_s, sreg, rt/msr_bad, sreg, rt/' \
    "$src" > "$work/bad-macro.c"
sed 's/0xd5000000/0xd5100000/' \
    "$src" > "$work/bad-opcode.c"
for variant in bad-tuple bad-constraint bad-macro bad-opcode; do
    "${HOST_CC:-cc}" -std=gnu11 -E -P "$work/$variant.c" \
        -o "$work/$variant.i"
    if "$mini" --target=aarch64-linux -S "$work/$variant.i" \
        -o "$work/$variant.s" >/dev/null 2>&1; then
        echo "APPLE_AIC_SYSREG=FAIL unsafe=$variant" >&2; exit 1
    fi
done
if command -v aarch64-linux-gnu-gcc >/dev/null 2>&1; then
    aarch64-linux-gnu-gcc -c "$work/positive.s" -o "$work/minic.o"
    aarch64-linux-gnu-gcc -c "$src" -o "$work/gnu.o"
    echo 'APPLE_AIC_SYSREG=PASS writes=6 foreign=2 negative=4 GAS=PASS'
else
    echo 'APPLE_AIC_SYSREG=PASS writes=6 foreign=2 negative=4 GAS=SKIP'
fi
        ;;
    aarch64-linux-bpf-enum-acqrel)
# Exact Linux BPF enum-width acquire/release + strict GNU constraint rejects.
grep -Fq '  ldar w9, [x10]' "$work/positive.s"
grep -Fq '  stlr w9, [x10]' "$work/positive.s"
for target in x86_64 loongarch64; do
    if "$mini" --target="$target-linux" -S "$work/positive.i" \
        -o "$work/foreign.s" >/dev/null 2>&1; then
        echo "BPF_ENUM_ACQREL=FAIL foreign=$target" >&2; exit 1
    fi
done
sed 's/"Q" (value->state)/"m" (value->state)/' \
    "$src" > "$work/bad-memory.c"
sed 's/"=r" (result)/"=m" (result)/' \
    "$src" > "$work/bad-output.c"
sed 's/ldar %w0/ldaxr %w0/' \
    "$src" > "$work/bad-opcode.c"
for variant in bad-memory bad-output bad-opcode; do
    "${HOST_CC:-cc}" -std=gnu11 -E -P "$work/$variant.c" \
        -o "$work/$variant.i"
    if "$mini" --target=aarch64-linux -S "$work/$variant.i" \
        -o "$work/$variant.s" >/dev/null 2>&1; then
        echo "BPF_ENUM_ACQREL=FAIL unsafe=$variant" >&2; exit 1
    fi
done
"${HOST_CC:-cc}" -std=gnu11 -E -P -DMINIC_BAD_LARGE_ENUM "$src" \
    -o "$work/bad-width.i"
if "$mini" --target=aarch64-linux -S "$work/bad-width.i" \
    -o "$work/bad-width.s" >/dev/null 2>&1; then
    echo 'BPF_ENUM_ACQREL=FAIL unsafe=wide-enum' >&2; exit 1
fi
if command -v aarch64-linux-gnu-gcc >/dev/null 2>&1; then
    aarch64-linux-gnu-gcc -c "$work/positive.s" -o "$work/minic.o"
    aarch64-linux-gnu-gcc -c "$src" -o "$work/gnu.o"
    echo 'BPF_ENUM_ACQREL=PASS enum32=2 foreign=2 negative=4 GAS=PASS'
else
    echo 'BPF_ENUM_ACQREL=PASS enum32=2 foreign=2 negative=4 GAS=SKIP'
fi
        ;;
    aarch64-linux-lse-atomic-full)
# Exact Linux AArch64 CASP/CASPAL + signed CASAL retry, with role negatives.
# MachineIR emits the original GNU template (including its whitespace),
# rather than the removed direct FILE* emitter's two-space presentation.
# Keep the ISA, fixed-register pair, memory base and retry target exact.
for mnemonic in casp caspal; do
    grep -Eq "^[[:space:]]*$mnemonic[[:space:]]+x0,[[:space:]]*x1,[[:space:]]*x2,[[:space:]]*x3,[[:space:]]*\\[x10\\][[:space:]]*$" \
        "$work/positive.s"
done
grep -Eq '^[[:space:]]*casal[[:space:]]+x12,[[:space:]]*x9,[[:space:]]*\[x10\][[:space:]]*$' \
    "$work/positive.s"
grep -Eq '^[[:space:]]*\.arch_extension[[:space:]]+lse[[:space:]]*$' \
    "$work/positive.s"
grep -Eq '^[[:space:]]*cbnz[[:space:]]+x12,[[:space:]]*1b[[:space:]]*$' \
    "$work/positive.s"
for target in x86_64 loongarch64; do
    if "$mini" --target="$target-linux" -S "$work/positive.i" \
         -o "$work/foreign.s" >/dev/null 2>&1; then
        echo "AARCH64_LSE_FULL=FAIL foreign=$target" >&2; exit 1
    fi
done
sed 's/register unsigned long x3 asm("x3")/register unsigned long x3 asm("x7")/' \
    "$src" >"$work/bad-fixed.c"
sed 's/\[old1\] "+&r"/\[old1\] "+r"/' \
    "$src" >"$work/bad-early.c"
sed 's/\[v\] "+Q"/\[v\] "+m"/g' \
    "$src" >"$work/bad-memory.c"
sed 's/"casp" ORDER/"caspX" ORDER/' \
    "$src" >"$work/bad-opcode.c"
sed 's/casal/cas/' \
    "$src" >"$work/bad-dec-order.c"
for variant in bad-fixed bad-early bad-memory bad-opcode bad-dec-order; do
    "${HOST_CC:-cc}" -std=gnu11 -E -P "$work/$variant.c" \
        -o "$work/$variant.i"
    if "$mini" --target=aarch64-linux -S "$work/$variant.i" \
         -o "$work/$variant.s" >/dev/null 2>&1; then
        echo "AARCH64_LSE_FULL=FAIL unsafe=$variant" >&2; exit 1
    fi
done
if command -v aarch64-linux-gnu-gcc >/dev/null 2>&1; then
    aarch64-linux-gnu-gcc -c "$work/positive.s" -o "$work/minic.o"
    aarch64-linux-gnu-gcc -O2 -c "$src" -o "$work/gnu.o"
    echo 'AARCH64_LSE_FULL=PASS forms=3 foreign=2 negative=5 GAS=PASS'
else
    echo 'AARCH64_LSE_FULL=PASS forms=3 foreign=2 negative=5 GAS=SKIP'
fi
        ;;
    aarch64-linux-debug-pmu-mrs-banks)
test "$(grep -Ec '^  mrs x9, (dbg|pm)' "$work/banks.s")" -eq 72
for reg in dbgbcr0_el1 dbgbvr15_el1 dbgwcr7_el1 dbgwvr15_el1 \
    pmevcntr0_el0 pmevcntr15_el0 pmevcntr30_el0 \
    pmcr_el0 pmccntr_el0 pmovsclr_el0 pmceid0_el0 pmceid1_el0; do
    grep -Fq "mrs x9, $reg" "$work/banks.s"
done
# RV's generic GNU "mrs" passthrough is a separate backend contract gap;
# only assert targets currently enforcing a strict matching ASM grammar.
for target in x86_64 loongarch64; do
    if "$minic" --target="$target-linux" -S "$work/banks.i" \
        -o "$work/$target.s" >/dev/null 2>&1; then
        echo "FAIL accepted AArch debug/PMU MRS on $target" >&2
        exit 1
    fi
done
sed 's/dbgwvr15_el1/dbgwvr16_el1/g' "$work/banks.i" > "$work/negative-debug.i"
sed 's/pmevcntr30_el0/pmevcntr31_el0/g' "$work/banks.i" > "$work/negative-pmu.i"
sed 's/pmovsclr_el0/pmovsset_el0/g' "$work/banks.i" > "$work/negative-reg.i"
sed 's/"=r"(value)/"=m"(value)/g' "$work/banks.i" > "$work/negative-constraint.i"
sed 's/#reg : "=r"/#reg "; nop" : "=r"/' \
    "$root/tests/compiler/c0/aarch64_linux_debug_pmu_mrs_banks.c" \
    > "$work/negative-extra.c"
"${HOST_CC:-cc}" -E -P -std=gnu11 -x c "$work/negative-extra.c" \
    -o "$work/negative-extra.i"
for category in debug pmu reg constraint extra; do
    if "$minic" --target=aarch64-linux -S "$work/negative-$category.i" \
        -o "$work/negative-$category.s" >/dev/null 2>&1; then
        echo "FAIL accepted unsupported AArch debug/PMU $category" >&2
        exit 1
    fi
done
printf '%s\n' 'PASS exact GNU AArch 64 debug +8 PMU MRS, 2ISA gates +5 negative'
        ;;
    aarch64-linux-hyp-debug-qcom-sysregs)
test "$(grep -c '^  \.inst 0xd5' "$work/debug.s")" -eq 13
for opcode in '0xd51bf0c9' '0xd51bf0e9'; do
    grep -Fq ".inst $opcode" "$work/debug.s"
done
for reg in id_aa64dfr0_el1 mdccint_el1 tpidrro_el0 sp_el1 vmpidr_el2 \
    CPACR_EL1 SCTLR_EL1; do
    grep -Fq "mrs x9, $reg" "$work/debug.s"
done
for number in 17 18; do
    grep -Fq "hint #$number" "$work/debug.s"
done
for target in x86_64 loongarch64 riscv64 riscv32; do
    if "$minic" --target="$target-linux" -S "$work/debug.i" \
        -o "$work/$target.s" >/dev/null 2>&1; then
        echo "FAIL accepted AArch hyp debug/QCOM sysreg on $target" >&2
        exit 1
    fi
done
sed 's/"6"/"5"/' "$work/debug.i" > "$work/wrong-vendor.i"
sed 's/"rZ"(value)/"m"(value)/' "$work/debug.i" > "$work/wrong-constraint.i"
sed 's/"=r"(value)/"=m"(value)/' "$work/debug.i" > "$work/wrong-mrs.i"
sed 's/hint #18/hint #19/' "$work/debug.i" > "$work/wrong-hint.i"
for category in vendor constraint mrs hint; do
    if "$minic" --target=aarch64-linux -S "$work/wrong-$category.i" \
        -o "$work/wrong-$category.s" >/dev/null 2>&1; then
        echo "FAIL accepted unsupported AArch hardware $category" >&2
        exit 1
    fi
done
printf '%s\n' 'PASS GNU AArch KVM/SME 7 MRS, QCOM/VHE/SME 13 encoded MSR, PSB/TSB 2, 5ISA +4 negative'
        ;;
    aarch64-linux-hyp-spinlock-lse)
test "$(grep -c '^  \.hword 33$' "$work/hyp.s")" -eq 2
test "$(grep -c '^  \.word 661b - \.$' "$work/hyp.s")" -eq 2
test "$(grep -c '^  \.word 663f - \.$' "$work/hyp.s")" -eq 2
test "$(grep -c '^  \.org \.' "$work/hyp.s")" -eq 4
for opcode in 'ldaxr w9' 'stxr w11' 'ldadda w11' 'ldaxrh w11' \
    'stlrh w9' 'staddlh w9'; do
    grep -Fq "$opcode" "$work/hyp.s"
done
for target in x86_64 loongarch64 riscv64 riscv32; do
    if "$minic" --target="$target-linux" -S "$work/hyp.i" \
        -o "$work/$target.s" >/dev/null 2>&1; then
        echo "FAIL accepted AArch hyp spinlock on $target" >&2
        exit 1
    fi
done
sed 's/ldadda/ldadd/' "$work/hyp.i" > "$work/wrong-opcode.i"
sed 's/"+Q"(\*lock)/"+m"(*lock)/' "$work/hyp.i" > "$work/wrong-constraint.i"
sed 's/".hword 33\\n"/".hword 34\\n"/' "$work/hyp.i" > "$work/wrong-feature.i"
sed 's/"=&r"(newval)/"=r"(newval)/' "$work/hyp.i" > "$work/wrong-earlyclobber.i"
for category in opcode constraint feature earlyclobber; do
    if "$minic" --target=aarch64-linux -S "$work/wrong-$category.i" \
        -o "$work/wrong-$category.s" >/dev/null 2>&1; then
        echo "FAIL accepted modified hyp spinlock $category" >&2
        exit 1
    fi
done
printf '%s\n' 'PASS Linux nvhe hyp ticket-spinlock GNU LSE/LLSC lock+unlock, 5ISA, four fail-closed'
        ;;
    aarch64-linux-kvm-gic-registers)
test -s "$work/gic.s"
test "$(grep -c '^  \.inst 0xd5' "$work/gic.s")" -eq 24
for reg in ICH_AP0R0_EL2 ICH_AP1R3_EL2 ICH_LR0_EL2 ICH_LR7_EL2 \
    ICH_LR8_EL2 ICH_LR15_EL2; do
    grep -Fq "write_$reg:" "$work/gic.s"
done
for form in '8 0' '8 3' '9 0' '9 3' '12 0' '12 7' '13 0' '13 7'; do
    set -- $form
    crm=$1; op2=$2
    encoding=$(printf '0x%08x' "$((0xd5000000 | (3 << 19) | (4 << 16) | (12 << 12) | (crm << 8) | (op2 << 5) | 9))")
    grep -Fq ".inst $encoding" "$work/gic.s"
done
for target in x86_64 loongarch64 riscv64 riscv32; do
    if "$minic" --target="$target-linux" -S "$work/input.i" \
        -o "$work/$target.s" >/dev/null 2>&1; then
        printf 'FAIL ARM64 GIC reg accepted by %s\n' "$target" >&2
        exit 1
    fi
done
printf '%s\n' 'PASS compiler/c0/aarch64_linux_kvm_gic_registers GNU_macro_ops=24 encoding_spotchecks=8 target-gate=4'
        ;;
    aarch64-linux-kvm-kern-hyp-va-callback)
test "$(grep -Ec '^[[:space:]]*\.word[[:space:]]+kvm_update_va_mask[[:space:]]*-[[:space:]]*\.$' "$work/mask.s")" -eq 1
test "$(grep -Ec '^[[:space:]]*\.hword[[:space:]]+\(1 << 15\) \| \(1\)$' "$work/mask.s")" -eq 1
test "$(grep -Ec '^[[:space:]]*\.word[[:space:]]+661b[[:space:]]*-[[:space:]]*\.$' "$work/mask.s")" -eq 1
test "$(grep -Ec '^[[:space:]]*\.org([[:space:]]|$)' "$work/mask.s")" -eq 0
for opcode in 'and x9, x9, #1' 'ror x9, x9, #1' \
    'add x9, x9, #0' 'ror x9, x9, #63'; do
    grep -Fq "$opcode" "$work/mask.s"
done
# GCC/GAS accepts an optional '#' before an AArch64 shift immediate.
grep -Eq 'add[[:space:]]+x9, x9, #0, lsl[[:space:]]+#?12([[:space:]]|$)' "$work/mask.s"
cross=${AARCH64_CC:-aarch64-linux-gnu-gcc}
if command -v "$cross" >/dev/null 2>&1; then
    "$cross" -c -x assembler "$work/mask.s" -o "$work/mask.o"
    test -s "$work/mask.o"
    echo 'AARCH64_KVM_VA_CALLBACK_GNU_AS=PASS'
else
    echo 'AARCH64_KVM_VA_CALLBACK_GNU_AS=SKIP missing AArch64 GNU assembler'
fi
# RV32/RV64 still have an existing generic GNU-ASM passthrough path;
# C-only -S on those targets does not imply acceptance by their assembler.
for target in x86_64 loongarch64; do
    if "$minic" --target="$target-linux" -S "$work/mask.i" \
        -o "$work/$target.s" >/dev/null 2>&1; then
        echo "FAIL accepted AArch KVM VA mask callback on $target" >&2
        exit 1
    fi
done
sed 's/kvm_update_va_mask/kvm_update_va_pretend/' "$work/mask.i" > "$work/wrong-callback.i"
sed 's/(1 << 15)/(1 << 14)/' "$work/mask.i" > "$work/wrong-cap.i"
sed 's/"+r"(value)/"=r"(value)/' "$work/mask.i" > "$work/wrong-constraint.i"
sed 's/ror %0, %0, #63/ror %0, %0, #62/' "$work/mask.i" > "$work/wrong-rotation.i"
sed 's/" .byte 664f-663f/" .byte 663f-663f/' "$work/mask.i" > "$work/wrong-length.i"
for category in callback cap constraint rotation length; do
    if "$minic" --target=aarch64-linux -S "$work/wrong-$category.i" \
        -o "$work/wrong-$category.s" >/dev/null 2>&1; then
        echo "FAIL accepted invalid KVM VA mask $category" >&2
        exit 1
    fi
done
printf '%s\n' 'PASS KVM __kern_hyp_va GNU ALTERNATIVE_CB callback PREL32 + live 5 instruction patch, 2 target gates +5 negative'
        ;;
    aarch64-linux-kvm-tlbi-range)
for mnemonic in ripas2e1is ripas2e1 vmalls12e1; do
    grep -Fq "tlbi $mnemonic" "$work/tlbi.s"
done
printf '%s\n' \
    'void unknown(unsigned long v) { asm volatile(".arch armv8.5-a\n\ttlbi ripas2e2, %x0" :: "rZ"(v)); }' \
    > "$work/unknown.i"
if "$minic" --target=aarch64-linux -S "$work/unknown.i" -o "$work/unknown.s" >/dev/null 2>&1; then
    echo 'FAIL accepted unrecognized TLBI opcode' >&2
    exit 1
fi
printf '%s\n' \
    'void bad(unsigned long v) { asm volatile(".arch armv8.5-a\n\ttlbi ripas2e1is, %x0\n\tnop" :: "rZ"(v)); }' \
    > "$work/bad.i"
if "$minic" --target=aarch64-linux -S "$work/bad.i" -o "$work/bad.s" >/dev/null 2>&1; then
    echo 'FAIL accepted appended unqualified hardware opcode' >&2
    exit 1
fi
printf '%s\n' 'PASS AArch64 KVM range TLBI hardware=3 forbidden=2'
        ;;
    aarch64-linux-kvm-tlbi)
test -s "$work/tlbi.s"
for op in vale1is vale2is aside1is rvale1is vae1is rvae1is \
    vaale1is vaae1is ipas2e1is ipas2e1 vae2is vmalle1 \
    vmalle1is alle1is vmalls12e1is; do
    case "$op" in
        vmalle1|vmalle1is|alle1is|vmalls12e1is)
            grep -Eq "^[[:space:]]*tlbi $op[[:space:]]*$" "$work/tlbi.s" ;;
        *)
            grep -Eq "^[[:space:]]*tlbi $op, x9[[:space:]]*$" "$work/tlbi.s" ;;
    esac
done
for target in x86_64 riscv32 riscv64 loongarch64; do
    if "$minic" --target="$target-linux" -S "$work/input.i" \
        -o "$work/$target.s" >/dev/null 2>&1; then
        printf 'FAIL ARM64 KVM TLBI accepted by %s\n' "$target" >&2
        exit 1
    fi
done
printf '%s\n' 'PASS compiler/c0/aarch64_linux_kvm_tlbi hardware_ops=15 target-gate=4'
        ;;
    aarch64-linux-pstate-pan-tco-alternatives)
test "$(grep -c '^  \.hword 38$' "$work/pstate.s")" -eq 2
test "$(grep -c '^  \.hword 53$' "$work/pstate.s")" -eq 3
test "$(grep -c '^  \.org \.' "$work/pstate.s")" -eq 10
test "$(grep -c '^  \.if 0 == 1$' "$work/pstate.s")" -eq 1
for opcode in 0xd500409f 0xd500419f 0xd503409f 0xd503419f; do
    grep -Fq ".inst $opcode" "$work/pstate.s"
done
# RV32/RV64 currently retain their pre-existing generic zero-operand GNU
# inline-ASM passthrough path; their -S is not a machine-ISA validation.
for target in x86_64 loongarch64; do
    if "$minic" --target="$target-linux" -S "$work/pstate.i" \
        -o "$work/$target.s" >/dev/null 2>&1; then
        echo "FAIL accepted AArch PAN/TCO on $target" >&2
        exit 1
    fi
done
sed 's/"38"/"39"/' "$work/pstate.i" > "$work/wrong-cap.i"
sed 's/"53"/"52"/' "$work/pstate.i" > "$work/wrong-mte-cap.i"
sed 's/".if " "0"/".if " "2"/' "$work/pstate.i" > "$work/wrong-config.i"
sed 's/".org . - (664b-663b) + (662b-661b)/".org . - (664b-663b) + (661b-661b)/' \
    "$work/pstate.i" > "$work/wrong-length.i"
sed 's/"(0xd500401f/"(0xd500401e/' "$work/pstate.i" > "$work/wrong-opcode.i"
for category in cap mte-cap config length opcode; do
    if "$minic" --target=aarch64-linux -S "$work/wrong-$category.i" \
        -o "$work/wrong-$category.s" >/dev/null 2>&1; then
        echo "FAIL accepted modified PSTATE alternative $category" >&2
        exit 1
    fi
done
printf '%s\n' 'PASS Linux PAN cap38 / MTE TCO cap53 ALT GNU original .inst+PREL32 cfg enabled/disabled, 2 target gates +5 negative'
        ;;
    aarch64-linux-rcu-record-acquire-release)
test "$(grep -c '^  ldarb w9, \[x10\]$' "$work/rcu.s")" -eq 1
test "$(grep -c '^  stlrb w9, \[x10\]$' "$work/rcu.s")" -eq 1
for target in x86_64 loongarch64 riscv64 riscv32; do
    if "$minic" --target="$target-linux" -S "$work/rcu.i" \
        -o "$work/$target.s" >/dev/null 2>&1; then
        echo "FAIL accepted AArch RCU LDARB/STLRB on $target" >&2
        exit 1
    fi
done
sed 's/"Q"(\*flags)/"m"(*flags)/' "$work/rcu.i" > "$work/wrong-constraint.i"
sed 's/unsigned char s;/unsigned long s;/' "$work/rcu.i" > "$work/wrong-width.i"
sed 's/ldarb %w0/ldarh %w0/' "$work/rcu.i" > "$work/wrong-opcode.i"
sed 's/"memory"/"cc"/' "$work/rcu.i" > "$work/wrong-clobber.i"
for category in constraint width opcode clobber; do
    if "$minic" --target=aarch64-linux -S "$work/wrong-$category.i" \
        -o "$work/wrong-$category.s" >/dev/null 2>&1; then
        echo "FAIL accepted invalid AArch RCU $category" >&2
        exit 1
    fi
done
printf '%s\n' 'PASS AArch GNU RCU Q union carrier exact byte LDARB/STLRB, 5ISA +4 negative'
        ;;
    aarch64-linux-efi-implicit-cache)
test "$(grep -c '^  dc civac, x9$' "$work/efi.s")" -eq 2
test "$(grep -c '^  dc cvau, x9$' "$work/efi.s")" -eq 1
grep -Fq '  ic ialluis' "$work/efi.s"
grep -Fq '  dsb ish' "$work/efi.s"
grep -Fq '  isb' "$work/efi.s"

# Alternate DC/IC cache opcodes retain their original stricter contract;
# only EFI DCTYPE civac/cvau is admitted without an explicit memory clobber.
sed 's/"civac"/"cvac"/' "$src" >"$work/wrong-dctype.c"
sed 's/"cvau"/"ivac"/' "$src" >"$work/wrong-cvau.c"
sed 's/"dc " "civac" ", %0"/"dc " "civac" ", %1"/' \
    "$src" >"$work/wrong-operand.c"
sed 's/"dc " "civac" ", %0"/"dc " "civac" ", %0 ; nop"/' \
    "$src" >"$work/extra-instruction.c"
sed 's/"r"(base)/"m"(base)/' "$src" >"$work/memory-input.c"
sed 's/"r"(base));/"r"(base) : "cc");/' \
    "$src" >"$work/cc-clobber.c"
sed 's/"r"(base));/"r"(base), "r"(base));/' \
    "$src" >"$work/extra-input.c"

for variant in wrong-dctype wrong-cvau wrong-operand extra-instruction \
               memory-input cc-clobber extra-input; do
    cmp -s "$src" "$work/$variant.c" && {
        echo "EFI_CACHE=FAIL mutation_absent=$variant" >&2; exit 1;
    }
    "$host" -E -P -std=gnu11 "$work/$variant.c" -o "$work/$variant.i"
    if "$mini" --target=aarch64-linux -S "$work/$variant.i" \
        -o "$work/$variant.s" >/dev/null 2>&1; then
        echo "EFI_CACHE=FAIL accepted=$variant" >&2; exit 1
    fi
    if test -e "$work/$variant.s"; then
        echo "EFI_CACHE=FAIL partial_output=$variant" >&2; exit 1
    fi
done

for target in x86_64 loongarch64; do
    if "$mini" --target="$target-linux" -S "$work/efi.i" \
        -o "$work/foreign.s" >/dev/null 2>&1; then
        echo "EFI_CACHE=FAIL foreign=$target" >&2; exit 1
    fi
done

if command -v aarch64-linux-gnu-gcc >/dev/null 2>&1; then
    aarch64-linux-gnu-gcc -c -x assembler "$work/efi.s" -o "$work/efi.o"
    echo 'EFI_CACHE=PASS civac=2 cvau=1 foreign=2 negative=7 GNU_GAS=PASS'
else
    echo 'EFI_CACHE=PASS civac=2 cvau=1 foreign=2 negative=7 GNU_GAS=SKIP'
fi
        ;;
    aarch64-linux-smmu-dynamic-dmb)
# Both fixed and dynamically-sized frames retain the ISA memory barrier.
test "$(grep -c '^  dmb oshst$' "$work/real.s")" -eq 2
sed -n '/^smmu_dynamic_barrier:/,/^.size smmu_dynamic_barrier/p' \
    "$work/real.s" | grep -Eq '^  sub sp, sp, x[0-9]+'

# A foreign ISA must not interpret the AArch64 hardware barrier as a
# compiler-only instruction, even though the C source itself is valid.
for target in x86_64 loongarch64; do
    if "$mini" --target="$target-linux" -S "$work/real.i" \
        -o "$work/foreign.s" >/dev/null 2>&1; then
        echo "SMMU_DYNAMIC_DMB=FAIL foreign=$target" >&2; exit 1
    fi
done

# Mutating the exact privileged instruction or inventing a GNU input role
# must not widen admission for variable-frame structured asm.
sed 's/dmb oshst/dmb oshst; nop/g' "$src" >"$work/extra.c"
"$host" -E -P -std=gnu11 "$work/extra.c" -o "$work/extra.i"
"$host" -E -P -std=gnu11 -DMINIC_DMB_BAD_INPUT \
    "$src" -o "$work/input.i"
for case_name in extra input; do
    if "$mini" --target=aarch64-linux -S "$work/$case_name.i" \
        -o "$work/$case_name.s" >/dev/null 2>&1; then
        echo "SMMU_DYNAMIC_DMB=FAIL invalid=$case_name" >&2; exit 1
    fi
done

if command -v aarch64-linux-gnu-gcc >/dev/null 2>&1; then
    aarch64-linux-gnu-gcc -c "$work/real.s" -o "$work/real.o"
    echo 'SMMU_DYNAMIC_DMB=PASS barriers=2 dynamic_frame=1 foreign=2 negative=2 GNU_GAS=PASS'
else
    echo 'SMMU_DYNAMIC_DMB=PASS barriers=2 dynamic_frame=1 foreign=2 negative=2 GNU_GAS=SKIP'
fi
        ;;
    aarch64-linux-mrs-read-family)
# Original privileged MRS contracts: named and finite architectural banks.
test "$(grep -Ec '^[[:space:]]*mrs x9, [[:alnum:]_]+[[:space:]]*$' "$work/read.s" || true)" -eq 31
for reg in pmcr_el0 pmceid0_el0 pmceid1_el0 cntpct_el0 \
    cnthctl_el2 SCTLR_EL1 dbgbcr15_el1 dbgbvr0_el1 \
    pmevcntr30_el0 pmevtyper30_el0 s3_0_c15_c0_5; do
    grep -Eq "^[[:space:]]*mrs x9, $reg[[:space:]]*$" "$work/read.s"
done
for target in x86_64 loongarch64; do
    if "$mini" --target="$target-linux" -S "$work/read.i" \
        -o "$work/foreign.s" >/dev/null 2>&1; then
        echo "AARCH64_MRS_READ=FAIL foreign=$target" >&2; exit 1
    fi
done
sed 's/READ_SYSREG(pmcr_el0)/READ_SYSREG(pmcr_el7)/' \
    "$src" >"$work/bad-reg.c"
sed 's/"=r" (result)/"=m" (result)/g' "$src" >"$work/bad-constraint.c"
sed 's/unsigned long long/unsigned int/g' "$src" >"$work/bad-width.c"
for variant in bad-reg bad-constraint bad-width; do
    "${HOST_CC:-cc}" -E -P -std=gnu11 "$work/$variant.c" -o "$work/$variant.i"
    if "$mini" --target=aarch64-linux -S "$work/$variant.i" \
        -o "$work/$variant.s" >/dev/null 2>&1; then
        echo "AARCH64_MRS_READ=FAIL unsafe-$variant" >&2; exit 1
    fi
    test ! -e "$work/$variant.s"
done
if command -v aarch64-linux-gnu-gcc >/dev/null 2>&1; then
    aarch64-linux-gnu-gcc -c "$work/read.s" -o "$work/mini.o"
    aarch64-linux-gnu-gcc -c "$src" -o "$work/gnu.o"
    echo 'AARCH64_MRS_READ=PASS originals=31 foreign=2 negative=3 GNU_GAS=PASS'
else
    echo 'AARCH64_MRS_READ=PASS originals=31 foreign=2 negative=3 GNU_GAS=SKIP'
fi
        ;;
    aarch64-linux-smccc-v11-full)
# Original SMCCCv1.1 fixed-register x0..x7 ABI; never execute EL1 HVC/SMC.
test "$(grep -Ec '^  hvc #0$' "$work/real.s")" -eq 6
test "$(grep -Ec '^  smc #0$' "$work/real.s")" -eq 3
grep -Eq '^  ldr x7, \[sp, #[0-9]+\]$' "$work/real.s"
grep -Eq '^  str x3, \[x10, #0\]$' "$work/real.s"
for target in x86_64 loongarch64; do
    if "$mini" --target="$target-linux" -S "$work/real.i" \
        -o "$work/foreign.s" >/dev/null 2>&1; then
        echo "AARCH64_SMCCC=FAIL foreign=$target" >&2; exit 1
    fi
done
sed 's/register unsigned long arg7 asm("r7")/register unsigned long arg7 asm("r6")/' \
    "$src" >"$work/bad-binding.c"
sed 's/: "memory");/: "cc");/g' "$src" >"$work/bad-clobber.c"
sed 's/#0/#1/g' "$src" >"$work/bad-opcode.c"
sed 's/"=r" (r0)/"=m" (r0)/' "$src" >"$work/bad-output.c"
sed 's/register void \*arg1 asm("r1")/register void *arg1 asm("r2")/' \
    "$src" >"$work/bad-pointer-binding.c"
sed 's/register unsigned long long arg1 asm("r1")/register unsigned long long arg1 asm("r2")/' \
    "$src" >"$work/bad-phys-binding.c"
sed 's/register enum minic_hyp_vector arg1 asm("r1")/register enum minic_hyp_vector arg1 asm("r2")/' \
    "$src" >"$work/bad-enum-binding.c"
sed 's/register const unsigned long long arg2 asm("r2")/register const unsigned long long arg2 asm("r3")/' \
    "$src" >"$work/bad-const-binding.c"
for variant in bad-binding bad-clobber bad-opcode bad-output bad-pointer-binding bad-phys-binding bad-enum-binding bad-const-binding; do
    "${HOST_CC:-cc}" -E -P -std=gnu11 "$work/$variant.c" -o "$work/$variant.i"
    if "$mini" --target=aarch64-linux -S "$work/$variant.i" \
        -o "$work/$variant.s" >/dev/null 2>&1; then
        echo "AARCH64_SMCCC=FAIL unsafe-$variant" >&2; exit 1
    fi
    test ! -e "$work/$variant.s"
done
if command -v aarch64-linux-gnu-gcc >/dev/null 2>&1; then
    aarch64-linux-gnu-gcc -c "$work/real.s" -o "$work/mini.o"
    aarch64-linux-gnu-gcc -c "$src" -o "$work/gnu.o"
    echo 'AARCH64_SMCCC=PASS call_forms=9 in=1,2,4,8 ptr=1 u64=1 enum=1 const_u64=1 out=4 negative=8 GNU_GAS=PASS'
else
    echo 'AARCH64_SMCCC=PASS call_forms=9 in=1,2,4,8 ptr=1 u64=1 enum=1 const_u64=1 out=4 negative=8 GNU_GAS=SKIP'
fi
        ;;
    aarch64-linux-tegra-ndiv-sysreg)
grep -Eq '^[[:space:]]*mrs x9, s3_0_c15_c0_4[[:space:]]*$' "$work/real.s"
grep -Eq '^[[:space:]]*msr s3_0_c15_c0_4, x9[[:space:]]*$' "$work/real.s"
for target in x86_64 loongarch64; do
    if "$mini" --target="$target-linux" -S "$work/real.i" \
        -o "$work/foreign.s" >/dev/null 2>&1; then
        echo "TEGRA_NDIV=FAIL foreign=$target" >&2; exit 1
    fi
done
sed 's/s3_0_c15_c0_4/s3_0_c15_c0_5/g' "$src" >"$work/bad.c"
"${HOST_CC:-cc}" -E -P -std=gnu11 "$work/bad.c" -o "$work/bad.i"
if "$mini" --target=aarch64-linux -S "$work/bad.i" -o "$work/bad.s" \
        >/dev/null 2>&1; then
    echo 'TEGRA_NDIV=FAIL unknown sysreg accepted' >&2; exit 1
fi
if command -v aarch64-linux-gnu-gcc >/dev/null 2>&1; then
    aarch64-linux-gnu-gcc -c "$work/real.s" -o "$work/real.o"
    echo 'TEGRA_NDIV=PASS read=1 write=1 foreign=2 negative=1 GAS=PASS'
else
    echo 'TEGRA_NDIV=PASS read=1 write=1 foreign=2 negative=1 GAS=SKIP'
fi
        ;;
    float32-const-eval)
${CC:-cc} -no-pie "$work/source.s" -o "$work/minic"
"$work/minic"
${CC:-cc} -O0 "$root/tests/compiler/c0/float32_const_eval.c" -o "$work/gnu"
"$work/gnu"
echo 'FLOAT32_CONST_EVAL=PASS cast=1 arithmetic=3 unary=-2 gcc_oracle=1'
        ;;
    runtime-designated-positional)
${CC:-cc} -no-pie "$work/source.s" -o "$work/minic"
"$work/minic"
${CC:-cc} -O0 "$root/tests/compiler/c0/runtime_designated_positional.c" \
    -o "$work/gnu"
"$work/gnu"
echo 'RUNTIME_DESIGNATED_POSITIONAL=PASS gcc_oracle=1 borrowed_arena_pointer=removed'
        ;;
    gnu-linux-nf-conntrack-size)
# GNU Linux 6.6.143 nf_conntrack_extend total_extension_size source-shape probe.
# Frozen owner/authenticated .i and real kernel GNU LD remain separate gates.
"$host" -c -x assembler "$work/gnu.s" -o "$work/gnu.o"
"$host" -c -x assembler "$work/minic.s" -o "$work/minic.o"
for obj in "$work/gnu.o" "$work/minic.o"; do
    if nm -u "$obj" | grep -Fq '__compiletime_assert_697'; then
        echo "LINUX_NF_CT_TOTAL_SIZE=FAIL unresolved impossible assert $obj" >&2
        exit 1
    fi
done
"$host" -no-pie "$work/gnu.o" -o "$work/gnu"
"$host" -no-pie "$work/minic.o" -o "$work/minic"
set +e
"$work/gnu"; gnu_rc=$?
"$work/minic"; mini_rc=$?
set -e
if [ "$gnu_rc" -ne 42 ] || [ "$mini_rc" -ne 42 ]; then
    echo "LINUX_NF_CT_TOTAL_SIZE=FAIL GNU=$gnu_rc MiniC=$mini_rc" >&2
    exit 1
fi
"$host" -E -P -std=gnu11 -DMINIC_NEGATIVE_CASES "$source" -o "$work/negative.i"
"$host" -std=gnu11 -O2 -c "$work/negative.i" -o "$work/negative.gnu.o"
"$mini" --target=x86_64-linux -S "$work/negative.i" -o "$work/negative.minic.s"
"$host" -c -x assembler "$work/negative.minic.s" -o "$work/negative.minic.o"
for obj in "$work/negative.gnu.o" "$work/negative.minic.o"; do
    if ! nm -u "$obj" | grep -Fq 'runtime_size_assert_probe'; then
        echo "LINUX_NF_CT_TOTAL_SIZE=FAIL lost dynamic assert $obj" >&2
        exit 1
    fi
done
echo 'LINUX_NF_CT_TOTAL_SIZE=PASS GNU_E=1 GNU_O2=1 MiniC_S=1 GNU_AS=4 GNU_LD=2 GNU_runtime=42 MiniC_runtime=42 negative=2 source_model_only=1'
        ;;
    gnu-linux-task-mmu-swp-pfn-guard)
# MiniC Linux 6.6.143 fs/proc/task_mmu.c _347: exact shape, distinct from
# the full, authenticated frozen .i owner that is separately replayed.
# GNU -E -> GCC -O2 or MiniC -S -> original GNU AS/LD -> native execute.
"$host" -c -x assembler "$work/gnu.s" -o "$work/gnu.o"
"$host" -c -x assembler "$work/minic.s" -o "$work/minic.o"
for object in "$work/gnu.o" "$work/minic.o"; do
    if nm -u "$object" | grep -Fq '__compiletime_assert_347'; then
        echo "LINUX_TASK_MMU_PFN_GUARD=FAIL retained_impossible_assert object=$object" >&2
        exit 1
    fi
done

"$host" -no-pie "$work/gnu.o" -o "$work/gnu"
"$host" -no-pie "$work/minic.o" -o "$work/minic"
set +e
"$work/gnu"
gnu_rc=$?
"$work/minic"
minic_rc=$?
set -e
if [ "$gnu_rc" -ne 42 ] || [ "$minic_rc" -ne "$gnu_rc" ]; then
    echo "LINUX_TASK_MMU_PFN_GUARD=FAIL GNU_runtime=$gnu_rc MiniC_runtime=$minic_rc" >&2
    exit 1
fi

"$host" -E -P -std=gnu11 -DMINIC_NEGATIVE_CASES "$source" -o "$work/negative.i"
"$host" -std=gnu11 -O2 -c "$work/negative.i" -o "$work/negative.gnu.o"
"$mini" --target=x86_64-linux -S "$work/negative.i" -o "$work/negative.minic.s"
"$host" -c -x assembler "$work/negative.minic.s" -o "$work/negative.minic.o"
for object in "$work/negative.gnu.o" "$work/negative.minic.o"; do
    for symbol in dangerous_pfn_bits_source runtime_pfn_bits_probe; do
        if ! nm -u "$object" | grep -Fq "$symbol"; then
            echo "LINUX_TASK_MMU_PFN_GUARD=FAIL lost_negative_symbol=$symbol object=$object" >&2
            exit 1
        fi
    done
done

printf 'LINUX_TASK_MMU_PFN_GUARD=PASS GNU_E=1 GNU_O2=1 MiniC_S=1 GNU_AS=4 GNU_LD=2 GNU_runtime=%s MiniC_runtime=%s negative=3 source_model_only=1\n' "$gnu_rc" "$minic_rc"
        ;;
esac
