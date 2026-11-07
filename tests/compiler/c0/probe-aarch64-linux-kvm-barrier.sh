#!/usr/bin/env bash
# Exact Linux 6.6 KVM/arm64 zero-input GNU ALTERNATIVE contracts:
# 71 NOP->DMB SY for PAR_EL1 erratum and 95 NOP->ISB for stage-2 updates.
set -Eeuo pipefail
mini=${1:?MiniC compiler}
test -x "$mini"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat > "$work/kvm-barrier.c" <<'C'
#define ALT(cap, new_instruction) \
  ".if 1 == 1\n" \
  "661:\n\t" "nop" "\n" \
  "662:\n" \
  ".pushsection .altinstructions,\"a\"\n" \
  " .word 661b - .\n .word 663f - .\n" \
  " .hword " #cap "\n" \
  " .byte 662b-661b\n .byte 664f-663f\n" \
  ".popsection\n.subsection 1\n" \
  "663:\n\t" new_instruction "\n" \
  "664:\n" \
  ".org\t. - (664b-663b) + (662b-661b)\n\t" \
  ".org\t. - (662b-661b) + (664b-663b)\n\t" \
  ".previous\n.endif\n"
void kvm_par_barrier(void) { asm(ALT(71, "dmb sy")); }
void kvm_stage2_barrier(void) { asm(ALT(95, "isb")); }
C
cc -E -P -x c "$work/kvm-barrier.c" -o "$work/kvm-barrier.i"
"$mini" --target=aarch64-linux -S "$work/kvm-barrier.i" \
  -o "$work/kvm-barrier.s"
test "$(grep -Fc '.pushsection .altinstructions' "$work/kvm-barrier.s")" -eq 2
test "$(grep -Fc '.word 661b - .' "$work/kvm-barrier.s")" -eq 2
test "$(grep -Fc '.word 663f - .' "$work/kvm-barrier.s")" -eq 2
test "$(grep -Fc '.hword 71' "$work/kvm-barrier.s")" -eq 1
test "$(grep -Fc '.hword 95' "$work/kvm-barrier.s")" -eq 1
test "$(grep -Ec '^  dmb sy$' "$work/kvm-barrier.s")" -eq 1
test "$(grep -Ec '^  isb$' "$work/kvm-barrier.s")" -eq 1
test "$(grep -Fc '.org . -' "$work/kvm-barrier.s")" -eq 4
echo 'AARCH64_KVM_BARRIER=PASS cap71_NOP_DMB_SY=1 cap95_NOP_ISB=1 records=2'
if command -v aarch64-linux-gnu-as >/dev/null 2>&1; then
  aarch64-linux-gnu-as "$work/kvm-barrier.s" -o "$work/kvm-barrier.o"
  echo 'AARCH64_KVM_BARRIER_GAS=PASS'
else
  echo 'AARCH64_KVM_BARRIER_GAS=SKIP'
fi
for changed in wrong-cap wrong-op missing-org extra-instr; do
  case "$changed" in
    wrong-cap) sed 's/"71"/"72"/g' "$work/kvm-barrier.i" > "$work/$changed.i" ;;
    wrong-op) sed 's/dmb sy/dsb ish/g' "$work/kvm-barrier.i" > "$work/$changed.i" ;;
    missing-org) sed 's/\.org/\.Xorg/g' "$work/kvm-barrier.i" > "$work/$changed.i" ;;
    extra-instr) sed 's/isb/isb; brk #0/g' "$work/kvm-barrier.i" > "$work/$changed.i" ;;
  esac
  if "$mini" --target=aarch64-linux -S "$work/$changed.i" \
      -o "$work/$changed.s" >/dev/null 2>"$work/$changed.err"; then
    echo "AARCH64_KVM_BARRIER=FAIL unexpected $changed accepted" >&2
    exit 1
  fi
  test ! -e "$work/$changed.s"
done
echo 'AARCH64_KVM_BARRIER_NEGATIVE=PASS 4/4'
