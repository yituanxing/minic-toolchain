#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
work=${BUILD_DIR:-"$root/build/debug"}/tests/static-local-self-reference
mkdir -p "$work"
cat > "$work/input.c" <<'SRC'
struct Node { struct Node *self; int value; };
static int probe(void) {
    static struct Node node = { .self = &node, .value = 7 };
    return node.self == &node && node.value == 7 ? 0 : 1;
}
int main(void) { return probe(); }
SRC
"$minic" -S "$work/input.c" -o "$work/output.s"
grep -Fq '  .dword ' "$work/output.s"
grep -Fq '  .word 7' "$work/output.s"
printf '%s\n' 'PASS compiler/c0/static-local-self-reference'
