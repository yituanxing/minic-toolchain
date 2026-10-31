#!/bin/sh
# MiniC Linux 6.6.143 fs/proc/task_mmu.c _347: exact shape, distinct from
# the full, authenticated frozen .i owner that is separately replayed.
# GNU -E -> GCC -O2 or MiniC -S -> original GNU AS/LD -> native execute.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
mini=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host=${HOST_CC:-cc}
source="$root/tests/compiler/c0/gnu_linux_task_mmu_swp_pfn_guard.c"
work=$(mktemp -d)
trap 'rm -r -- "$work"' EXIT HUP INT TERM

"$host" -E -P -std=gnu11 "$source" -o "$work/task_mmu.i"
"$host" -std=gnu11 -O2 -S "$work/task_mmu.i" -o "$work/gnu.s"
"$mini" --target=x86_64-linux -S "$work/task_mmu.i" -o "$work/minic.s"
"$host" -c -x assembler "$work/gnu.s" -o "$work/gnu.o"
"$host" -c -x assembler "$work/minic.s" -o "$work/minic.o"
for object in "$work/gnu.o" "$work/minic.o"; do
    if nm -u "$object" | grep -Fq '__compiletime_assert_347'; then
        echo "LINUX_TASK_MMU_PFN_GUARD=FAIL retained_impossible_assert object=$object" >&2
        exit 1
    fi
done

"$host" -no-pie "$work/gnu.o" -o "$work/gnu"
"$host" -no-pie "$work/minic.o" -o "$work/minic"
set +e
"$work/gnu"
gnu_rc=$?
"$work/minic"
minic_rc=$?
set -e
if [ "$gnu_rc" -ne 42 ] || [ "$minic_rc" -ne "$gnu_rc" ]; then
    echo "LINUX_TASK_MMU_PFN_GUARD=FAIL GNU_runtime=$gnu_rc MiniC_runtime=$minic_rc" >&2
    exit 1
fi

"$host" -E -P -std=gnu11 -DMINIC_NEGATIVE_CASES "$source" -o "$work/negative.i"
"$host" -std=gnu11 -O2 -c "$work/negative.i" -o "$work/negative.gnu.o"
"$mini" --target=x86_64-linux -S "$work/negative.i" -o "$work/negative.minic.s"
"$host" -c -x assembler "$work/negative.minic.s" -o "$work/negative.minic.o"
for object in "$work/negative.gnu.o" "$work/negative.minic.o"; do
    for symbol in dangerous_pfn_bits_source runtime_pfn_bits_probe; do
        if ! nm -u "$object" | grep -Fq "$symbol"; then
            echo "LINUX_TASK_MMU_PFN_GUARD=FAIL lost_negative_symbol=$symbol object=$object" >&2
            exit 1
        fi
    done
done

printf 'LINUX_TASK_MMU_PFN_GUARD=PASS GNU_E=1 GNU_O2=1 MiniC_S=1 GNU_AS=4 GNU_LD=2 GNU_runtime=%s MiniC_runtime=%s negative=3 source_model_only=1\n' "$gnu_rc" "$minic_rc"
