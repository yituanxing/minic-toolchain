#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
work=${BUILD_DIR:-"$root/build/debug"}/tests/gnu-switch-case-range
mkdir -p "$work"
cat > "$work/positive.c" <<'SRC'
int classify(unsigned int v) {
    switch (v) {
    case 0x40 ... 0x7f: return 8;
    case 0x80 ... 0xbf: return 16;
    case 0xc0 ... 0xff: return 32;
    default: return 4;
    }
}
int main(void) { return classify(0x40) == 8 && classify(0xbf) == 16 && classify(0xff) == 32 && classify(3) == 4 ? 0 : 1; }
SRC
"$minic" -S "$work/positive.c" -o "$work/positive.s"
grep -Fq '.Lswitch_range_next_' "$work/positive.s"
cat > "$work/overlap.c" <<'SRC'
int f(int v) { switch (v) { case 1 ... 5: return 1; case 5 ... 9: return 2; default: return 0; } }
SRC
if "$minic" -S "$work/overlap.c" -o "$work/overlap.s" 2>"$work/overlap.err"; then exit 1; fi
grep -Fq 'duplicate or overlapping case value range' "$work/overlap.err"
cat > "$work/descending.c" <<'SRC'
int f(int v) { switch (v) { case 9 ... 1: return 1; default: return 0; } }
SRC
if "$minic" -S "$work/descending.c" -o "$work/descending.s" 2>"$work/descending.err"; then exit 1; fi
grep -Fq 'GNU case range upper bound is below lower bound' "$work/descending.err"
printf '%s\n' 'PASS compiler/c0/gnu-switch-case-range'
