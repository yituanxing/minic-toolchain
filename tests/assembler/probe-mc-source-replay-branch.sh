#!/usr/bin/env bash
# Force the rare full SourceFrame replay: a branch grows while .set depends
# on its original lexical dot and the input includes conditional macro/rept.
# Compare the post-growth absolute symbol and emitted bytes with GNU GAS.
set -Eeuo pipefail
: "${MINIAS:?MiniAS executable required}"
: "${ARCH:?Target required}"

case "$ARCH" in
  x86_64)
    command -v as >/dev/null
    command -v objcopy >/dev/null
    mini=(--target=x86_64)
    gnu=(as --64)
    copy=(objcopy)
    branch='je far'
    insn='nop'
    repeats=256
    expected_size=6
    ;;
  riscv)
    mini=(--target=riscv -march=rv64i -mabi=lp64)
    if command -v riscv64-linux-gnu-as >/dev/null &&
       command -v riscv64-linux-gnu-objcopy >/dev/null; then
      gnu=(riscv64-linux-gnu-as -march=rv64i -mabi=lp64)
      copy=(riscv64-linux-gnu-objcopy)
    elif [[ "${MINIAS_ALLOW_MISSING_GNU:-0}" == 1 ]]; then
      gnu=()
      copy=()
    else
      echo 'RV GNU cross assembler/objcopy required for CI oracle' >&2
      exit 1
    fi
    branch='beq a0,a1,far'
    insn='addi x0,x0,0'
    repeats=1200
    expected_size=8
    ;;
  *) echo "unsupported ARCH=$ARCH" >&2; exit 2;;
esac

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
trap 'echo "MINIAS_MC_SOURCE_REPLAY=FAIL arch=$ARCH command=$BASH_COMMAND" >&2' ERR
{
  printf '.text\n.globl entry\nentry:\n  %s\n' "$branch"
  printf '%s\n' '.set branch_span, . - entry'
  printf '%s\n' '.if . - .' '  .error "unreachable conditional"' '.else'
  printf '%s\n' '.macro pad count' '  .rept \count'
  printf '    %s\n' "$insn"
  printf '%s\n' '  .endr' '.endm' '.endif'
  printf 'pad %d\nfar:\n  %s\n.data\n  .long branch_span\n' "$repeats" "$insn"
} >"$work/replay.s"

if ! MINIAS_TRACE_RELAYOUT=1 timeout 20s "$MINIAS" "${mini[@]}" \
    -o "$work/mini.o" "$work/replay.s" 2>"$work/trace"; then
  echo "MINIAS_MC_SOURCE_REPLAY=FAIL arch=$ARCH assembler_failure_or_timeout" >&2
  sed -n '1,30p' "$work/trace" >&2
  exit 1
fi
mini_span=$(readelf -sW "$work/mini.o" | awk '$NF == "branch_span" { print $2 }')
test -n "$mini_span"
test "$((16#$mini_span))" -eq "$expected_size"
oracle='NOT_TESTED:explicit_local_no_cross_GNU'
if ((${#gnu[@]})); then
  "${gnu[@]}" -o "$work/gnu.o" "$work/replay.s"
  "${copy[@]}" -j .text -O binary "$work/mini.o" "$work/mini.bin"
  "${copy[@]}" -j .text -O binary "$work/gnu.o" "$work/gnu.bin"
  cmp "$work/mini.bin" "$work/gnu.bin"
  "${copy[@]}" -j .data -O binary "$work/mini.o" "$work/mini.data.bin"
  "${copy[@]}" -j .data -O binary "$work/gnu.o" "$work/gnu.data.bin"
  cmp "$work/mini.data.bin" "$work/gnu.data.bin"
  gnu_span=$(readelf -sW "$work/gnu.o" | awk '$NF == "branch_span" { print $2 }')
  test "$mini_span" = "$gnu_span"
  oracle='exact'
fi
grep -q 'minias-fragment-graph:anchors_built=1' "$work/trace"
grep -qx 'minias-source-reparse:pass=1' "$work/trace"
if grep -Eq '^minias-source-reparse:pass=([2-9]|[1-9][0-9]+)$' "$work/trace"; then
  echo "unexpected repeated SourceFrame replay arch=$ARCH" >&2
  exit 1
fi

echo "MINIAS_MC_SOURCE_REPLAY=PASS arch=$ARCH replay=observed branch_size=$expected_size GNU_bytes=$oracle conditional_macro_rept=1 dot_set=exact"
