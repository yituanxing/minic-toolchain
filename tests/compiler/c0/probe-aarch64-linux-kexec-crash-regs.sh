#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
mini=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host=${HOST_CC:-cc}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
src="$root/tests/compiler/c0/aarch64_linux_kexec_crash_regs.c"

"$host" -E -P -std=gnu11 "$src" -o "$work/crash.i"
"$mini" --target=aarch64-linux -S "$work/crash.i" -o "$work/crash.s"

# The original GNU 675-byte template snapshots 31 GPRs, SP, synthetic
# PSTATE, and local-label PC. Verify the real target instructions, not a
# compiler-only placeholder or an abbreviated register dump.
test "$(grep -c '^  stp x.*\[x0, #' "$work/crash.s")" -eq 16
grep -Eq '^  ldr x0, \[sp, #[0-9]+\]$' "$work/crash.s"
pair=0
while test "$pair" -lt 15; do
    first=$((pair * 2))
    second=$((first + 1))
    offset=$((pair * 16))
    grep -Fq "  stp x$first, x$second, [x0, #$offset]" "$work/crash.s"
    pair=$((pair + 1))
done
grep -Fq '  stp x0, x1, [x0, #0]' "$work/crash.s"
grep -Fq '  stp x8, x9, [x0, #64]' "$work/crash.s"
grep -Fq '  stp x10, x11, [x0, #80]' "$work/crash.s"
grep -Fq '  stp x12, x13, [x0, #96]' "$work/crash.s"
grep -Fq '  stp x28, x29, [x0, #224]' "$work/crash.s"
grep -Fq '  mov x9, sp' "$work/crash.s"
grep -Fq '  stp x30, x9, [x0, #240]' "$work/crash.s"
grep -Fq '  mrs x9, CurrentEL' "$work/crash.s"
grep -Fq '  mrs x10, SPSEL' "$work/crash.s"
grep -Fq '  mrs x10, DAIF' "$work/crash.s"
grep -Fq '  mrs x10, NZCV' "$work/crash.s"
test "$(grep -c '^  orr x9, x9, x10$' "$work/crash.s")" -eq 3
grep -Fq '  adr x10, 1f' "$work/crash.s"
grep -Fq '1: stp x10, x9, [x0, #256]' "$work/crash.s"
# A transient frame or early scratch mutation falsifies the captured SP/GPR
# image even if all 34 output fields happen to be emitted.
if sed -n '/^  stp x0, x1, \[x0, #0\]/,/^1: stp x10, x9, \[x0, #256\]/p' \
    "$work/crash.s" | grep -Eq '^  (sub|add) sp|^  (ldr|mov|str) x11'; then
    echo 'AARCH_KEXEC_CRASH=FAIL pre-snapshot-mutation' >&2; exit 1
fi

for target in x86_64 loongarch64; do
    if "$mini" --target="$target-linux" -S "$work/crash.i" \
        -o "$work/foreign.s" >/dev/null 2>&1; then
        echo "AARCH_KEXEC_CRASH=FAIL foreign=$target" >&2; exit 1
    fi
done

# Each negative is valid C/GNU inline asm syntax but violates a distinct
# target contract: changed hardware state, absent early-clobber, omitted
# memory fence, or wrong register-pointer input binding.
sed 's/CurrentEL/CNTVCT_EL0/' "$src" >"$work/wrong-pstate.c"
sed 's/=&r/=r/g' "$src" >"$work/no-early-clobber.c"
sed 's/: "memory"/: \/\* missing clobber \*\//' "$src" >"$work/no-memory.c"
sed 's/"r" (newregs)/"m" (newregs)/' "$src" >"$work/no-reg-input.c"
sed 's/u64 regs\[31\]/u64 regs[1]/' "$src" >"$work/short-record.c"
for case_name in wrong-pstate no-early-clobber no-memory no-reg-input short-record; do
    "$host" -E -P -std=gnu11 "$work/$case_name.c" -o "$work/$case_name.i"
    if "$mini" --target=aarch64-linux -S "$work/$case_name.i" \
        -o "$work/$case_name.s" >/dev/null 2>&1; then
        echo "AARCH_KEXEC_CRASH=FAIL invalid=$case_name" >&2; exit 1
    fi
done

if command -v aarch64-linux-gnu-gcc >/dev/null 2>&1; then
    aarch64-linux-gnu-gcc -c "$work/crash.s" -o "$work/crash.o"
    echo 'AARCH_KEXEC_CRASH=PASS gpr_pairs=15 sp=1 pstate_pc=1 foreign=2 negative=5 GNU_GAS=PASS'
else
    echo 'AARCH_KEXEC_CRASH=PASS gpr_pairs=15 sp=1 pstate_pc=1 foreign=2 negative=5 GNU_GAS=SKIP'
fi
