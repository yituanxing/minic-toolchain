#!/usr/bin/env bash
# Actual Linux 6.6 arch/x86/Kconfig.assembler capability mnemonics, checked
# against independently assembled GNU GAS ELF machine code (not Kconfig text).
set -Eeuo pipefail
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
minias=${MINIAS:-"$root/build/release/bin/minic-as"}
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

cat >"$work/positive.s" <<'ASM'
.text
.globl x86_linux_kconfig_isa
x86_linux_kconfig_isa:
  vpmovm2b %k1,%zmm5
  sha1msg1 %xmm0,%xmm1
  sha256msg1 %xmm0,%xmm1
  vgf2p8mulb %xmm0,%xmm1,%xmm2
  wrussq %rax,(%rbx)
  # The same typed encoders must accept real register variations.
  vpmovm2b %k7,%zmm31
  vpmovm2b %k2,%xmm1
  vpmovm2b %k3,%ymm16
  sha1msg1 %xmm9,%xmm12
  sha256msg1 %xmm15,%xmm8
  vgf2p8mulb %xmm9,%xmm12,%xmm15
  wrussd %eax,(%rbx)
  wrussq %r9,8(%r12,%r13,4)
.section .note.GNU-stack,"",@progbits
ASM

as --64 -o "$work/gnu.o" "$work/positive.s"
"$minias" --target=x86_64 -o "$work/mini.o" "$work/positive.s"
for name in gnu mini; do
  objcopy --only-section=.text -O binary "$work/$name.o" \
    "$work/$name.text"
  readelf -rW "$work/$name.o" >"$work/$name.reloc"
done
if ! cmp "$work/gnu.text" "$work/mini.text"; then
  echo 'GNU:' >&2
  od -An -tx1 "$work/gnu.text" >&2
  echo 'MiniAS:' >&2
  od -An -tx1 "$work/mini.text" >&2
  exit 1
fi
if ! cmp -s "$work/gnu.reloc" "$work/mini.reloc"; then
  # GNU/Mini may have different section descriptions; the assembled code
  # contains no symbols or operands requiring any actual ELF relocation.
  if readelf -rW "$work/gnu.o" | grep -q 'R_X86_' || \
     readelf -rW "$work/mini.o" | grep -q 'R_X86_'; then
    echo 'unexpected relocations in x86 Kconfig ISA GNU oracle' >&2
    exit 1
  fi
fi

# Neither backend may silently reinterpret malformed predicate/mask width or
# mnemonic spellings as another, syntactically valid ISA instruction.
for invalid in \
  'vpmovm2b %k8,%zmm1' \
  'vpmovm2b %k1,%zmm32' \
  'sha1msg1 %xmm0,%eax' \
  'vgf2p8mulb %xmm0,%xmm1' \
  'wrussq %eax,(%rbx)' \
  'wrussq %rax,%rbx' \
  'wrussl %eax,(%rbx)'; do
  if printf '.text\n%s\n' "$invalid" | as --64 -o "$work/gnu-bad.o" - \
       >/dev/null 2>&1; then
    echo "GNU unexpectedly accepted malformed instruction: $invalid" >&2
    exit 1
  fi
  if printf '.text\n%s\n' "$invalid" | \
     "$minias" --target=x86_64 -o "$work/mini-bad.o" /dev/stdin \
       >/dev/null 2>&1; then
    echo "MiniAS unexpectedly accepted malformed instruction: $invalid" >&2
    exit 1
  fi
done
echo "MINIAS_X86_KCONFIG_ISA_GNU=PASS probes=5 other_valid=8 invalid=7 GNU_text=byte-exact GNU_reloc=none"
