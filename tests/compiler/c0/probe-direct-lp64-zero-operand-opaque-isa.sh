#!/usr/bin/env bash
# Original GNU kernel hardware effects: source ASM without GNU operand list.
# Privileged operations are assembled and linked but deliberately never run
# at user EL0/PLV3. Harmless YIELD/DBAR runs under real GNU QEMU.
set -Eeuo pipefail
: "${MINIC_CC:?compiler}"
: "${MINIC_OPAQUE_TARGET:?target}"
d=$(mktemp -d);trap 'rm -rf "$d"' EXIT
case "$MINIC_OPAQUE_TARGET" in
aarch64)
 cc=aarch64-linux-gnu-gcc; qemu=qemu-aarch64
 cat >"$d/origin.i" <<'SRC'
__attribute__((noinline)) void linux_mask(void) { asm volatile("msr daifset, #3"); }
__attribute__((noinline)) void linux_unmask(void) { asm volatile("msr daifclr, #3"); }
__attribute__((noinline)) void linux_idle(void) { asm volatile("wfi"); }
__attribute__((noinline)) void linux_notify(void) { asm volatile("sev"); }
__attribute__((noinline)) int safe(void) {
 asm volatile("yield"); asm volatile("dsb ishst"); asm volatile("dsb ld");
 return 42;
}
int main(void) { return safe(); }
SRC
 cat >"$d/bad.i" <<'SRC'
void linux_bad(void) { asm volatile("msr daifset, #5; nop"); }
SRC
 ;;
loongarch64)
 cc=loongarch64-linux-gnu-gcc-13; qemu=qemu-loongarch64
 cat >"$d/origin.i" <<'SRC'
__attribute__((noinline)) void linux_idle(void) { asm volatile("idle 0"); }
__attribute__((noinline)) void linux_tlb(void) { asm volatile("tlbrd"); }
__attribute__((noinline)) int safe(void) {
 asm volatile("dbar 0"); asm volatile("nop");
 return 42;
}
int main(void) { return safe(); }
SRC
 cat >"$d/bad.i" <<'SRC'
void linux_bad(void) { asm volatile("idle 1; break 0"); }
SRC
 ;;
*) echo "MINIC_OPAQUE_SOURCE=FAIL unknown target" >&2;exit 2;;
esac
"$cc" -static -x c "$d/origin.i" -o "$d/origin.gnu"
"$MINIC_CC" --target="$MINIC_OPAQUE_TARGET-linux" -S "$d/origin.i" -o "$d/origin.mini.s"
"$cc" -c -x assembler "$d/origin.mini.s" -o "$d/origin.mini.o"
"$cc" -static -x assembler "$d/origin.mini.s" -o "$d/origin.mini"
case "$MINIC_OPAQUE_TARGET" in
aarch64)
 for op in "msr daifset, #3" "msr daifclr, #3" "wfi" "sev" "yield" "dsb ishst" "dsb ld";do
   grep -Fq "$op" "$d/origin.mini.s"
 done ;;
loongarch64)
 for op in "idle 0" "tlbrd" "dbar 0";do
   grep -Fq "$op" "$d/origin.mini.s"
 done ;;
esac
set +e
timeout 45s "$qemu" "$d/origin.gnu" >"$d/gnu.out" 2>"$d/gnu.err"; rc_gnu=$?
timeout 45s "$qemu" "$d/origin.mini" >"$d/mini.out" 2>"$d/mini.err"; rc_mini=$?
set -e
if (( rc_gnu != 42 || rc_mini != 42 )) ||
   ! cmp -s "$d/gnu.out" "$d/mini.out" ||
   ! cmp -s "$d/gnu.err" "$d/mini.err";then
 echo "MINIC_OPAQUE_SOURCE=FAIL target=$MINIC_OPAQUE_TARGET GNU_QEMU" >&2
 exit 1
fi
if "$MINIC_CC" --target="$MINIC_OPAQUE_TARGET-linux" -S "$d/bad.i" -o "$d/bad.s" >/dev/null 2>"$d/bad.err";then
 echo "MINIC_OPAQUE_SOURCE=FAIL accepted extra hardware instructions" >&2;exit 1
fi
test ! -e "$d/bad.s"
echo "MINIC_OPAQUE_SOURCE=PASS target=$MINIC_OPAQUE_TARGET GNU_GAS_LINK=1 QEMU_safe=1 privileged_assembled_only=1 invalid_rejected=1"
