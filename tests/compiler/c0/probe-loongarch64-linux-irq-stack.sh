#!/usr/bin/env bash
# Original Linux 6.6.143 do_vint IRQ-stack switch, with exact fixed register
# clobbers and a hidden BL return-address side effect in MachineIR.
set -Eeuo pipefail
mini=${1:-${MINIC_CC:-}}
test -n "$mini" && test -x "$mini"
here=$(cd "$(dirname "$0")" && pwd)
src="$here/loongarch64_linux_irq_stack.c"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

cc -std=gnu11 -Wall -Wextra -Werror -fsyntax-only "$src"
cc -E -P -x c "$src" -o "$work/irq.i"
"$mini" --target=loongarch64-linux -S "$work/irq.i" -o "$work/irq.s"
test "$(grep -Fc '# MINIC_SOURCE_INLINE_ASM_BEGIN' "$work/irq.s")" -eq 1
grep -Fq '  addi.d $sp, $sp, -16' "$work/irq.s"
grep -Fq '  st.d $ra, $sp, 0' "$work/irq.s"
grep -Fq '  st.d $s0, $sp, 8' "$work/irq.s"
grep -Fq '  ld.d $s0, $sp, 8' "$work/irq.s"
grep -Fq '  ld.d $ra, $sp, 0' "$work/irq.s"
grep -Fq '  addi.d $sp, $sp, 16' "$work/irq.s"
awk '
  /# MINIC_SOURCE_INLINE_ASM_BEGIN/ { inside=1; begin=NR; next }
  /# MINIC_SOURCE_INLINE_ASM_END/ { inside=0; end=NR; next }
  inside && /move[[:space:]]+\$s0, \$sp/ { step1=NR }
  inside && /move[[:space:]]+\$sp, \$t0/ { step2=NR }
  inside && /move[[:space:]]+\$a0, \$t1/ { step3=NR }
  inside && /bl[[:space:]]+handle_loongarch_irq/ { step4=NR }
  inside && /move[[:space:]]+\$sp, \$s0/ { step5=NR }
  END { if (!(begin < step1 && step1 < step2 && step2 < step3 &&
              step3 < step4 && step4 < step5 && step5 < end)) exit 1 }
' "$work/irq.s"

# Wrong forms cannot borrow the exact privileged stack-switch descriptor.
sed 's/bl\\thandle_loongarch_irq/bl\\thandle_wrong_irq/' \
    "$work/irq.i" > "$work/wrong-call.i"
sed 's/\$t8",/\$t7",/' "$work/irq.i" > "$work/wrong-clobber.i"
sed 's/unsigned long stack/unsigned int stack/' "$work/irq.i" > "$work/wrong-width.i"
sed 's/\[stk\] "r"/\[stk\] "m"/' "$work/irq.i" > "$work/wrong-constraint.i"
sed 's/"memory");/"cc");/' "$work/irq.i" > "$work/wrong-memory.i"
for name in wrong-call wrong-clobber wrong-width wrong-constraint wrong-memory; do
    if "$mini" --target=loongarch64-linux -S "$work/$name.i" \
        -o "$work/$name.s" >"$work/$name.out" 2>"$work/$name.err"; then
        echo "MINIC_LOONG_IRQ_STACK_NEGATIVE=FAIL $name admitted" >&2
        exit 1
    fi
    test ! -e "$work/$name.s"
done
echo 'MINIC_LOONG_IRQ_STACK=PASS exact_source_call=1 preserved_ra_s0=1 negatives=5'

cross=${LOONGARCH64_CC:-loongarch64-linux-gnu-gcc-13}
if command -v "$cross" >/dev/null 2>&1; then
    "$cross" -c -x assembler "$work/irq.s" -o "$work/irq.o"
    echo 'MINIC_LOONG_IRQ_STACK_GNU_AS=PASS'
else
    echo 'MINIC_LOONG_IRQ_STACK_GNU_AS=SKIP missing Loong GNU assembler'
fi

if [[ -n "${LOONG_TRAPS_ORIGINAL_I:-}" ]]; then
    test -f "$LOONG_TRAPS_ORIGINAL_I"
    # This probe's first recovery was 167/1; the later static-key recovery
    # also legitimately yields a fully compiled 167/0 original traps.i.
    if MINIC_BACKEND_CENSUS=1 "$mini" --target=loongarch64-linux \
            -fshort-wchar -funsigned-char -fno-pie -mabi=lp64s -S \
            "$LOONG_TRAPS_ORIGINAL_I" -o "$work/traps.s" \
            >"$work/traps.out" 2>"$work/traps.err"; then
        :
    fi
    grep -Eq 'MINIC_BACKEND_CENSUS arch=loongarch64 scanned=167 rejected=[01](\\n)?$' \
        "$work/traps.err"
    if grep -Fq 'MINIC_BACKEND_REJECT arch=loongarch64 function=do_vint' \
            "$work/traps.err"; then
        echo 'MINIC_LOONG_IRQ_CENSUS=FAIL do_vint still rejected' >&2
        exit 1
    fi
    echo 'MINIC_LOONG_IRQ_CENSUS=PASS scanned=167 rejected_at_most=1 recovered=do_vint'
fi
