#!/usr/bin/env bash
# Named manual probes; shared initialization is exact, case assertions are unchanged.
if [ "${1:-}" = "--list" ]; then
    printf '%s\n' 'probe-x86-linux-boot-string-native' 'probe-x86-linux-delay-native' 'probe-x86-linux-gs-atomic-native' 'probe-x86-linux-lock-cc-native' 'probe-x86-linux-lowlevel-descriptor-machine' 'probe-x86-linux-lowlevel-isa-native' 'probe-x86-linux-rep-stos-gcc-native' 'probe-x86-linux-segment-extable-machine' 'probe-x86-linux-vdso-cpunode-alt' 'probe-x86-linux-vdso-syscall-native'
    exit 0
fi
__minic_probe_case=${1:?probe case required; use --list}
shift
case "$__minic_probe_case" in
  probe-x86-linux-boot-string-native|probe-x86-linux-delay-native|probe-x86-linux-gs-atomic-native|probe-x86-linux-lock-cc-native|probe-x86-linux-lowlevel-descriptor-machine|probe-x86-linux-lowlevel-isa-native|probe-x86-linux-rep-stos-gcc-native|probe-x86-linux-segment-extable-machine|probe-x86-linux-vdso-cpunode-alt|probe-x86-linux-vdso-syscall-native) ;;
  *) printf "%s\n" "unknown probe: $__minic_probe_case" >&2; exit 64 ;;
esac
set -Eeuo pipefail
mini=${1:?MiniC compiler}
test -x "$mini"
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
case "$__minic_probe_case" in
probe-x86-linux-boot-string-native)
# Frozen x86 compressed boot original REP/CMPSB ZF, EDX:EAX DIVL and
# REP MOVSQ/MOVSB tied early-clobber grammar versus GCC native behavior.
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
;;
probe-x86-linux-delay-native)
# Original GNU x86 delay calibration grammar.  Multiplication and the counted
# busywait compare live C results with GCC; TPAUSE is instruction-set gated,
# so both compilers' TPAUSE encodings are assembled but never executed.
cat > "$d/mini.i" <<'SRC'
unsigned long mini_mul(unsigned long xloops, unsigned long lpj) {
    int d0;
    asm("mull %%edx" : "=d"(xloops), "=&a"(d0)
        : "1"(xloops), "0"(lpj));
    return xloops;
}
unsigned long mini_loop(unsigned long loops) {
    asm volatile("test %0,%0\n\tjz 3f\n\tjmp 1f\n\t"
                 ".align 16\n1:\tjmp 2f\n\t.align 16\n"
                 "2:\tdec %0\n\tjnz 2b\n3:\tdec %0\n"
        : "+a"(loops));
    return loops;
}
void mini_tpause(unsigned int ecx, unsigned int edx, unsigned int eax) {
    asm volatile("tpause %%ecx\n" :: "c"(ecx), "d"(edx), "a"(eax));
}
SRC
sed 's/mini_/gnu_/g' "$d/mini.i" > "$d/gnu.c"
"$mini" --target=x86_64-linux -S "$d/mini.i" -o "$d/mini.s"
gcc -c -x assembler "$d/mini.s" -o "$d/mini.o"
gcc -O0 -c "$d/gnu.c" -o "$d/gnu.o"
objdump -dr "$d/mini.o" | grep -Eq '[[:space:]](mul|tpause|dec)[[:space:]]'
cat > "$d/main.c" <<'SRC'
extern unsigned long mini_mul(unsigned long,unsigned long);
extern unsigned long gnu_mul(unsigned long,unsigned long);
extern unsigned long mini_loop(unsigned long);
extern unsigned long gnu_loop(unsigned long);
int main(void) {
    for (unsigned long i=0;i<256;i++) {
        unsigned long a=(i*0x9e3779b9UL) ^ (i<<34);
        unsigned long b=(i*0xc3e734abUL) ^ (i<<39);
        if(mini_mul(a,b)!=gnu_mul(a,b)) return 1;
    }
    for (unsigned long i=0;i<32;i++)
        if(mini_loop(i)!=gnu_loop(i)) return 2;
    return 0;
}
SRC
gcc -O0 -no-pie "$d/main.c" "$d/mini.o" "$d/gnu.o" -o "$d/run"
"$d/run"
sed 's/mull %%edx/idivl %%edx/' "$d/mini.i" > "$d/negative.i"
if "$mini" --target=x86_64-linux -S "$d/negative.i" -o "$d/negative.s" \
    >/dev/null 2>&1; then
    echo 'X86_DELAY_FAIL=unadmitted multiply variant' >&2
    exit 1
