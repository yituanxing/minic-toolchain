#!/usr/bin/env bash
# Privileged x86 instructions must be assembled, not executed in userspace.
# Exact GNU Linux operand contracts and object evidence are compared to GCC.
set -Eeuo pipefail
mini=${1:?usage: probe-x86-linux-control-cache-isa.sh <minic-cc>}
test -x "$mini"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat > "$work/privileged.i" <<'SOURCE'
void minic_write_cr0(unsigned long x) {
  asm volatile("mov %0,%%cr0": "+r" (x) : : "memory");
}
void minic_write_cr4(unsigned long x) {
  asm volatile("mov %0,%%cr4": "+r" (x) : : "memory");
}
void minic_invlpg(unsigned long address) {
  asm volatile("invlpg (%0)" :: "r" (address) : "memory");
}
void minic_clflush(volatile char *address) {
  asm volatile("clflush %0" : "+m" (*address));
}
SOURCE
"$mini" --target=x86_64-linux -S "$work/privileged.i" -o "$work/mini.s"
gcc -c -x assembler "$work/mini.s" -o "$work/mini.o"
gcc -c -x c "$work/privileged.i" -o "$work/gnu.o"
objdump -d "$work/mini.o" > "$work/mini.dump"
objdump -d "$work/gnu.o" > "$work/gnu.dump"
for op in '%cr0' '%cr4' invlpg clflush; do
  grep -Fq "$op" "$work/mini.dump"
  grep -Fq "$op" "$work/gnu.dump"
done
for bad in mismatched-cr register clflush-extra-op; do
  case "$bad" in
    mismatched-cr) sed 's/mov %0,%%cr0/mov %0,%%cr8/' "$work/privileged.i" > "$work/bad.i" ;;
    clflush-extra-op) sed 's/clflush %0/clflush %0; nop/' "$work/privileged.i" > "$work/bad.i" ;;
  esac
  if "$mini" --target=x86_64-linux -S "$work/bad.i" -o "$work/bad.s" \
       >/dev/null 2>&1; then
    echo "X86_CONTROL_CACHE_ISA=FAIL admitted=$bad" >&2
    exit 1
  fi
  test ! -e "$work/bad.s"
done
echo 'X86_CONTROL_CACHE_ISA=PASS CR0/CR4 tied-reg invlpg clflush GNU_AS_objdump negative=2'
