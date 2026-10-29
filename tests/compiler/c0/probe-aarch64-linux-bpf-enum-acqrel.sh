#!/bin/sh
# Exact Linux BPF enum-width acquire/release + strict GNU constraint rejects.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
mini=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
src="$root/tests/compiler/c0/aarch64_linux_bpf_enum_acqrel.c"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
"${HOST_CC:-cc}" -std=gnu11 -E -P "$src" -o "$work/positive.i"
"$mini" --target=aarch64-linux -S "$work/positive.i" -o "$work/positive.s"
grep -Fq '  ldar w9, [x10]' "$work/positive.s"
grep -Fq '  stlr w9, [x10]' "$work/positive.s"
for target in x86_64 loongarch64; do
    if "$mini" --target="$target-linux" -S "$work/positive.i" \
        -o "$work/foreign.s" >/dev/null 2>&1; then
        echo "BPF_ENUM_ACQREL=FAIL foreign=$target" >&2; exit 1
    fi
done
sed 's/"Q" (value->state)/"m" (value->state)/' \
    "$src" > "$work/bad-memory.c"
sed 's/"=r" (result)/"=m" (result)/' \
    "$src" > "$work/bad-output.c"
sed 's/ldar %w0/ldaxr %w0/' \
    "$src" > "$work/bad-opcode.c"
for variant in bad-memory bad-output bad-opcode; do
    "${HOST_CC:-cc}" -std=gnu11 -E -P "$work/$variant.c" \
        -o "$work/$variant.i"
    if "$mini" --target=aarch64-linux -S "$work/$variant.i" \
        -o "$work/$variant.s" >/dev/null 2>&1; then
        echo "BPF_ENUM_ACQREL=FAIL unsafe=$variant" >&2; exit 1
    fi
done
"${HOST_CC:-cc}" -std=gnu11 -E -P -DMINIC_BAD_LARGE_ENUM "$src" \
    -o "$work/bad-width.i"
if "$mini" --target=aarch64-linux -S "$work/bad-width.i" \
    -o "$work/bad-width.s" >/dev/null 2>&1; then
    echo 'BPF_ENUM_ACQREL=FAIL unsafe=wide-enum' >&2; exit 1
fi
if command -v aarch64-linux-gnu-gcc >/dev/null 2>&1; then
    aarch64-linux-gnu-gcc -c "$work/positive.s" -o "$work/minic.o"
    aarch64-linux-gnu-gcc -c "$src" -o "$work/gnu.o"
    echo 'BPF_ENUM_ACQREL=PASS enum32=2 foreign=2 negative=4 GAS=PASS'
else
    echo 'BPF_ENUM_ACQREL=PASS enum32=2 foreign=2 negative=4 GAS=SKIP'
fi
