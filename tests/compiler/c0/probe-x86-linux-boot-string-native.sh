#!/usr/bin/env bash
# Frozen x86 compressed boot original REP/CMPSB ZF, EDX:EAX DIVL and
# REP MOVSQ/MOVSB tied early-clobber grammar versus GCC native behavior.
set -Eeuo pipefail
mini=${1:?MiniC compiler}
test -x "$mini"
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
cat > "$d/mini.i" <<'SRC'
typedef unsigned int u32;
typedef unsigned long long u64;
typedef unsigned long size_t;
typedef _Bool bool;
int mini_cmp(const void *s1,const void *s2,size_t len) {
    bool diff;
    asm("repe; cmpsb\n\t/* output condition code nz */\n"
        : "=@ccnz"(diff), "+D"(s1), "+S"(s2), "+c"(len));
    return diff;
}
u64 mini_div(u64 value,u32 divisor,u32 *rem) {
    union { u64 value; u32 word[2]; } d = {value};
    u32 upper = d.word[1];
    d.word[1] = 0;
    if(upper>=divisor){d.word[1]=upper/divisor;upper%=divisor;}
    asm("divl %2" : "=a"(d.word[0]), "=d"(*rem)
        : "rm"(divisor),"0"(d.word[0]),"1"(upper));
    return d.value;
}
void *mini_copy(void *dest,const void *src,size_t n) {
    long d0,d1,d2;
    asm volatile("rep ; movsq\n\t"
                 "movq %4,%%rcx\n\t"
                 "rep ; movsb\n\t"
        : "=&c"(d0),"=&D"(d1),"=&S"(d2)
        : "0"(n>>3),"g"(n&7),"1"(dest),"2"(src) : "memory");
    return dest;
}
SRC
sed 's/mini_/gnu_/g' "$d/mini.i" > "$d/gnu.c"
"$mini" --target=x86_64-linux -S "$d/mini.i" -o "$d/mini.s"
gcc -c -x assembler "$d/mini.s" -o "$d/mini.o"
gcc -O0 -c "$d/gnu.c" -o "$d/gnu.o"
cat > "$d/main.c" <<'SRC'
#include <stdint.h>
#include <string.h>
extern int mini_cmp(const void*,const void*,size_t);
extern int gnu_cmp(const void*,const void*,size_t);
extern uint64_t mini_div(uint64_t,uint32_t,uint32_t*);
extern uint64_t gnu_div(uint64_t,uint32_t,uint32_t*);
extern void *mini_copy(void*,const void*,size_t);
extern void *gnu_copy(void*,const void*,size_t);
int main(void) {
    unsigned char a[128],b[128],c[128],d[128];
    for(unsigned i=0;i<128;i++) a[i]=(unsigned char)(i*37U+11U);
    for(unsigned n=1;n<=120;n++) {
        memcpy(b,a,sizeof(a));
        if(mini_cmp(a,b,n)!=gnu_cmp(a,b,n)) return 1;
        b[n-1]^=0x80;
        if(mini_cmp(a,b,n)!=gnu_cmp(a,b,n)) return 2;
    }
    for(uint64_t n=1;n<=512;n++) {
        uint64_t value=(n*0x9e3779b97f4a7c15ULL)^(n<<47);
        uint32_t divisor=(uint32_t)(n*0x1001001UL)|1U,ra=0,rb=0;
        uint64_t qa=mini_div(value,divisor,&ra);
        uint64_t qb=gnu_div(value,divisor,&rb);
        if(qa!=qb||ra!=rb) return 3;
    }
    for(unsigned n=0;n<=120;n++) {
        memset(c,0xa5,sizeof(c));memset(d,0xa5,sizeof(d));
        if(mini_copy(c,a,n)!=c||gnu_copy(d,a,n)!=d) return 4;
        if(memcmp(c,d,sizeof(c))!=0) return 5;
        if(memcmp(c,a,n)!=0) return 6;
    }
    return 0;
}
SRC
gcc -O0 -no-pie "$d/main.c" "$d/mini.o" "$d/gnu.o" -o "$d/run"
"$d/run"
sed 's/repe; cmpsb/repne; cmpsb/' "$d/mini.i" > "$d/negative.i"
if "$mini" --target=x86_64-linux -S "$d/negative.i" -o "$d/negative.s" \
    >/dev/null 2>&1; then
    echo 'X86_BOOT_STRING_FAIL=unadmitted repeat condition' >&2
    exit 1
fi
test ! -e "$d/negative.s"
echo 'MINIC_X86_BOOT_STRING_NATIVE=PASS CMPS=240 DIV=512 COPY=121 negative=1'
