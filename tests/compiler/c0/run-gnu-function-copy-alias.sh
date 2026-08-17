#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
work=${BUILD_DIR:-"$root/build/debug"}/tests/gnu-function-copy-alias
mkdir -p "$work"
cat > "$work/positive.c" <<'SRC'
static int target(void) { return 7; }
static inline __attribute__((__copy__(target))) __attribute__((alias("target"))) int alias_fn(void);
int main(void) { return alias_fn() == 7 ? 0 : 1; }
SRC
"$minic" -S "$work/positive.c" -o "$work/positive.s"
grep -Fq '.set alias_fn, target' "$work/positive.s"
cat > "$work/mismatch.c" <<'SRC'
static int target(int value) { return value; }
static inline __attribute__((alias("target"))) int alias_fn(void);
SRC
if "$minic" -S "$work/mismatch.c" -o "$work/mismatch.s" 2>"$work/mismatch.err"; then exit 1; fi
grep -Fq 'GNU function alias requires a defined same-TU target with matching signature' "$work/mismatch.err"
cat > "$work/copy-unknown.c" <<'SRC'
static inline __attribute__((__copy__(missing_target))) int alias_fn(void);
SRC
if "$minic" -S "$work/copy-unknown.c" -o "$work/copy-unknown.s" 2>"$work/copy-unknown.err"; then exit 1; fi
grep -Fq 'GNU copy requires a previously declared function' "$work/copy-unknown.err"
cat > "$work/undefined.c" <<'SRC'
static int target(void);
static inline __attribute__((alias("target"))) int alias_fn(void);
SRC
if "$minic" -S "$work/undefined.c" -o "$work/undefined.s" 2>"$work/undefined.err"; then exit 1; fi
grep -Fq 'GNU function alias requires a defined same-TU target with matching signature' "$work/undefined.err"
printf '%s\n' 'PASS compiler/c0/gnu-function-copy-alias'
