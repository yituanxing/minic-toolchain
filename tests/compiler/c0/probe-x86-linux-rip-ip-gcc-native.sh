#!/usr/bin/env bash
# Original Linux 6.6.143 _THIS_IP_: GNU nonvolatile LEA 0(%%rip),%0.
# Independent GCC/GAS native callsite address oracle and strict negative.
set -Eeuo pipefail
: "${MINIC_CC:?exact compiler required}"
tmp=$(mktemp -d);trap 'rm -rf "$tmp"' EXIT
cat >"$tmp/mini.i" <<'SRC'
__attribute__((noinline)) unsigned long mini_pc(void) {
  unsigned long here;
  asm ("lea 0(%%rip), %0" : "=r" (here));
  return here;
}
__attribute__((noinline)) unsigned long mini_macro(void) {
  return ({ unsigned long here; asm ("lea 0(%%rip), %0" : "=r" (here)); here; });
}
SRC
sed 's/mini_pc/gnu_pc/g;s/mini_macro/gnu_macro/g' "$tmp/mini.i" >"$tmp/gnu.c"
cat >"$tmp/caller.c" <<'SRC'
extern unsigned long mini_pc(void),gnu_pc(void),mini_macro(void),gnu_macro(void);
int main(void) {
  unsigned long a=mini_pc(),b=gnu_pc(),c=mini_macro(),d=gnu_macro();
  if (!a||!b||!c||!d||a==b||c==d) return 1;
  if (a<(unsigned long)&mini_pc || a>(unsigned long)&mini_pc+4096) return 2;
  if (b<(unsigned long)&gnu_pc || b>(unsigned long)&gnu_pc+4096) return 3;
  if (c<(unsigned long)&mini_macro || c>(unsigned long)&mini_macro+4096) return 4;
  if (d<(unsigned long)&gnu_macro || d>(unsigned long)&gnu_macro+4096) return 5;
  return 42;
}
SRC
gcc -O0 -fno-pie -c "$tmp/gnu.c" -o "$tmp/gnu.o"
"$MINIC_CC" --target=x86_64-linux -S "$tmp/mini.i" -o "$tmp/mini.s"
gcc -c -x assembler "$tmp/mini.s" -o "$tmp/mini.o"
grep -Fq 'leaq 0(%rip)' "$tmp/mini.s"
gcc -static -no-pie -O0 "$tmp/caller.c" "$tmp/gnu.o" "$tmp/mini.o" -o "$tmp/oracle"
set +e
timeout 40s "$tmp/oracle" >"$tmp/stdout" 2>"$tmp/stderr"
rc=$?
set -e
if (( rc != 42 ));then
    echo "X86_LINUX_THIS_IP=FAIL native GNU/MiniC caller ABI rc=$rc" >&2
    cat "$tmp/stderr" >&2
    exit 1
fi
cat >"$tmp/inject.i" <<'SRC'
unsigned long evil(void) {
  unsigned long v;
  asm ("lea 0(%%rip), %0; int3" : "=r" (v));
  return v;
}
SRC
cat >"$tmp/bad_constraint.i" <<'SRC'
unsigned long evil(void) {
  unsigned long v;
  asm ("lea 0(%%rip), %0" : "=m" (v));
  return v;
}
SRC
for variant in inject bad_constraint;do
 if "$MINIC_CC" --target=x86_64-linux -S "$tmp/$variant.i" -o "$tmp/$variant.s" 2>"$tmp/$variant.err";then
   echo "X86_LINUX_THIS_IP=FAIL illegal source accepted variant=$variant" >&2
   exit 1
 fi
 test ! -e "$tmp/$variant.s"
done
echo "X86_LINUX_THIS_IP=PASS GNU_GAS_link_native=1 source_macro=1 pc_relative_machine=1 illegal_asm_rejected=2"