fi
test ! -e "$d/negative.s"
echo 'MINIC_X86_DELAY_NATIVE=PASS MUL=256 LOOP=32 TPAUSE_GNU_AS=1 negative=1'
;;
probe-x86-linux-gs-atomic-native)
# Original Linux 6.6.143 __this_cpu_{add_return,cmpxchg,xchg} carrier patterns.
# The GNU oracle and MiniC both access real %gs-based memory; compare values,
# previous-value results and failed/successful compare-and-exchange effects.
python3 - "$d" <<'PY'
from pathlib import Path
import sys
d=Path(sys.argv[1])
widths=[('b','unsigned char','q'),('w','unsigned short','r'),
        ('l','unsigned int','r'),('q','unsigned long','r')]
defs=[]
driver=['#include <sys/syscall.h>','#include <unistd.h>',
        '#include <stdint.h>','#define ARCH_SET_GS 0x1001']
checks=[]
for suf,ty,reg in widths:
    defs += [f'''{ty} mini_xadd_{suf}(volatile {ty} *p, {ty} value) {{
      asm("xadd{suf} %[tmp], %%gs:%[var]"
          : [tmp] "+{reg}"(value), [var] "+m"(*p) :: "memory");
      return value;
    }}''',f'''{ty} mini_cas_{suf}(volatile {ty} *p, {ty} old, {ty} value) {{
      asm("cmpxchg{suf} %[nval], %%gs:%[var]"
          : [oval] "+a"(old), [var] "+m"(*p)
          : [nval] "{reg}"(value) : "memory");
      return old;
    }}''',f'''{ty} mini_loop_{suf}(volatile {ty} *p, {ty} value) {{
      {ty} old;
      asm volatile("mov{suf} %%gs:%[var], %[oval]\\n"
          "1:\\tcmpxchg{suf} %[nval], %%gs:%[var]\\n\\tjnz 1b"
          : [oval] "=&a"(old), [var] "+m"(*p)
          : [nval] "{reg}"(value) : "memory");
      return old;
    }}''']
    for prefix in ('mini','gnu'):
        for kind,args in [('xadd',f'volatile {ty} *, {ty}'),
                          ('cas',f'volatile {ty} *, {ty}, {ty}'),
                          ('loop',f'volatile {ty} *, {ty}')]:
            driver.append(f'extern {ty} {prefix}_{kind}_{suf}({args});')
    cast=f'(volatile {ty} *)(uintptr_t)0'
    checks.append(f'''{{
       volatile {ty} *p={cast};
       storage=37;
       {ty} m=mini_xadd_{suf}(p, 11);
       unsigned long long changed=storage;
       storage=37;
       {ty} g=gnu_xadd_{suf}(p, 11);
       if (m!=g || changed!=storage || storage!=48) return 11;
       storage=37;
       m=mini_cas_{suf}(p, 37, 22);
       changed=storage;
       storage=37;
       g=gnu_cas_{suf}(p, 37, 22);
       if (m!=g || changed!=storage || storage!=22) return 12;
       storage=37;
       m=mini_cas_{suf}(p, 19, 22);
       changed=storage;
       storage=37;
       g=gnu_cas_{suf}(p, 19, 22);
       if (m!=g || changed!=storage || storage!=37) return 13;
       storage=37;
       m=mini_loop_{suf}(p, 16);
       changed=storage;
       storage=37;
       g=gnu_loop_{suf}(p, 16);
       if (m!=g || changed!=storage || storage!=16) return 14;
    }}''')
source='\n'.join(defs)+'\n'
(d/'mini.i').write_text(source)
(d/'gnu.c').write_text(source.replace('mini_', 'gnu_'))
(d/'bad.i').write_text(source.replace('cmpxchgl %[nval], %%gs:%[var]',
                                      'cmpxchgl %[nval], %%gs:%[var]; cli'))
(d/'bad-loop.i').write_text(source.replace('jnz 1b', 'jz 1b'))
(d/'bad-segment.i').write_text(source.replace(
    'xaddq %[tmp], %%gs:%[var]', 'xaddq %[tmp], %%fs:%[var]'))
(d/'bad-carrier.i').write_text(source.replace(
    '[oval] "+a"(old)', '[oval] "=a"(old)'))
