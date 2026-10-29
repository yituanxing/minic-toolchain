#!/usr/bin/env bash
# Persistent GNU .equ/.set aliases are a SymbolID dependency DAG, not a
# source reparse problem. This adversarial chain must be linear-time through
# MCLayout relayout and produce the same moving anchors as GNU GAS.
set -Eeuo pipefail
: "${MINIAS:?MiniAS executable required}"
: "${ARCH:?Target required}"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
trap 'echo "ALIAS_DAG_FAIL arch=$ARCH command=$BASH_COMMAND" >&2; test ! -f "$work/trace" || cat "$work/trace" >&2' ERR
case "$ARCH" in
  x86_64) mini=(--target=x86_64); gas=(as --64); copy=(objcopy)
    branch='je far'; nop='nop'; repeats=256;;
  riscv) mini=(--target=riscv); gas=(riscv64-linux-gnu-as -march=rv64i -mabi=lp64)
    copy=(riscv64-linux-gnu-objcopy)
    branch='beq a0,a1,far'; nop='addi x0,x0,0'; repeats=1200;;
  *) echo "unsupported ARCH=$ARCH" >&2; exit 2;;
esac

{
  printf '.text\n.globl entry\nentry:\n  %s\n' "$branch"
  printf '.equ alias_0000, far\n'
  for ((i=1;i<2048;i++)); do
    printf '.set alias_%04d, alias_%04d + 1\n' "$i" "$((i-1))"
  done
  printf '  .rept %s\n    %s\n  .endr\nfar:\n  %s\n' "$repeats" "$nop" "$nop"
  printf '  .long alias_2047 - entry\n'
} >"$work/dag.s"

MINIAS_TRACE_RELAYOUT=1 "$MINIAS" "${mini[@]}" \
  -o "$work/dag-mini.o" "$work/dag.s" 2>"$work/trace"
"${gas[@]}" -o "$work/dag-gnu.o" "$work/dag.s"
grep -q '^minias-fragment-relayout:inplace-passes=' "$work/trace"
grep -q '^minias-fragment-graph:anchors_built=1.*reparse=0$' "$work/trace"
"${copy[@]}" -j .text -O binary "$work/dag-mini.o" "$work/dag-mini.bin"
"${copy[@]}" -j .text -O binary "$work/dag-gnu.o" "$work/dag-gnu.bin"
cmp "$work/dag-mini.bin" "$work/dag-gnu.bin"
for name in alias_0000 alias_0128 alias_1024 alias_2047; do
  mini_value=$(readelf -sW "$work/dag-mini.o" | awk -v n="$name" '$NF == n { print $2 }')
  gnu_value=$(readelf -sW "$work/dag-gnu.o" | awk -v n="$name" '$NF == n { print $2 }')
  test -n "$mini_value" && test "$mini_value" = "$gnu_value"
done

cat >"$work/cycle.s" <<'SRC'
.text
.set alpha, beta + 1
.set beta, alpha + 1
.long alpha
SRC
if "$MINIAS" "${mini[@]}" -o "$work/cycle.o" \
   "$work/cycle.s" 2>"$work/cycle.err"; then
  echo 'MiniAS accepted cyclic SymbolID alias graph' >&2
  exit 1
fi
grep -q 'unresolved-set-target:' "$work/cycle.err"
test ! -e "$work/cycle.o"

cat >"$work/undefined.s" <<'SRC'
.text
.set alias, missing + 1
.long alias
SRC
if "$MINIAS" "${mini[@]}" -o "$work/undefined.o" \
   "$work/undefined.s" 2>"$work/undefined.err"; then
  echo 'MiniAS accepted undefined SymbolID alias graph' >&2
  exit 1
fi
grep -q 'unresolved-set-target:' "$work/undefined.err"
test ! -e "$work/undefined.o"

echo "MINIAS_MC_ALIAS_DAG=PASS arch=$ARCH edges=2048 GNU_bytes=exact SymbolID_values=4 source_reparse=0 cycles=rejected undefined=rejected"
