#!/bin/sh
# R5 -> R6 typed retirement: eight exact Source ASM families, byte-identical
# GNU .i assembly, ELF relocation/patchtable oracles, native execution and
# independently rejected mutations. Baseline must be a FRESH pinned R5 build.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/debug/bin/minic-cc"}
baseline=${MINIC_X86_TYPED_BASELINE_CC:-}
host_cc=${HOST_CC:-cc}
work=${BUILD_DIR:-"$root/build/debug"}/tests/x86-typed-eight-legacy
mkdir -p "$work"
test -x "$minic" || { echo "X86_TYPED_EIGHT=BLOCKED missing_minic=$minic" >&2; exit 2; }
if test -n "$baseline"; then
    test -x "$baseline" || { echo "X86_TYPED_EIGHT=BLOCKED missing_baseline=$baseline" >&2; exit 2; }
fi
src="$root/tests/compiler/c0/x86_typed_eight_legacy_abi.c"
"$host_cc" -std=gnu11 -Wall -Wextra -Werror -fsyntax-only "$src"
"$host_cc" -E -P -x c "$src" -o "$work/positive.i"
"$minic" --target=x86_64-linux -S "$work/positive.i" -o "$work/mini.s"
if test -n "$baseline"; then
    "$baseline" --target=x86_64-linux -S "$work/positive.i" -o "$work/baseline.s"
    if ! cmp -s "$work/baseline.s" "$work/mini.s"; then
        echo 'X86_TYPED_EIGHT_DIFF=FAIL changed .s' >&2
        diff -u "$work/baseline.s" "$work/mini.s" || true
        exit 1
    fi
    echo 'X86_TYPED_EIGHT_DIFF=PASS same_gnu_i=1 byte_identical_s=1 ELF_templates=2'
fi

"$host_cc" -std=gnu11 -O0 -no-pie -x c "$work/positive.i" -o "$work/gnu.elf"
"$host_cc" -no-pie -x assembler "$work/mini.s" -o "$work/mini.elf"
"$host_cc" -O0 -c -x c "$work/positive.i" -o "$work/gnu.o"
"$host_cc" -c -x assembler "$work/mini.s" -o "$work/mini.o"
for producer in gnu mini; do
    readelf -SW "$work/$producer.o" | grep -Eq '\.tracedata[[:space:]]+PROGBITS.*000014'
    readelf -SW "$work/$producer.o" | grep -Eq '\.altinstructions[[:space:]]+PROGBITS.*00000e'
    readelf -SW "$work/$producer.o" | grep -Eq '\.altinstr_replacement[[:space:]]+PROGBITS.*000004'
    test "$(readelf -rW "$work/$producer.o" | grep -c 'R_X86_64_64.*\.rodata')" -ge 2
    rc=0
    timeout 20s "$work/$producer.elf" >"$work/$producer.out" \
        2>"$work/$producer.err" || rc=$?
    if test "$rc" -ne 42; then
        echo "X86_TYPED_EIGHT=FAIL runtime=$producer exit=$rc" >&2
        exit 1
    fi
done
cmp "$work/gnu.out" "$work/mini.out"
cmp "$work/gnu.err" "$work/mini.err"

for family in pm_trace i915_movntdqa pvclock_scale preempt_static_call \
              ipv6_checksum boot_string vdso_syscall vdso_cpunode; do
    case "$family" in
        pm_trace)
            sed 's/word %c1/word %c1; int3/g' "$work/positive.i" >"$work/invalid.i" ;;
        i915_movntdqa)
            sed 's/movntdqa (%0)/movntdqa (%0); int3/g' "$work/positive.i" >"$work/invalid.i" ;;
        pvclock_scale)
            sed 's/shrd \$32/shrd $31/g' "$work/positive.i" >"$work/invalid.i" ;;
        preempt_static_call)
            sed 's/call __SCT__preempt_schedule_notrace/call __SCT__preempt_schedule_other/g' \
                "$work/positive.i" >"$work/invalid.i" ;;
        ipv6_checksum)
            sed 's/adcq \$0,%\[sum\]/subq $0,%[sum]/g' \
                "$work/positive.i" >"$work/invalid.i" ;;
        boot_string)
            sed 's/repe; cmpsb/repne; cmpsb/g' "$work/positive.i" >"$work/invalid.i" ;;
        vdso_syscall)
            sed 's/asm("syscall"/asm("syscall; cli"/g' "$work/positive.i" >"$work/invalid.i" ;;
        vdso_cpunode)
            sed 's/16\*32+22/16*32+23/g' "$work/positive.i" >"$work/invalid.i" ;;
    esac
    if cmp -s "$work/invalid.i" "$work/positive.i"; then
        echo "X86_TYPED_EIGHT=FAIL mutation_missed=$family" >&2
        exit 1
    fi
    rm -f "$work/invalid.s"
    if "$minic" --target=x86_64-linux -S "$work/invalid.i" -o "$work/invalid.s" \
        >"$work/invalid.out" 2>"$work/invalid.err"; then
        echo "X86_TYPED_EIGHT=FAIL admitted=$family" >&2
        exit 1
    fi
    test ! -e "$work/invalid.s"
    if test -n "$baseline"; then
        rm -f "$work/baseline-invalid.s"
        if "$baseline" --target=x86_64-linux -S "$work/invalid.i" \
            -o "$work/baseline-invalid.s" \
            >"$work/baseline-invalid.out" 2>"$work/baseline-invalid.err"; then
            echo "X86_TYPED_EIGHT_DIFF=FAIL baseline_admitted=$family" >&2
            exit 1
        fi
        test ! -e "$work/baseline-invalid.s"
    fi
done
echo 'X86_TYPED_EIGHT=PASS families=8 GNU_AS_LD=1 GNU_MiniC_native=2 exit=42 invalid=8 ELF_tracedata=20 ELF_alt=14 partial_s=0'
