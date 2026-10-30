#!/bin/sh
# GNU -O2 vs MiniC strict Linux kaslr memory-range assertion diagnostic.
# A source model of authenticated frozen .i, not a real vmlinux link/QEMU.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
mini=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host=${HOST_CC:-cc}
src="$root/tests/compiler/c0/gnu_linux_kaslr_addr_range_guard.c"
work=$(mktemp -d)
trap 'rm -r -- "$work"' EXIT HUP INT TERM

"$host" -E -P -std=gnu11 "$src" -o "$work/positive.i"
"$host" -std=gnu11 -O2 -S "$work/positive.i" -o "$work/gnu.s"
"$mini" --target=x86_64-linux -S "$work/positive.i" -o "$work/minic.s"
"$host" -c -x assembler "$work/gnu.s" -o "$work/gnu.o"
"$host" -c -x assembler "$work/minic.s" -o "$work/minic.o"
if nm -u "$work/gnu.o" | grep -Fq __compiletime_assert_287; then
    echo 'LINUX_KASLR_RANGE=FAIL GNU_O2 retained impossible assert' >&2
    exit 1
fi
if nm -u "$work/minic.o" | grep -Fq __compiletime_assert_287; then
    echo 'LINUX_KASLR_RANGE=FAIL MiniC retained impossible assert' >&2
    exit 1
fi
"$host" -no-pie "$work/gnu.o" -o "$work/gnu"
"$host" -no-pie "$work/minic.o" -o "$work/minic"
set +e
"$work/gnu"
gnu_rc=$?
"$work/minic"
mini_rc=$?
set -e
if [ "$gnu_rc" -ne 42 ] || [ "$mini_rc" -ne "$gnu_rc" ]; then
    echo "LINUX_KASLR_RANGE=FAIL GNU_runtime=$gnu_rc MiniC_runtime=$mini_rc" >&2
    exit 1
fi

"$host" -E -P -std=gnu11 -DMINIC_NEGATIVE_CASES "$src" -o "$work/negative.i"
"$host" -std=gnu11 -O2 -c "$work/negative.i" -o "$work/negative.gnu.o"
"$mini" --target=x86_64-linux -S "$work/negative.i" -o "$work/negative.minic.s"
"$host" -c -x assembler "$work/negative.minic.s" -o "$work/negative.minic.o"
for obj in "$work/negative.gnu.o" "$work/negative.minic.o"; do
    if ! nm -u "$obj" | grep -Fq observed_above_end; then
        echo "LINUX_KASLR_RANGE=FAIL lost negative runtime call $obj" >&2
        exit 1
    fi
done
echo "LINUX_KASLR_RANGE=PASS GNU_E=1 GNU_O2=1 MiniC_S=1 GNU_AS=4 GNU_LD=2 GNU_runtime=$gnu_rc MiniC_runtime=$mini_rc negative=2 source_model_only=1"
