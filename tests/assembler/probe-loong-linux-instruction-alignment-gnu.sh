#!/usr/bin/env bash
# Linux Kbuild's real -o /dev/null ULEB probe and a target ISA alignment
# corner case, compared against GNU as with no special-casing or fallback.
set -Eeuo pipefail
test -n "${MINIAS:?Set MINIAS to the current minic-as binary}"
gas=${LOONG_GNU_AS:-loongarch64-linux-gnu-as}
objcopy=${LOONG_GNU_OBJCOPY:-loongarch64-linux-gnu-objcopy}
for tool in "$gas" "$objcopy" readelf cmp wc awk; do command -v "$tool" >/dev/null; done
repo=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
fixtures="$repo/tests/assembler/fixtures"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

probe="$fixtures/loong-linux-uleb-kbuild-feature-probe.S"
"$gas" -mabi=lp64s -o "$work/probe.gnu.o" "$probe"
"$MINIAS" --target=loongarch64 -mabi=lp64s -o "$work/probe.mini.o" "$probe"
"$objcopy" -O binary -j .text "$work/probe.gnu.o" "$work/probe.gnu.bin"
"$objcopy" -O binary -j .text "$work/probe.mini.o" "$work/probe.mini.bin"
cmp "$work/probe.gnu.bin" "$work/probe.mini.bin"
test "$(wc -c <"$work/probe.gnu.bin")" -eq 1
get_text_alignment() {
  readelf -SW "$1" | awk '$3==".text" {print $NF;exit}'
}
test "$(get_text_alignment "$work/probe.gnu.o")" = \
  "$(get_text_alignment "$work/probe.mini.o")"

# The oracle determines whether GAS permits or diagnoses this deliberately
# unaligned instruction. Do not silently change the expected result when
# GNU's semantics disagree; fail the strict differential in either case.
odd="$fixtures/loong-linux-odd-machine-instruction.S"
if "$gas" -mabi=lp64s -o "$work/odd.gnu.o" "$odd" >"$work/odd.gnu.log" 2>&1; then
  gnu_ok=1
else
  gnu_ok=0
fi
if "$MINIAS" --target=loongarch64 -mabi=lp64s -o "$work/odd.mini.o" "$odd" >"$work/odd.mini.log" 2>&1; then
  mini_ok=1
else
  mini_ok=0
fi
if [[ $gnu_ok != "$mini_ok" ]]; then
  echo "MINIAS_LOONG_MACHINE_ALIGNMENT=FAIL GNU=$gnu_ok MiniAS=$mini_ok" >&2
  cat "$work/odd.gnu.log" "$work/odd.mini.log" >&2
  exit 1
fi
if [[ $gnu_ok == 1 ]]; then
  "$objcopy" -O binary -j .text "$work/odd.gnu.o" "$work/odd.gnu.bin"
  "$objcopy" -O binary -j .text "$work/odd.mini.o" "$work/odd.mini.bin"
  cmp "$work/odd.gnu.bin" "$work/odd.mini.bin"
  gnu_align=$(get_text_alignment "$work/odd.gnu.o")
  mini_align=$(get_text_alignment "$work/odd.mini.o")
  if [[ -z $gnu_align || $gnu_align != "$mini_align" ]]; then
    echo "MINIAS_LOONG_MACHINE_ALIGNMENT=FAIL sh_addralign GNU=$gnu_align MiniAS=$mini_align" >&2
    exit 1
  fi
fi
echo "MINIAS_LOONG_MACHINE_ALIGNMENT=PASS kbuild_feature_probe=1byte odd_machine_GNU=$gnu_ok MiniAS=$mini_ok"
