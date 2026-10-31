#!/usr/bin/env bash
# Exact target-owned x86 descriptor/syscall-side ISA selection; the privileged
# cases are GNU AS verified but must never run in user mode. BTC/MOVNTI have
# independent GCC-vs-MiniC native effects and negative admission tests.
set -Eeuo pipefail
mini=${1:?MiniC compiler}
test -x "$mini"
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
cat > "$d/mini.i" <<'SRC'
void mini_hlt(void) { asm("hlt"); }
void mini_lldt(unsigned int n) { asm volatile("lldt %w0" :: "q"(n)); }
void mini_sldt(unsigned short *p) { asm("sldt %0" : "=m"(*p)); }
void mini_fs(unsigned int n) { asm volatile("mov %0, %%fs" :: "rm"(n)); }
struct Desc { unsigned short size; unsigned long address; } __attribute__((packed));
void mini_sidt(struct Desc *p) { asm volatile("sidt %0" : "=m"(*p)); }
struct Invpcid { unsigned long d[2]; };
void mini_invpcid(struct Invpcid *p, unsigned long type) {
    asm volatile("invpcid %[desc], %[type]"
                 :: [desc] "m"(*p), [type] "r"(type) : "memory");
}
void mini_bit(volatile unsigned long *p, unsigned long n) {
    asm volatile("btcq %1, %0" :: "m"(*(volatile long*)p), "Ir"(n) : "memory");
}
void mini_nt32(unsigned int *p, unsigned int value) {
    asm("movntil %1, %0" : "=m"(*p) : "r"(value));
}
void mini_nt64(unsigned long *p, unsigned long value) {
    asm("movntiq %1, %0" : "=m"(*p) : "r"(value));
}
SRC
sed 's/mini_/gnu_/g' "$d/mini.i" > "$d/gnu.c"
"$mini" --target=x86_64-linux -S "$d/mini.i" -o "$d/mini.s"
gcc -c -x assembler "$d/mini.s" -o "$d/mini.o"
gcc -c -O0 "$d/gnu.c" -o "$d/gnu.o"
objdump -dr "$d/mini.o" > "$d/mini.dump"
objdump -dr "$d/gnu.o" > "$d/gnu.dump"
for mnemonic in hlt lldt sldt sidt invpcid btc movnti; do
    grep -q "$mnemonic" "$d/mini.dump"
    grep -q "$mnemonic" "$d/gnu.dump"
done
cat > "$d/main.c" <<'SRC'
#include <stdint.h>
#include <stdatomic.h>
extern void mini_bit(volatile unsigned long*,unsigned long);
extern void gnu_bit(volatile unsigned long*,unsigned long);
extern void mini_nt32(unsigned int*,unsigned int);
extern void gnu_nt32(unsigned int*,unsigned int);
extern void mini_nt64(unsigned long*,unsigned long);
extern void gnu_nt64(unsigned long*,unsigned long);
int main(void) {
    unsigned long a[3],b[3];
    for (unsigned n=0;n<128;n++) {
        a[0]=a[1]=a[2]=b[0]=b[1]=b[2]=0;
        mini_bit(a,n);
        gnu_bit(b,n);
        if(a[0]!=b[0]||a[1]!=b[1]||a[2]!=b[2]) return 1;
        mini_bit(a,n);gnu_bit(b,n);
        if(a[0]!=b[0]||a[1]!=b[1]||a[2]!=b[2]) return 2;
    }
    for (unsigned n=0;n<64;n++) {
        unsigned int x=0,y=0;
        unsigned long lx=0,ly=0;
        mini_nt32(&x,n*31U);gnu_nt32(&y,n*31U);
        mini_nt64(&lx,(unsigned long)n*0x123456789UL);
        gnu_nt64(&ly,(unsigned long)n*0x123456789UL);
        atomic_thread_fence(memory_order_seq_cst);
        if(x!=y||lx!=ly) return 3;
    }
    return 0;
}
SRC
gcc -O0 -no-pie "$d/main.c" "$d/mini.o" "$d/gnu.o" -o "$d/run"
"$d/run"
sed 's/btcq %1, %0/btcq %1, %0; cli/' "$d/mini.i" > "$d/bad.i"
if "$mini" --target=x86_64-linux -S "$d/bad.i" -o "$d/bad.s" >/dev/null 2>&1; then
    echo 'X86_LOWLEVEL_FAIL=unknown instruction suffix accepted' >&2
    exit 1
fi
test ! -e "$d/bad.s"
echo 'MINIC_X86_LOWLEVEL_ISA=PASS privileged_GNU_AS=6 native_BTC=256 native_MOVNTI=128 negative=1'
