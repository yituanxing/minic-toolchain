#!/bin/sh
set -eu

minic_cc=${MINIC_CC:-build/debug/bin/minic-cc}
host_cc=${HOST_CC:-cc}
source_root=${MINIC_SOURCE_ROOT:-.}
workdir=$(mktemp -d)
trap 'rm -rf -- "$workdir"' EXIT HUP INT TERM

"$host_cc" -E -P "$source_root/tests/target/x86_64_linux_efi_kernel_transfer.c" \
    -o "$workdir/efi.i"
"$minic_cc" -S "$workdir/efi.i" --target=x86_64-linux \
    -fshort-wchar -funsigned-char -fno-PIE -o "$workdir/efi.s"
"$host_cc" -c -x assembler "$workdir/efi.s" -o "$workdir/efi.o"
objdump -d "$workdir/efi.o" > "$workdir/efi.dump"
if ! grep -Fq 'jmp *%r11' "$workdir/efi.s" ||
   ! grep -Eq 'movq .*?, %rsi' "$workdir/efi.s" ||
   ! grep -Eq 'jmp\s+\*%r11' "$workdir/efi.dump"; then
    printf 'FAIL: EFI kernel transfer lost RSI boot input or real indirect jump\n' >&2
    exit 1
fi

for mode in 1 2 3; do
    "$host_cc" -E -P -DMINIC_EFI_TRANSFER_NEGATIVE="$mode" \
        "$source_root/tests/target/x86_64_linux_efi_kernel_transfer.c" \
        -o "$workdir/efi-neg-$mode.i"
    if "$minic_cc" -S "$workdir/efi-neg-$mode.i" --target=x86_64-linux \
        -fshort-wchar -funsigned-char -fno-PIE \
        -o "$workdir/efi-neg-$mode.s" >/dev/null 2>&1; then
        printf 'FAIL: invalid EFI kernel transfer variant %s accepted\n' "$mode" >&2
        exit 1
    fi
done
printf 'X86_EFI_KERNEL_TRANSFER=PASS GNU_AS=1 RSI_boot=1 indirect_jump=1 strict_negative=3\n'
