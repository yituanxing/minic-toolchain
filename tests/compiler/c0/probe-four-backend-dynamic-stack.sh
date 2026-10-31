#!/bin/sh
# Both new LP64 backends: native Core dynamic stack semantics, not only
# linking Kbuild objects that may never execute their alloca paths.
# Reuse the existing RV64 two-alloca/ten-argument and VLA runtime fixtures.
set -eu
test "$#" -eq 4
: "${MINIC_CC:?set MINIC_CC}"
target=$1
gcc=$2
qemu=$3
sysroot=$4
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
fixtures='dynamic_stack_alloca dynamic_local_vla'
expected=2
# The 64-wide-argument AAPCS64 path spills 960 bytes across live alloca.
if [ "$target" = aarch64 ]; then
  fixtures="$fixtures dynamic_stack_wide_spill"
  expected=3
fi
passed=0
for fixture in $fixtures; do
  source="$repo/tests/compiler/c0/$fixture.c"
  mini_asm="$work/$fixture.minic.s"
  helper=
  if [ "$fixture" = dynamic_stack_alloca ]; then
    helper="$repo/tests/compiler/c0/dynamic_stack_alloca_helper.c"
  elif [ "$fixture" = dynamic_stack_wide_spill ]; then
    helper="$repo/tests/compiler/c0/dynamic_stack_wide_spill_helper.c"
  fi
  # minic-cc consumes preprocessed translation units. GNU -E -P removes
  # comments/directives and matches the original C seen by cross-GCC.
  if ! "$gcc" -E -P -std=gnu11 "$source" -o "$work/$fixture.i"; then
    echo "FOUR_BACKEND_DYNAMIC_STACK=FAIL target=$target fixture=$fixture stage=GNU_PREPROCESS" >&2
    exit 1
  fi
  if ! timeout 40s "$MINIC_CC" --target="$target-linux" -S "$work/$fixture.i" \
        -o "$mini_asm" >"$work/$fixture.mini.out" 2>"$work/$fixture.mini.err" ||
     ! test -s "$mini_asm"; then
    echo "FOUR_BACKEND_DYNAMIC_STACK=FAIL target=$target fixture=$fixture stage=MiniC" >&2
    tail -n 18 "$work/$fixture.mini.err" >&2 || true
    exit 1
  fi
  if ! "$gcc" -O0 -std=gnu11 "$source" $helper -o "$work/$fixture.gnu.elf" \
       >"$work/$fixture.gnu.build" 2>&1 ||
     ! "$gcc" -O0 "$mini_asm" $helper -o "$work/$fixture.mini.elf" \
       >"$work/$fixture.mini.build" 2>&1; then
    echo "FOUR_BACKEND_DYNAMIC_STACK=FAIL target=$target fixture=$fixture stage=GNU_AS_OR_LINK" >&2
    tail -n 20 "$work/$fixture.gnu.build" "$work/$fixture.mini.build" >&2 || true
    exit 1
  fi
  if ! timeout 30s "$qemu" -L "$sysroot" "$work/$fixture.gnu.elf" \
           >"$work/$fixture.gnu.stdout" 2>"$work/$fixture.gnu.stderr" ||
     ! timeout 30s "$qemu" -L "$sysroot" "$work/$fixture.mini.elf" \
           >"$work/$fixture.mini.stdout" 2>"$work/$fixture.mini.stderr" ||
     ! cmp -s "$work/$fixture.gnu.stdout" "$work/$fixture.mini.stdout" ||
     ! cmp -s "$work/$fixture.gnu.stderr" "$work/$fixture.mini.stderr"; then
    echo "FOUR_BACKEND_DYNAMIC_STACK=FAIL target=$target fixture=$fixture stage=QEMU_GNU_DIFFERENTIAL" >&2
    tail -n 20 "$work/$fixture.gnu.stderr" "$work/$fixture.mini.stderr" >&2 || true
    # Minimized GNU/QEMU reproduction: preserve the full failed TU and
    # final AArch64 basic-block/CPU registers (not a speculative fix).
    echo "FOUR_BACKEND_DYNAMIC_STACK_ASM_BEGIN target=$target fixture=$fixture" >&2
    sed -n '1,650p' "$mini_asm" >&2 || true
    echo "FOUR_BACKEND_DYNAMIC_STACK_ASM_END target=$target fixture=$fixture" >&2
    timeout 12s "$qemu" -L "$sysroot" -d in_asm,cpu \
      -D "$work/$fixture.qemu-trace" "$work/$fixture.mini.elf" \
      > /dev/null 2> /dev/null || true
    if [ -f "$work/$fixture.qemu-trace" ]; then
      echo "FOUR_BACKEND_DYNAMIC_STACK_QEMU_TRACE_BEGIN target=$target fixture=$fixture" >&2
      tail -n 145 "$work/$fixture.qemu-trace" >&2 || true
      echo "FOUR_BACKEND_DYNAMIC_STACK_QEMU_TRACE_END target=$target fixture=$fixture" >&2
    fi
    exit 1
  fi
  passed=$((passed + 1))
  echo "FOUR_BACKEND_DYNAMIC_STACK_FIXTURE=PASS target=$target fixture=$fixture MiniC_GNU_AS_LINK=1 GNU_QEMU_exit0_byte_exact=1"
done
test "$passed" -eq "$expected"
echo "FOUR_BACKEND_DYNAMIC_STACK=PASS target=$target fixtures=$expected nested_alloca=2 spilled_call_args=2 VLA_bound_once=1 wide_spill_aarch64=$((expected - 2)) GNU_QEMU_differential=1"
