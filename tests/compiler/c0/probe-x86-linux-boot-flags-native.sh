#!/usr/bin/env bash
# Compare the exact GNU x86 compressed-boot CPUID/bit/flags/register grammar
# with GCC at runtime.  CR0 read/write and task-register writes are privileged
# and are verified only via the original frozen kernel owner and GNU AS.
set -Eeuo pipefail
mini=${1:?MiniC compiler}
test -x "$mini"
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
cat > "$d/mini.i" <<'SRC'
typedef unsigned int u32;
typedef _Bool bool;
int mini_bit(u32 *p, u32 n) {
    bool result = 0;
    asm volatile("btl %2,%1\n\t/* output condition code c */"
        : "=@ccc"(result) : "m"(*p), "Ir"(n));
    return result;
}
void mini_set(u32 *p, u32 n) {
    asm volatile("btsl %1,%0" : "+m"(*p) : "Ir"(n));
}
void mini_cpuid(u32 id,u32 count,u32 *a,u32 *b,u32 *c,u32 *d) {
    asm volatile("cpuid" : "=a"(*a),"=b"(*b),"=c"(*c),"=d"(*d)
        : "0"(id),"2"(count));
}
int mini_eflag(unsigned long mask) {
    unsigned long f0,f1;
    asm volatile("pushfq\n\tpushfq\n\tpop %0\n\tmov %0,%1\n\t"
                 "xor %2,%1\n\tpush %1\n\tpopfq\n\tpushfq\n\tpop %1\n\tpopfq"
        : "=&r"(f0),"=&r"(f1) : "ri"(mask));
    return !!((f0 ^ f1) & mask);
}
unsigned long mini_str(void) {
    unsigned long tr;
    asm volatile("str %0" : "=r"(tr));
    return tr;
}
SRC
sed 's/mini_/gnu_/g' "$d/mini.i" > "$d/gnu.c"
"$mini" --target=x86_64-linux -S "$d/mini.i" -o "$d/mini.s"
gcc -c -x assembler "$d/mini.s" -o "$d/mini.o"
gcc -O0 -c "$d/gnu.c" -o "$d/gnu.o"
cat > "$d/main.c" <<'SRC'
#include <stdint.h>
extern int mini_bit(uint32_t*,uint32_t),gnu_bit(uint32_t*,uint32_t);
extern void mini_set(uint32_t*,uint32_t),gnu_set(uint32_t*,uint32_t);
extern void mini_cpuid(uint32_t,uint32_t,uint32_t*,uint32_t*,uint32_t*,uint32_t*);
extern void gnu_cpuid(uint32_t,uint32_t,uint32_t*,uint32_t*,uint32_t*,uint32_t*);
extern int mini_eflag(unsigned long),gnu_eflag(unsigned long);
extern unsigned long mini_str(void),gnu_str(void);
int main(void) {
    for (unsigned n=0;n<32;n++) {
        uint32_t a=0,b=0;
        mini_set(&a,n);gnu_set(&b,n);
        if(a!=b || mini_bit(&a,n)!=gnu_bit(&b,n)) return 1;
        if(mini_bit(&a,(n+1U)%32)!=gnu_bit(&b,(n+1U)%32)) return 2;
    }
    for (unsigned leaf=0;leaf<2;leaf++) {
        uint32_t a[4]={0},b[4]={0};
        mini_cpuid(leaf,0,&a[0],&a[1],&a[2],&a[3]);
        gnu_cpuid(leaf,0,&b[0],&b[1],&b[2],&b[3]);
        for(unsigned i=0;i<4;i++) if(a[i]!=b[i]) return 3;
    }
    if(mini_eflag(1UL<<21)!=gnu_eflag(1UL<<21)) return 4;
    if((mini_str()&0xffffUL)!=(gnu_str()&0xffffUL)) return 5;
    return 0;
}
SRC
gcc -O0 -no-pie "$d/main.c" "$d/mini.o" "$d/gnu.o" -o "$d/run"
"$d/run"
sed 's/btsl %1,%0/btcl %1,%0/' "$d/mini.i" > "$d/negative.i"
if "$mini" --target=x86_64-linux -S "$d/negative.i" -o "$d/negative.s" \
    >/dev/null 2>&1; then
    echo 'X86_BOOT_FLAGS_FAIL=unadmitted bit opcode' >&2
    exit 1
fi
test ! -e "$d/negative.s"
echo 'MINIC_X86_BOOT_FLAGS_NATIVE=PASS bit=64 cpuid=2 eflags=1 str=1 negative=1'
