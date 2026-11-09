#!/usr/bin/env bash
# Linux v6.6 asm/{cmpxchg,percpu,barrier}.h macro-level feature matrix:
# four original GNU widths, four XCHG memory orders, three per-CPU operators.
# The new LSE instruction bodies are GNU .rept N/NOP/.endr, never fake NOPs.
set -Eeuo pipefail
minic=${1:?MiniC compiler}
test -x "$minic"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat > "$work/atomic-alternatives.c" <<'C'
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;
#define NOPS(n) ".rept\t" #n "\nnop\n.endr\n"
#define ALT(old,new) \
  ".if 1 == 1\n661:\n\t" old "\n" \
  "662:\n.pushsection .altinstructions,\"a\"\n" \
  " .word 661b - .\n .word 663f - .\n .hword 33\n" \
  " .byte 662b-661b\n .byte 664f-663f\n" \
  ".popsection\n.subsection 1\n663:\n\t" \
  ".arch_extension lse\n" new "\n" \
  "664:\n\t.org\t. - (664b-663b) + (662b-661b)\n\t" \
  ".org\t. - (662b-661b) + (664b-663b)\n\t" \
  ".previous\n.endif\n"
#define XCHG(w,sfx,name,sz,mb,nop_lse,acq,acq_lse,rel,cl) \
u##sz xchg_##name##sz(u##sz x, volatile void *ptr) { \
  u##sz ret; unsigned long tmp; \
  asm volatile(ALT( \
    "\tprfm\tpstl1strm, %2\n" \
    "1:\tld" #acq "xr" #sfx "\t%" #w "0, %2\n" \
    "\tst" #rel "xr" #sfx "\t%w1, %" #w "3, %2\n" \
    "\tcbnz\t%w1, 1b\n" "\t" #mb, \
    "\tswp" #acq_lse #rel #sfx "\t%" #w "3, %" #w "0, %2\n" \
    NOPS(3) "\t" #nop_lse) \
    : "=&r"(ret), "=&r"(tmp), "+Q"(*(u##sz *)ptr) : "r"(x) : cl); \
  return ret; \
}
XCHG(w,b,normal,8,,,,,,)
XCHG(w,h,normal,16,,,,,,)
XCHG(w,,normal,32,,,,,,)
XCHG(,,normal,64,,,,,,)
XCHG(w,b,acq,8,,,a,a,,"memory")
XCHG(w,h,acq,16,,,a,a,,"memory")
XCHG(w,,acq,32,,,a,a,,"memory")
XCHG(,,acq,64,,,a,a,,"memory")
XCHG(w,b,rel,8,,,,,l,"memory")
XCHG(w,h,rel,16,,,,,l,"memory")
XCHG(w,,rel,32,,,,,l,"memory")
XCHG(,,rel,64,,,,,l,"memory")
XCHG(w,b,mb,8,dmb ish,nop,,a,l,"memory")
XCHG(w,h,mb,16,dmb ish,nop,,a,l,"memory")
XCHG(w,,mb,32,dmb ish,nop,,a,l,"memory")
XCHG(,,mb,64,dmb ish,nop,,a,l,"memory")
#define PERCPU(w,sfx,name,sz,alu,lse) \
void percpu_##name##sz(void *ptr, unsigned long val) { \
  unsigned int loop; u##sz tmp; \
  asm volatile(ALT( \
    "1:\tldxr" #sfx "\t%" #w "[tmp], %[ptr]\n" \
    #alu "\t%" #w "[tmp], %" #w "[tmp], %" #w "[val]\n" \
    "\tstxr" #sfx "\t%w[loop], %" #w "[tmp], %[ptr]\n" \
    "\tcbnz\t%w[loop], 1b", \
    #lse "\t%" #w "[val], %[ptr]\n" NOPS(3)) \
    : [loop] "=&r"(loop), [tmp] "=&r"(tmp), \
      [ptr] "+Q"(*(u##sz *)ptr) : [val] "r"((u##sz)val)); \
}
#define PERCPU_ALL(name,alu,lse) \
PERCPU(w,b,name,8,alu,lse) \
PERCPU(w,h,name,16,alu,lse) \
PERCPU(w,,name,32,alu,lse) \
PERCPU(,,name,64,alu,lse)
PERCPU_ALL(add,add,stadd)
PERCPU_ALL(andnot,bic,stclr)
PERCPU_ALL(or,orr,stset)
#define PERCPU_RETURN(w,sfx,sz) \
u##sz percpu_add_return_##sz(void *ptr, unsigned long val) { \
  unsigned int loop; u##sz ret; \
  asm volatile(ALT( \
    "1:\tldxr" #sfx "\t%" #w "[ret], %[ptr]\n" \
    "add\t%" #w "[ret], %" #w "[ret], %" #w "[val]\n" \
    "\tstxr" #sfx "\t%w[loop], %" #w "[ret], %[ptr]\n" \
    "\tcbnz\t%w[loop], 1b", \
    "ldadd\t%" #w "[val], %" #w "[ret], %[ptr]\n" \
    "add\t%" #w "[ret], %" #w "[ret], %" #w "[val]\n" \
    NOPS(2)) \
    : [loop] "=&r"(loop), [ret] "=&r"(ret), \
      [ptr] "+Q"(*(u##sz *)ptr) : [val] "r"((u##sz)val)); \
  return ret; \
}
PERCPU_RETURN(w,b,8)
PERCPU_RETURN(w,h,16)
PERCPU_RETURN(w,,32)
PERCPU_RETURN(,,64)
C
cc -E -P -x c "$work/atomic-alternatives.c" -o "$work/atomic-alternatives.i"
"$minic" --target=aarch64-linux -S "$work/atomic-alternatives.i" \
  -o "$work/atomic-alternatives.s"
test "$(grep -Fc '.pushsection .altinstructions' "$work/atomic-alternatives.s")" -eq 32
test "$(grep -Fc '.hword 33' "$work/atomic-alternatives.s")" -eq 32
grep -Fq 'swpal x11, x9, [x10]' "$work/atomic-alternatives.s"
grep -Fq 'stadd x11, [x10]' "$work/atomic-alternatives.s"
grep -Fq 'ldadd w11, w9, [x10]' "$work/atomic-alternatives.s"
echo 'AARCH64_LINUX_ALT_MATRIX=PASS xchg16 percpu_add_andnot_or12 add_return4 total32'
sed 's/\.hword 33/\.hword 34/g' "$work/atomic-alternatives.i" > "$work/invalid-cap.i"
if "$minic" --target=aarch64-linux -S "$work/invalid-cap.i" \
    -o "$work/invalid-cap.s" >/dev/null 2>&1; then
  echo 'AARCH64_LINUX_ALT_MATRIX=FAIL invalid capability admitted' >&2
  exit 1
fi
echo 'AARCH64_LINUX_ALT_MATRIX_NEGATIVE=PASS wrong_capability=rejected'
