#!/usr/bin/env bash
# Original Linux 6.6.143 privileged MSR exception path: DO NOT EXECUTE IN USER MODE.
set -Eeuo pipefail
cc="$1";test -x "$cc"
d=$(mktemp -d); trap 'rm -rf "$d"' EXIT
cat > "$d/safe-msr.i" <<'SRC'
typedef unsigned int u32;
unsigned long long native_read_msr_safe(unsigned int msr,int *err){
 unsigned long low,high;
 asm volatile(
   "1: rdmsr ; xor %[err],%[err]\n2:\n\t .pushsection \"__ex_table\",\"a\"\n .balign 4\n .long (1b) - .\n .long (2b) - .\n.macro extable_type_reg type:req reg:req\n.set .Lfound, 0\n.set .Lregnr, 0\n.irp rs,rax,rcx,rdx,rbx,rsp,rbp,rsi,rdi,r8,r9,r10,r11,r12,r13,r14,r15\n.ifc \\reg, %%\\rs\n.set .Lfound, .Lfound+1\n.long \\type + (.Lregnr << 8)\n.endif\n.set .Lregnr, .Lregnr+1\n.endr\n.set .Lregnr, 0\n.irp rs,eax,ecx,edx,ebx,esp,ebp,esi,edi,r8d,r9d,r10d,r11d,r12d,r13d,r14d,r15d\n.ifc \\reg, %%\\rs\n.set .Lfound, .Lfound+1\n.long \\type + (.Lregnr << 8)\n.endif\n.set .Lregnr, .Lregnr+1\n.endr\n.if (.Lfound != 1)\n.error \"extable_type_reg: bad register argument\"\n.endif\n.endm\nextable_type_reg reg=%[err], type=11 \n.purgem extable_type_reg\n .popsection\n"
   : [err] "=r" (*err), "=a" (low), "=d" (high) : "c" (msr));
 return low | high << 32;
}
int native_write_msr_safe(unsigned int msr,u32 low,u32 high){
 int err;
 asm volatile(
   "1: wrmsr ; xor %[err],%[err]\n2:\n\t .pushsection \"__ex_table\",\"a\"\n .balign 4\n .long (1b) - .\n .long (2b) - .\n.macro extable_type_reg type:req reg:req\n.set .Lfound, 0\n.set .Lregnr, 0\n.irp rs,rax,rcx,rdx,rbx,rsp,rbp,rsi,rdi,r8,r9,r10,r11,r12,r13,r14,r15\n.ifc \\reg, %%\\rs\n.set .Lfound, .Lfound+1\n.long \\type + (.Lregnr << 8)\n.endif\n.set .Lregnr, .Lregnr+1\n.endr\n.set .Lregnr, 0\n.irp rs,eax,ecx,edx,ebx,esp,ebp,esi,edi,r8d,r9d,r10d,r11d,r12d,r13d,r14d,r15d\n.ifc \\reg, %%\\rs\n.set .Lfound, .Lfound+1\n.long \\type + (.Lregnr << 8)\n.endif\n.set .Lregnr, .Lregnr+1\n.endr\n.if (.Lfound != 1)\n.error \"extable_type_reg: bad register argument\"\n.endif\n.endm\nextable_type_reg reg=%[err], type=10 \n.purgem extable_type_reg\n .popsection\n"
   : [err] "=a" (err) : "c" (msr), "0" (low), "d" (high) : "memory");
 return err;
}
SRC
"$cc" --target=x86_64-linux -S "$d/safe-msr.i" -o "$d/safe-msr.s"
test -s "$d/safe-msr.s"
gcc -c -x assembler "$d/safe-msr.s" -o "$d/safe-msr.o"
gcc -c -x c "$d/safe-msr.i" -o "$d/gnu.o"
readelf -WS "$d/safe-msr.o" | grep -Fq '__ex_table'
readelf -WS "$d/gnu.o" | grep -Fq '__ex_table'
readelf -r "$d/safe-msr.o" | grep -Fq 'R_X86_64_PC32'
readelf -x __ex_table "$d/safe-msr.o" | grep -Fq '0b0b0000'
readelf -x __ex_table "$d/safe-msr.o" | grep -Fq '0a000000'
sed 's/type=11/type=12/' "$d/safe-msr.i" > "$d/unsafe.i"
if "$cc" --target=x86_64-linux -S "$d/unsafe.i" -o "$d/unsafe.s" >/dev/null 2>&1;then
 echo 'unsafe MSR exception type accepted' >&2;exit 1
fi
test ! -e "$d/unsafe.s"
cat > "$d/gs.i" <<'SRC'
typedef struct {int value;} Record;
static Record sample={41};
static Record * __irq_regs=&sample;
static unsigned long read_slot(void){
 unsigned long val;
 asm("movq %%gs:%[var], %[val]" : [val] "=r" (val) : [var] "m" (__irq_regs));
 return val;
}
int main(void){return ((Record*)read_slot())->value+1;}
SRC
"$cc" --target=x86_64-linux -S "$d/gs.i" -o "$d/gs.s"
gcc -static -x assembler "$d/gs.s" -o "$d/gs-mini"
gcc -static -x c "$d/gs.i" -o "$d/gs-gnu"
for compiler in mini gnu;do
 code=0;"$d/gs-$compiler" || code=$?
 test "$code" -eq 42 || { echo "$compiler GS pointer returned $code" >&2;exit 1; }
done
echo 'X86_ORIGINAL_MSR_SAFE=PASS GNU_RELA=4 exact_types=11/10 unsafe=rejected'
echo 'X86_PERCPU_POINTER_GS=PASS GNU_native_diff=42'
