#!/usr/bin/env bash
# ORIGINAL GNU Linux 6.6.143 CMPXCHG: four widths and SMP/local modes.
set -Eeuo pipefail
minic=$1; w=$(mktemp -d); trap 'rm -rf "$w"' EXIT
cat >"$w/try.i" <<'EOF'
_Bool mini_try_8_lock(unsigned char *p, unsigned char *expected, unsigned char newval) {
    unsigned char old=*expected;
    _Bool success;
    __asm__ __volatile__(".pushsection .smp_locks,\"a\"\n.balign 4\n.long 671f - .\n.popsection\n671:\n\tlock; cmpxchgb %[new], %[ptr]\n\t/* output condition code z */\n"
      : [success] "=@ccz"(success), [ptr] "+m"(*p), [old] "+a"(old)
      : [new] "q"(newval) : "memory");
    if (!success) *expected=old;
    return success;
}

_Bool mini_try_8_local(unsigned char *p, unsigned char *expected, unsigned char newval) {
    unsigned char old=*expected;
    _Bool success;
    __asm__ __volatile__("cmpxchgb %[new], %[ptr]\n\t/* output condition code z */\n"
      : [success] "=@ccz"(success), [ptr] "+m"(*p), [old] "+a"(old)
      : [new] "q"(newval) : "memory");
    if (!success) *expected=old;
    return success;
}

_Bool mini_try_16_lock(unsigned short *p, unsigned short *expected, unsigned short newval) {
    unsigned short old=*expected;
    _Bool success;
    __asm__ __volatile__(".pushsection .smp_locks,\"a\"\n.balign 4\n.long 671f - .\n.popsection\n671:\n\tlock; cmpxchgw %[new], %[ptr]\n\t/* output condition code z */\n"
      : [success] "=@ccz"(success), [ptr] "+m"(*p), [old] "+a"(old)
      : [new] "r"(newval) : "memory");
    if (!success) *expected=old;
    return success;
}

_Bool mini_try_16_local(unsigned short *p, unsigned short *expected, unsigned short newval) {
    unsigned short old=*expected;
    _Bool success;
    __asm__ __volatile__("cmpxchgw %[new], %[ptr]\n\t/* output condition code z */\n"
      : [success] "=@ccz"(success), [ptr] "+m"(*p), [old] "+a"(old)
      : [new] "r"(newval) : "memory");
    if (!success) *expected=old;
    return success;
}

_Bool mini_try_32_lock(unsigned int *p, unsigned int *expected, unsigned int newval) {
    unsigned int old=*expected;
    _Bool success;
    __asm__ __volatile__(".pushsection .smp_locks,\"a\"\n.balign 4\n.long 671f - .\n.popsection\n671:\n\tlock; cmpxchgl %[new], %[ptr]\n\t/* output condition code z */\n"
      : [success] "=@ccz"(success), [ptr] "+m"(*p), [old] "+a"(old)
      : [new] "r"(newval) : "memory");
    if (!success) *expected=old;
    return success;
}

_Bool mini_try_32_local(unsigned int *p, unsigned int *expected, unsigned int newval) {
    unsigned int old=*expected;
    _Bool success;
    __asm__ __volatile__("cmpxchgl %[new], %[ptr]\n\t/* output condition code z */\n"
      : [success] "=@ccz"(success), [ptr] "+m"(*p), [old] "+a"(old)
      : [new] "r"(newval) : "memory");
    if (!success) *expected=old;
    return success;
}

_Bool mini_try_64_lock(unsigned long long *p, unsigned long long *expected, unsigned long long newval) {
    unsigned long long old=*expected;
    _Bool success;
    __asm__ __volatile__(".pushsection .smp_locks,\"a\"\n.balign 4\n.long 671f - .\n.popsection\n671:\n\tlock; cmpxchgq %[new], %[ptr]\n\t/* output condition code z */\n"
      : [success] "=@ccz"(success), [ptr] "+m"(*p), [old] "+a"(old)
      : [new] "r"(newval) : "memory");
    if (!success) *expected=old;
    return success;
}