driver += ['int main(void) {','  volatile unsigned long long storage=0;',
           '  if (syscall(SYS_arch_prctl, ARCH_SET_GS, (unsigned long)&storage)) return 2;',
           *checks,'  return 0;','}']
(d/'driver.c').write_text('\n'.join(driver)+'\n')
PY
"$mini" --target=x86_64-linux -S "$d/mini.i" -o "$d/mini.s"
gcc -c -x assembler "$d/mini.s" -o "$d/mini.o"
gcc -O0 -c "$d/gnu.c" -o "$d/gnu.o"
grep -Fq 'xaddb ' "$d/mini.s"
grep -Fq 'cmpxchgq ' "$d/mini.s"
grep -Fq '%gs:' "$d/mini.s"
gcc -O0 -no-pie "$d/driver.c" "$d/mini.o" "$d/gnu.o" -o "$d/run"
"$d/run"
for negative in bad bad-loop bad-segment bad-carrier; do
    if "$mini" --target=x86_64-linux -S "$d/$negative.i" \
        -o "$d/$negative.s" >/dev/null 2>&1; then
        echo "X86_GS_ATOMIC_FAIL=unsafe $negative variant accepted" >&2
        exit 1
    fi
    test ! -e "$d/$negative.s"
done
echo 'MINIC_X86_GS_ATOMIC=PASS forms=12 GNU_native=12 negative=4'
;;
probe-x86-linux-lock-cc-native)
# GNU Linux 6.6.143 local ADD sign, queued-lock BTS carry and byte AND sign.
# Compare both memory state and architectural condition-code outputs to GCC.
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
;;
probe-x86-linux-lowlevel-descriptor-machine)
# Source-qualified Linux LLDT/INVPCID -> physical x86 MachineIR -> GNU AS.
# These are privileged instructions: assemble only, never execute them.
cat > "$d/mini.i" <<'SRC'
void mini_lldt(unsigned int value) {
    asm volatile("lldt %w0" :: "q"(value));
}
void mini_lldt_word(unsigned short value) {
    asm volatile("lldt %w0" :: "q"(value));
}
void mini_lldt_long(unsigned long value) {
    asm volatile("lldt %w0" :: "q"(value));
}
void mini_sldt(unsigned short *p) {
    asm("sldt %0" : "=m"(*p));
}
void mini_fs(unsigned int value) {
    asm volatile("mov %0, %%fs" :: "rm"(value));
}
struct Desc { unsigned long d[2]; };
void mini_invpcid(struct Desc *p, unsigned long type) {
    asm volatile("invpcid %[desc], %[type]"
                 :: [desc] "m"(*p), [type] "r"(type) : "memory");
}
SRC
sed 's/mini_/gnu_/g' "$d/mini.i" > "$d/gnu.c"
"$mini" --target=x86_64-linux -S "$d/mini.i" -o "$d/mini.s"
gcc -c -x assembler "$d/mini.s" -o "$d/mini.o"
gcc -c -O0 "$d/gnu.c" -o "$d/gnu.o"
objdump -dr "$d/mini.o" > "$d/mini.dump"
objdump -dr "$d/gnu.o" > "$d/gnu.dump"
test "$(grep -Ec '[[:space:]]lldt[[:space:]]' "$d/mini.dump")" -eq 3
test "$(grep -Ec '[[:space:]]lldt[[:space:]]' "$d/gnu.dump")" -eq 3
grep -q 'invpcid' "$d/mini.dump"
grep -q 'invpcid' "$d/gnu.dump"
grep -q 'sldt' "$d/mini.dump"
grep -q 'sldt' "$d/gnu.dump"
grep -q '%fs' "$d/mini.dump"
grep -q '%fs' "$d/gnu.dump"
grep -q 'lldt %r10w' "$d/mini.s"
grep -q 'invpcid (%r10), %r11' "$d/mini.s"
grep -q 'sldt (%r10)' "$d/mini.s"
grep -q 'movw %r10w, %fs' "$d/mini.s"

# Negative source and target physical contracts: no arbitrary passthrough,
# malformed constraints, wrong architectural width, or bad descriptor size.
sed 's/"q"(value)/"r"(value)/g' "$d/mini.i" > "$d/bad-constraint.i"
sed 's/"invpcid %\[desc\], %\[type\]"/"invpcid %[desc], %[type]; cli"/' \
    "$d/mini.i" > "$d/bad-template.i"
sed 's/unsigned long d\[2\]/unsigned long d[1]/' \
    "$d/mini.i" > "$d/bad-descriptor.i"
