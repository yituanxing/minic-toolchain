#!/bin/sh
# Verify that x86-64 follows shared Core CFG reachability while keeping GNU
# compilation and GNU AS/LD as independent strict-differential oracles.
set -eu
: "${MINIC_CC:?set MINIC_CC to the current minic-cc}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=${BUILD_DIR:-"$root/build/debug"}/tests/x86-reachable-vma-assert
mkdir -p "$work"
source="$root/tests/compiler/c0/x86_reachable_vma_assert.c"
cc -E -P -std=gnu11 -x c "$source" -o "$work/probe.i"
cc -std=gnu11 -O2 -S -fno-pie -x c "$work/probe.i" -o "$work/gnu.s"
"$MINIC_CC" --target=x86_64-linux -S "$work/probe.i" -o "$work/minic.s"

# The original Linux source used a diagnostic attribute, not a runtime stub.
# Neither compiler is permitted to leave a reference to the diagnostic call.
if grep -Fq '__compiletime_assert_80' "$work/gnu.s" ||
   grep -Fq '__compiletime_assert_80' "$work/minic.s"; then
    echo 'X86_REACHABLE_VMA_ASSERT=FAIL unreachable diagnostic leaked' >&2
    exit 1
fi
cc -c -x assembler "$work/gnu.s" -o "$work/gnu.o"
cc -c -x assembler "$work/minic.s" -o "$work/minic.o"
if nm -u "$work/gnu.o" "$work/minic.o" | grep -Fq '__compiletime_assert_80'; then
    echo 'X86_REACHABLE_VMA_ASSERT=FAIL undefined diagnostic' >&2
    exit 1
fi
cc -no-pie "$work/gnu.o" -o "$work/gnu"
cc -no-pie "$work/minic.o" -o "$work/minic"
set +e
"$work/gnu"
gnu_rc=$?
"$work/minic"
minic_rc=$?
set -e
if [ "$gnu_rc" -ne 42 ] || [ "$minic_rc" -ne 42 ]; then
    echo "X86_REACHABLE_VMA_ASSERT=FAIL GNU=$gnu_rc MiniC=$minic_rc" >&2
    exit 1
fi

# Optional full 2.4 MiB original GNU -E VDSO witness from the strict Linux
# runtime artifact; the committed fixture above remains self-contained.
if [ -n "${MINIC_X86_VMA_FROZEN_I:-}" ]; then
    test -r "$MINIC_X86_VMA_FROZEN_I"
    "$MINIC_CC" --target=x86_64-linux -S "$MINIC_X86_VMA_FROZEN_I" \
        -o "$work/frozen-vma.minic.s"
    if grep -Fq 'call __compiletime_assert_80' "$work/frozen-vma.minic.s"; then
        echo 'X86_REACHABLE_VMA_ASSERT=FAIL frozen Linux VDSO unreachable diagnostic' >&2
        exit 1
    fi
fi
printf 'X86_REACHABLE_VMA_ASSERT=PASS GNU_AS=2 GNU_LD=2 GNU_runtime=%s MiniC_runtime=%s frozen_vma=%s\n' \
    "$gnu_rc" "$minic_rc" "${MINIC_X86_VMA_FROZEN_I:+checked}"
