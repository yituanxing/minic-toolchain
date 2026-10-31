#!/usr/bin/env bash
set -Eeuo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail() { echo "LOONG_IOCSR_WRAPPER=FAIL $*" >&2; exit 1; }

test -x "$minic" || fail 'MiniC compiler missing'
"${HOST_CC:-cc}" -E -P -std=gnu11 -x c \
    "$root/tests/compiler/c0/loongarch64_linux_iocsr_wrapper_alias.c" \
    -o "$work/wrappers.i"
"$minic" --target=loongarch64-linux -fshort-wchar -funsigned-char \
    -fno-PIE -fPIE -mabi=lp64s -S "$work/wrappers.i" \
    -o "$work/wrappers.s" || fail 'same GNU wrapper preprocessed C compile'
for mnemonic in iocsrrd.d iocsrrd.w iocsrwr.d iocsrwr.w; do
    test "$(grep -Fc "  $mnemonic " "$work/wrappers.s")" -eq 1 \
        || fail "missing or duplicated direct $mnemonic ISA operation"
done
if grep -Eq '^[[:space:]]+bl[[:space:]]+__iocsr(rd|wr)_[dw]' \
    "$work/wrappers.s"; then
    fail 'GNU extern-inline wrapper remains an unresolved ELF call'
fi
cross=${LOONGARCH64_CC:-loongarch64-linux-gnu-gcc-13}
if command -v "$cross" >/dev/null 2>&1; then
    "$cross" -c -x assembler "$work/wrappers.s" -o "$work/wrappers.o" \
        || fail 'GNU LoongArch assembler rejected IOCSR instructions'
    nm=${LOONGARCH64_NM:-loongarch64-linux-gnu-nm}
    command -v "$nm" >/dev/null 2>&1 || fail 'GNU LoongArch nm missing'
    "$nm" -u "$work/wrappers.o" > "$work/undefined.txt" \
        || fail 'GNU LoongArch nm failed'
    if grep -Eq '__iocsr(rd|wr)_[dw]' "$work/undefined.txt"; then
        fail 'GNU assembled ELF has unresolved IOCSR wrapper'
    fi
    echo 'LOONG_IOCSR_WRAPPER_GNU_ELF=PASS'
else
    echo 'LOONG_IOCSR_WRAPPER_GNU_ELF=SKIP cross GNU assembler unavailable'
fi
echo 'LOONG_IOCSR_WRAPPER=PASS direct_isa=4 no_external_elf_call=4'