sed 's/struct Desc \*p, unsigned long type/struct Desc *p, unsigned int type/' \
    "$d/mini.i" > "$d/bad-type.i"
sed 's/"=m"(\*p)/"=r"(*p)/' "$d/mini.i" > "$d/bad-sldt-constraint.i"
sed 's/unsigned short \*p/unsigned long *p/' \
    "$d/mini.i" > "$d/bad-sldt-width.i"
sed 's/"rm"(value)/"r"(value)/' "$d/mini.i" > "$d/bad-fs-constraint.i"
sed 's/mov %0, %%fs/mov %0, %%cs/' "$d/mini.i" > "$d/bad-fs-template.i"
for case in constraint template descriptor type sldt-constraint sldt-width \
    fs-constraint fs-template; do
    if "$mini" --target=x86_64-linux -S "$d/bad-$case.i" \
        -o "$d/bad-$case.s" >/dev/null 2>&1; then
        echo "X86_LOWLEVEL_DESCRIPTOR_FAIL=accepted_bad_$case" >&2
        exit 1
    fi
    test ! -e "$d/bad-$case.s"
done
echo 'MINIC_X86_LOWLEVEL_DESCRIPTOR=PASS GNU_AS=6 negative=8'
;;
probe-x86-linux-lowlevel-isa-native)
# Exact target-owned x86 descriptor/syscall-side ISA selection; the privileged
# cases are GNU AS verified but must never run in user mode. BTC/MOVNTI have
# independent GCC-vs-MiniC native effects and negative admission tests.
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
;;
probe-x86-linux-rep-stos-gcc-native)
# Linux memset16/32/64: exact GNU =c,=D / a,1,0 register contract,
# including observable RCX/RDI outputs after REP STOS.
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
;;
probe-x86-linux-segment-extable-machine)
# Original Linux ES/DS/SS and LKGS exception-table contracts, not executed
# in user mode. The third __ex_table word is a physical register-index ABI.
cat > "$d/mini.c" <<'SRC'
#define EXT_REG_CORE \
" .pushsection \"__ex_table\",\"a\"\n" \
" .balign 4\n" \
" .long (1b) - .\n" \
" .long (1b) - .\n" \
".macro extable_type_reg type:req reg:req\n" \
".set .Lfound, 0\n" \
".set .Lregnr, 0\n" \
".irp rs,rax,rcx,rdx,rbx,rsp,rbp,rsi,rdi,r8,r9,r10,r11,r12,r13,r14,r15\n" \
".ifc \\reg, %%\\rs\n" \
".set .Lfound, .Lfound+1\n" \
".long \\type + (.Lregnr << 8)\n" \
".endif\n" \
".set .Lregnr, .Lregnr+1\n" \
".endr\n" \
".set .Lregnr, 0\n" \
".irp rs,eax,ecx,edx,ebx,esp,ebp,esi,edi,r8d,r9d,r10d,r11d,r12d,r13d,r14d,r15d\n" \
".ifc \\reg, %%\\rs\n" \
".set .Lfound, .Lfound+1\n" \
".long \\type + (.Lregnr << 8)\n" \
".endif\n" \
".set .Lregnr, .Lregnr+1\n" \
".endr\n" \
".if (.Lfound != 1)\n" \
".error \"extable_type_reg: bad register argument\"\n" \
".endif\n" \
".endm\n" \
"extable_type_reg reg=%k0, type=(17 | ((0) << 16)) \n" \
".purgem extable_type_reg\n" \
" .popsection\n"
#define SEGMENT(reg) "\t\t\t\t\t\t\n1:\tmovl %k0,%%" reg "\t\t\n" EXT_REG_CORE
#define LKGS "1:  .byte 0xf2,0x0f,0x00,0xf7 ; " EXT_REG_CORE
void mini_es(unsigned short selector) {
    asm volatile(SEGMENT("es") : "+r"(selector) :: "memory");
}
void mini_ds(unsigned short selector) {
    asm volatile(SEGMENT("ds") : "+r"(selector) :: "memory");
}
void mini_ss(unsigned short selector) {
    asm volatile(SEGMENT("ss") : "+r"(selector) :: "memory");
}
void mini_lkgs(unsigned short selector) {
    asm volatile(LKGS : "+D"(selector));
}
SRC
# LKGS Source text's named operand is only used inside the .macro body.
# The kernel's Core normalized `%k[sel]` form is equivalent to `%k0` here.
sed 's/mini_/gnu_/g' "$d/mini.c" > "$d/gnu.c"
gcc -E -P -x c "$d/mini.c" -o "$d/mini.i"
"$mini" --target=x86_64-linux -S "$d/mini.i" -o "$d/mini.s"
gcc -c -x assembler "$d/mini.s" -o "$d/mini.o"
gcc -c -O0 "$d/gnu.c" -o "$d/gnu.o"
objdump -dr "$d/mini.o" > "$d/mini.dump"
objdump -s -j __ex_table "$d/mini.o" > "$d/mini.extable"
objdump -s -j __ex_table "$d/gnu.o" > "$d/gnu.extable"
for seg in es ds ss; do
    grep -q "mov.*%$seg" "$d/mini.dump"
