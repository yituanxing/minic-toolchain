#!/bin/sh
# MC-004/005: GNU aligned(sizeof(struct/enum {...})) may grow Parser-owned
# tag/enumerator storage. A lexical probe must not borrow those containers.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic_cc=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host_cc=${HOST_CC:-cc}
ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=0:halt_on_error=1}
export ASAN_OPTIONS
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

for scenario in mc004_attribute_tag_probe_uaf mc005_attribute_enum_probe_uaf; do
    source="$root/tests/compiler/audit/$scenario.c"
    for phase in before boundary after; do
        defines=
        case "$scenario:$phase" in
            mc004*:before) defines=-DMC004_SKIP_PAD16 ;;
            mc004*:after) defines=-DMC004_ADD_PAD17 ;;
            mc005*:before) defines=-DMC005_SKIP_C15 ;;
            mc005*:after) defines=-DMC005_ADD_C16 ;;
        esac
        stem="$scenario.$phase"
        "$host_cc" -std=gnu11 -E -P $defines "$source" -o "$work/$stem.i"
        "$host_cc" -std=gnu11 -Wall -Wextra -Werror -no-pie \
            -x c "$work/$stem.i" -o "$work/$stem.gnu"
        for target in x86_64-linux riscv32-linux riscv64-linux aarch64-linux loongarch64-linux; do
            "$minic_cc" --target="$target" -S "$work/$stem.i" -o "$work/$stem.$target.s"
            test -s "$work/$stem.$target.s"
        done
        "$host_cc" -no-pie -x assembler "$work/$stem.x86_64-linux.s" -o "$work/$stem.minic"
        "$work/$stem.gnu"
        "$work/$stem.minic"
    done
done

echo 'MC004_MC005_ATTRIBUTE_PROBE=PASS 2_owners_x_3_phases_x_5_targets GNU_MiniC_x86_native=0 ASAN_if_enabled=NO_UAF'
