#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-local-label-address
assembly="$work/gnu_local_label_address.s"

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/gnu_local_label_address.c" \
    -o "$work/gnu_local_label_address.i"
"$minic" -S "$work/gnu_local_label_address.i" -o "$assembly"

test -s "$assembly"
grep -F 'first_local_label:' "$assembly" >/dev/null
grep -F 'repeated_local_labels:' "$assembly" >/dev/null
grep -F 'forward_local_label:' "$assembly" >/dev/null
grep -E '^\.Luser_[0-9]+:$' "$assembly" >/dev/null
grep -E '^  la a0, \.Luser_[0-9]+$' "$assembly" >/dev/null
label_count=$(grep -E -c '^\.Luser_[0-9]+:$' "$assembly")
address_count=$(grep -E -c '^  la a0, \.Luser_[0-9]+$' "$assembly")
test "$label_count" -ge 4
test "$address_count" -ge 4

cat >"$work/unknown_label.c" <<'EOF'
unsigned long bad(void) {
    return (unsigned long)&&missing;
}
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/unknown_label.c" -o "$work/unknown_label.i"
if "$minic" -S "$work/unknown_label.i" -o "$work/unknown_label.s" \
    >"$work/unknown_label.stdout" 2>"$work/unknown_label.stderr"; then
    printf '%s\n' 'unknown GNU label address unexpectedly compiled' >&2
    exit 1
fi
grep -F 'address of unknown label' "$work/unknown_label.stderr" >/dev/null

printf '%s\n' \
    "PASS compiler/c0/gnu_local_label_address local-scope=1 duplicate-names=1 forward-address=1 labels=$label_count addresses=$address_count"