_Bool mini_try_64_local(unsigned long long *p, unsigned long long *expected, unsigned long long newval) {
    unsigned long long old=*expected;
    _Bool success;
    __asm__ __volatile__("cmpxchgq %[new], %[ptr]\n\t/* output condition code z */\n"
      : [success] "=@ccz"(success), [ptr] "+m"(*p), [old] "+a"(old)
      : [new] "r"(newval) : "memory");
    if (!success) *expected=old;
    return success;
}
EOF
sed 's/mini_/gcc_/g' "$w/try.i" > "$w/gcc.c"
cat >"$w/main.c" <<'EOF'
#include <stdio.h>
typedef unsigned char u8;typedef unsigned short u16;
typedef unsigned int u32;typedef unsigned long long u64;
#define FUN(bits,type) \
extern _Bool mini_try_##bits##_lock(type*,type*,type); \
extern _Bool mini_try_##bits##_local(type*,type*,type); \
extern _Bool gcc_try_##bits##_lock(type*,type*,type); \
extern _Bool gcc_try_##bits##_local(type*,type*,type);
FUN(8,u8) FUN(16,u16) FUN(32,u32) FUN(64,u64)
#define CHECK(bits,type,mode,x,y,z) do{ \
 type a=(type)(x),b=a,e1=(type)(y),e2=e1; \
 _Bool i=mini_try_##bits##_##mode(&a,&e1,(type)(z)); \
 _Bool j=gcc_try_##bits##_##mode(&b,&e2,(type)(z)); \
 if (i!=j||a!=b||e1!=e2) { \
  fprintf(stderr,"CAS_DIFF bits=%s mode=%s memory=%llu expected=%llu new=%llu mini_success=%d gnu_success=%d mini_mem=%llu gnu_mem=%llu mini_old=%llu gnu_old=%llu\\n", \
  #bits,#mode,(unsigned long long)(x),(unsigned long long)(y),(unsigned long long)(z), \
  (int)i,(int)j,(unsigned long long)a,(unsigned long long)b, \
  (unsigned long long)e1,(unsigned long long)e2); return 1; } \
}while(0)
int main(void) {
 static const int values[]={0,1,2,3,8,42,127,255,-1};
 int a,b;
 for(a=0;a<9;a++)for(b=0;b<9;b++){
 CHECK(8,u8,lock,values[a],values[b],values[a]+1);
 CHECK(8,u8,local,values[a],values[b],values[a]+1);
 CHECK(16,u16,lock,values[a],values[b],values[a]+1);
 CHECK(16,u16,local,values[a],values[b],values[a]+1);
 CHECK(32,u32,lock,values[a],values[b],values[a]+1);
 CHECK(32,u32,local,values[a],values[b],values[a]+1);
 CHECK(64,u64,lock,values[a],values[b],values[a]+1);
 CHECK(64,u64,local,values[a],values[b],values[a]+1);
 }
 return 42;
}
EOF
"$minic" --target=x86_64-linux -S "$w/try.i" -o "$w/try.s"
for op in cmpxchgb cmpxchgw cmpxchgl cmpxchgq 'sete %r8b';do
 grep -Fq "$op" "$w/try.s"
done
cc -O0 -no-pie "$w/main.c" "$w/gcc.c" -x assembler "$w/try.s" -o "$w/diff"
set +e
"$w/diff"; rc=$?
set -e
test "$rc" -eq 42 || { echo "FAIL x86 trycas GNU diff $rc";exit 1; }
cc -c -x assembler "$w/try.s" -o "$w/try.o"
objdump -h "$w/try.o" | grep -Fq '.smp_locks'
sed 's/cmpxchgb/cmpxchgb; cli/' "$w/try.i" >"$w/evil.i"
if "$minic" --target=x86_64-linux -S "$w/evil.i" -o "$w/evil.s" >/dev/null 2>&1;then
 echo 'FAIL privileged suffix passed x86 trycas';exit 1;fi
echo 'MINIC_X86_LINUX_TRYCAS=PASS variants=8 GNU_pairs=648 SMP_ELF=1 negative=1'
