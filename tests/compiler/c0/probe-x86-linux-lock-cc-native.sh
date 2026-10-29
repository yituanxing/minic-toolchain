#!/usr/bin/env bash
# GNU Linux 6.6.143 local ADD sign, queued-lock BTS carry and byte AND sign.
# Compare both memory state and architectural condition-code outputs to GCC.
set -Eeuo pipefail
mini=${1:?MiniC compiler}
test -x "$mini"
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
cat > "$d/mini.i" <<'SRC'
typedef _Bool bool;
typedef unsigned int u32;
bool mini_add(long value, long long *p) {
    bool negative;
    asm volatile("addq %[val], %[var]\n\t/* output condition code s */\n"
        : [var] "+m"(*p), "=@ccs"(negative)
        : [val] "er"(value) : "memory");
    return negative;
}
bool mini_bts(u32 *p) {
    bool was_set;
    asm volatile(".pushsection .smp_locks,\"a\"\n"
                 ".balign 4\n" ".long 671f - .\n" ".popsection\n"
                 "671:\n\tlock; btsl %[val], %[var]\n"
                 "\t/* output condition code c */\n"
        : [var] "+m"(*p), "=@ccc"(was_set)
        : [val] "I"(8) : "memory");
    return was_set;
}
bool mini_and(unsigned char mask, unsigned char *p) {
    bool negative;
    asm volatile(".pushsection .smp_locks,\"a\"\n"
                 ".balign 4\n" ".long 671f - .\n" ".popsection\n"
                 "671:\n\tlock; andb %2,%1\n"
                 "\t/* output condition code s */\n"
        : "=@ccs"(negative), "+m"(*p) : "ir"(mask) : "memory");
    return negative;
}
SRC
sed 's/mini_/gnu_/g' "$d/mini.i" > "$d/gnu.c"
"$mini" --target=x86_64-linux -S "$d/mini.i" -o "$d/mini.s"
gcc -c -x assembler "$d/mini.s" -o "$d/mini.o"
gcc -O0 -c "$d/gnu.c" -o "$d/gnu.o"
cat > "$d/main.c" <<'SRC'
#include <stdint.h>
#include <stdbool.h>
extern bool mini_add(long,long long*),gnu_add(long,long long*);
extern bool mini_bts(uint32_t*),gnu_bts(uint32_t*);
extern bool mini_and(unsigned char,unsigned char*),gnu_and(unsigned char,unsigned char*);
int main(void) {
    for(unsigned long n=0;n<256;n++) {
        long x=(long)(n*0x810101UL) - 0x60000000L;
        long long a=(long long)n<<56,b=a;
        bool f=mini_add(x,&a),g=gnu_add(x,&b);
        if(a!=b||f!=g) return 1;
        uint32_t aa=n&1?0xffU:0U,bb=aa;
        f=mini_bts(&aa);g=gnu_bts(&bb);
        if(aa!=bb||f!=g) return 2;
        unsigned char ca=(unsigned char)n,cb=ca;
        f=mini_and((unsigned char)(n^0x80U),&ca);
        g=gnu_and((unsigned char)(n^0x80U),&cb);
        if(ca!=cb||f!=g) return 3;
    }
    return 0;
}
SRC
gcc -O0 -no-pie "$d/main.c" "$d/mini.o" "$d/gnu.o" -o "$d/run"
"$d/run"
sed 's/lock; btsl/lock; btcl/' "$d/mini.i" > "$d/negative.i"
if "$mini" --target=x86_64-linux -S "$d/negative.i" -o "$d/negative.s" \
    >/dev/null 2>&1; then
    echo 'X86_LOCK_CC_FAIL=unadmitted bit opcode' >&2
    exit 1
fi
test ! -e "$d/negative.s"
echo 'MINIC_X86_LOCK_CC_NATIVE=PASS local_add=256 lock_bts=256 lock_and=256 negative=1'
