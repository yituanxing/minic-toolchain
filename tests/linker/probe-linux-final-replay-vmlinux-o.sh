#!/usr/bin/env bash
# The real x86-64 Linux 6.6.143 kallsyms1 command uses a GNU-linked vmlinux.o
# aggregate (not vmlinux.a). Exercise that replay admission boundary with GNU
# and MiniLD while rejecting arbitrary .o inputs and unrelated linker scripts.
set -Eeuo pipefail

repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd -P)
mini=${MINILD:-$repo/build/release/bin/minic-ld}
replay=$repo/tools/ci/minild-linux-real-final-replay.sh
capture_wrapper=$repo/tools/ci/minild-linux-real-final-capture-ld.sh
for bin in ar as ld readelf sha256sum timeout; do
  command -v -- "$bin" >/dev/null || {
    echo "MINILD_X86_VMLINUX_O=INCONCLUSIVE missing=$bin" >&2
    exit 8
  }
done
[[ -x $mini ]] || {
  echo "MINILD_X86_VMLINUX_O=INCONCLUSIVE missing_MiniLD=$mini" >&2
  exit 8
}
mini=$(realpath -- "$mini")
gnu=$(command -v ld)
work=$(mktemp -d "${TMPDIR:-/tmp}/minild-x86-vmlinux-o.XXXXXXXX")
finish() {
  if [[ ${MINILD_X86_VMLINUX_O_KEEP:-0} == 1 ]]; then
    echo "MINILD_X86_VMLINUX_O_ARTIFACTS=$work"
  else
    rm -r -- "$work"
  fi
}
trap finish EXIT
mkdir -p -- "$work/arch/x86/kernel" "$work/capture"

printf '%s\n' \
  '.text' \
  '.globl _start' \
  '_start:' \
  '  mov $60, %eax' \
  '  xor %edi, %edi' \
  '  syscall' >"$work/entry.s"
printf '%s\n' \
  'OUTPUT_ARCH(i386:x86-64)' \
  'ENTRY(_start)' \
  'SECTIONS {' \
  '  . = 0x400000;' \
  '  .text : { *(.text .text.*) }' \
  '}' >"$work/arch/x86/kernel/vmlinux.lds"
cp -- "$work/arch/x86/kernel/vmlinux.lds" "$work/unrelated.lds"
as -o "$work/vmlinux.o" "$work/entry.s"
cp -- "$work/vmlinux.o" "$work/standalone.o"
ar rcs "$work/vmlinux.a" "$work/vmlinux.o"
cp -- "$work/vmlinux.a" "$work/unrelated.a"

# Capture a successfully linked GNU first-kallsyms1 command using the same
# argv0/cwd/tool-hash evidence format as the production Kbuild LD wrapper.
(
  cd -- "$work"
  MINILD_LINUX_GNU_LD="$gnu" \
    MINILD_LINUX_FINAL_CAPTURE_DIR="$work/capture" \
    bash "$capture_wrapper" -m elf_x86_64 \
      --script=./arch/x86/kernel/vmlinux.lds \
      -o .tmp_vmlinux.kallsyms1 \
      --whole-archive vmlinux.o --no-whole-archive
) >"$work/capture.stdout"
grep -q 'MINILD_LINUX_CAPTURE=PASS' "$work/capture.stdout"

linked_count=0
policy_gated_count=0
check_positive_replay() {
  local name=$1
  local input_capture=$2
  local evidence="$work/positive-$name-evidence"
  local status=0
  bash "$replay" "$input_capture" "$mini" "$evidence" \
    >"$work/replay-$name.stdout" \
    2>"$work/replay-$name.stderr" || status=$?
  [[ $(<"$evidence/gnu.status") == 0 ]]
  if [[ $status == 0 ]]; then
    grep -q 'MINILD_LINUX_FINAL_REPLAY=PASS' "$work/replay-$name.stdout"
    [[ $(<"$evidence/minild.status") == 0 ]]
    readelf -W -h "$evidence/.tmp_vmlinux.kallsyms1.minild" \
      | grep -q 'Machine:.*Advanced Micro Devices X86-64'
    "$evidence/.tmp_vmlinux.kallsyms1.gnu"
    "$evidence/.tmp_vmlinux.kallsyms1.minild"
    linked_count=$((linked_count + 1))
  elif [[ $status == 76 ]] &&
      [[ $(<"$evidence/minild.status") == 2 ]] &&
      grep -Fqx \
        'minic-ld: unsupported-target-static-script-or-gc:elf_x86_64' \
        "$evidence/minild.stderr"; then
    # Production CI explicitly lifts this profile policy on its private
    # runner. A stock local binary intentionally rejects x86 static scripts;
    # that is a policy-gated admission PASS, not a successful MiniLD link.
    [[ ! -s "$evidence/.tmp_vmlinux.kallsyms1.minild" ]]
    "$evidence/.tmp_vmlinux.kallsyms1.gnu"
    policy_gated_count=$((policy_gated_count + 1))
  else
    echo "MINILD_X86_VMLINUX_O=FAIL unexpected_replay=$name rc=$status" >&2
    sed -n '1,55p' "$work/replay-$name.stderr" >&2
    exit 1
  fi
}
check_positive_replay object "$work/capture"

