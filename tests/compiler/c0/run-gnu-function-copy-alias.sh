#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
work=${BUILD_DIR:-"$root/build/debug"}/tests/gnu-function-copy-alias
mkdir -p "$work"

require_text() {
    file=$1
    text=$2
    label=$3
    if ! grep -Fq "$text" "$file"; then
        printf '%s\n' "FAIL compiler/c0/gnu-function-copy-alias step=$label missing=$text" >&2
        sed -n '1,220p' "$file" >&2
        exit 1
    fi
}

cat > "$work/positive.c" <<'SRC'
static int target(void) { return 7; }
static inline __attribute__((__copy__(target))) __attribute__((alias("target"))) int alias_fn(void);
int main(void) { return alias_fn() == 7 ? 0 : 1; }
SRC
if ! "$minic" -S "$work/positive.c" -o "$work/positive.s"     >"$work/positive.stdout" 2>"$work/positive.err"; then
    printf '%s\n' 'FAIL compiler/c0/gnu-function-copy-alias step=positive-compile' >&2
    cat "$work/positive.err" >&2
    exit 1
fi
require_text "$work/positive.s" '.set alias_fn, target' positive-set

cat > "$work/forward.c" <<'SRC'
int target(int value);
int __attribute__((weak, alias("target"))) alias_fn(int value);
int target(int value) { return value + 1; }
SRC
if ! "$minic" -S "$work/forward.c" -o "$work/forward.s"     >"$work/forward.stdout" 2>"$work/forward.err"; then
    printf '%s\n' 'FAIL compiler/c0/gnu-function-copy-alias step=forward-compile' >&2
    cat "$work/forward.err" >&2
    exit 1
fi
require_text "$work/forward.s" '.weak alias_fn' forward-weak
require_text "$work/forward.s" '.set alias_fn, target' forward-set

cat > "$work/mismatch.c" <<'SRC'
static int target(int value) { return value; }
static inline __attribute__((alias("target"))) int alias_fn(void);
SRC
if "$minic" -S "$work/mismatch.c" -o "$work/mismatch.s"     >"$work/mismatch.stdout" 2>"$work/mismatch.err"; then
    printf '%s\n' 'FAIL compiler/c0/gnu-function-copy-alias step=mismatch-unexpected-success' >&2
    exit 1
fi
require_text "$work/mismatch.err"     'GNU function alias requires a defined same-TU target with matching signature' mismatch-diagnostic

cat > "$work/copy-unknown.c" <<'SRC'
static inline __attribute__((__copy__(missing_target))) int alias_fn(void);
SRC
if "$minic" -S "$work/copy-unknown.c" -o "$work/copy-unknown.s"     >"$work/copy-unknown.stdout" 2>"$work/copy-unknown.err"; then
    printf '%s\n' 'FAIL compiler/c0/gnu-function-copy-alias step=copy-unknown-unexpected-success' >&2
    exit 1
fi
require_text "$work/copy-unknown.err"     'GNU copy requires a previously declared function' copy-unknown-diagnostic

cat > "$work/undefined.c" <<'SRC'
static int target(void);
static inline __attribute__((alias("target"))) int alias_fn(void);
SRC
if "$minic" -S "$work/undefined.c" -o "$work/undefined.s"     >"$work/undefined.stdout" 2>"$work/undefined.err"; then
    printf '%s\n' 'FAIL compiler/c0/gnu-function-copy-alias step=undefined-unexpected-success' >&2
    exit 1
fi
require_text "$work/undefined.err"     'parsed AST violates compiler contracts' undefined-diagnostic

printf '%s\n' 'PASS compiler/c0/gnu-function-copy-alias'