done
grep -q 'lkgs' "$d/mini.dump"
# Three fixed R10D +r records (0xa11) and one fixed EDI LKGS record (0x711).
test "$(grep -o '110a0000' "$d/mini.extable" | wc -l)" -eq 3
grep -q '11070000' "$d/mini.extable"
test "$(grep -o '11070000' "$d/gnu.extable" | wc -l)" -eq 1

sed 's/+r"(selector)/+m"(selector)/g' "$d/mini.i" > "$d/bad-constraint.i"
sed 's/\.long (1b) - \./.quad (1b) - ./g' \
    "$d/mini.i" > "$d/bad-extable.i"
sed 's/0xf2,0x0f,0x00,0xf7/0xf3,0x0f,0x00,0xf7/' \
    "$d/mini.i" > "$d/bad-lkgs.i"
sed 's/+D"(selector)/+r"(selector)/' \
    "$d/mini.i" > "$d/bad-di.i"
for case in constraint extable lkgs di; do
    if "$mini" --target=x86_64-linux -S "$d/bad-$case.i" \
        -o "$d/bad-$case.s" >/dev/null 2>&1; then
        echo "X86_SEGMENT_EXTABLE_FAIL=accepted_bad_$case" >&2
        exit 1
    fi
    test ! -e "$d/bad-$case.s"
done
echo 'MINIC_X86_SEGMENT_EXTABLE=PASS GNU_AS=4 type17=4 negative=4'
;;
probe-x86-linux-vdso-cpunode-alt)
# Linux vDSO LSL -> RDPID alternative is a privileged-descriptor-sensitive
# probe. Verify *both* GNU object patchtable sections, exact feature number,
# replacement bytes and relocations; never execute an invalid LSL selector.
cat > "$d/mini.i" <<'SRC'
unsigned int mini_cpunode(void) {
    unsigned int p;
    asm volatile(
        "# ALT: oldnstr\n"
        "661:\n\tlsl %[seg],%[p]\n"
        "662:\n# ALT: padding\n"
        ".skip -(((6651f-6641f)-(662b-661b)) > 0) * "
        "((6651f-6641f)-(662b-661b)),0x90\n"
        "663:\n"
        ".pushsection .altinstructions,\"a\"\n"
        " .long 661b - .\n .long 6641f - .\n"
        " .4byte (16*32+22)\n"
        " .byte 663b-661b\n .byte 6651f-6641f\n"
        ".popsection\n"
        ".pushsection .altinstr_replacement, \"ax\"\n"
        "# ALT: replacement 1\n"
        "6641:\n\t.byte 0xf3,0x0f,0xc7,0xf8\n"
        "6651:\n.popsection\n"
        : [p] "=a"(p) : "i"(0), [seg] "r"(15*8+3));
    return p;
}
SRC
sed 's/mini_/gnu_/g' "$d/mini.i" > "$d/gnu.c"
"$mini" --target=x86_64-linux -S "$d/mini.i" -o "$d/mini.s"
gcc -c -x assembler "$d/mini.s" -o "$d/mini.o"
gcc -O0 -c "$d/gnu.c" -o "$d/gnu.o"
for obj in "$d/mini.o" "$d/gnu.o"; do
    readelf -SW "$obj" | grep -Eq '\.altinstructions[[:space:]]+PROGBITS.*00000e'
    readelf -SW "$obj" | grep -Eq '\.altinstr_replacement[[:space:]]+PROGBITS.*000004'
    readelf -r "$obj" | grep -q '.rela.altinstructions'
    objdump -dr "$obj" | grep -Eq '[[:space:]]lsl[[:space:]]'
    readelf -x .altinstr_replacement "$obj" | grep -qi 'f30fc7f8'
