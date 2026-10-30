#!/usr/bin/env bash
# Original x86 Linux codegen flags must affect MiniC's machine instructions,
# not merely pass through the GNU -E source-frozen stage. No GCC C fallback.
set -Eeuo pipefail

root=$(cd "$(dirname "$0")/../../.." && pwd)
minic_cc=${1:-"$root/build/release/bin/minic-cc"}
minic=${2:-"$root/build/release/bin/minic"}
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

cc -E -P "$root/tests/compiler/c0/x86_kbuild_mitigations.c" -o "$work/input.i"
flags=(-fcf-protection=branch -mindirect-branch=thunk-extern
       -mfunction-return=thunk-extern -fpatchable-function-entry=16,16)

"$minic_cc" -S "$work/input.i" --target=x86_64-linux "${flags[@]}" \
    -o "$work/minic.s"
"$minic" -S --target=x86_64-linux "${flags[@]}" \
    "$work/input.i" -o "$work/driver.s"
cmp "$work/minic.s" "$work/driver.s"
as --64 "$work/minic.s" -o "$work/minic.o"
readelf -rW "$work/minic.o" > "$work/relocs"
readelf -SW "$work/minic.o" > "$work/sections"
objdump -dr "$work/minic.o" > "$work/disasm"

for symbol in __x86_return_thunk __x86_indirect_thunk_r11 \
              __x86_indirect_thunk_rax; do
    if ! grep -Fq "$symbol" "$work/relocs"; then
        printf 'FAIL: x86 Kbuild mitigation relocation %s missing\n' "$symbol" >&2
        exit 1
    fi
done
if ! grep -Fq '__patchable_function_entries' "$work/sections" ||
   ! grep -Fq '.note.gnu.property' "$work/sections" ||
   ! grep -Fq 'endbr64' "$work/disasm"; then
    echo 'FAIL: Linux patchable function entry or IBT ELF contract missing' >&2
    exit 1
fi
if grep -Eq '[[:space:]]call[[:space:]]+\*|[[:space:]]jmp[[:space:]]+\*' "$work/minic.s" ||
   grep -Eq '^[[:space:]]+ret$' "$work/minic.s"; then
    echo 'FAIL: x86 thunk-extern output still has raw indirect control flow' >&2
    exit 1
fi

# Runtime stubs preserve the ABI for the differential fixture. They are not
# security thunk implementations; the assembly/relocation gate above verifies
# that production code references the Linux-owned external thunk symbols.
printf '%s\n' \
    '.text' \
    '.globl __x86_return_thunk' \
    '__x86_return_thunk: ret' \
    '.globl __x86_indirect_thunk_r11' \
    '__x86_indirect_thunk_r11: jmp *%r11' \
    '.globl __x86_indirect_thunk_rax' \
    '__x86_indirect_thunk_rax: jmp *%rax' \
    '.section .note.GNU-stack,"",@progbits' \
    | as --64 -o "$work/linux-thunks.o" -
cc -no-pie -Wl,-z,noexecstack "$work/minic.o" "$work/linux-thunks.o" \
    -o "$work/runtime"
"$work/runtime"

"$minic_cc" -S "$work/input.i" --target=x86_64-linux -o "$work/plain.s"
if grep -Fq '__x86_return_thunk' "$work/plain.s" ||
   grep -Fq 'endbr64' "$work/plain.s" ||
   grep -Fq '__patchable_function_entries' "$work/plain.s"; then
    echo 'FAIL: default x86 codegen changed without machine options' >&2
    exit 1
fi
for bad in -mindirect-branch=thunk -fpatchable-function-entry=17,16; do
    if "$minic_cc" -S "$work/input.i" --target=x86_64-linux "$bad" \
        -o "$work/invalid.s" >/dev/null 2>&1; then
        echo "FAIL: unsupported x86 option admitted: $bad" >&2
        exit 1
    fi
done
if "$minic_cc" -S "$work/input.i" --target=aarch64-linux -fcf-protection=branch \
    -o "$work/invalid-aarch64.s" >/dev/null 2>&1; then
    echo 'FAIL: x86 mitigation option leaked into AArch64' >&2
    exit 1
fi
if "$minic" -c --target=rv64-linux -fcf-protection=branch \
    "$work/input.i" -o "$work/invalid-rv64.o" >/dev/null 2>&1; then
    echo 'FAIL: staged compile mode silently ignored machine codegen options' >&2
    exit 1
fi
printf '%s\n' 'PASS: x86 Kbuild mitigation options, GNU AS/ELF, external thunks and runtime'
