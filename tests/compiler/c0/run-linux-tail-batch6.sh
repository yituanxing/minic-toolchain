#!/bin/sh
set -eu
# Batch6 validates one canonical tentative-definition lifecycle for static aggregate/pointer arrays.
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
work=${BUILD_DIR:-"$root/build/debug"}/tests/linux-tail-batch6
rm -rf "$work"
mkdir -p "$work"

cat >"$work/record-array.c" <<'SRC'
struct row { int value; };

static struct row inferred[];
static struct row fixed[2];

static struct row inferred[] = {
    { 1 },
    { 2 },
    { 3 },
};
static struct row fixed[2] = {
    { 4 },
    { 5 },
};

static struct row already[1] = { { 7 } };
static struct row already[];
SRC
"$minic" -S "$work/record-array.c" -o "$work/record-array.s"
grep -F '.size inferred, 12' "$work/record-array.s" >/dev/null
grep -F '.size fixed, 8' "$work/record-array.s" >/dev/null
grep -F '.size already, 4' "$work/record-array.s" >/dev/null

cat >"$work/pointer-array.c" <<'SRC'
struct clock { int id; };
static const struct clock realtime = { 1 };
static const struct clock monotonic = { 2 };
static const struct clock * const clocks[];
static const struct clock * const clocks[] = {
    [0] = &realtime,
    [1] = &monotonic,
};
SRC
"$minic" -S "$work/pointer-array.c" -o "$work/pointer-array.s"
grep -F '.size clocks, 16' "$work/pointer-array.s" >/dev/null

cat >"$work/conflict.c" <<'SRC'
struct row { int value; };
static struct row rows[2];
static struct row rows[3] = { { 1 }, { 2 }, { 3 } };
SRC
if "$minic" -S "$work/conflict.c" -o "$work/conflict.s" 2>"$work/conflict.err"; then
    echo 'conflicting static record array counts unexpectedly compiled' >&2
    exit 1
fi
grep -F 'conflicting static record array' "$work/conflict.err" >/dev/null

printf '%s\n' 'PASS compiler/c0/linux-tail-batch6 record-array=tentative+incomplete+fixed pointer-array=incomplete+definition composite=compatible conflict=fail-closed'
