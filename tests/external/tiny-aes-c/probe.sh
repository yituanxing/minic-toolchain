#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
riscv_cc=${RISCV_CC:-riscv64-linux-gnu-gcc}
work=${BUILD_DIR:-"$root/build/debug"}/tests/external/tiny-aes-c
upstream_commit=23856752fbd139da0b8ca6e471a13d5bcc99a08d
base_url="https://raw.githubusercontent.com/kokke/tiny-AES-c/$upstream_commit"

mkdir -p "$work/upstream"

fetch_and_verify() {
    name=$1
    expected_blob=$2
    path="$work/upstream/$name"

    curl --fail --location --silent --show-error \
        "$base_url/$name" -o "$path"
    actual_blob=$(git hash-object "$path")
    if test "$actual_blob" != "$expected_blob"; then
        printf '%s\n' \
            "FAIL external/tiny-aes-c: $name blob=$actual_blob expected=$expected_blob" >&2
        exit 1
    fi
}

print_failure_context() {
    diagnostic=$1
    line=$(printf '%s\n' "$diagnostic" | \
        sed -n 's/.*:\([0-9][0-9]*\):[0-9][0-9]*: error:.*/\1/p')
    if test -z "$line"; then
        return
    fi

    start=$((line - 2))
    if test "$start" -lt 1; then
        start=1
    fi
    end=$((line + 2))
    printf '%s\n' "--- preprocessed failure context ---" >&2
    nl -ba "$work/aes-ecb.i" | sed -n "${start},${end}p" >&2
}

fetch_and_verify aes.c 4481f7b24ec964019d38669842913fd571d28ba3
fetch_and_verify aes.h b29b6683549632676ec11c06eb86efd02964db57
fetch_and_verify unlicense.txt 68a49daad8ff7e35068f2b7a97d643aab440eaec

cat >"$work/aes-ecb-harness.c" <<'EOF'
#include "aes.c"

int main(void)
{
    return 0;
}
EOF

"$riscv_cc" \
    -E -P -nostdinc -x c \
    -I"$root/tests/external/tiny-aes-c/include" \
    -I"$work/upstream" \
    -DECB=1 -DCBC=0 -DCTR=0 \
    -DMULTIPLY_AS_A_FUNCTION=1 \
    "$work/aes-ecb-harness.c" \
    -o "$work/aes-ecb.i"

if ! "$minic" -S "$work/aes-ecb.i" -o "$work/aes-ecb.s" \
    >"$work/minic.stdout" 2>"$work/minic.stderr"; then
    printf '%s\n' \
        "FAIL external/tiny-aes-c: shimmed upstream core did not compile" >&2
    cat "$work/minic.stderr" >&2
    print_failure_context "$(sed -n '1p' "$work/minic.stderr")"
    exit 1
fi

if ! "$riscv_cc" -c "$work/aes-ecb.s" -o "$work/aes-ecb.o"; then
    printf '%s\n' \
        "FAIL external/tiny-aes-c: MiniC assembly did not assemble" >&2
    exit 1
fi
if test ! -s "$work/aes-ecb.o"; then
    printf '%s\n' \
        "FAIL external/tiny-aes-c: assembled object is empty" >&2
    exit 1
fi

riscv_nm=${riscv_cc%gcc}nm
if command -v "$riscv_nm" >/dev/null 2>&1; then
    "$riscv_nm" "$work/aes-ecb.o" >"$work/aes-ecb.nm"
    grep -F " AES_ECB_encrypt" "$work/aes-ecb.nm" >/dev/null
    grep -F " AES_ECB_decrypt" "$work/aes-ecb.nm" >/dev/null
fi

printf '%s\n' \
    "PASS external/tiny-aes-c frontier=shimmed-core-assembly object=$(wc -c <\"$work/aes-ecb.o\" | tr -d ' ')"
