#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host_cc=${HOST_CC:-cc}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
cat >"$work/input.c" <<'SRC'
static int targets[4] = {3, 5, 7, 11};
struct Flat { unsigned char a; int *p; unsigned short b; };
static struct Flat single = {17, &targets[1], 23};
static struct Flat array[4] = {
    {29, &targets[2], 31}, {37, &targets[3], 41}
};
struct Nested { union { int *p; unsigned long n; } u; int v; };
static struct Nested nested[2] = {
    { .u.p = &targets[0], .v = 43 }, { .u.n = 47, .v = 53 }
};
int main(void) {
    return !(single.a == 17 && single.p == &targets[1] && single.b == 23 &&
             array[0].a == 29 && array[0].p == &targets[2] && array[0].b == 31 &&
             array[1].a == 37 && array[1].p == &targets[3] && array[1].b == 41 &&
             array[2].a == 0 && array[2].p == 0 && array[2].b == 0 &&
             array[3].a == 0 && array[3].p == 0 && array[3].b == 0 &&
             nested[0].u.p == &targets[0] && nested[0].v == 43 &&
             nested[1].u.n == 47 && nested[1].v == 53);
}
SRC
"$host_cc" -std=gnu11 -no-pie "$work/input.c" -o "$work/gnu"
"$work/gnu"
"$host_cc" -E -P -std=gnu11 -x c "$work/input.c" -o "$work/input.i"
for target in x86_64 aarch64 loongarch64 riscv64 riscv32; do
    "$minic" --target="$target-linux" -S "$work/input.i" -o "$work/$target.s"
    test -s "$work/$target.s"
done
"$host_cc" -no-pie -x assembler "$work/x86_64.s" -o "$work/minic"
"$work/minic"
printf '%s\n' 'PASS flat-global-payload: standalone + array + implicit zero tail + recursive union; five target source output; GNU and MiniC x86 native'