done
sed 's/16\*32+22/16*32+23/' "$d/mini.i" > "$d/negative.i"
if "$mini" --target=x86_64-linux -S "$d/negative.i" -o "$d/negative.s" \
    >/dev/null 2>&1; then
    echo 'X86_VDSO_ALT_FAIL=changed CPUID feature admitted' >&2
    exit 1
fi
test ! -e "$d/negative.s"
echo 'MINIC_X86_VDSO_ALT_GNU_AS=PASS LSL=1 RDPID=1 alt_record=14 replacement=4 relocs=2 negative=1'
;;
probe-x86-linux-vdso-syscall-native)
# Original x86 vDSO fallback syscall forms: 5-operand clock and 4-operand
# gettimeofday. Real Linux syscalls exercise register ABI and =m writes.
cat > "$d/mini.i" <<'SRC'
struct KernelTs { long sec; long nsec; };
struct OldTv { long sec; long usec; };
struct Timezone { int minutes; int dst; };
long mini_gettime(int id, struct KernelTs *ts) {
    long ret;
    asm("syscall" : "=a"(ret), "=m"(*ts)
        : "0"(228), "D"(id), "S"(ts) : "rcx", "r11");
    return ret;
}
long mini_getres(int id, struct KernelTs *ts) {
    long ret;
    asm("syscall" : "=a"(ret), "=m"(*ts)
        : "0"(229), "D"(id), "S"(ts) : "rcx", "r11");
    return ret;
}
long mini_gettimeofday(struct OldTv *tv,struct Timezone *tz) {
    long ret;
    asm("syscall" : "=a"(ret)
        : "0"(96), "D"(tv), "S"(tz) : "memory");
    return ret;
}
SRC
sed 's/mini_/gnu_/g' "$d/mini.i" > "$d/gnu.c"
"$mini" --target=x86_64-linux -S "$d/mini.i" -o "$d/mini.s"
gcc -c -x assembler "$d/mini.s" -o "$d/mini.o"
gcc -O0 -c "$d/gnu.c" -o "$d/gnu.o"
cat > "$d/main.c" <<'SRC'
struct KernelTs { long sec; long nsec; };
struct OldTv { long sec; long usec; };
struct Timezone { int minutes; int dst; };
extern long mini_gettime(int,struct KernelTs*),gnu_gettime(int,struct KernelTs*);
extern long mini_getres(int,struct KernelTs*),gnu_getres(int,struct KernelTs*);
extern long mini_gettimeofday(struct OldTv*,struct Timezone*);
extern long gnu_gettimeofday(struct OldTv*,struct Timezone*);
int main(void) {
    for (int n=0;n<16;n++) {
        struct KernelTs a={0},b={0},c={0},d={0};
        struct OldTv tv={0},gv={0};
        struct Timezone tz={0},gtz={0};
        int id=n&1;
        long ra=mini_gettime(id,&a),rb=gnu_gettime(id,&b);
        if(ra!=rb||ra!=0||a.sec-b.sec>1||b.sec-a.sec>1||
           a.nsec<0||a.nsec>=1000000000L||
           b.nsec<0||b.nsec>=1000000000L) return 1;
        ra=mini_getres(id,&c);rb=gnu_getres(id,&d);
        if(ra!=rb||ra!=0||c.sec!=d.sec||c.nsec!=d.nsec) return 2;
        ra=mini_gettimeofday(&tv,&tz);rb=gnu_gettimeofday(&gv,&gtz);
        if(ra!=rb||ra!=0||tv.sec-gv.sec>1||gv.sec-tv.sec>1||
           tv.usec<0||tv.usec>=1000000L||
           gv.usec<0||gv.usec>=1000000L||
           tz.minutes!=gtz.minutes||tz.dst!=gtz.dst) return 3;
    }
    return 0;
}
SRC
gcc -O0 -no-pie "$d/main.c" "$d/mini.o" "$d/gnu.o" -o "$d/run"
"$d/run"
sed 's/asm("syscall"/asm("syscall; cli"/' "$d/mini.i" > "$d/negative.i"
if "$mini" --target=x86_64-linux -S "$d/negative.i" -o "$d/negative.s" \
    >/dev/null 2>&1; then
    echo 'X86_VDSO_SYSCALL_FAIL=unadmitted opcode suffix' >&2
    exit 1
fi
test ! -e "$d/negative.s"
echo 'MINIC_X86_VDSO_SYSCALL_NATIVE=PASS gettime=16 getres=16 gettimeofday=16 negative=1'
;;
esac
