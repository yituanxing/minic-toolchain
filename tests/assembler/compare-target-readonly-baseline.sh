#!/usr/bin/env bash
# Opt-in independent old/new byte and rejection parity for the PreLayout,
# PostEmit and Symbol snapshot boundary. BASELINE_AS is built from the exact
# previous commit; both binaries read precisely the same source file.
set -Eeuo pipefail
: "${MINIAS:?Set path to the candidate assembler}"
: "${BASELINE_AS:?Set path to the previous-head assembler}"
test -x "$MINIAS"
test -x "$BASELINE_AS"

work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

fixtures=(
  'x86_64:source-context-composite-gnu.S'
  'x86_64:source-macro-lifetime-altvararg-gnu.S'
  'x86_64:cfi-source-section-clean-gnu.S'
  'x86_64:cfi-source-section-collision-gnu.S'
  'x86_64:cfi-source-section-unaligned-gnu.S'
  'x86_64:reloc-source-no-symbol-gnu.S'
  'x86_64:reloc-source-dot-gnu.S'
  'riscv:rv-branch-binding-gnu.S'
  'riscv:riscv-gcc-absolute-symbol-relocs.S'
  'riscv:source-context-composite-gnu.S'
  'riscv:cfi-source-section-clean-gnu.S'
  'aarch64:aarch64-ldr-binding-strong-gnu.S'
  'aarch64:aarch64-ldr-binding-gnu.S'
  'aarch64:aarch64-branch-binding-strong-gnu.S'
  'aarch64:aarch64-branch-binding-gnu.S'
  'aarch64:aarch64-readonly-finalization.S'
  'aarch64:source-context-composite-gnu.S'
  'loongarch64:loong-linux-altinstructions-data-widths.S'
  'loongarch64:loong-expr-dot-anchor-gnu.S'
  'loongarch64:loong-linux-uleb-kbuild-feature-probe.S'
  'loongarch64:loong-linux-odd-machine-instruction.S'
  'loongarch64:cfi-loong-short-relax.S'
  'loongarch64:source-context-composite-gnu.S'
  'x86_64:source-macro-lifetime-bad-qualifier.S'
  'x86_64:source-macro-lifetime-bad-body.S'
  'x86_64:source-macro-lifetime-unterminated.S'
)

equal=0
rejected=0
for i in "${!fixtures[@]}"; do
    IFS=: read -r target input <<<"${fixtures[i]}"
    source="tests/assembler/fixtures/$input"
    before="$work/$i.before.o"
    after="$work/$i.after.o"
    old_rc=0
    new_rc=0
    "$BASELINE_AS" --target="$target" -o "$before" "$source" \
        >"$work/$i.before.log" 2>&1 || old_rc=$?
    "$MINIAS" --target="$target" -o "$after" "$source" \
        >"$work/$i.after.log" 2>&1 || new_rc=$?
    if (( old_rc == 0 && new_rc == 0 )); then
        if ! cmp -s "$before" "$after"; then
            echo "changed object: $target $input" >&2
            exit 1
        fi
        ((equal+=1))
    elif (( old_rc != 0 && new_rc != 0 )) &&
         [[ ! -e "$before" && ! -e "$after" ]]; then
        ((rejected+=1))
    else
        echo "changed acceptance or partial object: $target $input old=$old_rc new=$new_rc" >&2
        exit 1
    fi
done
if (( equal < 10 || rejected < 2 )); then
    echo "insufficient old/new coverage: equal=$equal rejected=$rejected" >&2
    exit 1
fi
echo "MINIAS_TARGET_READONLY_PARITY=PASS fixtures=${#fixtures[@]} objects=$equal same_reject=$rejected targets=4"
