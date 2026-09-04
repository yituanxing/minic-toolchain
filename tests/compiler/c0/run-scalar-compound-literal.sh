#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
work=${BUILD_DIR:-"$root/build/debug"}/tests/scalar-compound-literal
rm -rf "$work"
mkdir -p "$work"

cat >"$work/int.c" <<'SRC'
int probe(void)
{
    return (int){ 0 } != 0;
}

int probe_value(void)
{
    return (int){ 7 };
}

int main(void)
{
    return probe() != 0 || probe_value() != 7;
}
SRC
"$minic" -S "$work/int.c" -o "$work/int.s"
grep -F '.globl probe' "$work/int.s" >/dev/null
grep -F '.globl probe_value' "$work/int.s" >/dev/null
grep -E '^[[:space:]]+sw[[:space:]]+[^,]+,[[:space:]]*0\([^)]*\)
cat >"$work/trailing-comma.c" <<'SRC'
int probe(void)
{
    return (int){ 3, };
}
SRC
"$minic" -S "$work/trailing-comma.c" -o "$work/trailing-comma.s"

cat >"$work/too-many.c" <<'SRC'
int probe(void)
{
    return (int){ 1, 2 };
}
SRC
if "$minic" -S "$work/too-many.c" -o "$work/too-many.s" 2>"$work/too-many.err"; then
    echo 'multi-value scalar compound literal unexpectedly compiled' >&2
    exit 1
fi
grep -F 'scalar compound literal requires exactly one initializer' "$work/too-many.err" >/dev/null

printf '%s\n' 'PASS compiler/c0/scalar-compound-literal hidden-local=shared owned-block=shared rvalue-load=core runtime=rv64 trailing-comma=1 multi-value=fail-closed'
 "$work/int.s" >/dev/null
grep -E '^[[:space:]]+lw[[:space:]]+[^,]+,[[:space:]]*0\([^)]*\)
cat >"$work/trailing-comma.c" <<'SRC'
int probe(void)
{
    return (int){ 3, };
}
SRC
"$minic" -S "$work/trailing-comma.c" -o "$work/trailing-comma.s"

cat >"$work/too-many.c" <<'SRC'
int probe(void)
{
    return (int){ 1, 2 };
}
SRC
if "$minic" -S "$work/too-many.c" -o "$work/too-many.s" 2>"$work/too-many.err"; then
    echo 'multi-value scalar compound literal unexpectedly compiled' >&2
    exit 1
fi
grep -F 'scalar compound literal requires exactly one initializer' "$work/too-many.err" >/dev/null

printf '%s\n' 'PASS compiler/c0/scalar-compound-literal hidden-local=shared owned-block=shared rvalue-load=rv64 trailing-comma=1 multi-value=fail-closed'
 "$work/int.s" >/dev/null
if command -v riscv64-linux-gnu-gcc >/dev/null 2>&1 &&
   command -v qemu-riscv64 >/dev/null 2>&1; then
    riscv64-linux-gnu-gcc -static "$work/int.s" -o "$work/int"
    qemu-riscv64 "$work/int"
fi

cat >"$work/trailing-comma.c" <<'SRC'
int probe(void)
{
    return (int){ 3, };
}
SRC
"$minic" -S "$work/trailing-comma.c" -o "$work/trailing-comma.s"

cat >"$work/too-many.c" <<'SRC'
int probe(void)
{
    return (int){ 1, 2 };
}
SRC
if "$minic" -S "$work/too-many.c" -o "$work/too-many.s" 2>"$work/too-many.err"; then
    echo 'multi-value scalar compound literal unexpectedly compiled' >&2
    exit 1
fi
grep -F 'scalar compound literal requires exactly one initializer' "$work/too-many.err" >/dev/null

printf '%s\n' 'PASS compiler/c0/scalar-compound-literal hidden-local=shared owned-block=shared rvalue-load=rv64 trailing-comma=1 multi-value=fail-closed'
