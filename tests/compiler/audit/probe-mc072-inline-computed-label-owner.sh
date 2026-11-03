#!/bin/sh
# MC-072 positive source-label owner proof. A cloned inline function may use a
# static &&label table, but every resulting relocation must resolve to a label
# emitted by the same Core/Module reachability closure. GNU/QEMU is tested
# when cross toolchains are installed; never confuse assembler acceptance
# with a successful target link/runtime.
set -eu
: "${MINIC_CC:?set MINIC_CC to the compiler under test}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
source="$root/tests/compiler/audit/mc072_inline_computed_label_owner.c"

cc -std=gnu11 -O0 -DMC072_RUNTIME -c -x c "$source" -o "$work/gnu-O0.o"
cc -std=gnu11 -O2 -DMC072_RUNTIME -c -x c "$source" -o "$work/gnu-O2.o"
cc -std=gnu11 -E -P -DMC072_RUNTIME -x c "$source" -o "$work/input.i"

target_runtime=0
target_isa=0

for target in riscv32 riscv64 aarch64 loongarch64; do
    "$MINIC_CC" --target="$target-linux" -S "$work/input.i" \
        -o "$work/$target.s"
    # Preserve both static-table slots and resolve every .Luser reference.
    # An .s with undefined symbols may pass GNU AS but fail GNU LD later.
    refs=$(sed -nE 's/^[[:space:]]*\.(8byte|dword|4byte)[[:space:]]+(\.Luser_[[:alnum:]_]+).*$/\2/p' \
        "$work/$target.s")
    refs_count=$(printf '%s\n' "$refs" | grep -c '^\.Luser_' || :)
    defs_count=$(grep -Ec '^\.Luser_[[:alnum:]_]+:' "$work/$target.s" || :)
    if [ "$refs_count" -ne 2 ] || [ "$defs_count" -lt 2 ] ||
       grep -Fq '.data.mc072_dead_owner' "$work/$target.s" ||
       ! grep -Fq '__minic_inline_spec_' "$work/$target.s"; then
        echo "MC072_AUDIT=FAIL target=$target refs=$refs_count defs=$defs_count" >&2
        exit 1
    fi
    for symbol in $refs; do
        if ! grep -Fxq "$symbol:" "$work/$target.s"; then
            echo "MC072_AUDIT=FAIL target=$target unresolved_label=$symbol" >&2
            exit 1
        fi
    done
    target_isa=$((target_isa + 1))

    case "$target" in
        riscv32) gnu=riscv32-linux-gnu-gcc; qemu=qemu-riscv32 ;;
        riscv64) gnu=riscv64-linux-gnu-gcc; qemu=qemu-riscv64 ;;
        aarch64) gnu=aarch64-linux-gnu-gcc; qemu=qemu-aarch64 ;;
        loongarch64) gnu=loongarch64-linux-gnu-gcc-13; qemu=qemu-loongarch64 ;;
    esac
    if ! command -v "$gnu" >/dev/null 2>&1 ||
       ! command -v "$qemu" >/dev/null 2>&1; then
        if [ "${MINIC_REACHABILITY_REQUIRE_CROSS:-0}" = 1 ]; then
            echo "MC072_AUDIT=FAIL target=$target cross_GNU_or_QEMU_unavailable" >&2
            exit 1
        fi
    echo "MC072_ISA=PASS target=$target refs=2 defs=$defs_count dead_owner=pruned cross_GNU_QEMU=SKIP"
        continue
    fi
    "$gnu" -O0 -static -x c "$work/input.i" -o "$work/$target.gnu"
    "$gnu" -O0 -static -x assembler "$work/$target.s" -o "$work/$target.minic"
    for producer in gnu minic; do
        if ! timeout 45s "$qemu" "$work/$target.$producer" \
            >"$work/$target.$producer.out" 2>"$work/$target.$producer.err"; then
            echo "MC072_AUDIT=FAIL target=$target producer=$producer runtime" >&2
            exit 1
        fi
    done
    cmp "$work/$target.gnu.out" "$work/$target.minic.out"
    cmp "$work/$target.gnu.err" "$work/$target.minic.err"
    target_runtime=$((target_runtime + 1))
    echo "MC072_GNU_QEMU=PASS target=$target reloc_definition=2 runtime=0"
done
echo "MC072_LABEL_OWNER=PASS GCC_O0_O2=PASS four_ISA=$target_isa no_unresolved_source_label=YES target_GNU_QEMU=$target_runtime"
