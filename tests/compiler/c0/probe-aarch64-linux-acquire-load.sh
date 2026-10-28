#!/usr/bin/env bash
# Original Linux AArch64 __smp_load_acquire genuine GNU =r/Q four widths.
set -Eeuo pipefail
minic=$1;test -x "$minic"
w=$(mktemp -d);trap 'rm -rf "$w"' EXIT
cat >"$w/load.i" <<'SRC'
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;
u8 mini_read8(const volatile u8 *p){
    u8 v;asm volatile("ldarb %w0, %1" : "=r"(v) : "Q"(*p) : "memory");return v;
}
u16 mini_read16(const volatile u16 *p){
    u16 v;asm volatile("ldarh %w0, %1" : "=r"(v) : "Q"(*p) : "memory");return v;
}
u32 mini_read32(const volatile u32 *p){
    u32 v;asm volatile("ldar %w0, %1" : "=r"(v) : "Q"(*p) : "memory");return v;
}
u64 mini_read64(const volatile u64 *p){
    u64 v;asm volatile("ldar %0, %1" : "=r"(v) : "Q"(*p) : "memory");return v;
}
SRC
sed 's/mini_/gnu_/g' "$w/load.i" >"$w/gnu.c"
aarch64-linux-gnu-gcc -O0 -c "$w/gnu.c" -o "$w/gnu.o"
"$minic" --target=aarch64-linux -S "$w/load.i" -o "$w/mini.s"
for ins in 'ldarb w9, [x10]' 'ldarh w9, [x10]' \
 'ldar w9, [x10]' 'ldar x9, [x10]' \
 'strb w9, [x11]' 'strh w9, [x11]' 'str w9, [x11]' 'str x9, [x11]';do
 grep -Fq "$ins" "$w/mini.s"
done
aarch64-linux-gnu-gcc -c -x assembler "$w/mini.s" -o "$w/mini.o"
cat >"$w/main.c" <<'SRC'
#include <stdio.h>
typedef unsigned char u8;typedef unsigned short u16;
typedef unsigned int u32;typedef unsigned long long u64;
extern u8 mini_read8(const volatile u8*),gnu_read8(const volatile u8*);
extern u16 mini_read16(const volatile u16*),gnu_read16(const volatile u16*);
extern u32 mini_read32(const volatile u32*),gnu_read32(const volatile u32*);
extern u64 mini_read64(const volatile u64*),gnu_read64(const volatile u64*);
int main(void){
  volatile u8 b=0xa5;volatile u16 h=0xabcd;
  volatile u32 w=0x87654321U;volatile u64 d=0x1122334455667788ULL;
  if(mini_read8(&b)!=gnu_read8(&b)||mini_read16(&h)!=gnu_read16(&h)||
     mini_read32(&w)!=gnu_read32(&w)||mini_read64(&d)!=gnu_read64(&d))return 1;
  if(mini_read8(&b)!=0xa5||mini_read16(&h)!=0xabcd||
     mini_read32(&w)!=0x87654321U||mini_read64(&d)!=0x1122334455667788ULL)return 2;
  puts("MINIC_AARCH64_ACQUIRE_LOAD_GNU_QEMU=PASS widths=4");
  return 42;
}
SRC
aarch64-linux-gnu-gcc -static -O0 "$w/main.c" "$w/gnu.o" "$w/mini.o" -o "$w/run.elf"
rc=0;qemu-aarch64 "$w/run.elf" || rc=$?
test "$rc" -eq 42
sed 's/ldarb %w0, %1/ldarb %w0, %1; brk #1/' "$w/load.i" >"$w/extra.i"
sed 's/"Q"(\*p)/"r"(*p)/' "$w/load.i" >"$w/constraint.i"
sed 's/u8 v;asm volatile("ldarb/u16 v;asm volatile("ldarb/' "$w/load.i" >"$w/width.i"
sed 's/ : "memory");return v;/ );return v;/' "$w/load.i" >"$w/nomemory.i"
for bad in extra constraint width nomemory;do
 if "$minic" --target=aarch64-linux -S "$w/$bad.i" -o "$w/$bad.s" >/dev/null 2>&1;then
  echo "unsafe AArch load $bad accepted" >&2;exit 1
 fi
 test ! -e "$w/$bad.s"
done
echo 'MINIC_AARCH64_ACQUIRE_LOAD=PASS widths=4 GNU_QEMU=1 negative=4'
