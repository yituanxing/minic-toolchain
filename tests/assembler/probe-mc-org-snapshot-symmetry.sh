#!/usr/bin/env bash
# Adversarial MCLayout fixed-point: GNU `.org` targets, aliases, branch
# alternatives and .size anchors must observe one coherent Section-relative
# symbol snapshot, even when subsection source order is reversed.
set -Eeuo pipefail
: "${MINIAS:?MiniAS executable required}"

work=$(mktemp -d)
trap 'status=$?; if ((status != 0)); then echo "MINIAS_MC_ORG_SNAPSHOT_FAIL status=$status" >&2; test ! -f "$work/trace" || tail -50 "$work/trace" >&2; fi; rm -rf "$work"' EXIT

check_symbols() {
    local kind="$1"
    shift
    local name mini_value gnu_value
    for name in "$@"; do
        mini_value=$(readelf -sW "$work/${kind}-mini.o" | awk -v n="$name" '$NF==n {print $2 ":" $3}')
        gnu_value=$(readelf -sW "$work/${kind}-gnu.o" | awk -v n="$name" '$NF==n {print $2 ":" $3}')
        if [[ -z "$mini_value" || "$mini_value" != "$gnu_value" ]]; then
            echo "symbol ABI mismatch case=$kind name=$name mini=$mini_value GNU=$gnu_value" >&2
            return 1
        fi
    done
}

check_case() {
    local kind="$1" section="$2"
    as --64 -o "$work/${kind}-gnu.o" "$work/${kind}.s"
    MINIAS_TRACE_RELAYOUT=1 "$MINIAS" --target=x86_64 \
        -o "$work/${kind}-mini.o" "$work/${kind}.s" 2>"$work/trace"
    grep -q 'minias-fragment-graph:anchors_built=1.*reparse=0' "$work/trace"
    objcopy -j "$section" -O binary "$work/${kind}-mini.o" "$work/${kind}-mini.bin"
    objcopy -j "$section" -O binary "$work/${kind}-gnu.o" "$work/${kind}-gnu.bin"
    cmp "$work/${kind}-mini.bin" "$work/${kind}-gnu.bin"
}

# A backward branch width decision must move `after_branch` and `.org` in
# lockstep. Taking the old symbol value with the new branch width produces a
# spurious `.org backwards` error even though GNU keeps exactly two fill bytes.
# The .size end anchor follows the `.org` in subsection 0, with the target
# branch label in subsection 1; the body must grow without size drift.
cat >"$work/branch-size.s" <<'SRC'
.text
.subsection 0
.globl func
.type func,@function
func:
  jmp far
after_branch:
  .org after_branch + 2,0xa3
  .rept 384
    nop
  .endr
.size func,.-func
.subsection 1
far:
  ret
SRC
check_case branch-size .text
check_symbols branch-size func after_branch

# A function started in subsection 0 can end in subsection 1 of the SAME
# output `.text` Section. GNU accepts the cross-subsection `.size` endpoint;
# neither the parser nor layout should mistake a subsection for a Section.
cat >"$work/branch-cross-size.s" <<'SRC'
.text
.subsection 0
.globl crossed_func
.type crossed_func,@function
crossed_func:
  jmp crossed_far
crossed_anchor:
  .org crossed_anchor + 2,0xa3
  .rept 384
    nop
  .endr
.subsection 1
crossed_far:
  ret
.size crossed_func,.-crossed_func
SRC
check_case branch-cross-size .text
check_symbols branch-cross-size crossed_func crossed_anchor

# Output subsection numbers, not source order, define concatenation. The
# forward `.space` changes the base of the anchor's subsection, and a typed
# compound `.org` expression in subsection 2 depends on that exact base.
cat >"$work/reversed.s" <<'SRC'
.data
.subsection 1
anchor_mid:
  .byte 0xbb
.subsection 2
  .org anchor_mid + 24 + (anchor_mid-anchor_mid),0x72
.globl reversed_origin
reversed_origin:
  .byte 0xcc
.subsection 0
  .space span_end-span_begin,0x61
  .byte 0xaa
.section .rodata,"a"
span_begin:
  .byte 1,2,3,4,5,6,7,8
span_end:
SRC
check_case reversed .data
check_symbols reversed reversed_origin anchor_mid

# Multiple alias edges must be refreshed from the same subsection-adjusted
# anchor snapshot. Force the typed expression path with a self-difference.
cat >"$work/alias-subsection.s" <<'SRC'
.data
.subsection 0
  .space span_end-span_begin,0x51
anchor:
  .byte 0xaa
.set alias_1,anchor + 4
.set alias_2,alias_1 + 5
.subsection 1
  .org alias_2 + 24 + (alias_2-alias_2),0x73
.globl alias_origin
alias_origin:
  .byte 0x99
.section .rodata,"a"
span_begin:
  .byte 1,2,3,4,5,6,7,8
span_end:
SRC
check_case alias-subsection .data
check_symbols alias-subsection alias_origin alias_1 alias_2

echo 'MINIAS_MC_ORG_SNAPSHOT_SYMMETRY=PASS arch=x86_64 branch_and_org=gnu_exact reversed_subsections=gnu_exact aliases=gnu_exact cross_subsection_size=gnu_exact source_reparse=0'
