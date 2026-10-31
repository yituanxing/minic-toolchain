#!/usr/bin/env bash
set -Eeuo pipefail
cc=${1:?MiniC binary};test -x "$cc"
d=$(mktemp -d);trap 'rm -rf "$d"' EXIT
cat > "$d/cmpwait.i" <<'SRC'
typedef unsigned int u32;
static inline void __cmpwait_case_32(volatile void *ptr,unsigned long val) {
 unsigned long tmp;
 asm volatile(" sevl\n" " wfe\n" " ldxr\t%w[tmp], %[v]\n"
 " eor %w[tmp], %w[tmp], %w[val]\n"
 " cbnz %w[tmp], 1f\n" " wfe\n" "1:"
 : [tmp] "=&r" (tmp), [v] "+Q" (*(u32 *)ptr) : [val] "r" (val));
}
static volatile u32 value=1;
int main(void) {__cmpwait_case_32((void *)&value,0);return value==1?42:1;}
SRC
"$cc" --target=aarch64-linux -S "$d/cmpwait.i" -o "$d/mini.s"
grep -q 'ldxr w9' "$d/mini.s"
grep -q 'sevl' "$d/mini.s"
if [[ "${MINIC_SOURCE_ONLY:-0}" != 1 ]]; then
 aarch64-linux-gnu-gcc -static -x c "$d/cmpwait.i" -o "$d/gnu"
 aarch64-linux-gnu-gcc -static -x assembler "$d/mini.s" -o "$d/mini"
 for exe in mini gnu;do rc=0;timeout 30s qemu-aarch64 "$d/$exe" || rc=$?;test "$rc" -eq 42;done
fi
sed 's/ldxr/ldr/' "$d/cmpwait.i" > "$d/unsafe.i"
if "$cc" --target=aarch64-linux -S "$d/unsafe.i" -o "$d/unsafe.s" >/dev/null 2>&1;then
 echo 'nonexclusive CMPWAIT accepted' >&2;exit 1
fi
test ! -e "$d/unsafe.s"
echo 'AARCH64_ORIGINAL_CMPWAIT=PASS exclusive=1 negative=rejected'
