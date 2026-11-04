#!/usr/bin/env bash
# P46: five-target source acceptance and a real GNU assembler/linker/native
# byte-image oracle for the x86 machine aggregate-copy transport. Reuse a
# pinned original Linux LoongArch GNU inline-asm fixture as a text barrier.
set -Eeuo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
minic=${1:-"$root/build/debug/bin/minic-cc"}
cc=${HOST_CC:-gcc}
work=$(mktemp -d)
trap 'rm -r -- "$work"' EXIT
die() { printf 'P46_ORACLE=FAIL %s\n' "$*" >&2; exit 1; }

test -x "$minic" || die "missing MiniC compiler: $minic"
source="$root/tests/compiler/c0/p46_oracle_aggregate_copy.c"
"$cc" -E -P -w -std=gnu11 -x c "$source" -o "$work/copy.i" \
    || die 'host preprocessing'

targets=(riscv32-linux riscv64-linux x86_64-linux aarch64-linux loongarch64-linux)
for target in "${targets[@]}"; do
    "$minic" --target="$target" -S "$work/copy.i" \
        -o "$work/copy.$target.s" \
        || die "target=$target failed aggregate source emission"
    test -s "$work/copy.$target.s" \
        || die "target=$target generated empty assembly"
done

# Each byte extent is checked by the native executable, including a copy
# into itself, the entire result image, and two unmodified frame sentinels.
"$cc" -O0 -fno-PIE -no-pie -S "$work/copy.i" -o "$work/gnu.s" \
    || die 'GNU C assembly'
"$cc" -no-pie -c -x assembler "$work/gnu.s" -o "$work/gnu.o" \
    || die 'GNU C assembler'
"$cc" -no-pie -c -x assembler "$work/copy.x86_64-linux.s" \
    -o "$work/minic.o" || die 'MiniC GNU assembler'
"$cc" -no-pie "$work/gnu.o" -o "$work/gnu.elf" \
    || die 'GNU C linker'
"$cc" -no-pie "$work/minic.o" -o "$work/minic.elf" \
    || die 'MiniC GNU linker'
"$work/gnu.elf" > "$work/gnu.out" || die 'GNU C native runtime'
"$work/minic.elf" > "$work/minic.out" || die 'MiniC native runtime'
cmp -s "$work/gnu.out" "$work/minic.out" \
    || die 'GNU/MiniC native output mismatch'
grep -Fxq 'P46_AGGREGATE_COPY=PASS' "$work/minic.out" \
    || die 'invalid native success marker'

# Keep the actual Linux LoongArch user-origin ASM byte stream untouched.
# This catches a text compactor that accidentally forwards/rewrites across
# the compiler's inline-ASM barrier, even without cross GAS or QEMU.
loong="$root/tests/compiler/c0/loongarch64_linux_final_opaque_asm.c"
"$cc" -E -P -w -std=gnu11 -x c "$loong" -o "$work/loong.i" \
    || die 'original LoongArch Linux preprocessing'
"$minic" --target=loongarch64-linux -S "$work/loong.i" \
    -o "$work/loong.s" || die 'original LoongArch Linux ASM admission'
for native in 'ibar 0' 'dbar 0' 'break 1'; do
    grep -Fq "$native" "$work/loong.s" \
        || die "LoongArch lost native inline ASM instruction: $native"
done

baseline=${P46_BASELINE_MINIC_CC:-}
if test -n "$baseline"; then
    test -x "$baseline" || die "baseline compiler missing: $baseline"
    "$baseline" --target=loongarch64-linux -S "$work/loong.i" \
        -o "$work/loong-baseline.s" || die 'Loong baseline emission'
    cmp -s "$work/loong-baseline.s" "$work/loong.s" \
        || die 'original LoongArch user ASM drift against P45.1'
    "$baseline" --target=x86_64-linux -S "$work/copy.i" \
        -o "$work/x86-baseline.s" || die 'x86 baseline emission'
    baseline_bytes=$(wc -c < "$work/x86-baseline.s")
    current_bytes=$(wc -c < "$work/copy.x86_64-linux.s")
    if (( current_bytes * 2 >= baseline_bytes )); then
        die "large copy assembly not compacted old=$baseline_bytes new=$current_bytes"
    fi
    printf 'P46_ORACLE_BASELINE=PASS x86_old_bytes=%s x86_new_bytes=%s Loong_native_byte_equal=1\n' \
        "$baseline_bytes" "$current_bytes"
fi

printf '%s\n' 'P46_ORACLE=PASS five_target_assembly=5 copy_sizes=9 max_bytes=16384 GNU_AS=2 GNU_LD=2 GNU_native=1 MiniC_native=1 same_source=1 self_copy=1 byte_guards=1 Loong_original_asm=1'
