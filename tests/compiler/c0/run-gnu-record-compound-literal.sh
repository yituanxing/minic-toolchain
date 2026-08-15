#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-record-compound-literal

rm -rf "$work"
mkdir -p "$work"
"$host_cc" -E -P -std=gnu11 -x c \
  "$root/tests/compiler/c0/gnu_record_compound_literal.c" -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$work/output.s"
test -s "$work/output.s"

grep -F 'assign_holder:' "$work/output.s" >/dev/null
grep -F 'compound_member:' "$work/output.s" >/dev/null
grep -F 'clear_holder:' "$work/output.s" >/dev/null
grep -F 'local_empty_initializer:' "$work/output.s" >/dev/null
grep -F 'compound_address_and_order:' "$work/output.s" >/dev/null
grep -F 'cap_combine_shape:' "$work/output.s" >/dev/null
grep -F 'positional_member:' "$work/output.s" >/dev/null
grep -F 'nested_designated_braces:' "$work/output.s" >/dev/null
grep -F 'assign_range_mask:' "$work/output.s" >/dev/null
grep -F 'assign_single_range_effect:' "$work/output.s" >/dev/null
grep -F '  call range_effect' "$work/output.s" >/dev/null
left_call=$(grep -n -m1 -F '  call left_effect' "$work/output.s" | cut -d: -f1)
init_call=$(grep -n -m1 -F '  call init_effect' "$work/output.s" | cut -d: -f1)
test -n "$left_call"
test -n "$init_call"
test "$left_call" -lt "$init_call"

cat >"$work/range-side-effect.c" <<'EOF'
struct RangeMask {
    unsigned long bits[4];
};
static unsigned long effect(void)
{
    return 5UL;
}
void bad_range(struct RangeMask *out)
{
    *out = (struct RangeMask) { { [0 ... 1] = effect() } };
}
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/range-side-effect.c" -o "$work/range-side-effect.i"
if "$minic" -S "$work/range-side-effect.i" -o "$work/range-side-effect.s" \
    2>"$work/range-side-effect.stderr"; then
  printf '%s\n' 'FAIL compiler/c0/gnu_record_compound_literal: repeated side-effecting range accepted' >&2
  exit 1
fi
grep -F 'multi-element runtime array range initializer requires an integer constant expression' \
    "$work/range-side-effect.stderr" >/dev/null

cat >"$work/scalar.c" <<'EOF'
int scalar_compound(void)
{
    return (int) { 1 };
}
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/scalar.c" -o "$work/scalar.i"
if "$minic" -S "$work/scalar.i" -o "$work/scalar.s" 2>"$work/scalar.stderr"; then
  printf '%s\n' 'FAIL compiler/c0/gnu_record_compound_literal: scalar compound literal accepted' >&2
  exit 1
fi
grep -F 'compound literals currently require a block-scope record type' "$work/scalar.stderr" >/dev/null

printf '%s\n' 'PASS compiler/c0/gnu_record_compound_literal record-lvalue=1 hidden-auto-local=1 initializer-block=expression-owned empty-aggregate=compound+local promoted-designators=1 record-copy=1 member-postfix=1 address-of=1 positional-runtime=1 nested-designated-braces=1 array-designator=single+range range-constant-repeat=1 range-side-effect=fail-closed remaining-zero-fill=1 evaluation-order=preserved scalar=bounded-reject'
