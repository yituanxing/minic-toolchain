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

fetch_and_verify aes.c 4481f7b24ec964019d38669842913fd571d28ba3
fetch_and_verify aes.h b29b6683549632676ec11c06eb86efd02964db57
fetch_and_verify unlicense.txt 68a49daad8ff7e35068f2b7a97d643aab440eaec

"$riscv_cc" \
    -E -P -nostdinc -x c \
    -I"$root/tests/external/tiny-aes-c/include" \
    -I"$work/upstream" \
    -DECB=1 -DCBC=0 -DCTR=0 \
    -DMULTIPLY_AS_A_FUNCTION=1 \
    "$work/upstream/aes.c" \
    -o "$work/aes-ecb.i"

if "$minic" -S "$work/aes-ecb.i" -o "$work/aes-ecb.s" \
    >"$work/minic.stdout" 2>"$work/minic.stderr"; then
    printf '%s\n' \
        "FAIL external/tiny-aes-c: upstream core unexpectedly compiled; advance the gate to execution" >&2
    exit 1
fi

if ! grep -F ":75:25:" "$work/minic.stderr" >/dev/null ||
   ! grep -F "use of undeclared local" "$work/minic.stderr" >/dev/null; then
    printf '%s\n' \
        "FAIL external/tiny-aes-c: unexpected compiler frontier" >&2
    cat "$work/minic.stderr" >&2
    exit 1
fi

frontier=$(sed -n '1p' "$work/minic.stderr")
printf '%s\n' \
    "PASS external/tiny-aes-c frontier=KeyExpansion-global-sbox-reference diagnostic=$frontier"
