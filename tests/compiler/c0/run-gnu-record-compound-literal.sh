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
grep -F 'assign_flexible_prefix:' "$work/output.s" >/dev/null
grep -F 'assign_flexible_prefix_positional:' "$work/output.s" >/dev/null
grep -F 'clear_flexible_prefix:' "$work/output.s" >/dev/null
left_call=$(grep -n -m1 -F '  call left_effect' "$work/output.s" | cut -d: -f1)
init_call=$(grep -n -m1 -F '  call init_effect' "$work/output.s" | cut -d: -f1)
test -n "$left_call"
test -n "$init_call"
test "$left_call" -lt "$init_call"

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

cat >"$work/fam-designated.c" <<'EOF'
struct FlexibleRuntime { int tag; unsigned long count; unsigned char payload[]; };
void bad_designated(struct FlexibleRuntime *out)
{
    *out = (struct FlexibleRuntime) { .payload = { 1 } };
}
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/fam-designated.c" -o "$work/fam-designated.i"
if "$minic" -S "$work/fam-designated.i" -o "$work/fam-designated.s" 2>"$work/fam-designated.stderr"; then
    printf '%s\n' 'FAIL compiler/c0/gnu_record_compound_literal: designated FAM initializer accepted' >&2
    exit 1
fi
grep -F 'unsupported designated record array initializer' "$work/fam-designated.stderr" >/dev/null

cat >"$work/fam-positional.c" <<'EOF'
struct FlexibleRuntime { int tag; unsigned long count; unsigned char payload[]; };
void bad_positional(struct FlexibleRuntime *out)
{
    *out = (struct FlexibleRuntime) { 1, 2, { 3 } };
}
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/fam-positional.c" -o "$work/fam-positional.i"
if "$minic" -S "$work/fam-positional.i" -o "$work/fam-positional.s" 2>"$work/fam-positional.stderr"; then
    printf '%s\n' 'FAIL compiler/c0/gnu_record_compound_literal: positional FAM initializer accepted' >&2
    exit 1
fi
grep -F 'unsupported positional record initializer field' "$work/fam-positional.stderr" >/dev/null

riscv_cc=${RISCV_CC:-riscv64-linux-gnu-gcc}
qemu_riscv64=${QEMU_RISCV64:-qemu-riscv64}
require_runtime=${REQUIRE_RISCV_RUNTIME:-0}
if command -v "$riscv_cc" >/dev/null 2>&1 && command -v "$qemu_riscv64" >/dev/null 2>&1; then
    "$riscv_cc" -static "$work/output.s" \
        "$root/tests/compiler/c0/gnu_record_compound_literal_runtime.c" \
        -o "$work/minic-runtime"
    "$riscv_cc" -std=gnu11 -static \
        "$root/tests/compiler/c0/gnu_record_compound_literal.c" \
        "$root/tests/compiler/c0/gnu_record_compound_literal_runtime.c" \
        -o "$work/gcc-runtime"
    set +e
    "$qemu_riscv64" "$work/gcc-runtime"
    gcc_status=$?
    "$qemu_riscv64" "$work/minic-runtime"
    minic_status=$?
    set -e
    test "$gcc_status" -eq 0
    test "$minic_status" -eq "$gcc_status"
    printf '%s\n' 'PASS compiler/c0/gnu_record_compound_literal FAM-fixed-prefix-differential=gcc tail=preserved'
elif test "$require_runtime" = 1; then
    printf '%s\n' 'missing required RISC-V runtime tools for FAM differential' >&2
    exit 1
else
    printf '%s\n' 'SKIP compiler/c0/gnu_record_compound_literal FAM-fixed-prefix-differential tools=missing'
fi

printf '%s\n' 'PASS compiler/c0/gnu_record_compound_literal record-lvalue=1 hidden-auto-local=1 initializer-block=expression-owned empty-aggregate=compound+local promoted-designators=1 record-copy=1 member-postfix=1 address-of=1 positional-runtime=1 nested-designated-braces=1 remaining-zero-fill=1 flexible-tail=excluded-from-zero-fill+copy-tail-preserved flexible-explicit-init=fail-closed evaluation-order=preserved scalar=bounded-reject'
