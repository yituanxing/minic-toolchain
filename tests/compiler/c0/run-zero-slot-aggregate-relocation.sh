#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
work=${BUILD_DIR:-"$root/build/debug"}/tests/zero-slot-aggregate-relocation
mkdir -p "$work"
cat > "$work/input.c" <<'SRC'
struct Empty {};
int target;
struct Holder {
    struct Empty empty;
    int *pointer;
};
static struct Holder holder = { {}, &target };
int main(void) { return holder.pointer == &target ? 0 : 1; }
SRC
"$minic" -S "$work/input.c" -o "$work/output.s"
grep -Fq 'holder:' "$work/output.s"
grep -Fq '  .dword target' "$work/output.s"
printf '%s\n' 'PASS compiler/c0/zero-slot-aggregate-relocation'
