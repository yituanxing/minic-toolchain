#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-enum-constant-scopes

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/enum_constant_scopes.c" \
    -o "$work/enum_constant_scopes.i"
"$minic" -S "$work/enum_constant_scopes.i" \
    -o "$work/enum_constant_scopes.s"
test -s "$work/enum_constant_scopes.s"
grep -F 'first:' "$work/enum_constant_scopes.s" >/dev/null
grep -F 'second:' "$work/enum_constant_scopes.s" >/dev/null

cat >"$work/duplicate.c" <<'EOF'
int main(void) {
    enum { ARG_same = 1 };
    enum { ARG_same = 2 };
    return ARG_same;
}
EOF
"$host_cc" -E -P -x c "$work/duplicate.c" -o "$work/duplicate.i"
if "$minic" -S "$work/duplicate.i" -o "$work/duplicate.s" \
    >"$work/duplicate.stdout" 2>"$work/duplicate.stderr"; then
    printf '%s\n' 'FAIL compiler/c0/enum_constant_scopes: same-scope duplicate accepted' >&2
    exit 1
fi
grep -F 'duplicate enumerator name' "$work/duplicate.stderr" >/dev/null

printf '%s\n' 'PASS compiler/c0/enum_constant_scopes cross-function=1 nested-shadow=1 restore-outer=1 duplicate-same-scope=reject'
