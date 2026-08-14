#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-typeof-generic
assembly="$work/typeof_generic.s"

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/typeof_generic.c" \
    -o "$work/typeof_generic.i"
"$minic" -S "$work/typeof_generic.i" -o "$assembly"

test -s "$assembly"
for symbol in generic_selected_value generic_default_value generic_controlling_is_unevaluated \
              typeof_expression_size typeof_type_name_size typeof_generic_pointer_size \
              typeof_incomplete_object_address typeof_function_redeclaration; do
    grep -F "$symbol:" "$assembly" >/dev/null
done
# Selected/default generic values must survive frontend selection lowering.
grep -F '  li a0, 7' "$assembly" >/dev/null
grep -F '  li a0, 9' "$assembly" >/dev/null
grep -F '  li a0, 11' "$assembly" >/dev/null
# The controlling expression of _Generic is unevaluated: no emitted call to this declaration.
if grep -F 'call generic_side_effect' "$assembly" >/dev/null; then
    printf '%s\n' 'FAIL compiler/c0/typeof_generic controlling expression was emitted' >&2
    exit 1
fi
# RV64 unsigned long and pointers are both eight bytes.
size8=$(grep -c '  li a0, 8' "$assembly" || true)
test "$size8" -ge 3
grep -F 'typeof_pending_object' "$assembly" >/dev/null
# GNU typeof of a function designator preserves the function type rather than
# the execution-time function-pointer representation used by ordinary expressions.
test "$(grep -c '^typeof_function_redeclaration:$' "$assembly")" -eq 1
grep -F '.globl typeof_function_redeclaration' "$assembly" >/dev/null

cat >"$work/incomplete-sizeof.c" <<'EOF'
struct StillPending;
unsigned long bad_size(void) { return sizeof(__typeof__(struct StillPending)); }
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/incomplete-sizeof.c" -o "$work/incomplete-sizeof.i"
if "$minic" -S "$work/incomplete-sizeof.i" -o "$work/incomplete-sizeof.s" \
    2>"$work/incomplete-sizeof.stderr"; then
    printf '%s\n' 'FAIL compiler/c0/typeof_generic: sizeof incomplete typeof accepted' >&2
    exit 1
fi
grep -F 'incomplete record type requires pointer declarator' "$work/incomplete-sizeof.stderr" >/dev/null

printf '%s\n' 'PASS compiler/c0/typeof_generic typeof=expression,type-name,incomplete-type-preserved,function-designator-preserved generic=typed,default controlling=unevaluated linux-shape=1 completeness=consumer-owned'
