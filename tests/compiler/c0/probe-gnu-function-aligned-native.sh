#!/usr/bin/env bash
# Compare GNU and MiniC function entry alignment at the actual ELF symbol.
# Other ISAs receive exact `.balign` source checks until QEMU/GNU is available.
set -Eeuo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
mini=${1:-${MINIC_CC:-"$root/build/release/bin/minic-cc"}}
target=${2:-x86_64}
fixture="$root/tests/compiler/c0/gnu_function_aligned.c"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail() { printf 'GNU_FUNCTION_ALIGNED=FAIL target=%s reason=%s\n' "$target" "$*" >&2; exit 1; }

test -x "$mini" || fail 'MiniC compiler absent'
cc -std=gnu11 -E -P -x c "$fixture" -o "$work/gnu.i"
"$mini" --target="$target-linux" -S "$work/gnu.i" -o "$work/minic.s" \
    || fail 'valid prefix/suffix/redeclaration attribute rejected'
test -s "$work/minic.s" || fail 'no MiniC assembly'

for alignment in 32 64 128; do
    grep -Fq ".balign $alignment" "$work/minic.s" ||
        fail "required explicit $alignment-byte entry directive absent"
done
grep -Eq '^\.weak linux_weak_aligned$' "$work/minic.s" ||
    fail 'GNU weak symbol linkage lost'

# All backends must reject dangerous non-power-of-two and out-of-budget
# alignment requests, without a partial assembler output.
sed 's/aligned(64)/aligned(3)/' "$fixture" >"$work/bad-three.c"
sed 's/aligned(64)/aligned(2097152)/' "$fixture" >"$work/bad-huge.c"
for variant in bad-three bad-huge; do
    cc -E -P -x c "$work/$variant.c" -o "$work/$variant.i"
    if "$mini" --target="$target-linux" -S "$work/$variant.i" \
            -o "$work/$variant.s" >"$work/$variant.stdout" \
            2>"$work/$variant.stderr"; then
        fail "invalid GNU entry alignment accepted: $variant"
    fi
    test ! -e "$work/$variant.s" || fail "partial output for $variant"
done

if [[ $target == x86_64 ]]; then
    cc -std=gnu11 -O0 -c -x c "$fixture" -o "$work/gnu.o"
    cc -c -x assembler "$work/minic.s" -o "$work/minic.o"
    for producer in gnu minic; do
        for mapping in 'aligned_prefix:64' 'aligned_suffix:32' \
                       'aligned_declaration:128' 'linux_weak_aligned:32'; do
            symbol=${mapping%%:*}
            alignment=${mapping##*:}
            address=$(nm -n "$work/$producer.o" | awk -v s="$symbol" '$3==s {print $1}')
            test -n "$address" || fail "$producer missing symbol $symbol"
            test $((16#$address % alignment)) -eq 0 ||
                fail "$producer misaligned symbol $symbol address=$address"
        done
    done
    cc "$work/minic.o" -o "$work/minic-runtime"
    cc -std=gnu11 -O0 "$fixture" -o "$work/gnu-runtime"
    "$work/minic-runtime" || fail 'MiniC native execution differs'
    "$work/gnu-runtime" || fail 'GNU native execution differs'
    printf '%s\n' 'GNU_FUNCTION_ALIGNED_NATIVE=PASS GNU_ELF=4 MiniC_ELF=4 runtime=2'
fi
printf 'GNU_FUNCTION_ALIGNED_SOURCE=PASS target=%s prefix=1 suffix=1 declaration=1 weak=1 negative=2\n' "$target"
