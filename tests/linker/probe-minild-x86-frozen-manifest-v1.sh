#!/usr/bin/env bash
# Regression: the real GNU producer uses leading './' on generated Kbuild .o.
# Rebuilding kallsyms/piggy and zoffset-dependent header.S (GNU AS only) is
# allowed, while *every* original C-owned object and unrelated .o stays frozen.
set -Eeuo pipefail
repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd -P)
helper="$repo/tools/ci/minild-x86-linux-frozen-full-bzimage-v1.sh"
work=$(mktemp -d)
trap 'rm -r -- "$work"' EXIT
mkdir -p "$work/arch/x86/boot/compressed" "$work/init" "$work/kernel"

printf '%s\n' 'gnu pass1' >"$work/.tmp_vmlinux.kallsyms1.o"
printf '%s\n' 'gnu pass2' >"$work/.tmp_vmlinux.kallsyms2.o"
printf '%s\n' 'gnu pass3' >"$work/.tmp_vmlinux.kallsyms3.o"
printf '%s\n' 'gnu piggy' >"$work/arch/x86/boot/compressed/piggy.o"
printf '%s\n' 'gnu original setup header assembly' >"$work/arch/x86/boot/header.o"
printf '%s\n' 'frozen kernel C' >"$work/init/version-timestamp.o"
printf '%s\n' 'frozen kernel C2' >"$work/kernel/start.o"
printf '%s\n' 'unrelated with same basename' >"$work/kernel/.tmp_vmlinux.kallsyms1.o"
printf '%s\n' 'unrelated header.o' >"$work/kernel/header.o"

(cd "$work" && sha256sum \
  ./.tmp_vmlinux.kallsyms1.o \
  ./.tmp_vmlinux.kallsyms2.o \
  .tmp_vmlinux.kallsyms3.o \
  ./arch/x86/boot/compressed/piggy.o \
  ./arch/x86/boot/header.o \
  init/version-timestamp.o \
  ./kernel/start.o \
  kernel/.tmp_vmlinux.kallsyms1.o \
  kernel/header.o) >"$work/gnu-all.sha256"

bash "$helper" project-immutable-manifest \
  "$work/gnu-all.sha256" "$work/immutable.sha256" >"$work/projection.log"
grep -Fq 'generated=5' "$work/projection.log"
[[ $(wc -l <"$work/immutable.sha256") == 4 ]] || {
  echo 'MINILD_X86_MANIFEST_REGRESSION=FAIL incorrect_immutable_count' >&2
  exit 1
}

# Running the original Kbuild generation dependencies mutates these five.
for name in .tmp_vmlinux.kallsyms1.o .tmp_vmlinux.kallsyms2.o \
    .tmp_vmlinux.kallsyms3.o arch/x86/boot/compressed/piggy.o \
    arch/x86/boot/header.o; do
  printf '%s\n' 'new generated GNU assembly output' >"$work/$name"
done
(cd "$work" && sha256sum --quiet -c immutable.sha256) || {
  echo 'MINILD_X86_MANIFEST_REGRESSION=FAIL generated_asm_in_immutable_manifest' >&2
  exit 1
}

# Negative: keep every unrelated original C-owned object SHA-frozen.
printf '%s\n' 'malicious mutation' >"$work/init/version-timestamp.o"
if (cd "$work" && sha256sum --quiet -c immutable.sha256) \
    >"$work/negative.stdout" 2>"$work/negative.stderr"; then
  echo 'MINILD_X86_MANIFEST_REGRESSION=FAIL accepted_modified_original_C_object' >&2
  exit 1
fi
grep -Fq 'init/version-timestamp.o: FAILED' "$work/negative.stdout" || {
  echo 'MINILD_X86_MANIFEST_REGRESSION=FAIL wrong_immutable_rejection' >&2
  exit 1
}

# Negative: do not exempt a same-named file outside the actual generated path.
printf '%s\n' 'malicious similarly named mutation' >"$work/kernel/.tmp_vmlinux.kallsyms1.o"
if (cd "$work" && sha256sum --quiet -c immutable.sha256) \
    >"$work/negative-named.stdout" 2>"$work/negative-named.stderr"; then
  echo 'MINILD_X86_MANIFEST_REGRESSION=FAIL accepted_unrelated_named_object' >&2
  exit 1
fi
grep -Fq 'kernel/.tmp_vmlinux.kallsyms1.o: FAILED' \
  "$work/negative-named.stdout" || {
  echo 'MINILD_X86_MANIFEST_REGRESSION=FAIL wrong_unrelated_named_rejection' >&2
  exit 1
}

printf '%s\n' 'modified unrelated header.o' >"$work/kernel/header.o"
if (cd "$work" && sha256sum --quiet -c immutable.sha256) \
    >"$work/negative-header.stdout" 2>"$work/negative-header.stderr"; then
  echo 'MINILD_X86_MANIFEST_REGRESSION=FAIL accepted_unrelated_header_object' >&2
  exit 1
fi
grep -Fq 'kernel/header.o: FAILED' "$work/negative-header.stdout" || {
  echo 'MINILD_X86_MANIFEST_REGRESSION=FAIL wrong_unrelated_header_rejection' >&2
  exit 1
}

echo 'MINILD_X86_MANIFEST_REGRESSION=PASS generated_kallsyms_1_2_3_piggy_and_header_AS_mutable=true original_kernel_C_and_unrelated_o_frozen=true'
