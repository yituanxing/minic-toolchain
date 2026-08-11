#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-enum-constant-expressions

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/enum_constant_expression.c" \
    -o "$work/enum_constant_expression.i"
"$minic" -S "$work/enum_constant_expression.i" \
    -o "$work/enum_constant_expression.s"
test -s "$work/enum_constant_expression.s"

grep -F '  li a0, 256' "$work/enum_constant_expression.s" >/dev/null
grep -F '  li a0, 264' "$work/enum_constant_expression.s" >/dev/null

cat >"$work/nonconstant.c" <<'EOF'
extern int runtime_value;
enum Invalid {
    INVALID_ENUM_VALUE = runtime_value ? 1 : 2,
};
EOF
"$host_cc" -E -P -x c "$work/nonconstant.c" -o "$work/nonconstant.i"
if "$minic" -S "$work/nonconstant.i" -o "$work/nonconstant.s" \
    >"$work/nonconstant.stdout" 2>"$work/nonconstant.stderr"; then
    printf '%s\n' 'FAIL compiler/c0/enum_constant_expression: runtime initializer accepted' >&2
    exit 1
fi
grep -F 'enum initializer must be an integer constant expression' \
    "$work/nonconstant.stderr" >/dev/null

printf '%s\n' \
    'PASS compiler/c0/enum_constant_expression typed-ast-consteval=1 arithmetic=1 prior-enumerator=1 relational=1 logical=1 conditional=linux-shape short-circuit=1 runtime=reject'
