#!/usr/bin/env bash
# GNU Linux 6.6 kexec crash_setup_regs: 16 exact live-GPR `=m` snapshots,
# plus the architectural flags snapshot. Never substitute a C register read.
set -Eeuo pipefail
cc_bin=$1
test -x "$cc_bin"
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
printf 'typedef unsigned long u64;\nvoid mini_snapshot(u64 *p) {\n u64 rip;\n' >"$d/regs.i"
for reg in rbx rcx rdx rsi rdi rbp rax rsp r8 r9 r10 r11 r12 r13 r14 r15; do
 printf ' asm volatile("movq %%%%%s,%%0" : "=m"(*p));\n' "$reg" >>"$d/regs.i"
done
printf ' asm volatile("pushfq; popq %%0" : "=m"(*p));\n' >>"$d/regs.i"
printf '%s\n' ' asm("lea 0(%%rip),%0" : "=r"(rip));' ' *p=rip;' '}' >>"$d/regs.i"
sed 's/mini_/gnu_/g' "$d/regs.i" >"$d/gnu.c"
gcc -S -O0 -x c "$d/gnu.c" -o "$d/gnu.s"
gcc -c -x assembler "$d/gnu.s" -o "$d/gnu.o"
"$cc_bin" --target=x86_64-linux -S "$d/regs.i" -o "$d/mini.s"
gcc -c -x assembler "$d/mini.s" -o "$d/mini.o"
for reg in rbx rcx rdx rsi rdi rbp rax rsp r8 r9 r10 r11 r12 r13 r14 r15; do
 for compiler in gnu mini; do
  if ! grep -Eq "[[:space:]]movq[[:space:]]+%${reg}," "$d/$compiler.s"; then
   echo "FAIL missing GNU physical register $reg from $compiler" >&2
   exit 1
  fi
 done
done
for compiler in gnu mini; do
 grep -Eq '[[:space:]]pushfq' "$d/$compiler.s"
 grep -Eq '[[:space:]]popq[[:space:]]' "$d/$compiler.s"
 grep -Eq '[[:space:]]lea[q]?[[:space:]]+0?\(%rip\),' "$d/$compiler.s"
done
# Materializing the destination address must not destroy R10/R11 before
# reading the register being captured.
grep -Fq 'movq %r10, (%r11)' "$d/mini.s"
grep -Fq 'movq %r11, (%r10)' "$d/mini.s"
sed 's/movq %%rbx,%0/movq %%rbx,%0; hlt/' "$d/regs.i" >"$d/bad-inject.i"
sed 's/unsigned long u64/unsigned int u64/' "$d/regs.i" >"$d/bad-width.i"
sed 's/: "=m"(\*p));/: "=m"(*p) : : "memory");/' \
 "$d/regs.i" >"$d/bad-clobber.i"
sed 's/asm volatile("movq %%rbx/asm("movq %%rbx/' \
 "$d/regs.i" >"$d/bad-volatility.i"
sed 's/lea 0(%%rip),%0/lea 0(%%rip),%0; hlt/' \
 "$d/regs.i" >"$d/bad-rip-inject.i"
sed 's/"=r"(rip)/"=m"(rip)/' \
 "$d/regs.i" >"$d/bad-rip-constraint.i"
for case in bad-inject bad-width bad-clobber bad-volatility bad-rip-inject bad-rip-constraint; do
 if "$cc_bin" --target=x86_64-linux -S "$d/$case.i" \
    -o "$d/$case.s" >/dev/null 2>&1; then
  echo "FAIL accepted unsafe crash-register snapshot $case" >&2
  exit 1
 fi
 test ! -e "$d/$case.s"
done
echo 'MINIC_X86_LINUX_KEXEC_CRASH_REGS=PASS GNU_GAS=1 GPR=16 RFLAGS=pushfq_popq RIP=lea R10_R11=preserved negative=6'
