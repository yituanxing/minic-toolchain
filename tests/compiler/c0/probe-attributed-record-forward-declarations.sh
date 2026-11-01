#!/usr/bin/env bash
set -Eeuo pipefail

root=$(cd "$(dirname "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
cc=${HOST_CC:-cc}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail() { echo "MC067_ATTRIBUTED_RECORD_FORWARD=FAIL $*" >&2; exit 1; }

test -x "$minic" || fail 'MiniC binary missing'

check_runtime() {
    local name=$1 source=$2 expected=$3 gnu_status=0 minic_status=0
    "$cc" -std=gnu11 -E -P "$source" -o "$work/$name.i" ||
        fail "$name GNU preprocessing"
    "$cc" -std=gnu11 -Werror -O0 -no-pie -x c "$work/$name.i" -o "$work/$name.gnu" ||
        fail "$name GNU native compile"
    "$minic" --target=x86_64-linux -S "$work/$name.i" -o "$work/$name.s" ||
        fail "$name MiniC compile"
    "$cc" -no-pie -x assembler "$work/$name.s" -o "$work/$name.minic" ||
        fail "$name MiniC native link"
    "$work/$name.gnu" || gnu_status=$?
    "$work/$name.minic" || minic_status=$?
    test "$gnu_status" -eq "$expected" || fail "$name unexpected GNU exit $gnu_status"
    test "$minic_status" -eq "$gnu_status" ||
        fail "$name GNU=$gnu_status MiniC=$minic_status"
}

check_runtime attributed \
    "$root/tests/compiler/c0/attributed_record_forward_declarations.c" 0
check_runtime audit \
    "$root/tests/compiler/audit/mc067_attributed_record_forward_declaration.c" 4

for target in riscv32-linux riscv64-linux aarch64-linux loongarch64-linux; do
    "$minic" --target="$target" -S "$work/attributed.i" -o "$work/$target.s" ||
        fail "$target front-end/backend compile"
    test -s "$work/$target.s" || fail "$target produced empty assembly"
done

"$cc" -std=gnu11 -E -P \
    "$root/tests/compiler/c0/attributed_record_forward_conflict.c" \
    -o "$work/negative.i" || fail 'GNU negative preprocessing'
if "$cc" -std=gnu11 -Werror -fsyntax-only -x c "$work/negative.i" \
    >"$work/negative.gnu.log" 2>&1; then
    fail 'GNU unexpectedly accepted conflicting tag kinds'
fi
if "$minic" --target=x86_64-linux -S "$work/negative.i" -o "$work/negative.s" \
    >"$work/negative.minic.log" 2>&1; then
    fail 'MiniC unexpectedly accepted conflicting tag kinds'
fi
test ! -e "$work/negative.s" || fail 'MiniC left partial negative assembly'
echo 'MC067_ATTRIBUTED_RECORD_FORWARD=PASS GNU_MiniC_x86_0_4_four_target_S_and_kind_reject'
