#!/usr/bin/env bash
# Exact Linux 6.6.143 LoongArch prepare_frametrace source ASM, selected
# through target-qualified physical MachineIR. No generic ASM fallback.
set -Eeuo pipefail
mini=${1:-${MINIC_CC:-}}
test -n "$mini" && test -x "$mini"
here=$(cd "$(dirname "$0")" && pwd)
src="$here/loongarch64_linux_frametrace.c"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

cc -std=gnu11 -Wall -Wextra -Werror -fsyntax-only "$src"
cc -E -P -x c "$src" -o "$work/frametrace.i"
"$mini" --target=loongarch64-linux -S "$work/frametrace.i" \
    -o "$work/frametrace.s"
test "$(grep -Ec '^# MINIC_SOURCE_INLINE_ASM_BEGIN$' "$work/frametrace.s")" -eq 1
grep -Fq 'st.d $r1, $t0, 8*1' "$work/frametrace.s"
grep -Fq 'st.d $ra, $t1, 0' "$work/frametrace.s"
grep -Fq 'ld.d $ra, $t0, 8' "$work/frametrace.s"
grep -Fq 'st.d $r31, $t0, 8*31' "$work/frametrace.s"
grep -Fq 'pcaddi $ra, 0' "$work/frametrace.s"
test "$(grep -Ec 'st\.d \$r[0-9]+, \$t0, 8\*[0-9]+' "$work/frametrace.s")" -eq 31

# The template alone cannot authorize mismatched GNU operand constraints,
# lvalue type or memory-clobber policy.
sed 's/pcaddi \$ra, 0/pcaddi $ra, 1/' "$work/frametrace.i" > "$work/wrong-template.i"
sed 's/: "=m" (regs->csr_era)/: "=r" (regs->csr_era)/' \
    "$work/frametrace.i" > "$work/wrong-output.i"
sed 's/: "memory");/: "cc");/' "$work/frametrace.i" > "$work/wrong-clobber.i"
sed 's/unsigned long csr_era;/unsigned int csr_era;/' \
    "$work/frametrace.i" > "$work/wrong-type.i"
for name in wrong-template wrong-output wrong-clobber wrong-type; do
    if "$mini" --target=loongarch64-linux -S "$work/$name.i" \
        -o "$work/$name.s" >"$work/$name.out" 2>"$work/$name.err"; then
        echo "MINIC_LOONG_FRAMETRACE_NEGATIVE=FAIL $name admitted" >&2
        exit 1
    fi
    test ! -e "$work/$name.s"
done
echo 'MINIC_LOONG_FRAMETRACE=PASS exact_m1_source_asm=1 negatives=4'

cross=${LOONGARCH64_CC:-loongarch64-linux-gnu-gcc-13}
if command -v "$cross" >/dev/null 2>&1; then
    "$cross" -c -x assembler "$work/frametrace.s" -o "$work/frametrace.o"
    echo 'MINIC_LOONG_FRAMETRACE_GNU_AS=PASS'
else
    echo 'MINIC_LOONG_FRAMETRACE_GNU_AS=SKIP missing Loong GNU assembler'
fi

# Official GCC-preprocessed traps.i is a separately authenticated input.
# Assert the exact owner recovery census when explicitly provided.
if [[ -n "${LOONG_TRAPS_ORIGINAL_I:-}" ]]; then
    test -f "$LOONG_TRAPS_ORIGINAL_I"
    # Later owner recoveries can drive the rejected count below this probe's
    # original 167/2 milestone, including a full 167/0 successful compile.
    if MINIC_BACKEND_CENSUS=1 "$mini" --target=loongarch64-linux \
            -fshort-wchar -funsigned-char -fno-pie -mabi=lp64s -S \
            "$LOONG_TRAPS_ORIGINAL_I" -o "$work/traps.s" \
            >"$work/traps.out" 2>"$work/traps.err"; then
        :
    fi
    grep -Eq 'MINIC_BACKEND_CENSUS arch=loongarch64 scanned=167 rejected=[012](\\n)?$' \
        "$work/traps.err"
    if grep -Fq 'MINIC_BACKEND_REJECT arch=loongarch64 function=prepare_frametrace' \
            "$work/traps.err"; then
        echo 'MINIC_LOONG_TRAPS_CENSUS=FAIL frametrace still rejected' >&2
        exit 1
    fi
    echo 'MINIC_LOONG_TRAPS_CENSUS=PASS scanned=167 rejected_at_most=2 recovered=prepare_frametrace'
fi
