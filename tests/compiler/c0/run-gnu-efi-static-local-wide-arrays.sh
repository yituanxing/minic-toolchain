#!/usr/bin/env bash
set -euo pipefail

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
mini=${MINIC_CC:-"$root/build/debug/bin/minic-cc"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-efi-static-local-wide
source_file="$root/tests/compiler/c0/gnu_efi_static_local_wide_arrays.c"
negative_file="$root/tests/compiler/c0/gnu_efi_static_local_wide_negative.c"

mkdir -p "$work"

# Use the actual Linux EFI -fshort-wchar language mode. The .i freezes
# preprocessing, not wchar_t's per-invocation type or ABI.
"$host_cc" -std=gnu11 -Wall -Wextra -Werror -fshort-wchar \
    -c "$source_file" -o "$work/gnu-positive.o"
"$host_cc" -E -P -std=gnu11 -fshort-wchar -x c \
    "$source_file" -o "$work/positive.i"

for arch in x86_64-linux riscv64-linux riscv32-linux aarch64-linux loongarch64-linux; do
    env MINIC_CORE_IR=strict "$mini" "--target=$arch" -fshort-wchar \
        -S "$work/positive.i" -o "$work/$arch.s"
    grep -Fq 'efi_vendor_name:' "$work/$arch.s"
    grep -Eq '^\.size \.Lminic_string_0,[[:space:]]*40$' "$work/$arch.s"
    grep -Eq '^\.size __minic_static_local_[0-9]+_[0-9]+,[[:space:]]*6$' "$work/$arch.s"
    printf 'EFI_STATIC_WIDE_POSITIVE=PASS target=%s inferred/braced/concat/escapes\n' "$arch"
done

# Check emitted x86 ELF UTF-16LE data against GCC, not just that -S succeeds.
"$host_cc" -c -x assembler "$work/x86_64-linux.s" -o "$work/minic-positive.o"
objcopy -O binary --only-section=.rodata "$work/gnu-positive.o" "$work/gnu-rodata.bin"
objcopy -O binary --only-section=.rodata "$work/minic-positive.o" "$work/minic-rodata.bin"
cmp -n 40 "$work/gnu-rodata.bin" "$work/minic-rodata.bin"
printf '%s\n' 'EFI_STATIC_WIDE_BYTES=PASS GNU_UTF16LE first_40_bytes'

# Native GCC rejects the same bad source; MiniC must not silently accept it.
for bad in BAD_WIDE_TO_NARROW BAD_NARROW_TO_WIDE BAD_WIDE_TO_WRONG_WIDTH BAD_WIDE_BRACED_TO_NARROW; do
    if "$host_cc" -std=gnu11 -Werror -fshort-wchar -D"$bad" \
        -c "$negative_file" -o "$work/invalid-gnu.o" \
        >"$work/$bad.gnu.stdout" 2>"$work/$bad.gnu.stderr"; then
        echo "FAIL GNU accepted negative case $bad" >&2
        exit 1
    fi
    "$host_cc" -E -P -std=gnu11 -D"$bad" -x c \
        "$negative_file" -o "$work/$bad.i"
    if env MINIC_CORE_IR=strict "$mini" --target=x86_64-linux -fshort-wchar \
        -S "$work/$bad.i" -o "$work/$bad.s" \
        >"$work/$bad.minic.stdout" 2>"$work/$bad.minic.stderr"; then
        echo "FAIL MiniC accepted GNU-invalid case $bad" >&2
        exit 1
    fi
    grep -Fq 'error:' "$work/$bad.minic.stderr"
    printf 'EFI_STATIC_WIDE_NEGATIVE=PASS case=%s\n' "$bad"
done

# Canonical 32-bit wchar_t is incompatible with EFI's unsigned-short array.
if "$host_cc" -std=gnu11 -Werror -fno-short-wchar \
    -c "$source_file" -o "$work/invalid-gnu-default.o" \
    >"$work/invalid-default.gnu.stdout" 2>"$work/invalid-default.gnu.stderr"; then
    echo 'FAIL GNU default wchar unexpectedly accepted EFI array' >&2
    exit 1
fi
for arch in x86_64-linux riscv64-linux riscv32-linux aarch64-linux loongarch64-linux; do
    if env MINIC_CORE_IR=strict "$mini" "--target=$arch" -fno-short-wchar \
        -S "$work/positive.i" -o "$work/$arch.invalid.s" \
        >"$work/$arch.invalid.stdout" 2>"$work/$arch.invalid.stderr"; then
        echo "FAIL MiniC accepted 32-bit wchar EFI array for $arch" >&2
        exit 1
    fi
    grep -Fq 'string initializer requires a compatible static local array element type' \
        "$work/$arch.invalid.stderr"
    printf 'EFI_STATIC_WIDE_TARGET_MISMATCH=PASS target=%s default_wchar32\n' "$arch"
done
