#!/usr/bin/env bash
# Exact Linux 6.6 KVM __translate_far_to_hpfar GNU inline asm:
# AT S1E1R, SPSR/ELR save and fixup, and its two-word __kvm_ex_table entry.
set -Eeuo pipefail
mini=${1:?MiniC compiler}
test -x "$mini"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat > "$work/at-fixup.c" <<'C'
typedef unsigned long long u64;
int kvm_translate_far(u64 far) {
  int err = 0;
  u64 spsr, elr;
  asm volatile(
      "\tmrs\t%1, spsr_el2\n"
      "\tmrs\t%2, elr_el2\n"
      "1:\tat\t" "s1e1r" ", %3\n"
      "\tisb\n"
      "\tb\t9f\n"
      "2:\tmsr\tspsr_el2, %1\n"
      "\tmsr\telr_el2, %2\n"
      "\tmov\t%w0, %4\n"
      "9:\n"
      "\t.pushsection\t__kvm_ex_table, \"a\"\n"
      "\t.align\t\t3\n"
      "\t.long\t\t(" "1b" " - .), (" "2b" " - .)\n"
      "\t.popsection\n"
      : "+r"(err), "=&r"(spsr), "=&r"(elr) : "r"(far), "i"(-14));
  return err;
}
C
cc -E -P -x c "$work/at-fixup.c" -o "$work/at-fixup.i"
"$mini" --target=aarch64-linux -S "$work/at-fixup.i" -o "$work/at-fixup.s"
test "$(grep -Ec '^  mrs x10, spsr_el2$' "$work/at-fixup.s")" -eq 1
test "$(grep -Ec '^  mrs x11, elr_el2$' "$work/at-fixup.s")" -eq 1
test "$(grep -Ec '^1: at s1e1r, x14$' "$work/at-fixup.s")" -eq 1
test "$(grep -Ec '^2: msr spsr_el2, x10$' "$work/at-fixup.s")" -eq 1
test "$(grep -Ec '^  msr elr_el2, x11$' "$work/at-fixup.s")" -eq 1
test "$(grep -Ec '^  mov w9, #-14$' "$work/at-fixup.s")" -eq 1
test "$(grep -Fc '.pushsection __kvm_ex_table, "a"' "$work/at-fixup.s")" -eq 1
test "$(grep -Fc '.long (1b - .), (2b - .)' "$work/at-fixup.s")" -eq 1
echo 'AARCH64_KVM_AT_FIXUP=PASS AT=S1E1R restore=SPSR+ELR exception_table=2xPREL32'
if command -v aarch64-linux-gnu-as >/dev/null 2>&1; then
  aarch64-linux-gnu-as "$work/at-fixup.s" -o "$work/at-fixup.o"
  echo 'AARCH64_KVM_AT_FIXUP_GAS=PASS'
else
  echo 'AARCH64_KVM_AT_FIXUP_GAS=SKIP'
fi
for changed in bad-op bad-fixup bad-table bad-immediate bad-constraint; do
  case "$changed" in
    bad-op) sed 's/s1e1r/s1e1w/g' "$work/at-fixup.i" > "$work/$changed.i" ;;
    bad-fixup) sed 's/msr\\tspsr_el2/msr\\telr_el2/g' "$work/at-fixup.i" > "$work/$changed.i" ;;
    bad-table) sed 's/__kvm_ex_table/__bad_ex_table/g' "$work/at-fixup.i" > "$work/$changed.i" ;;
    bad-immediate) sed 's/"i"(-14)/"i"(-13)/g' "$work/at-fixup.i" > "$work/$changed.i" ;;
    bad-constraint) sed 's/"+r"(err)/"=r"(err)/g' "$work/at-fixup.i" > "$work/$changed.i" ;;
  esac
  if "$mini" --target=aarch64-linux -S "$work/$changed.i" -o "$work/$changed.s" \
      >/dev/null 2>"$work/$changed.err"; then
    echo "AARCH64_KVM_AT_FIXUP=FAIL unsafe GNU template $changed accepted" >&2
    exit 1
  fi
  test ! -e "$work/$changed.s"
done
echo 'AARCH64_KVM_AT_FIXUP_NEGATIVE=PASS 5/5'
