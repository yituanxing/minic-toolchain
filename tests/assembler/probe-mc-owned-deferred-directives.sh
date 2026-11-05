#!/usr/bin/env bash
# Common MCLayout contract: complicated GNU .org/.fill/.space expressions
# own stable typed fragments across every target; never re-enter lexical
# source parsing after symbol anchors or .set values change.
set -Eeuo pipefail
: "${MINIAS:?MiniAS required}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
trap 'echo "OWNED_DEFERRED_FAIL command=$BASH_COMMAND" >&2; test ! -f "$work/trace" || cat "$work/trace" >&2' ERR

cat >"$work/positive.s" <<'SRC'
.data
.globl deferred_layout_contract
deferred_layout_contract:
  .byte 0x11
base:
  .byte 0x22,0x33,0x44
done:
  .space ((done-base)*2) + (2^1), (0x40 | 0x15)
  .fill 3*(2+1), (1 << 1), 0x1234
  .set snap, 3
  .space snap*2, 0x7d
  .set snap, 91
  .org (. + ((4 << 1) + 1)), 0xa4
  .fill ((done-base) > 0) + 1,4,0x11223344
SRC

# GNU x86 output is an independent, exact oracle for this target-neutral
# data representation. None of the directives emit machine instructions.
as --64 -o "$work/gnu.o" "$work/positive.s"
objcopy --only-section=.data -O binary "$work/gnu.o" "$work/gnu.data"

for target in x86_64 riscv aarch64 loongarch64; do
    MINIAS_TRACE_RELAYOUT=1 "$MINIAS" --target="$target" \
        -o "$work/$target.o" "$work/positive.s" 2>"$work/trace"
    grep -Eq '^minias-mcexpr-variable:typed=[1-9][0-9]* fallback=0$' \
        "$work/trace"
    grep -q 'minias-fragment-graph:anchors_built=1' "$work/trace"
    # GNU binutils' host objcopy cannot consume every cross-ELF machine.
    # readelf can; hex section dump gives identical bytes on all LE targets.
    readelf -x .data "$work/$target.o" | sed -n '/^  0x/p' \
        >"$work/$target.hex"
    readelf -x .data "$work/gnu.o" | sed -n '/^  0x/p' \
        >"$work/gnu.hex"
    cmp "$work/$target.hex" "$work/gnu.hex"
    echo "MINIAS_OWNED_DEFERRED=PASS target=$target GNU_data=exact source_fallback=0"
done

# Invalid symbol or numeric expressions must fail closed, rather than
# silently materializing a zero-sized source-parser fallback.
for bad in '  .space missing_symbol+2,0' \
           '  .fill (1/0),2,0x11' \
           '  .org (. - ((2+1)*2)),0x11' \
           '  .space (1 << 64),0x11'; do
    printf '.data\n.byte 1\n%s\n' "$bad" >"$work/bad.s"
    if "$MINIAS" --target=x86_64 -o "$work/bad.o" "$work/bad.s" \
       >"$work/bad.out" 2>&1; then
        echo "Invalid deferred GNU expression succeeded: $bad" >&2
        exit 1
    fi
done
echo 'MINIAS_OWNED_DEFERRED_INVALID=PASS undefined/divzero/backwards/shift'
