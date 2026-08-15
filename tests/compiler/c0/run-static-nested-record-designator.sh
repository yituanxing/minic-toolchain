#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
build_dir=${BUILD_DIR:-"$root/build/debug"}
work="$build_dir/tests/compiler-c0-static-nested-record-designator"
mkdir -p "$work"

"$minic" -S \
    "$root/tests/programs/c0/static_nested_record_designator.c" \
    -o "$work/positive.s"
for value in 3 7 9; do
    grep -F "  .word $value" "$work/positive.s" >/dev/null
done
grep -F '  .word 0' "$work/positive.s" >/dev/null

"$minic" -S \
    "$root/tests/compiler/c0/static_nested_record_designator_backward_scalar.c" \
    -o "$work/backward-scalar.s"
first_line=$(grep -n -F '  .word 2' "$work/backward-scalar.s" | head -n 1 | cut -d: -f1)
second_line=$(grep -n -F '  .word 1' "$work/backward-scalar.s" | head -n 1 | cut -d: -f1)
test -n "$first_line"
test -n "$second_line"
test "$first_line" -lt "$second_line"

cat >"$work/backward-aggregate.c" <<'EOF'
struct Inner {
    int x;
    int y;
};

struct Outer {
    struct Inner inner;
    int marker;
};

static struct Outer value = {
    .marker = 1,
    .inner = {.x = 2, .y = 3},
};

int main(void) {
    return value.inner.x + value.marker;
}
EOF

if "$minic" -S \
    "$work/backward-aggregate.c" \
    -o "$work/backward-aggregate.s" \
    >"$work/backward-aggregate.stdout" 2>"$work/backward-aggregate.stderr"; then
    printf '%s\n' 'FAIL compiler/c0/static-nested-record-designator-backward-aggregate: unexpectedly succeeded' >&2
    exit 1
fi
grep -F 'backward static record designator currently requires a direct scalar field' \
    "$work/backward-aggregate.stderr" >/dev/null

printf '%s\n' \
    'PASS compiler/c0/static-nested-record-designator direct-member=1 skipped-fields=zero continuation=next-field backward-direct-scalar=1 declaration-order=canonical backward-aggregate=fail-closed'
