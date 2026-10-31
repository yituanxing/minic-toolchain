#!/usr/bin/env bash
# Original Linux 6.6.143 arch/x86/include/asm/cmpxchg.h:
# cmpxchgb/w/l/q GNU =a,+m:q/r,"0" (NOT GNU CC_SET z try_cmpxchg).
set -Eeuo pipefail
mini=${1:?MiniC binary}
test -x "$mini"
d=$(mktemp -d);trap 'rm -rf "$d"' EXIT
cat > "$d/raw.i" <<'SRC'
unsigned char mini_cas8(unsigned char *p, unsigned char old, unsigned char new) {
  unsigned char ret;
  asm volatile(".pushsection .smp_locks,\"a\"\n"
      ".balign 4\n.long 671f - .\n.popsection\n671:\n\tlock; cmpxchgb %2,%1"
      : "=a"(ret), "+m"(*p) : "q"(new), "0"(old) : "memory");
  return ret;
}
unsigned short mini_cas16(unsigned short *p, unsigned short old, unsigned short new) {
  unsigned short ret;
  asm volatile(".pushsection .smp_locks,\"a\"\n"
      ".balign 4\n.long 671f - .\n.popsection\n671:\n\tlock; cmpxchgw %2,%1"
      : "=a"(ret), "+m"(*p) : "r"(new), "0"(old) : "memory");
  return ret;
}
unsigned mini_cas32(unsigned *p, unsigned old, unsigned new) {
  unsigned ret;
  asm volatile(".pushsection .smp_locks,\"a\"\n"
      ".balign 4\n.long 671f - .\n.popsection\n671:\n\tlock; cmpxchgl %2,%1"
      : "=a"(ret), "+m"(*p) : "r"(new), "0"(old) : "memory");
  return ret;
}
unsigned long mini_cas64(unsigned long *p, unsigned long old, unsigned long new) {
  unsigned long ret;
  asm volatile(".pushsection .smp_locks,\"a\"\n"
      ".balign 4\n.long 671f - .\n.popsection\n671:\n\tlock; cmpxchgq %2,%1"
      : "=a"(ret), "+m"(*p) : "r"(new), "0"(old) : "memory");
  return ret;
}
void *mini_casptr(void **p, void *old, void *new) {
  void *ret;
  asm volatile(".pushsection .smp_locks,\"a\"\n"
      ".balign 4\n.long 671f - .\n.popsection\n671:\n\tlock; cmpxchgq %2,%1"
      : "=a"(ret), "+m"(*p) : "r"(new), "0"(old) : "memory");
  return ret;
}
SRC
"$mini" --target=x86_64-linux -S "$d/raw.i" -o "$d/mini.s"
for opcode in cmpxchgb cmpxchgw cmpxchgl cmpxchgq;do
  grep -Fq "lock; $opcode " "$d/mini.s"
done
sed 's/mini_/gnu_/g' "$d/raw.i" > "$d/gnu.c"
gcc -c -O0 "$d/gnu.c" -o "$d/gnu.o"
gcc -c -x assembler "$d/mini.s" -o "$d/mini.o"
cat > "$d/main.c" <<'SRC'
extern unsigned char mini_cas8(unsigned char*,unsigned char,unsigned char);
extern unsigned char gnu_cas8(unsigned char*,unsigned char,unsigned char);
extern unsigned short mini_cas16(unsigned short*,unsigned short,unsigned short);
extern unsigned short gnu_cas16(unsigned short*,unsigned short,unsigned short);
extern unsigned mini_cas32(unsigned*,unsigned,unsigned);
extern unsigned gnu_cas32(unsigned*,unsigned,unsigned);
extern unsigned long mini_cas64(unsigned long*,unsigned long,unsigned long);
extern unsigned long gnu_cas64(unsigned long*,unsigned long,unsigned long);
extern void *mini_casptr(void **,void *,void *);
extern void *gnu_casptr(void **,void *,void *);
int main(void) {
  {
    int a=1,b=2,c=3;
    void *left=&a,*right=&a;
    void *ma=mini_casptr(&left,&a,&b),*ga=gnu_casptr(&right,&a,&b);
    if(ma!=ga||left!=right||left!=&b)return 2;
    ma=mini_casptr(&left,&c,&a);ga=gnu_casptr(&right,&c,&a);
    if(ma!=ga||left!=right||left!=&b)return 3;
  }
  for(unsigned i=0;i<90;++i)for(unsigned old=0;old<100;++old) {
    unsigned char a8=(unsigned char)i,b8=a8;
    unsigned short a16=(unsigned short)i,b16=a16;
    unsigned a32=i,b32=i;
    unsigned long a64=i,b64=i;
    unsigned char v8=mini_cas8(&a8,old,55),w8=gnu_cas8(&b8,old,55);
    unsigned short v16=mini_cas16(&a16,old,999),w16=gnu_cas16(&b16,old,999);
    unsigned v32=mini_cas32(&a32,old,99999),w32=gnu_cas32(&b32,old,99999);
    unsigned long v64=mini_cas64(&a64,old,123456789UL),w64=gnu_cas64(&b64,old,123456789UL);
    if(a8!=b8||v8!=w8||a16!=b16||v16!=w16||
       a32!=b32||v32!=w32||a64!=b64||v64!=w64)return 1;
  }
  return 0;
}
SRC
gcc -O0 -no-pie "$d/main.c" "$d/mini.o" "$d/gnu.o" -o "$d/run"
"$d/run"
for bad in unexpected-instruction wrong-constraint wrong-width; do
  case "$bad" in
    unexpected-instruction) sed 's/cmpxchgb %2,%1/cmpxchgb %2,%1; nop/' "$d/raw.i" > "$d/bad.i" ;;
    wrong-constraint) sed 's/: "=a"(ret)/: "=r"(ret)/' "$d/raw.i" > "$d/bad.i" ;;
    wrong-width) sed 's/cmpxchgb %2,%1/cmpxchgw %2,%1/' "$d/raw.i" > "$d/bad.i" ;;
  esac
  if "$mini" --target=x86_64-linux -S "$d/bad.i" -o "$d/bad.s" >/dev/null 2>&1;then
    echo "X86_RAW_CMPXCHG_FAIL=$bad" >&2;exit 1
  fi
  test ! -e "$d/bad.s"
done
echo 'MINIC_X86_LINUX_RAW_CMPXCHG=PASS integer_widths=4 pointer_width=8 SMP_LOCK=1 GNU_native_pairs=36000 negative=3'
