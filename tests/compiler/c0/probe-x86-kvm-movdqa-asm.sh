#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/debug/bin/minic-cc"}
gnu=${GNU_CC:-gcc}
qemu=${QEMU_X86_64:-qemu-x86_64}
work=${BUILD_DIR:-"$root/build/debug"}/tests/x86-kvm-movdqa
mkdir -p "$work"

source="$root/tests/compiler/c0/x86_gnu_vector_size_movdqa.c"
"$gnu" -E -P -x c "$source" -o "$work/kvm.i"
"$minic" --target=x86_64-linux -mno-sse -S "$work/kvm.i" \
    -o "$work/kvm.minic.s"
"$gnu" -O2 -mno-sse -mno-sse2 -mno-mmx -mno-avx \
    -S "$source" -o "$work/kvm.gnu.s"
for compiler in minic gnu; do
    count=$(grep -Ec '^[[:space:]]*movdqa[[:space:]]' "$work/kvm.$compiler.s")
    if [ "$count" -ne 32 ]; then
        echo "FAIL $compiler generated $count MOVDQA, expected all 32 XMM load/store instructions" >&2
        exit 1
    fi
done
"$gnu" -static "$work/kvm.minic.s" -o "$work/kvm.minic.elf"
"$gnu" -O2 -static -mno-sse -mno-sse2 -mno-mmx -mno-avx \
    "$source" -o "$work/kvm.gnu.elf"
"$qemu" -cpu max "$work/kvm.minic.elf"
"$qemu" -cpu max "$work/kvm.gnu.elf"

for bad in 1 2 3 4 5; do
    "$gnu" -E -P -x c -DBAD_FORM="$bad" \
        "$root/tests/compiler/c0/x86_kvm_movdqa_reject.c" \
        -o "$work/bad-$bad.i"
    if "$minic" --target=x86_64-linux -mno-sse -S "$work/bad-$bad.i" \
        -o "$work/bad-$bad.s" >"$work/bad-$bad.out" \
        2>"$work/bad-$bad.err"; then
        echo "FAIL invalid KVM MOVDQA form $bad was admitted" >&2
        exit 1
    fi
done

echo 'PASS x86 KVM vector_size(16) GNU/MiniC QEMU runtime; 32/32 real MOVDQA XMM0..15, five failclosed forms'
