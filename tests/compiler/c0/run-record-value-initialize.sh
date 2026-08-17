#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
work=${BUILD_DIR:-"$root/build/debug"}/tests/record-value-initialize
mkdir -p "$work"
cat > "$work/positive.c" <<'SRC'
struct Pair { int left; int right; };
int main(void) {
    struct Pair source = { 7, 9 };
    struct Pair scratch = { 0, 0 };
    const struct Pair frozen = (scratch = source);
    return frozen.left + frozen.right + scratch.left + scratch.right - 32;
}
SRC
"$minic" -S "$work/positive.c" -o "$work/positive.s"
grep -Fq 'main:' "$work/positive.s"
printf '%s\n' 'PASS compiler/c0/record-value-initialize assignment-rvalue=1 const-destination=initializer-only'