# All four actual GNU Kbuild captures were inspected: RV32, AArch64 and
# LoongArch64 use exact vmlinux.a; x86-64 uses exact vmlinux.o. Both remain
# admissible only with a target kernel vmlinux.lds and verified GNU input.
mkdir -p -- "$work/capture-archive"
(
  cd -- "$work"
  MINILD_LINUX_GNU_LD="$gnu" \
    MINILD_LINUX_FINAL_CAPTURE_DIR="$work/capture-archive" \
    bash "$capture_wrapper" -m elf_x86_64 \
      --script=./arch/x86/kernel/vmlinux.lds \
      -o .tmp_vmlinux.kallsyms1 \
      --whole-archive vmlinux.a --no-whole-archive
) >"$work/capture-archive.stdout"
grep -q 'MINILD_LINUX_CAPTURE=PASS' "$work/capture-archive.stdout"
check_positive_replay archive "$work/capture-archive"

# This mutates only argv0 in an otherwise correctly captured GNU fixture.
# Every rejected case must stop before either linker replays a fake "kernel".
expect_inconclusive() {
  local name=$1
  shift
  local fixture="$work/reject-$name"
  cp -a -- "$work/capture" "$fixture"
  printf '%s\0' "$@" >"$fixture/first-kallsyms1.argv0"
  local status=0
  bash "$replay" "$fixture" "$mini" "$work/evidence-reject-$name" \
    >"$work/reject-$name.stdout" \
    2>"$work/reject-$name.stderr" || status=$?
  if [[ $status != 71 ]] ||
      ! grep -q 'INCONCLUSIVE no_real_kernel_archive_or_linker_script' \
        "$work/reject-$name.stderr"; then
    echo "MINILD_X86_VMLINUX_O=FAIL invalid_fixture_accepted=$name rc=$status" >&2
    exit 1
  fi
  [[ ! -e "$work/evidence-reject-$name/gnu.status" ]]
  [[ ! -e "$work/evidence-reject-$name/minild.status" ]]
}
expect_inconclusive standalone \
  -m elf_x86_64 --script=./arch/x86/kernel/vmlinux.lds \
  -o .tmp_vmlinux.kallsyms1 standalone.o
expect_inconclusive unrelated_archive \
  -m elf_x86_64 --script=./arch/x86/kernel/vmlinux.lds \
  -o .tmp_vmlinux.kallsyms1 --whole-archive unrelated.a --no-whole-archive
expect_inconclusive unrelated_script \
  -m elf_x86_64 --script=./unrelated.lds \
  -o .tmp_vmlinux.kallsyms1 vmlinux.o
expect_inconclusive archive_unrelated_script \
  -m elf_x86_64 --script=./unrelated.lds \
  -o .tmp_vmlinux.kallsyms1 --whole-archive vmlinux.a --no-whole-archive
expect_inconclusive unscripted \
  -m elf_x86_64 -o .tmp_vmlinux.kallsyms1 vmlinux.o

printf '%s\n' \
  "MINILD_X86_VMLINUX_O=PASS GNU_control=2/2 MiniLD_linked=$linked_count/2 MiniLD_policy_gated=$policy_gated_count/2 real_script=true exact_vmlinux_o_and_a=true fake_object_and_archive_rejected=true unrelated_script_rejected=true unscripted_rejected=true"
