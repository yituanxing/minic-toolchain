#!/bin/sh
# Source-shape regression only; authentic frozen Loong pci.i + GNU LD/QEMU
# remains a separate SHA-locked runtime certification.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
mini=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host=${HOST_CC:-cc}
source="$root/tests/compiler/c0/gnu_linux_nvme_prp_constant_guard.c"
work=$(mktemp -d)
trap 'rm -r -- "$work"' EXIT HUP INT TERM

"$host" -E -P -std=gnu11 -O2 "$source" -o "$work/positive.i"
"$host" -std=gnu11 -O2 -S "$work/positive.i" -o "$work/gnu.s"
for target in x86_64-linux aarch64-linux loongarch64-linux riscv64-linux riscv32-linux; do
    "$mini" --target="$target" -S "$work/positive.i" -o "$work/$target.s"
    if grep -Fq '__compiletime_assert_449' "$work/$target.s"; then
        echo "NVME_PRP_GUARD=FAIL still_emits_impossible_assert:$target" >&2
        exit 1
    fi
done
"$host" -c -x assembler "$work/gnu.s" -o "$work/gnu.o"
"$host" -c -x assembler "$work/x86_64-linux.s" -o "$work/minic.o"
for object in "$work/gnu.o" "$work/minic.o"; do
    if nm -u "$object" | grep -Fq '__compiletime_assert_449'; then
        echo "NVME_PRP_GUARD=FAIL unexpected_ELF_assert:$object" >&2
        exit 1
    fi
done
"$host" -no-pie "$work/gnu.o" -o "$work/gnu"
"$host" -no-pie "$work/minic.o" -o "$work/minic"
set +e
"$work/gnu"; gnu_rc=$?
"$work/minic"; minic_rc=$?
set -e
if [ "$gnu_rc" -ne 42 ] || [ "$minic_rc" -ne 42 ]; then
    echo "NVME_PRP_GUARD=FAIL GNU=$gnu_rc MiniC=$minic_rc" >&2
    exit 1
fi

"$host" -E -P -std=gnu11 -O2 -DMINIC_NVME_PRP_NEGATIVES "$source" \
    -o "$work/negative.i"
"$host" -std=gnu11 -O2 -c "$work/negative.i" -o "$work/negative.gnu.o"
for target in x86_64-linux aarch64-linux loongarch64-linux riscv64-linux riscv32-linux; do
    "$mini" --target="$target" -S "$work/negative.i" -o "$work/$target-negative.s"
    for symbol in witnessed_effect __compiletime_assert_runtime; do
        if ! grep -Fq "$symbol" "$work/$target-negative.s"; then
            echo "NVME_PRP_GUARD=FAIL lost_effect_or_runtime_guard:$target:$symbol" >&2
            exit 1
        fi
    done
done
"$host" -c -x assembler "$work/x86_64-linux-negative.s" -o "$work/negative.minic.o"
for object in "$work/negative.gnu.o" "$work/negative.minic.o"; do
    for symbol in witnessed_effect __compiletime_assert_runtime; do
        if ! nm -u "$object" | grep -Fq "$symbol"; then
            echo "NVME_PRP_GUARD=FAIL lost_negative_ELF:$object:$symbol" >&2
            exit 1
        fi
    done
done
echo 'NVME_PRP_GUARD=PASS authentic_shape=1 target_ASM=5 GNU_AS_NM=4 GNU_LD=2 GNU_runtime=42 MiniC_runtime=42 negative_effect_volatile_parameter=3'
