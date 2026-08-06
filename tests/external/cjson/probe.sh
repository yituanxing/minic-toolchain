#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
riscv_cc=${RISCV_CC:-riscv64-linux-gnu-gcc}
vendor="$root/tests/vendor/cjson/upstream"
work=${BUILD_DIR:-"$root/build/debug"}/tests/external/cjson
include="$work/include"
preprocessed="$work/cJSON.i"
diagnostic="$work/minic.stderr"

rm -rf "$work"
mkdir -p "$include"

verify_vendor_file() {
    name=$1
    expected_blob=$2
    path="$vendor/$name"

    if test ! -f "$path"; then
        printf '%s\n' "FAIL external/cjson: missing vendored file $path" >&2
        exit 1
    fi

    actual_blob=$(git hash-object "$path")
    if test "$actual_blob" != "$expected_blob"; then
        printf '%s\n' \
            "FAIL external/cjson: $name blob=$actual_blob expected=$expected_blob" >&2
        exit 1
    fi
}

if ! command -v "$riscv_cc" >/dev/null 2>&1; then
    printf '%s\n' "FAIL external/cjson: missing RISC-V preprocessor $riscv_cc" >&2
    exit 1
fi
if ! command -v git >/dev/null 2>&1; then
    printf '%s\n' 'FAIL external/cjson: missing git for vendor identity checks' >&2
    exit 1
fi

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
    -o "$preprocessed"

first_line=$(sed -n '1p' "$preprocessed")
expected_line='typedef long unsigned int size_t;'
if test "$first_line" != "$expected_line"; then
    printf '%s\n' \
        "FAIL external/cjson: preprocessed first line changed: $first_line" >&2
    exit 1
fi

set +e
"$minic" -S "$preprocessed" -o "$work/cJSON.s" \
    >"$work/minic.stdout" 2>"$diagnostic"
status=$?
set -e

if test "$status" -eq 0; then
    printf '%s\n' \
        'FAIL external/cjson: cJSON crossed the recorded frontier; advance the project gate' >&2
    exit 1
fi

first_error=$(sed -n '/error:/p' "$diagnostic" | sed -n '1p')
case "$first_error" in
    *':1:9: error: expected type name')
        ;;
    *)
        printf '%s\n' \
            "FAIL external/cjson: unexpected first diagnostic: $first_error" >&2
        cat "$diagnostic" >&2
        exit 1
        ;;
esac

printf '%s\n' \
    'PASS external/cjson frontier=size_t-long-unsigned-int diagnostic=expected-type-name source=cJSON-1.7.19 offline=1'
