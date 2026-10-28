#!/usr/bin/env bash
set -Eeuo pipefail
cc="$1"
test -x "$cc"
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
cat > "$d/carry.i" <<'SRC'
typedef unsigned int u32;
typedef unsigned short u16;
static inline u16 csum(const void *iph,unsigned int ihl){
 unsigned int sum;
 asm("  movl (%1), %0\n" "  subl $4, %2\n" "  jbe 2f\n"
     "  addl 4(%1), %0\n" "  adcl 8(%1), %0\n" "  adcl 12(%1), %0\n"
     "1: adcl 16(%1), %0\n" "  lea 4(%1), %1\n" "  decl %2\n"
     "  jne\t1b\n" "  adcl $0, %0\n" "  movl %0, %2\n"
     "  shrl $16, %0\n" "  addw %w2, %w0\n" "  adcl $0, %0\n"
     "  notl %0\n" "2:"
     : "=r"(sum), "=r"(iph), "=r"(ihl) : "1"(iph), "2"(ihl) : "memory");
 return (u16)sum;
}
int main(void) {u32 h[6]={0x45,0x10002,0x30004,0x50006,0x70008,0};return csum(h,5)==csum(h,5)?42:1;}
SRC
cat > "$d/smp.i" <<'SRC'
unsigned char b=1; unsigned short w=2; unsigned int l=3; unsigned long q=4;
unsigned char a=7;
void f0(void) {asm volatile(".pushsection .smp_locks,\"a\"\n.balign 4\n.long 671f - .\n.popsection\n671:\n\tlock; xaddb %b0, %1\n" : "+q"(a),"+m"(b)::"memory","cc");}
unsigned short c=8;
void f1(void) {asm volatile(".pushsection .smp_locks,\"a\"\n.balign 4\n.long 671f - .\n.popsection\n671:\n\tlock; xaddw %w0, %1\n" : "+r"(c),"+m"(w)::"memory","cc");}
unsigned int e=9;
void f2(void) {asm volatile(".pushsection .smp_locks,\"a\"\n.balign 4\n.long 671f - .\n.popsection\n671:\n\tlock; xaddl %0, %1\n" : "+r"(e),"+m"(l)::"memory","cc");}
unsigned long f=10;
void f3(void) {asm volatile(".pushsection .smp_locks,\"a\"\n.balign 4\n.long 671f - .\n.popsection\n671:\n\tlock; xaddq %q0, %1\n" : "+r"(f),"+m"(q)::"memory","cc");}
int main(void) {
 f0();if(a!=1||b!=8)return 1;
 f1();if(c!=2||w!=10)return 2;
 f2();if(e!=3||l!=12)return 3;
 f3();if(f!=4||q!=14)return 4;
 return 42;
}
SRC
cat > "$d/records.i" <<'SRC'
typedef struct {unsigned int value;} Small;
typedef struct {unsigned long value;} Medium;
typedef struct {void* ptr;unsigned int value;} Pair;
static Small a(unsigned int x){return (Small){x};}
static Medium b(unsigned long x){return (Medium){x};}
static Pair c(void*p,unsigned int x){return (Pair){p,x};}
int main(void){
 Small s=a(19);Medium m=b(20);Pair p=c((void*)1,2);
 return s.value==19&&m.value==20&&p.ptr==(void*)1&&p.value==2?42:1;
}
SRC
for kind in carry smp records;do
 "$cc" --target=x86_64-linux -S "$d/$kind.i" -o "$d/$kind.s"
 test -s "$d/$kind.s"
 gcc -static -x assembler "$d/$kind.s" -o "$d/$kind-minic"
 gcc -static -x c "$d/$kind.i" -o "$d/$kind-gnu"
 for compiler in minic gnu;do
  code=0;"$d/$kind-$compiler" || code=$?
  test "$code" -eq 42 || { echo "$kind/$compiler returned $code" >&2;exit 1; }
 done
 echo "ORIGINAL_X86_GNU_DIFF=$kind PASS"
done
readelf -SW "$d/smp-minic" | grep -Fq '.smp_locks'
sed 's/lock; xaddb/lock; xorb/' "$d/smp.i" > "$d/unsafe.i"
if "$cc" --target=x86_64-linux -S "$d/unsafe.i" -o "$d/unsafe.s" >/dev/null 2>&1;then
 echo 'unqualified lock xadd accepted' >&2;exit 1
fi
test ! -e "$d/unsafe.s"
echo 'ORIGINAL_X86_LINUX_CARRY_XADD_RECORD=PASS GNU_oracle=3/3 hardware_XADD=4/4 no_unsafe_fallback=1'
