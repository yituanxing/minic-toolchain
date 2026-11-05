#!/usr/bin/env bash
# Compare the complete ELF output (or the refusal to emit it) of two MiniAS
# binaries against one fixed set of source/target regression fixtures.
# This is deliberately independent of the GNU-oracle and A0 test suites.
set -Eeuo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
: "${MINIAS:=$project_root/build/release/bin/minic-as}"
: "${MINIAS_BEFORE:?set MINIAS_BEFORE to the pre-change minic-as binary}"
test -x "$MINIAS"
test -x "$MINIAS_BEFORE"

work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

cases=(
    'x86_64 cfi-source-section-clean-gnu.S'
    'x86_64 cfi-source-section-collision-gnu.S'
    'x86_64 cfi-source-section-unaligned-gnu.S'
    'x86_64 reloc-source-dot-gnu.S'
    'x86_64 reloc-source-no-symbol-gnu.S'
    'x86_64 source-context-composite-gnu.S'
    'x86_64 source-macro-lifetime-altvararg-gnu.S'
    'x86_64 source-macro-lifetime-bad-qualifier.S'
    'riscv64 riscv-gcc-absolute-symbol-relocs.S'
    'riscv64 rv-branch-binding-gnu.S'
    'riscv64 source-context-composite-gnu.S'
    'riscv32 rv-branch-binding-gnu.S'
    'aarch64 aarch64-branch-binding-gnu.S'
    'aarch64 aarch64-branch-binding-strong-gnu.S'
    'aarch64 aarch64-ldr-binding-gnu.S'
    'aarch64 aarch64-ldr-binding-strong-gnu.S'
    'aarch64 source-context-composite-gnu.S'
    'loongarch64 loong-expr-dot-anchor-gnu.S'
    'loongarch64 loong-linux-altinstructions-data-widths.S'
    'loongarch64 loong-linux-odd-machine-instruction.S'
    'loongarch64 cfi-loong-short-relax.S'
    'loongarch64 source-context-composite-gnu.S'
)

matching_objects=0
matching_rejections=0
for i in "${!cases[@]}"; do
    read -r target fixture <<<"${cases[$i]}"
    input="$project_root/tests/assembler/fixtures/$fixture"
    before="$work/$i.before.o"
    after="$work/$i.after.o"
    before_status=0
    after_status=0
    "$MINIAS_BEFORE" --target="$target" -o "$before" "$input" \
        >"$work/$i.before.out" 2>&1 || before_status=$?
    "$MINIAS" --target="$target" -o "$after" "$input" \
        >"$work/$i.after.out" 2>&1 || after_status=$?

    if (( before_status == 0 && after_status == 0 )); then
        if ! cmp -s -- "$before" "$after"; then
            echo "byte mismatch: $target/$fixture" >&2
            cmp -- "$before" "$after" >&2 || true
            exit 1
        fi
        ((matching_objects += 1))
    elif (( before_status != 0 && after_status != 0 )) && \
         ! test -e "$before" && ! test -e "$after"; then
        ((matching_rejections += 1))
    else
        echo "behavior mismatch: $target/$fixture (before=$before_status after=$after_status)" >&2
        echo 'before:' >&2
        sed -n '1,8p' "$work/$i.before.out" >&2
        echo 'after:' >&2
        sed -n '1,8p' "$work/$i.after.out" >&2
        exit 1
    fi
done

echo "MINIAS_BEFORE_AFTER=PASS cases=${#cases[@]} objects=$matching_objects both_reject=$matching_rejections mismatch=0"
