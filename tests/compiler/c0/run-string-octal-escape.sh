#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
work=${BUILD_DIR:-"$root/build/debug"}/tests/string-octal-escape
mkdir -p "$work"
cat > "$work/positive.c" <<'SRC'
static const char text[] = "\101\102\103\0\7";
int main(void) {
    return text[0] == 'A' && text[1] == 'B' && text[2] == 'C' &&
           text[3] == 0 && text[4] == 7 && text[5] == 0 ? 0 : 1;
}
SRC
"$minic" -S "$work/positive.c" -o "$work/positive.s"
grep -Fq '  .byte 65' "$work/positive.s"
grep -Fq '  .byte 66' "$work/positive.s"
grep -Fq '  .byte 67' "$work/positive.s"
grep -Fq '  .byte 7' "$work/positive.s"
cat > "$work/overflow.c" <<'SRC'
static const char text[] = "\400";
SRC
if "$minic" -S "$work/overflow.c" -o "$work/overflow.s" 2>"$work/overflow.err"; then
    echo 'FAIL compiler/c0/string-octal-escape: out-of-range octal escape accepted' >&2
    exit 1
fi
grep -Fq 'unsupported string escape' "$work/overflow.err"
printf '%s\n' 'PASS compiler/c0/string-octal-escape'
