#!/usr/bin/env bash
# Exact four-LP64-ISA GNU/MiniC execution of wide Core pointer offsets.
# No target may silently skip: every shard compares its own GNU reference.
set -Eeuo pipefail
target=${1:?x86_64, riscv64, aarch64 or loongarch64}
mini=${2:?MiniC executable}
repo=$(cd "$(dirname "$0")/../../.." && pwd)
source="$repo/tests/compiler/c0/wide_pointer_offset_runtime.c"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail() { printf 'WIDE_POINTER_OFFSET=FAIL target=%s stage=%s\n' "$target" "$1" >&2; exit 1; }

case "$target" in
    x86_64) gcc=gcc; qemu= ;;
    riscv64) gcc=riscv64-linux-gnu-gcc; qemu=qemu-riscv64 ;;
    aarch64) gcc=aarch64-linux-gnu-gcc; qemu=qemu-aarch64 ;;
    loongarch64) gcc=loongarch64-linux-gnu-gcc-13; qemu=qemu-loongarch64 ;;
    *) fail 'UNKNOWN_TARGET' ;;
esac
test -x "$mini" || fail 'NO_MINIC'
command -v "$gcc" >/dev/null || fail 'NO_GNU_C'
if [[ -n "$qemu" ]]; then
    command -v "$qemu" >/dev/null || fail 'NO_QEMU'
fi
"$gcc" -std=gnu11 -E -P "$source" -o "$work/fixture.i" ||
    fail 'GNU_PREPROCESS'
"$mini" --target="$target-linux" -S "$work/fixture.i" -o "$work/minic.s" ||
    fail 'MINIC_CORE_OR_MACHINE_SELECTION'
test -s "$work/minic.s" || fail 'MINIC_EMPTY_ASM'
"$gcc" -static -O0 -std=gnu11 "$source" -o "$work/reference.elf" ||
    fail 'GNU_REFERENCE_LINK'
"$gcc" -static -x assembler "$work/minic.s" -o "$work/minic.elf" ||
    fail 'MINIC_GNU_AS_LINK'
for mode in reference minic; do
    result=0
    if [[ -n "$qemu" ]]; then
        timeout 35s "$qemu" "$work/$mode.elf" || result=$?
    else
        timeout 35s "$work/$mode.elf" || result=$?
    fi
    if [[ "$result" -ne 42 ]]; then
        printf 'WIDE_POINTER_OFFSET=FAIL target=%s mode=%s return=%d expected=42\n' \
            "$target" "$mode" "$result" >&2
        exit 1
    fi
    printf 'WIDE_POINTER_OFFSET_MODE=PASS target=%s mode=%s exit=42\n' "$target" "$mode"
done
printf 'WIDE_POINTER_OFFSET=PASS target=%s signed_i128_negative=1 unsigned_i128=1 scaled_stride4=1 gnu_zero_stride=1 GNU_AS=1 native_or_QEMU_GNU_differential=1\n' "$target"
