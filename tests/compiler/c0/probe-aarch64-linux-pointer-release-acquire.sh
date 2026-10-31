#!/usr/bin/env bash
# Real Linux RCU pointer publish/read via AAPCS64 typed GNU Q memory,
# strict hardware acquire/release ordering and pointer ABI (64-bit).
set -Eeuo pipefail
minic=$1;test -x "$minic"
d=$(mktemp -d);trap 'rm -rf "$d"' EXIT
cat >"$d/pointer.i" <<'SRC'
typedef unsigned long long u64;
typedef struct Node { long value; } Node;
void mini_publish(Node * volatile *slot, Node *value) {
    u64 bits=(u64)(unsigned long)value;
    asm volatile("stlr %x1, %0" : "=Q"(*slot) : "rZ"(bits) : "memory");
}
Node *mini_consume(Node * volatile *slot) {
    u64 bits=0;
    asm volatile("ldar %0, %1" : "=r"(bits) : "Q"(*slot) : "memory");
    return (Node *)(unsigned long)bits;
}
SRC
sed 's/mini_/gnu_/g' "$d/pointer.i" >"$d/gnu.c"
"$minic" --target=aarch64-linux -S "$d/pointer.i" -o "$d/mini.s"
aarch64-linux-gnu-gcc -O0 -c -x c "$d/gnu.c" -o "$d/gnu.o"
aarch64-linux-gnu-gcc -c -x assembler "$d/mini.s" -o "$d/mini.o"
grep -Fq 'stlr x9, [x10]' "$d/mini.s"
grep -Fq 'ldar x9, [x10]' "$d/mini.s"
cat >"$d/driver.c" <<'SRC'
#include <stdio.h>
typedef struct Node{long value;} Node;
extern void mini_publish(Node * volatile *,Node *);
extern void gnu_publish(Node * volatile *,Node *);
extern Node *mini_consume(Node * volatile *);
extern Node *gnu_consume(Node * volatile *);
int main(void){
    Node a={42},b={91};Node *slot=(Node *)0;
    mini_publish(&slot,&a);
    if(slot!=&a||mini_consume(&slot)!=&a||gnu_consume(&slot)!=&a)return 1;
    gnu_publish(&slot,&b);
    if(slot!=&b||mini_consume(&slot)!=&b||gnu_consume(&slot)!=&b)return 2;
    puts("MINIC_AARCH64_POINTER_RELEASE_ACQUIRE_GNU_QEMU=PASS");
    return 42;
}
SRC
aarch64-linux-gnu-gcc -static -O0 "$d/driver.c" "$d/mini.o" "$d/gnu.o" -o "$d/run.elf"
result=0;qemu-aarch64 "$d/run.elf" || result=$?
test "$result" -eq 42
sed 's/stlr %x1, %0/stlr %w1, %0/' "$d/pointer.i" >"$d/bad-width.i"
sed 's/"=Q"(\*slot)/"=m"(*slot)/' "$d/pointer.i" >"$d/bad-output.i"
sed 's/stlr %x1, %0/stlr %x1, %0; brk #1/' "$d/pointer.i" >"$d/bad-injection.i"
sed 's/"Q"(\*slot)/"m"(*slot)/' "$d/pointer.i" >"$d/bad-load.i"
for bad in bad-width bad-output bad-injection bad-load;do
 if "$minic" --target=aarch64-linux -S "$d/$bad.i" -o "$d/$bad.s" >/dev/null 2>&1;then
  echo "FAIL unqualified AArch pointer ISA $bad" >&2;exit 1
 fi
 test ! -e "$d/$bad.s"
done
echo 'MINIC_AARCH64_POINTER_RELEASE_ACQUIRE=PASS pointer_bits=64 GNU_QEMU=1 negative=4'
