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
 type lhs_value=(type)(x),rhs_value=lhs_value; \
 type old_mini=(type)(y),old_gcc=old_mini; \
 _Bool m_result=mini_try_##bits##_##mode(&lhs_value,&old_mini,(type)(z)); \
 _Bool g_result=gcc_try_##bits##_##mode(&rhs_value,&old_gcc,(type)(z)); \
 if (m_result!=g_result||lhs_value!=rhs_value||old_mini!=old_gcc){ \
 fprintf(stderr,"CAS_DIFF bits=%s mode=%s src_mem=%llu src_old=%llu src_new=%llu mini_success=%d gnu_success=%d mini_mem=%llu gnu_mem=%llu mini_old=%llu gnu_old=%llu\\n", \
 #bits,#mode,(unsigned long long)(x),(unsigned long long)(y),(unsigned long long)(z), \
 (int)m_result,(int)g_result,(unsigned long long)lhs_value, \
 (unsigned long long)rhs_value,(unsigned long long)old_mini, \
 (unsigned long long)old_gcc);return 1;} \
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
# Original Linux atomic_t.counter (signed int) is passed through a
# volatile unsigned int MMIO/atomic carrier. Exercise ALL 4 byte widths and
# both SMP/local modes, with separately signed old/new operands. A narrowed
# or widened physical register operand must remain forbidden.
python3 - "$w/try.i" "$w/try-mixed.i" <<'PY'
import pathlib, sys
src=pathlib.Path(sys.argv[1]).read_text()
widths=[(8,"unsigned char","signed char"),
        (16,"unsigned short","short"),
        (32,"unsigned int","int"),
        (64,"unsigned long long","long long")]
out=[]
for bits, unsigned, signed in widths:
    for mode in ("lock","local"):
        start=src.index(f"_Bool mini_try_{bits}_{mode}(")
        end=src.index("\n}\n",start)+2
        body=src[start:end]
        old=f"({unsigned} *p, {unsigned} *expected, {unsigned} newval)"
        new=f"({unsigned} *p, {signed} *expected, {signed} newval)"
        if old not in body or f"{unsigned} old=*expected;" not in body:
            raise SystemExit(f"source fixture drift {bits} {mode}")
        body=body.replace(f"mini_try_{bits}_{mode}",f"mini_mix_{bits}_{mode}",1)
        body=body.replace(old,new,1)
        body=body.replace(f"{unsigned} old=*expected;",f"{signed} old=*expected;",1)
        out.append(body)
pathlib.Path(sys.argv[2]).write_text("\n\n".join(out)+"\n")
PY
sed 's/mini_mix_/gcc_mix_/g' "$w/try-mixed.i" >"$w/mixed-gnu.c"
cat >"$w/mixed-main.c" <<'EOF'
#include <stdio.h>
#define DIFF(BITS,U,S,MODE) do { \
  extern _Bool mini_mix_##BITS##_##MODE(U*,S*,S); \
  extern _Bool gcc_mix_##BITS##_##MODE(U*,S*,S); \
  int i,j; \
  for(i=-3;i<9;++i)for(j=-3;j<9;++j){ \
    U mem_m=(U)i,mem_g=mem_m; S old_m=(S)j,old_g=old_m; \
    S replacement=(S)(i+j+3); \
    _Bool mini=mini_mix_##BITS##_##MODE(&mem_m,&old_m,replacement); \
    _Bool gnu=gcc_mix_##BITS##_##MODE(&mem_g,&old_g,replacement); \
    if(mini!=gnu||mem_m!=mem_g||old_m!=old_g){ \
      fprintf(stderr,"TRY_CAS_SIGN_DIFF bits=%d mode=%s i=%d j=%d\\n", \
              BITS,#MODE,i,j); return 1; \
    } \
  } \
} while(0)
int main(void) {
    DIFF(8,unsigned char,signed char,lock);
    DIFF(8,unsigned char,signed char,local);
    DIFF(16,unsigned short,short,lock);
    DIFF(16,unsigned short,short,local);
    DIFF(32,unsigned int,int,lock);
    DIFF(32,unsigned int,int,local);
    DIFF(64,unsigned long long,long long,lock);
    DIFF(64,unsigned long long,long long,local);
    return 42;
}
EOF
"$minic" --target=x86_64-linux -S "$w/try-mixed.i" -o "$w/try-mixed.s"
cc -O0 -no-pie "$w/mixed-main.c" "$w/mixed-gnu.c" \
    -x assembler "$w/try-mixed.s" -o "$w/mixed-diff"
set +e
"$w/mixed-diff"; mixed_rc=$?
set -e
test "$mixed_rc" -eq 42 || {
    echo "FAIL x86 trycas signedness GNU differential $mixed_rc"; exit 1;
}
# A 16-bit carrier paired with 32-bit +a/new may not be accepted merely
# because the memory and value operands have different signedness.
sed 's/mini_mix_32_lock(unsigned int \*p/mini_mix_32_lock(unsigned short *p/' \
    "$w/try-mixed.i" >"$w/mixed-invalid.i"
if "$minic" --target=x86_64-linux -S "$w/mixed-invalid.i" \
    -o "$w/mixed-invalid.s" >/dev/null 2>&1; then
    echo "FAIL x86 trycas mismatched machine width accepted"; exit 1
fi
test ! -e "$w/mixed-invalid.s"

echo 'MINIC_X86_LINUX_TRYCAS=PASS variants=8 signed_mixed=8 GNU_pairs=1800 SMP_ELF=1 negative=2'
