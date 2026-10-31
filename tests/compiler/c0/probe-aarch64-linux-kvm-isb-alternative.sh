#!/usr/bin/env bash
# Linux 6.6 arm64 KVM feature-95 stage-2 barrier: both patch directions
# retain original PREL32 relocation records and equal-length GNU guards.
set -Eeuo pipefail
cc="$1"
test -x "$cc"
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
cat >"$d/orig.i" <<'EOF'
void kvm_isb_to_nop(void) {
  asm(".if 1 == 1\n"
      "661:\n\tisb\n"
      "662:\n"
      ".pushsection .altinstructions,\"a\"\n"
      " .word 661b - .\n"
      " .word 663f - .\n"
      " .hword 95\n"
      " .byte 662b-661b\n"
      " .byte 664f-663f\n"
      ".popsection\n"
      ".subsection 1\n"
      "663:\n\tnop\n"
      "664:\n"
      ".org\t. - (664b-663b) + (662b-661b)\n\t"
      ".org\t. - (662b-661b) + (664b-663b)\n\t"
      ".previous\n"
      ".endif\n");
}
void kvm_nop_to_isb(void) {
  asm(".if 1 == 1\n"
      "661:\n\tnop\n"
      "662:\n"
      ".pushsection .altinstructions,\"a\"\n"
      " .word 661b - .\n"
      " .word 663f - .\n"
      " .hword 95\n"
      " .byte 662b-661b\n"
      " .byte 664f-663f\n"
      ".popsection\n"
      ".subsection 1\n"
      "663:\n\tisb\n"
      "664:\n"
      ".org\t. - (664b-663b) + (662b-661b)\n\t"
      ".org\t. - (662b-661b) + (664b-663b)\n\t"
      ".previous\n"
      ".endif\n");
}
EOF
"$cc" --target=aarch64-linux -S "$d/orig.i" -o "$d/mini.s"
test "$(grep -Fc '  .hword 95' "$d/mini.s")" -eq 2
test "$(grep -Fc '  .word 661b - .' "$d/mini.s")" -eq 2
test "$(grep -Fc '  .word 663f - .' "$d/mini.s")" -eq 2
test "$(grep -Fc '  .org . - (664b-663b) + (662b-661b)' "$d/mini.s")" -eq 2
test "$(grep -Fc '  .org . - (662b-661b) + (664b-663b)' "$d/mini.s")" -eq 2
for bad in wrong-cap drop-org injected-op clobber; do
  case "$bad" in
    wrong-cap)
      sed '/void kvm_isb_to_nop/,/^}/s/\.hword 95/\.hword 96/' "$d/orig.i" >"$d/bad.i" ;;
    drop-org)
      sed '/void kvm_isb_to_nop/,/^}/s/\.org\\t\. - (662b-661b) + (664b-663b)/.byte 0/' "$d/orig.i" >"$d/bad.i" ;;
    injected-op)
      sed '/void kvm_isb_to_nop/,/^}/s/663:\\n\\tnop/663:\\n\\tnop\\n\\tnop/' "$d/orig.i" >"$d/bad.i" ;;
    clobber)
      sed '/void kvm_isb_to_nop/,/^}/s/".endif\\n");/".endif\\n" : : : "memory");/' "$d/orig.i" >"$d/bad.i" ;;
  esac
  if "$cc" --target=aarch64-linux -S "$d/bad.i" -o "$d/bad.s" >/dev/null 2>&1; then
    echo "AARCH64_KVM_ISB_ALTERNATIVE_UNSAFE_ACCEPT=$bad" >&2
    exit 1
  fi
  test ! -e "$d/bad.s"
done
if command -v aarch64-linux-gnu-gcc >/dev/null 2>&1; then
  aarch64-linux-gnu-gcc -c -x assembler "$d/mini.s" -o "$d/mini.o"
  aarch64-linux-gnu-gcc -c -x c "$d/orig.i" -o "$d/gnu.o"
  echo 'AARCH64_KVM_ISB_ALTERNATIVE_GAS=PASS'
else
  echo 'AARCH64_KVM_ISB_ALTERNATIVE_GAS=SKIP'
fi
echo 'AARCH64_KVM_ISB_ALTERNATIVE=PASS feature95=2 old_ISB_new_NOP=1 old_NOP_new_ISB=1 reloc_pairs=2 org_guards=4 negative=4'
