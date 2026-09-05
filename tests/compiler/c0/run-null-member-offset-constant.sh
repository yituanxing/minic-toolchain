#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
work=${BUILD_DIR:-"$root/build/debug"}/tests/null-member-offset-constant
rm -rf "$work"
mkdir -p "$work"

cat >"$work/input.c" <<'SRC'
typedef unsigned long size_t;
typedef struct {
    int tag;
    void *payload;
} Aux;

typedef union {
    void *last;
    char padding[(size_t)((char *)&(((Aux *)0)->payload) - (char *)0)];
} Box;

char offset_is_eight[
    (size_t)((char *)&(((Aux *)0)->payload) - (char *)0) == 8 ? 1 : -1
];

int probe(void)
{
    return sizeof(Box) == sizeof(void *) ? 0 : 1;
}
SRC

"$minic" -S "$work/input.c" -o "$work/output.s"
test -s "$work/output.s"
grep -F 'offset_is_eight:' "$work/output.s" >/dev/null
grep -F 'probe:' "$work/output.s" >/dev/null

if command -v riscv64-linux-gnu-gcc >/dev/null 2>&1; then
    riscv64-linux-gnu-gcc -c "$work/output.s" -o "$work/output.o"
fi

printf '%s\n' 'PASS compiler/c0/null-member-offset-constant null-base=1 member-offset=8 pointer-difference=ICE'
