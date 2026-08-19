#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
work=${BUILD_DIR:-"$root/build/debug"}/tests/linux-tail-batch3
mkdir -p "$work"

cat >"$work/field-head.c" <<'SRC'
struct show;
struct show {
    __attribute__((__format__(printf, 2, 0)))
    void (*showfn)(struct show *show, const char *fmt, void *args);
};
static struct show state;
int main(void) { return sizeof(state) == 8 ? 0 : 1; }
SRC
"$minic" -S "$work/field-head.c" -o "$work/field-head.s"
grep -F '.size state, 8' "$work/field-head.s" >/dev/null

cat >"$work/alias-section.c" <<'SRC'
int target(int value) __attribute__((section(".init.text")));
int alias_fn(int value) __attribute__((section(".init.text")));
int alias_fn(int value) __attribute__((weak, alias("target")));
int target(int value) { return value + 1; }
SRC
"$minic" -S "$work/alias-section.c" -o "$work/alias-section.s"
grep -F '.weak alias_fn' "$work/alias-section.s" >/dev/null
grep -F '.set alias_fn, target' "$work/alias-section.s" >/dev/null

cat >"$work/alias-section-mismatch.c" <<'SRC'
int target(int value) __attribute__((section(".target")));
int alias_fn(int value) __attribute__((section(".alias")));
int alias_fn(int value) __attribute__((weak, alias("target")));
int target(int value) { return value + 1; }
SRC
if "$minic" -S "$work/alias-section-mismatch.c" -o "$work/alias-section-mismatch.s" \
    2>"$work/alias-section-mismatch.err"; then
    echo 'mismatched alias sections unexpectedly compiled' >&2
    exit 1
fi
grep -F 'parsed AST violates compiler contracts' "$work/alias-section-mismatch.err" >/dev/null

cat >"$work/flatten.c" <<'SRC'
static int __attribute__((flatten)) add_one(int value) { return value + 1; }
int main(void) { return add_one(4) == 5 ? 0 : 1; }
SRC
"$minic" -S "$work/flatten.c" -o "$work/flatten.s"

cat >"$work/many-args.c" <<'SRC'
extern int sink(const char *fmt, ...);
int caller(void)
{
    return sink("x",
                1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
                11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
                21, 22, 23, 24, 25, 26, 27, 28, 29, 30,
                31, 32, 33, 34, 35, 36, 37, 38, 39, 40);
}
SRC
"$minic" -S "$work/many-args.c" -o "$work/many-args.s"
test -s "$work/many-args.s"

printf '%s\n' 'PASS compiler/c0/linux-tail-batch3 field-head-format=1 alias-section=compatible flatten=parse-only call-capacity=64'
