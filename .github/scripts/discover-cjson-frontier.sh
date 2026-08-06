#!/usr/bin/env bash
set -Eeuo pipefail

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"

minic=${MINIC:-"$root/build/debug/bin/minic"}
riscv_cc=${RISCV_CC:-riscv64-linux-gnu-gcc}
vendor="$root/tests/vendor/cjson/upstream"
work="$root/build/cjson-frontier-discovery"
include="$work/include"

rm -rf "$work"
mkdir -p "$include"

verify_vendor_file() {
    local name=$1
    local expected_blob=$2
    local path="$vendor/$name"

    [[ -f "$path" ]] || {
        printf 'FAIL cjson-frontier: missing %s\n' "$path" >&2
        exit 1
    }
    local actual_blob
    actual_blob=$(git hash-object "$path")
    [[ "$actual_blob" == "$expected_blob" ]] || {
        printf 'FAIL cjson-frontier: %s blob=%s expected=%s\n' \
            "$name" "$actual_blob" "$expected_blob" >&2
        exit 1
    }
}

verify_vendor_file cJSON.c 6e4fb0dd369cd905923da515be87ab06db6c1ee0
verify_vendor_file cJSON.h cab5feb427725f8e5c82287f7fe59481b609b9b5
verify_vendor_file LICENSE 78deb0406d713ab9730e3c2447be1abdbd70b9a2

cat >"$include/stddef.h" <<'EOF'
#ifndef MINIC_CJSON_STDDEF_H
#define MINIC_CJSON_STDDEF_H
typedef __SIZE_TYPE__ size_t;
#define NULL ((void *)0)
#endif
EOF

cat >"$include/limits.h" <<'EOF'
#ifndef MINIC_CJSON_LIMITS_H
#define MINIC_CJSON_LIMITS_H
#define INT_MAX __INT_MAX__
#define INT_MIN (-__INT_MAX__ - 1)
#endif
EOF

cat >"$include/float.h" <<'EOF'
#ifndef MINIC_CJSON_FLOAT_H
#define MINIC_CJSON_FLOAT_H
#define DBL_EPSILON 2.22044604925031308084726333618164062e-16
#endif
EOF

for header in string.h stdio.h math.h stdlib.h ctype.h locale.h; do
    guard=$(printf 'MINIC_CJSON_%s' "$header" | tr '[:lower:].' '[:upper:]_')
    cat >"$include/$header" <<EOF
#ifndef $guard
#define $guard
#endif
EOF
done

"$riscv_cc" \
    -E -P -nostdinc -std=c11 \
    -U__GNUC__ -U__GNUC_MINOR__ -U__GNUC_PATCHLEVEL__ \
    -I"$include" -I"$vendor" \
    "$vendor/cJSON.c" \
    -o "$work/cJSON.i"

set +e
"$minic" -S "$work/cJSON.i" -o "$work/cJSON.s" \
    >"$work/minic.stdout" 2>"$work/minic.stderr"
status=$?
set -e

if [[ "$status" -eq 0 ]]; then
    printf '%s\n' 'DISCOVERY cJSON unexpectedly compiled completely' >&2
    exit 1
fi

first=$(sed -n '/error:/p' "$work/minic.stderr" | sed -n '1p')
if [[ -z "$first" ]]; then
    printf '%s\n' 'FAIL cjson-frontier: MiniC failed without an error diagnostic' >&2
    cat "$work/minic.stderr" >&2
    exit 1
fi

printf 'DISCOVERY cJSON first-diagnostic: %s\n' "$first" >&2
line=$(printf '%s\n' "$first" | sed -n 's/.*:\([0-9][0-9]*\):[0-9][0-9]*: error:.*/\1/p')
if [[ -n "$line" ]]; then
    start=$((line - 4))
    (( start < 1 )) && start=1
    end=$((line + 4))
    printf '%s\n' '--- preprocessed cJSON context ---' >&2
    nl -ba "$work/cJSON.i" | sed -n "${start},${end}p" >&2
fi

printf '%s\n' 'Intentional discovery failure: record this frontier, then replace this temporary workflow with an offline expected-frontier gate.' >&2
exit 1
