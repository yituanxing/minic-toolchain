#!/usr/bin/env bash
set -Eeuo pipefail

root=$(cd "$(dirname "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
cc=${HOST_CC:-cc}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail() { echo "MC063_TAGGED_BARE_RECORD=FAIL $*" >&2; exit 1; }

test -x "$minic" || fail 'MiniC binary missing'

check_runtime() {
    local name=$1 source=$2 expected=$3 gnu_status=0 minic_status=0
    "$cc" -std=gnu11 -E -P "$source" -o "$work/$name.i" ||
        fail "$name GNU preprocessing"
    # GNU C deliberately warns for the accepted but memberless declarations.
    "$cc" -std=gnu11 -O0 -no-pie -x c "$work/$name.i" -o "$work/$name.gnu" \
        >"$work/$name.gnu.log" 2>&1 || fail "$name GNU native compile"
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

check_runtime tagged "$root/tests/compiler/c0/tagged_bare_record_members.c" 0
check_runtime audit "$root/tests/compiler/audit/mc063_tagged_bare_record_field_layout.c" 48

"$cc" -std=gnu11 -E -P \
    "$root/tests/compiler/c0/tagged_bare_record_member_incomplete_field.c" \
    -o "$work/negative.i" || fail 'GNU negative preprocessing'
if "$cc" -std=gnu11 -Werror -fsyntax-only -x c "$work/negative.i" \
    >"$work/negative.gnu.log" 2>&1; then
    fail 'GNU unexpectedly accepted incomplete named member'
fi
if "$minic" --target=x86_64-linux -S "$work/negative.i" -o "$work/negative.s" \
    >"$work/negative.minic.log" 2>&1; then
    fail 'MiniC unexpectedly accepted incomplete named member'
fi
test ! -e "$work/negative.s" || fail 'MiniC left partial negative assembly'
echo 'MC063_TAGGED_BARE_RECORD=PASS GNU_MiniC_x86_native_0_48_and_named_field_reject'
