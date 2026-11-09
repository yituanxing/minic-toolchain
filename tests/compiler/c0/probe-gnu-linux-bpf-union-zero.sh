#!/bin/sh
# Reduced original Linux 6.6.143 BPF lock-union initialization regression.
# Only MiniC is varied: GNU -E (once), MiniC -S vs GNU -O2 -S, GNU AS/LD,
# native execution, and required negative ELF undefined-symbol ownership.
# NOT a certificate that original GNU Kbuild vmlinux links or QEMU boots.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
mini=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host=${HOST_CC:-cc}
source="$root/tests/compiler/c0/gnu_linux_bpf_union_zero.c"
work=$(mktemp -d)
trap 'rm -r -- "$work"' EXIT HUP INT TERM

"$host" -E -P -std=gnu11 "$source" -o "$work/bpf.i"
"$host" -std=gnu11 -O2 -S "$work/bpf.i" -o "$work/bpf.gnu.s"
"$mini" --target=x86_64-linux -S "$work/bpf.i" -o "$work/bpf.minic.s"
"$host" -c -x assembler "$work/bpf.gnu.s" -o "$work/bpf.gnu.o"
"$host" -c -x assembler "$work/bpf.minic.s" -o "$work/bpf.minic.o"

for object in "$work/bpf.gnu.o" "$work/bpf.minic.o"; do
    if nm -u "$object" | grep -Fq '__compiletime_assert_758'; then
        echo "LINUX_BPF_UNION_ZERO=FAIL unresolved impossible assert in $object" >&2
        exit 1
    fi
done

"$host" -no-pie "$work/bpf.gnu.o" -o "$work/bpf.gnu"
"$host" -no-pie "$work/bpf.minic.o" -o "$work/bpf.minic"
set +e
"$work/bpf.gnu"
gnu_rc=$?
"$work/bpf.minic"
mini_rc=$?
set -e
if [ "$gnu_rc" -ne 42 ] || [ "$mini_rc" -ne "$gnu_rc" ]; then
    echo "LINUX_BPF_UNION_ZERO=FAIL native GNU=$gnu_rc MiniC=$mini_rc" >&2
    exit 1
fi

"$host" -E -P -std=gnu11 -DMINIC_NEGATIVE_CASES "$source" -o "$work/negative.i"
"$host" -std=gnu11 -O2 -c "$work/negative.i" -o "$work/negative.gnu.o"
"$mini" --target=x86_64-linux -S "$work/negative.i" -o "$work/negative.minic.s"
"$host" -c -x assembler "$work/negative.minic.s" -o "$work/negative.minic.o"
for object in "$work/negative.gnu.o" "$work/negative.minic.o"; do
    for symbol in runtime_nonzero_probe mutate_spinword; do
        if ! nm -u "$object" | grep -Fq "$symbol"; then
            echo "LINUX_BPF_UNION_ZERO=FAIL lost negative owner $object $symbol" >&2
            exit 1
        fi
    done
done

printf 'LINUX_BPF_UNION_ZERO=PASS GNU_E=1 GNU_O2=1 MiniC_S=1 GNU_AS=4 GNU_LD=2 GNU_runtime=%s MiniC_runtime=%s negative=4 source_model_only=1\n' \
    "$gnu_rc" "$mini_rc"
