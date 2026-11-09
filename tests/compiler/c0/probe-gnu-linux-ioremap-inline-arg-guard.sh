#!/bin/sh
# Source-shape GNU-vs-MiniC strict differential for Linux ioremap inline
# fix_to_virt(0) and its parameter-dependent no-effect BUILD_BUG_ON prefix.
# The full frozen GNU Kbuild .i/LD/QEMU acceptance remains separate.
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
mini=${MINIC_CC:-"$repo/build/release/bin/minic-cc"}
host=${HOST_CC:-cc}
src="$repo/tests/compiler/c0/gnu_linux_ioremap_inline_arg_guard.c"
work=$(mktemp -d)
trap 'rm -r -- "$work"' EXIT HUP INT TERM

"$host" -E -P -std=gnu11 "$src" -o "$work/fixture.i"
"$host" -std=gnu11 -O2 -S "$work/fixture.i" -o "$work/gnu.s"
"$mini" --target=x86_64-linux -S "$work/fixture.i" -o "$work/mini.s"
"$host" -c -x assembler "$work/gnu.s" -o "$work/gnu.o"
"$host" -c -x assembler "$work/mini.s" -o "$work/mini.o"

for obj in "$work/gnu.o" "$work/mini.o"; do
    if nm -u "$obj" | grep -Eq '__compiletime_assert_(183|369)'; then
        echo "LINUX_IOREMAP_INLINE_ARG=FAIL unresolved impossible assert in $obj" >&2
        exit 1
    fi
done
"$host" -no-pie "$work/gnu.o" -o "$work/gnu"
"$host" -no-pie "$work/mini.o" -o "$work/mini"
set +e
"$work/gnu"
gnu_rc=$?
"$work/mini"
mini_rc=$?
set -e
if [ "$gnu_rc" -ne 42 ] || [ "$mini_rc" -ne "$gnu_rc" ]; then
    echo "LINUX_IOREMAP_INLINE_ARG=FAIL GNU=$gnu_rc MiniC=$mini_rc" >&2
    exit 1
fi

"$host" -E -P -std=gnu11 -DMINIC_NEGATIVE_CASES "$src" -o "$work/negative.i"
"$host" -std=gnu11 -O2 -c "$work/negative.i" -o "$work/negative.gnu.o"
"$mini" --target=x86_64-linux -S "$work/negative.i" -o "$work/negative.mini.s"
"$host" -c -x assembler "$work/negative.mini.s" -o "$work/negative.mini.o"
for obj in "$work/negative.gnu.o" "$work/negative.mini.o"; do
    for symbol in __compiletime_assert_183 runtime_guard_side_effect; do
        if ! nm -u "$obj" | grep -Fq "$symbol"; then
            echo "LINUX_IOREMAP_INLINE_ARG=FAIL erased negative external $obj $symbol" >&2
            exit 1
        fi
    done
done

printf 'LINUX_IOREMAP_INLINE_ARG=PASS GNU_E=1 GCC_O2=1 MiniC_S=1 GNU_AS=4 GNU_LD=2 GNU_runtime=%s MiniC_runtime=%s negative=2 source_model_only=1\n' "$gnu_rc" "$mini_rc"
