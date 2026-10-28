#!/usr/bin/env bash
# Linux 6.6 alternative_has_cap_unlikely(), *actual* one-input asm goto.
# The original assembler table, patched branch and Core C label are tested
# together. This intentionally does not claim to execute Linux's patcher.
set -Eeuo pipefail
minic="${1:?expected minic-cc}"
test -x "$minic"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
source="$root/tests/compiler/c0/aarch64-linux-cap-alternative.c"
cc -E -P -x c "$source" -o "$work/original.i"
"$minic" --target=aarch64-linux -S "$work/original.i" -o "$work/mini.s"
test -s "$work/mini.s"

# Preserve the Linux boot-time patch table, actual feature ID and real CFG
# edge. A fallthrough NOP is not an equivalent replacement for asm goto.
grep -Fq '.pushsection .altinstructions,"a"' "$work/mini.s"
grep -Fq ' .word 661b - .' "$work/mini.s"
grep -Fq ' .word 663f - .' "$work/mini.s"
grep -Fq ' .hword 38' "$work/mini.s"
grep -Fq ' .word alt_cb_patch_nops- .' "$work/mini.s"
grep -Fq ' .hword (1 << 15) | (38)' "$work/mini.s"
grep -Fq ' .byte 662b-661b' "$work/mini.s"
grep -Fq ' .byte 664f-663f' "$work/mini.s"
grep -Fq '.subsection 1' "$work/mini.s"
grep -Fq '.org' "$work/mini.s"
grep -Eq 'b[[:space:]]+\.Lmini_arm_f[0-9]+_b[0-9]+' "$work/mini.s"
test "$(grep -Fc '.pushsection .altinstructions' "$work/mini.s")" -eq 2
test "$(grep -Ec $'^\tb\t\\.Lmini_arm_f[0-9]+_b[0-9]+' "$work/mini.s")" -eq 2

# Neither an unknown alternative instruction nor changed GNU constraints
# may silently fall back into a generic Core opaque instruction.
while IFS='|' read -r label before after; do
    test -n "$label" || continue
    sed "s/$before/$after/g" "$work/original.i" >"$work/$label.i"
    if cmp -s "$work/original.i" "$work/$label.i"; then
        echo "invalid alternative negative fixture: $label" >&2; exit 1
    fi
    if "$minic" --target=aarch64-linux -S "$work/$label.i" \
        -o "$work/$label.s" >"$work/$label.log" 2>&1; then
        echo "unsafe AArch64 Linux alternative accepted: $label" >&2; exit 1
    fi
    test ! -e "$work/$label.s"
done <<'CASES'
wrong-cap-number|\.hword|\.word
wrong-section|altinstructions|otherinstructions
wrong-subsection|\.subsection 1|.subsection 2
wrong-old-insn|nop|yield
wrong-new-insn|b\\t%l|bl\\t%l
wrong-cap-name|cpucap|cpubad
wrong-goto-label|l_yes|l_bad
wrong-input-constraint|"i"|"r"
wrong-label-length|662b-661b|663b-661b
wrong-replacement-length|664f-663f|663f-664f
CASES

# Callback alternatives use a different PREL32 second relocation and an
# unconditional original branch. Their metadata and label must also be
# independently fail-closed (not just the unlikely/regular alternative).
while IFS='|' read -r label before after; do
    test -n "$label" || continue
    sed "s/$before/$after/g" "$source" >"$work/$label.c"
    if cmp -s "$source" "$work/$label.c"; then
        echo "invalid callback negative fixture: $label" >&2; exit 1
    fi
    cc -E -P -x c "$work/$label.c" -o "$work/$label.i"
    if "$minic" --target=aarch64-linux -S "$work/$label.i" \
        -o "$work/$label.s" >"$work/$label.log" 2>&1; then
        echo "unsafe AArch64 callback alternative accepted: $label" >&2
        exit 1
    fi
    test ! -e "$work/$label.s"
done <<'CASES'
wrong-callback|alt_cb_patch_nops|alt_cb_patch_bad
wrong-callback-reloc|- .|+ .
wrong-callback-prefix|1 << 15|1 << 14
wrong-callback-cap|hword|word
wrong-callback-section|altinstructions|wronginstructions
wrong-callback-label|l_no|l_bad
wrong-callback-old-branch|b\\t%l|cbz\\t%l
wrong-callback-len|.byte 664f-663f|.byte 664f-662b
CASES

# Linux calls the helper with cpucap as a formal inline parameter. Today
# that remains a deferred immediate: -S discovery is permitted, but *not*
# described as a complete linkable kernel implementation.
cc -E -P -DCHECK_DEFERRED -x c "$source" -o "$work/deferred.i"
"$minic" --target=aarch64-linux -S "$work/deferred.i" -o "$work/deferred.s"
grep -Fq 'MINIC_DEFERRED_ASM_IMMEDIATE requires inline specialization' "$work/deferred.s"
grep -Fq '.extern __minic_deferred_asm_immediate_' "$work/deferred.s"
test "$(grep -Fc 'MINIC_DEFERRED_ASM_IMMEDIATE' "$work/deferred.s")" -eq 2

# Another Linux alternative (zero-operand PAN .inst) is deliberately still
# unsupported. Ensure the exact asm-goto exception did not widen admission.
cc -E -P -DCHECK_UNSUPPORTED_PAN -x c "$source" -o "$work/pan.i"
if "$minic" --target=aarch64-linux -S "$work/pan.i" \
    -o "$work/pan.s" >"$work/pan.log" 2>&1; then
    echo 'unsupported PAN alternative accidentally accepted' >&2; exit 1
fi
test ! -e "$work/pan.s"

if [[ "${MINIC_SOURCE_ONLY:-0}" == 1 ]]; then
    echo 'AARCH64_CAP_ASM_GOTO=PASS source forms=2 metadata=12B edges=2 deferred=tracked negatives=19'
    exit 0
fi

command -v aarch64-linux-gnu-gcc >/dev/null
command -v qemu-aarch64 >/dev/null
aarch64-linux-gnu-gcc -static -O0 "$source" -o "$work/gnu.elf"
aarch64-linux-gnu-gcc -static -x assembler "$work/mini.s" -o "$work/mini.elf"
for compiler in gnu mini; do
    readelf -S "$work/$compiler.elf" | grep -Fq '.altinstructions'
    code=0
    timeout 45s qemu-aarch64 "$work/$compiler.elf" || code=$?
    if [[ "$code" != 42 ]]; then
        echo "AArch64 unpatched alternative $compiler exit=$code expected=42" >&2
        exit 1
    fi
done
echo 'AARCH64_CAP_ASM_GOTO=PASS GNU_QEMU_42=2 forms=2 metadata=12B negatives=19 deferred=tracked'
