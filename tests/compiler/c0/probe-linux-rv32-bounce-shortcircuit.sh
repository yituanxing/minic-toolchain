#!/bin/sh
set -eu
: "${MINIC_CC:?set MINIC_CC to the current minic-cc}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=${BUILD_DIR:-"$root/build/debug"}/tests/linux-rv32-bounce-shortcircuit
mkdir -p "$work"
source="$root/tests/compiler/c0/linux_rv32_bounce_constant_guard.c"

cc -E -P -std=gnu11 -x c "$source" -o "$work/source.i"
cc -E -P -std=gnu11 -DMINI_BOUNCE_TEST_DISABLED_ONLY -x c "$source" -o "$work/disabled.i"
cc -O2 -Werror -c -x c -DMINI_BOUNCE_TEST_DISABLED_ONLY "$source" -o "$work/gnu-disabled.o"
if nm -u "$work/gnu-disabled.o" | grep -E '(__blk_queue_bounce|bio_has_data|max_low_pfn|max_pfn)'; then
    echo 'LINUX_RV32_BOUNCE=FAIL GNU -O2 disabled original has missing symbol' >&2
    exit 1
fi
"$MINIC_CC" --target=x86_64-linux -S "$work/disabled.i" -o "$work/minic-disabled.s"
cc -c -x assembler "$work/minic-disabled.s" -o "$work/minic-disabled.o"
if nm -u "$work/minic-disabled.o" | grep -E '(__blk_queue_bounce|bio_has_data|max_low_pfn|max_pfn)'; then
    echo 'LINUX_RV32_BOUNCE=FAIL MiniC disabled-only GNU AS object has missing symbol' >&2
    exit 1
fi

function_asm() {
    awk -v name="$2" '
        $0 == name ":" { active=1 }
        active { print }
        index($0, ".size " name ",") == 1 { active=0 }
    ' "$1"
}

for target in x86_64-linux aarch64-linux loongarch64-linux riscv64-linux riscv32-linux; do
    assembly="$work/$target.s"
    "$MINIC_CC" --target="$target" -S "$work/source.i" -o "$assembly"
    function_asm "$assembly" mini_queue_bounce > "$work/disabled-$target.s"
    function_asm "$assembly" linux_rv32_bounce_enabled > "$work/enabled-$target.s"
    function_asm "$assembly" linux_rv32_bounce_effectful_left > "$work/effectful-$target.s"
    function_asm "$assembly" linux_rv32_bounce_true_or > "$work/true-or-$target.s"
    function_asm "$assembly" linux_rv32_bounce_false_or > "$work/false-or-$target.s"
    function_asm "$assembly" linux_rv32_bounce_volatile_left > "$work/volatile-$target.s"

    if ! grep -q '^mini_queue_bounce:' "$work/disabled-$target.s" ||
       grep -Eq '__blk_queue_bounce|bio_has_data|max_low_pfn|max_pfn' "$work/disabled-$target.s"; then
        echo "LINUX_RV32_BOUNCE=FAIL target=$target disabled branch retained dead call" >&2
        exit 1
    fi
    if ! grep -q '__blk_queue_bounce' "$work/enabled-$target.s" ||
       ! grep -q 'bio_has_data' "$work/enabled-$target.s" ||
       ! grep -q 'bio_has_data' "$work/effectful-$target.s" ||
       ! grep -q 'bio_has_data' "$work/false-or-$target.s" ||
       grep -q 'bio_has_data' "$work/true-or-$target.s" ||
       ! grep -q 'runtime_gate' "$work/volatile-$target.s" ||
       grep -q 'bio_has_data' "$work/volatile-$target.s"; then
        echo "LINUX_RV32_BOUNCE=FAIL target=$target lost an effect or kept an unreachable one" >&2
        exit 1
    fi
done

echo 'LINUX_RV32_BOUNCE=PASS targets=5 gnu_O2_nm=1 minic_GNU_AS_nm=1 disabled_missing_symbol=0 negatives=enabled,effectful_left,false_or,volatile_left true_or=folded'
