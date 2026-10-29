#!/usr/bin/env bash
# Linux memset16/32/64: exact GNU =c,=D / a,1,0 register contract,
# including observable RCX/RDI outputs after REP STOS.
set -Eeuo pipefail
mini=${1:?MiniC compiler}
test -x "$mini"
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
cat > "$d/mini.i" <<'SRC'
unsigned long mini_stosw(unsigned short *s, unsigned short v,
                         unsigned long n, unsigned long *end) {
    long d0, d1;
    asm volatile("rep\n\tstosw" : "=&c"(d0), "=&D"(d1)
                 : "a"(v), "1"(s), "0"(n) : "memory");
    *end=(unsigned long)d1;
    return (unsigned long)d0;
}
unsigned long mini_stosl(unsigned int *s, unsigned int v,
                         unsigned long n, unsigned long *end) {
    long d0, d1;
    asm volatile("rep\n\tstosl" : "=&c"(d0), "=&D"(d1)
                 : "a"(v), "1"(s), "0"(n) : "memory");
    *end=(unsigned long)d1;
    return (unsigned long)d0;
}
unsigned long mini_stosq(unsigned long *s, unsigned long v,
                         unsigned long n, unsigned long *end) {
    long d0, d1;
    asm volatile("rep\n\tstosq" : "=&c"(d0), "=&D"(d1)
                 : "a"(v), "1"(s), "0"(n) : "memory");
    *end=(unsigned long)d1;
    return (unsigned long)d0;
}
SRC
sed 's/mini_/gnu_/g' "$d/mini.i" > "$d/gnu.c"
"$mini" --target=x86_64-linux -S "$d/mini.i" -o "$d/mini.s"
gcc -c -x assembler "$d/mini.s" -o "$d/mini.o"
gcc -O0 -c "$d/gnu.c" -o "$d/gnu.o"
cat > "$d/main.c" <<'SRC'
#include <string.h>
#include <stdint.h>
extern unsigned long mini_stosw(unsigned short*,unsigned short,unsigned long,unsigned long*);
extern unsigned long gnu_stosw(unsigned short*,unsigned short,unsigned long,unsigned long*);
extern unsigned long mini_stosl(unsigned int*,unsigned int,unsigned long,unsigned long*);
extern unsigned long gnu_stosl(unsigned int*,unsigned int,unsigned long,unsigned long*);
extern unsigned long mini_stosq(unsigned long*,unsigned long,unsigned long,unsigned long*);
extern unsigned long gnu_stosq(unsigned long*,unsigned long,unsigned long,unsigned long*);
int main(void) {
    unsigned long a[24],b[24],ma,ga,mr,gr;
    for (unsigned long n=0;n<12;n++) {
        for(unsigned i=0;i<24;i++)a[i]=b[i]=0x8877665544332211UL;
        mr=mini_stosw((unsigned short*)a+3,0xa1b2,(n),&ma);
        gr=gnu_stosw((unsigned short*)b+3,0xa1b2,(n),&ga);
        if(mr!=gr||ma!=(uintptr_t)((unsigned short*)a+3+n)||
           ga!=(uintptr_t)((unsigned short*)b+3+n)||memcmp(a,b,sizeof(a)))return 1;
        for(unsigned i=0;i<24;i++)a[i]=b[i]=0x8877665544332211UL;
        mr=mini_stosl((unsigned int*)a+3,0xa1b2c3d4,n,&ma);
        gr=gnu_stosl((unsigned int*)b+3,0xa1b2c3d4,n,&ga);
        if(mr!=gr||ma!=(uintptr_t)((unsigned int*)a+3+n)||
           ga!=(uintptr_t)((unsigned int*)b+3+n)||memcmp(a,b,sizeof(a)))return 2;
        for(unsigned i=0;i<24;i++)a[i]=b[i]=0x8877665544332211UL;
        mr=mini_stosq(a+3,0xa1b2c3d4e5f60718UL,n,&ma);
        gr=gnu_stosq(b+3,0xa1b2c3d4e5f60718UL,n,&ga);
        if(mr!=gr||ma!=(uintptr_t)(a+3+n)||
           ga!=(uintptr_t)(b+3+n)||memcmp(a,b,sizeof(a)))return 3;
    }
    return 0;
}
SRC
gcc -O0 -no-pie "$d/main.c" "$d/mini.o" "$d/gnu.o" -o "$d/run"
"$d/run"
sed 's/"=&D"/"=&r"/' "$d/mini.i" > "$d/bad.i"
if "$mini" --target=x86_64-linux -S "$d/bad.i" -o "$d/bad.s" >/dev/null 2>&1; then
    echo 'X86_REP_STOS_FAIL=wrong register constraint accepted' >&2
    exit 1
fi
test ! -e "$d/bad.s"
echo 'MINIC_X86_REP_STOS=PASS widths=3 GNU_native=36 negative=1'
