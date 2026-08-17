#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
work=${BUILD_DIR:-"$root/build/debug"}/tests/gnu-signed-left-shift-ice
mkdir -p "$work"
cat > "$work/positive.c" <<'SRC'
enum { SIGN_BIT = 1 << 31 };
static unsigned int value = (unsigned int)SIGN_BIT;
int main(void) { return value == 0x80000000U ? 0 : 1; }
SRC
"$minic" -S "$work/positive.c" -o "$work/positive.s"
grep -Fq 'value:' "$work/positive.s"
grep -Eq '2147483648|0x80000000|-2147483648' "$work/positive.s"
cat > "$work/negative.c" <<'SRC'
enum { BAD_SHIFT = (-1) << 1 };
int main(void) { return BAD_SHIFT; }
SRC
if "$minic" -S "$work/negative.c" -o "$work/negative.s" 2>"$work/negative.err"; then
    echo 'FAIL compiler/c0/gnu-signed-left-shift-ice: negative signed shift accepted as ICE' >&2
    exit 1
fi
grep -Fq 'integer constant expression' "$work/negative.err"
printf '%s\n' 'PASS compiler/c0/gnu-signed-left-shift-ice'
