#!/bin/sh
set -eu
: "${MINIC_CC:?set MINIC_CC to the current minic-cc}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=${BUILD_DIR:-"$root/build/debug"}/tests/gnu-linux-kbuild-disjoint-bitmask-assert
mkdir -p "$work"
source="$root/tests/compiler/c0/gnu_linux_kbuild_disjoint_bitmask_assert.c"
cc -E -P -std=gnu11 -x c "$source" -o "$work/gnu.i"
cc -std=gnu11 -O2 -S -x c "$work/gnu.i" -o "$work/gnu.s"
"$MINIC_CC" --target=x86_64-linux -S "$work/gnu.i" -o "$work/minic.s"
for target in aarch64-linux loongarch64-linux riscv64-linux riscv32-linux; do
    "$MINIC_CC" --target="$target" -S "$work/gnu.i" -o "$work/$target.s"
    if grep -Fq '__compiletime_assert_707' "$work/$target.s"; then
        echo "KBUILD_DISJOINT_BITMASK_ASSERT=FAIL $target unresolved diagnostic" >&2
        exit 1
    fi
done
if grep -Fq '__compiletime_assert_707' "$work/gnu.s" ||
   grep -Fq '__compiletime_assert_707' "$work/minic.s"; then
    echo 'KBUILD_DISJOINT_BITMASK_ASSERT=FAIL unresolved diagnostic in assembly' >&2
    exit 1
fi
cc -c -x assembler "$work/gnu.s" -o "$work/gnu.o"
cc -c -x assembler "$work/minic.s" -o "$work/minic.o"
cc -no-pie "$work/gnu.o" -o "$work/gnu"
cc -no-pie "$work/minic.o" -o "$work/minic"
set +e
"$work/gnu"
gnu_rc=$?
"$work/minic"
minic_rc=$?
set -e
if [ "$gnu_rc" -ne 42 ] || [ "$minic_rc" -ne "$gnu_rc" ]; then
    echo "KBUILD_DISJOINT_BITMASK_ASSERT=FAIL GNU=$gnu_rc MiniC=$minic_rc" >&2
    exit 1
fi
if [ -n "${MINIC_X86_EXEC_FROZEN_I:-}" ]; then
    test -r "$MINIC_X86_EXEC_FROZEN_I"
    "$MINIC_CC" --target=x86_64-linux -S "$MINIC_X86_EXEC_FROZEN_I" \
        -o "$work/frozen-exec.minic.s"
    if grep -Fq '__compiletime_assert_707' "$work/frozen-exec.minic.s"; then
        echo 'KBUILD_DISJOINT_BITMASK_ASSERT=FAIL full Linux fs/exec.i diagnostic' >&2
        exit 1
    fi
    cc -c -x assembler "$work/frozen-exec.minic.s" -o "$work/frozen-exec.minic.o"
    if nm -u "$work/frozen-exec.minic.o" | grep -Fq '__compiletime_assert_707'; then
        echo 'KBUILD_DISJOINT_BITMASK_ASSERT=FAIL full Linux fs/exec.i ELF unresolved' >&2
        exit 1
    fi
fi
printf 'KBUILD_DISJOINT_BITMASK_ASSERT=PASS five_targets=5 GNU_AS=2 GNU_LD=2 GNU_runtime=%s MiniC_runtime=%s frozen_exec=%s\n' \
    "$gnu_rc" "$minic_rc" "${MINIC_X86_EXEC_FROZEN_I:+checked}"
