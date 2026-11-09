#!/bin/sh
# GNU Linux 6.6.143 nf_conntrack_extend total_extension_size source-shape probe.
# Frozen owner/authenticated .i and real kernel GNU LD remain separate gates.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
mini=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host=${HOST_CC:-cc}
source="$root/tests/compiler/c0/gnu_linux_nf_conntrack_total_size.c"
work=$(mktemp -d)
trap 'rm -r -- "$work"' EXIT HUP INT TERM
"$host" -E -P -std=gnu11 "$source" -o "$work/positive.i"
"$host" -std=gnu11 -O2 -S "$work/positive.i" -o "$work/gnu.s"
"$mini" --target=x86_64-linux -S "$work/positive.i" -o "$work/minic.s"
"$host" -c -x assembler "$work/gnu.s" -o "$work/gnu.o"
"$host" -c -x assembler "$work/minic.s" -o "$work/minic.o"
for obj in "$work/gnu.o" "$work/minic.o"; do
    if nm -u "$obj" | grep -Fq '__compiletime_assert_697'; then
        echo "LINUX_NF_CT_TOTAL_SIZE=FAIL unresolved impossible assert $obj" >&2
        exit 1
    fi
done
"$host" -no-pie "$work/gnu.o" -o "$work/gnu"
"$host" -no-pie "$work/minic.o" -o "$work/minic"
set +e
"$work/gnu"; gnu_rc=$?
"$work/minic"; mini_rc=$?
set -e
if [ "$gnu_rc" -ne 42 ] || [ "$mini_rc" -ne 42 ]; then
    echo "LINUX_NF_CT_TOTAL_SIZE=FAIL GNU=$gnu_rc MiniC=$mini_rc" >&2
    exit 1
fi
"$host" -E -P -std=gnu11 -DMINIC_NEGATIVE_CASES "$source" -o "$work/negative.i"
"$host" -std=gnu11 -O2 -c "$work/negative.i" -o "$work/negative.gnu.o"
"$mini" --target=x86_64-linux -S "$work/negative.i" -o "$work/negative.minic.s"
"$host" -c -x assembler "$work/negative.minic.s" -o "$work/negative.minic.o"
for obj in "$work/negative.gnu.o" "$work/negative.minic.o"; do
    if ! nm -u "$obj" | grep -Fq 'runtime_size_assert_probe'; then
        echo "LINUX_NF_CT_TOTAL_SIZE=FAIL lost dynamic assert $obj" >&2
        exit 1
    fi
done
echo 'LINUX_NF_CT_TOTAL_SIZE=PASS GNU_E=1 GNU_O2=1 MiniC_S=1 GNU_AS=4 GNU_LD=2 GNU_runtime=42 MiniC_runtime=42 negative=2 source_model_only=1'
