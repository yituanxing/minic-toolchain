#!/usr/bin/env bash
# Original Linux this_cpu_ptr GS-relative arithmetic with GNU "0" tied
# register input and "m" displacement, real per-CPU segment memory.
# 5 ALU ops x 32/64-bit, GNU/GAS/native differential and strict negatives.
set -Eeuo pipefail
: "${MINIC_CC:?exact minic-cc required}"
d=$(mktemp -d);trap 'rm -rf "$d"' EXIT
cat >"$d/source.i" <<'SRC'
extern unsigned long gs_offset_long;
extern unsigned int gs_offset_int;
#define ALU64(name,opcode) \
unsigned long mini_##name##64(unsigned long seed) { \
    unsigned long out; \
    asm(opcode " %%gs:%1,%0" : "=r"(out) : "m"(gs_offset_long), "0"(seed)); \
    return out; \
}
#define ALU32(name,opcode) \
unsigned int mini_##name##32(unsigned int seed) { \
    unsigned int out; \
    asm(opcode "l %%gs:%1,%0" : "=r"(out) : "m"(gs_offset_int), "0"(seed)); \
    return out; \
}
ALU64(add, "add") ALU64(sub, "sub") ALU64(and, "and")
ALU64(or, "or") ALU64(xor, "xor")
ALU32(add, "add") ALU32(sub, "sub") ALU32(and, "and")
ALU32(or, "or") ALU32(xor, "xor")
unsigned long mini_ptr(const void *ptr) {
    unsigned long result;
    asm ("add %%gs:%1,%0" : "=r"(result) : "m"(gs_offset_long),"0"(ptr));
    return result;
}
SRC
# Feed an already macro-expanded C translation unit to MiniC, exactly as GNU
# Kbuild .i inputs. GNU preprocessor is an oracle, not a C fallback.
gcc -E -P -x c "$d/source.i" -o "$d/source.pp.i"
sed 's/mini_/gnu_/g' "$d/source.pp.i" >"$d/gnu.c"
cat >"$d/main.c" <<'SRC'
#include <asm/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>
unsigned long gs_offset_long=0x51239fUL;
unsigned int gs_offset_int=0x39127u;
#define ONE64(name) extern unsigned long mini_##name##64(unsigned long); \
 extern unsigned long gnu_##name##64(unsigned long);
#define ONE32(name) extern unsigned int mini_##name##32(unsigned int); \
 extern unsigned int gnu_##name##32(unsigned int);
ONE64(add) ONE64(sub) ONE64(and) ONE64(or) ONE64(xor)
ONE32(add) ONE32(sub) ONE32(and) ONE32(or) ONE32(xor)
extern unsigned long mini_ptr(const void *),gnu_ptr(const void *);
int main(void) {
    unsigned long seed=0x51f69a52de88UL;
    unsigned int seed32=0x76aab431U;
    if(syscall(SYS_arch_prctl,ARCH_SET_GS,0L)) return 7;
#define CHECK64(name) if(mini_##name##64(seed)!=gnu_##name##64(seed)) return 8;
#define CHECK32(name) if(mini_##name##32(seed32)!=gnu_##name##32(seed32)) return 9;
    CHECK64(add) CHECK64(sub) CHECK64(and) CHECK64(or) CHECK64(xor)
    CHECK32(add) CHECK32(sub) CHECK32(and) CHECK32(or) CHECK32(xor)
    if(mini_ptr((const void*)0x20000000UL)!=gnu_ptr((const void*)0x20000000UL))
        return 10;
    return 42;
}
SRC
gcc -O0 -c "$d/gnu.c" -o "$d/gnu.o"
"$MINIC_CC" --target=x86_64-linux -S "$d/source.pp.i" -o "$d/mini.s"
gcc -c -x assembler "$d/mini.s" -o "$d/mini.o"
gcc -static -O0 -no-pie "$d/main.c" "$d/gnu.o" "$d/mini.o" -o "$d/oracle"
set +e
timeout 45s "$d/oracle" >"$d/run.out" 2>"$d/run.err";rc=$?
set -e
if ((rc!=42));then
 echo "X86_GS_TIED_ALU=FAIL native_source_GNU_vs_MiniC rc=$rc" >&2
 cat "$d/run.err" >&2;exit 1
fi
cat >"$d/bad.i" <<'SRC'
unsigned long illegal(unsigned long x) {
 unsigned long out;
 asm("add %%gs:%1,%0; int3":"=r"(out):"m"(x),"0"(x));
 return out;
}
SRC
if "$MINIC_CC" --target=x86_64-linux -S "$d/bad.i" -o "$d/bad.s" 2>"$d/bad.err";then
 echo "X86_GS_TIED_ALU=FAIL extra ISA admitted" >&2;exit 1
fi
test ! -e "$d/bad.s"
echo "X86_GS_TIED_ALU=PASS ISA_forms=10 tied_pointer=1 real_GS_segment=1 native_GNU_oracle=1 unsupported_rejected=1"
