#!/usr/bin/env bash
# Native GNU Linux arch/x86/include asm/barrier.h effects, not C fences.
# CC/memory-clobber flags are preserved; unknown forms remain rejected.
set -Eeuo pipefail
: "${MINIC_CC:?exact minic-cc required}"
d=$(mktemp -d);trap 'rm -rf "$d"' EXIT
cat >"$d/source.i" <<'SRC'
__attribute__((noinline)) int kernel_ordering(int n) {
    asm volatile("lock; addl $0,-4(%%rsp)" ::: "memory", "cc");
    asm volatile("mfence" ::: "memory");
    asm volatile("lfence" ::: "memory");
    asm volatile("sfence" ::: "memory");
    asm volatile("pause");
    asm volatile("rep; nop");
    return n+40;
}
int main(void) { return kernel_ordering(2); }
SRC
gcc -O0 -static -x c "$d/source.i" -o "$d/gnu"
"$MINIC_CC" --target=x86_64-linux -S "$d/source.i" -o "$d/mini.s"
gcc -static -x assembler "$d/mini.s" -o "$d/mini"
for needle in 'lock; addl $0, -4(%rsp)' 'mfence' 'lfence' 'sfence' 'pause' 'rep; nop'; do
  grep -Fq "$needle" "$d/mini.s"
done
set +e
timeout 45s "$d/gnu" >"$d/gnu.out" 2>"$d/gnu.err";a=$?
timeout 45s "$d/mini" >"$d/mini.out" 2>"$d/mini.err";b=$?
set -e
if (( a!=42 || b!=42 )) ||
   ! cmp -s "$d/gnu.out" "$d/mini.out" ||
   ! cmp -s "$d/gnu.err" "$d/mini.err";then
 echo "X86_HARDWARE_BARRIERS=FAIL gnu=$a minic=$b" >&2; exit 1
fi
cat >"$d/bad-extra.i" <<'SRC'
int main(void){asm volatile("lock; addl $0,-4(%%rsp); int3" ::: "memory","cc");return 42;}
SRC
cat >"$d/bad-offset.i" <<'SRC'
int main(void){asm volatile("lock; addl $0,-8(%%rsp)" ::: "memory","cc");return 42;}
SRC
cat >"$d/bad-clobber.i" <<'SRC'
int main(void){asm volatile("mfence" ::: "cc");return 42;}
SRC
for bad in extra offset clobber;do
 if "$MINIC_CC" --target=x86_64-linux -S "$d/bad-$bad.i" -o "$d/bad-$bad.s" 2>"$d/bad-$bad.err";then
  echo "X86_HARDWARE_BARRIERS=FAIL unsafe_source_accepted=$bad" >&2;exit 1
 fi
 test ! -e "$d/bad-$bad.s"
done
echo "X86_HARDWARE_BARRIERS=PASS actual_ISA_forms=6 GNU_GAS_link_native=1 compiler_cc_and_memory=1 negatives=3"
