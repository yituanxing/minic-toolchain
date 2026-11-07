#!/bin/sh
set -eu
: "${MINIC_CC:?set MINIC_CC to current minic-cc}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
host=${HOST_CC:-cc}
work=$(mktemp -d)
trap 'rm -r -- "$work"' EXIT HUP INT TERM
source="$root/tests/compiler/c0/gnu_linux_memory_encode_page_boolean_range.c"

"$host" -E -P -std=gnu11 "$source" -o "$work/safe.i"
"$host" -std=gnu11 -O2 -S -x c "$work/safe.i" -o "$work/safe.gnu.s"
"$MINIC_CC" --target=x86_64-linux -S "$work/safe.i" -o "$work/safe.mini.s"
"$host" -c -x assembler "$work/safe.gnu.s" -o "$work/safe.gnu.o"
"$host" -c -x assembler "$work/safe.mini.s" -o "$work/safe.mini.o"

for object in "$work/safe.gnu.o" "$work/safe.mini.o"; do
    if nm -u "$object" | grep -Fq '__compiletime_assert_161'; then
        echo "LINUX_MEMORY_BOOLEAN_ASSERT=FAIL retained impossible guard: $object" >&2
        exit 1
    fi
done

for target in riscv32-linux riscv64-linux aarch64-linux loongarch64-linux; do
    "$MINIC_CC" --target="$target" -S "$work/safe.i" -o "$work/safe.$target.s"
    if grep -Fq '__compiletime_assert_161' "$work/safe.$target.s"; then
        echo "LINUX_MEMORY_BOOLEAN_ASSERT=FAIL target=$target retained impossible guard" >&2
        exit 1
    fi
done

"$host" -no-pie "$work/safe.gnu.o" -o "$work/safe.gnu"
"$host" -no-pie "$work/safe.mini.o" -o "$work/safe.mini"
set +e
"$work/safe.gnu"
gnu_rc=$?
"$work/safe.mini"
mini_rc=$?
set -e
if [ "$gnu_rc" -ne 42 ] || [ "$mini_rc" -ne "$gnu_rc" ]; then
    echo "LINUX_MEMORY_BOOLEAN_ASSERT=FAIL runtime GNU=$gnu_rc MiniC=$mini_rc" >&2
    exit 1
fi

# Four independent proofs that the width test must *not* be erased when
# the incoming range is unbounded, escaped, volatile, or may contain 4.
case_id=1
while [ "$case_id" -le 4 ]; do
    "$host" -E -P -std=gnu11 -DMINIC_NEGATIVE_CASE="$case_id" "$source" \
        -o "$work/unsafe-$case_id.i"
    "$host" -std=gnu11 -O2 -c -x c "$work/unsafe-$case_id.i" \
        -o "$work/unsafe-$case_id.gnu.o"
    "$MINIC_CC" --target=x86_64-linux -S "$work/unsafe-$case_id.i" \
        -o "$work/unsafe-$case_id.mini.s"
    "$host" -c -x assembler "$work/unsafe-$case_id.mini.s" \
        -o "$work/unsafe-$case_id.mini.o"
    for object in "$work/unsafe-$case_id.gnu.o" "$work/unsafe-$case_id.mini.o"; do
        if ! nm -u "$object" | grep -Fq '__compiletime_assert_161'; then
            echo "LINUX_MEMORY_BOOLEAN_ASSERT=FAIL lost real guard case=$case_id $object" >&2
            exit 1
        fi
    done
    case_id=$((case_id + 1))
done

# Optional full authentic owner replay after the SHA-verified receiver copied
# the original Linux .i. The .o.cmd ABI flags must match the exact owner.
if [ -n "${MINIC_MEMORY_FROZEN_I:-}" ]; then
    test -r "$MINIC_MEMORY_FROZEN_I"
    "$MINIC_CC" --target=x86_64-linux -mno-sse -funsigned-char -fshort-wchar \
        -S "$MINIC_MEMORY_FROZEN_I" -o "$work/frozen.s"
    "$host" -c -x assembler "$work/frozen.s" -o "$work/frozen.o"
    if nm -u "$work/frozen.o" | grep -Fq '__compiletime_assert_161'; then
        echo 'LINUX_MEMORY_BOOLEAN_ASSERT=FAIL original mm/memory.o unresolved' >&2
        exit 1
    fi
fi

printf 'LINUX_MEMORY_BOOLEAN_ASSERT=PASS five_ISA_S=5 GNU_native=%s MiniC_native=%s negative=%u/4 original_frozen=%s\n' \
    "$gnu_rc" "$mini_rc" 4 "${MINIC_MEMORY_FROZEN_I:+checked}"
