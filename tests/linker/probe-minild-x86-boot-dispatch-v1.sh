#!/usr/bin/env bash
# Exercise the real x86 boot-link dispatcher under set -u. No kernel is built.
# The synthetic linker makes an output solely to verify parameter binding,
# original .cmd argv parsing, and fail-closed command/output ownership.
set -Eeuo pipefail
repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd -P)
helper="$repo/tools/ci/minild-x86-linux-frozen-full-bzimage-v1.sh"
work=$(mktemp -d)
trap 'rm -r -- "$work"' EXIT
mkdir -p "$work/source" "$work/out/arch/x86/boot/compressed" "$work/evidence"

stub="$work/stub-mini-ld.sh"
printf '%s\n' '#!/usr/bin/env bash' 'set -Eeuo pipefail' \
  'out=' 'next=0' \
  'for arg in "$@"; do' \
  '  if (( next )); then out=$arg; next=0; continue; fi' \
  '  if [[ $arg == -o ]]; then next=1; fi' \
  'done' \
  '[[ $out == arch/x86/boot/compressed/vmlinux ]] || exit 80' \
  'printf "synthetic linker output\\n" >"$out"' >"$stub"
chmod +x "$stub"

cmd="$work/out/arch/x86/boot/compressed/.vmlinux.cmd"
printf '%s\n' \
  'savedcmd_arch/x86/boot/compressed/vmlinux := /producer/minild-linux-real-final-capture-ld.sh -m elf_x86_64 -pie -T arch/x86/boot/compressed/vmlinux.lds -o arch/x86/boot/compressed/vmlinux' >"$cmd"

bash "$helper" probe-boot-link "$stub" "$work/evidence" "$work/source" \
  "$work/out" compressed-vmlinux "$cmd" arch/x86/boot/compressed/vmlinux \
  >"$work/positive.stdout"
grep -Fxq 'MINILD_X86_BOOT_LD=PASS output=arch/x86/boot/compressed/vmlinux linker=MiniLD' \
  "$work/positive.stdout"
[[ -s $work/out/arch/x86/boot/compressed/vmlinux ]]
[[ -s $work/evidence/compressed-vmlinux.minild.argv0 ]]
[[ -s $work/evidence/compressed-vmlinux.sha256 ]]

# A captured command that points to a different output must be rejected
# before any linker invocation. No alternate GNU or mocked fallback allowed.
bad="$work/out/arch/x86/boot/compressed/.bad-vmlinux.cmd"
printf '%s\n' \
  'savedcmd_arch/x86/boot/compressed/vmlinux := /producer/minild-linux-real-final-capture-ld.sh -m elf_x86_64 -o wrong-vmlinux' >"$bad"
if bash "$helper" probe-boot-link "$stub" "$work/evidence" "$work/source" \
    "$work/out" compressed-bad "$bad" arch/x86/boot/compressed/vmlinux \
    >"$work/negative.stdout" 2>"$work/negative.stderr"; then
  echo 'MINILD_X86_BOOT_DISPATCH=FAIL accepted_wrong_output_from_captured_command' >&2
  exit 1
fi
grep -Fq 'wrong_frozen_link_output' "$work/negative.stderr"

echo 'MINILD_X86_BOOT_DISPATCH=PASS real_dispatch_under_nounset=true exact_captured_Kbuild_output=true wrong_output_rejected=true kernel_C_compiles=0'
