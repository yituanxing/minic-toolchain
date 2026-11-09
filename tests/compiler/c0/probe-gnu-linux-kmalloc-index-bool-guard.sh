#!/bin/sh
# GNU Linux slab.h's two-argument inline helper: a runtime parameter passed
# to __builtin_constant_p() must select a proven-zero candidate, never a
# speculative forced-true/false rewrite of its shared C CALL expression.
set -eu
: "${MINIC_CC:?set MINIC_CC to the current minic-cc}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=${BUILD_DIR:-"$root/build/debug"}/tests/gnu-linux-kmalloc-index-bool-guard
mkdir -p "$work"
source="$root/tests/compiler/c0/gnu_linux_kmalloc_index_bool_guard.c"
cc -E -P -std=gnu11 -x c "$source" -o "$work/source.i"
cc -std=gnu11 -O2 -S -fno-pie -x c "$work/source.i" -o "$work/gnu.s"
"$MINIC_CC" --target=x86_64-linux -S "$work/source.i" -o "$work/minic.s"
if grep -Fq 'call __compiletime_assert_212' "$work/gnu.s" ||
   grep -Fq 'call __compiletime_assert_212' "$work/minic.s"; then
    echo 'GNU_LINUX_KMALLOC_INDEX_BOOL=FAIL unresolved guard after zero-fact clone' >&2
    exit 1
fi
cc -c -x assembler "$work/gnu.s" -o "$work/gnu.o"
cc -c -x assembler "$work/minic.s" -o "$work/minic.o"
if nm -u "$work/gnu.o" "$work/minic.o" | grep -Fq '__compiletime_assert_212'; then
    echo 'GNU_LINUX_KMALLOC_INDEX_BOOL=FAIL unexpected external guard' >&2
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
    echo "GNU_LINUX_KMALLOC_INDEX_BOOL=FAIL GNU=$gnu_rc MiniC=$minic_rc" >&2
    exit 1
fi

# Negative: a possibly oversized source argument with explicit constant true
# must not be silently routed through the zero-fact candidate. GNU rejects
# this diagnostic at -O2; MiniC must retain the genuine unresolved guard if
# it has not yet diagnosed it during C translation.
cc -E -P -std=gnu11 -DMINIC_TEST_UNSAFE_TRUE -x c "$source" -o "$work/unsafe.i"
if cc -O2 -S -x c "$work/unsafe.i" -o "$work/unsafe.gnu.s" \
        >"$work/unsafe.gnu.stdout" 2>"$work/unsafe.gnu.stderr"; then
    echo 'GNU_LINUX_KMALLOC_INDEX_BOOL=FAIL GNU accepted unsafe guard' >&2
    exit 1
fi
if "$MINIC_CC" --target=x86_64-linux -S "$work/unsafe.i" \
        -o "$work/unsafe.minic.s" >"$work/unsafe.minic.stdout" \
        2>"$work/unsafe.minic.stderr"; then
    if ! grep -Fq 'call __compiletime_assert_212' "$work/unsafe.minic.s"; then
        echo 'GNU_LINUX_KMALLOC_INDEX_BOOL=FAIL MiniC silently erased unsafe guard' >&2
        exit 1
    fi
fi
printf 'GNU_LINUX_KMALLOC_INDEX_BOOL=PASS GNU_AS=2 GNU_LD=2 GNU_runtime=%s MiniC_runtime=%s negative_unsafe=true\n' \
    "$gnu_rc" "$minic_rc"
