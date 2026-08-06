#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
riscv_cc=${RISCV_CC:-riscv64-linux-gnu-gcc}
qemu=${QEMU_RISCV64:-qemu-riscv64}
work=${BUILD_DIR:-"$root/build/debug"}/tests/external/tiny-aes-c
harness="$root/tests/external/tiny-aes-c/aes128_ecb_vectors.c"
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
    nl -ba "$work/aes128-ecb.i" | sed -n "${start},${end}p" >&2
}

run_elf() {
    elf=$1
    stdout_file=$2
    stderr_file=$3
    status_file=$4

    set +e
    "$qemu" "$elf" >"$stdout_file" 2>"$stderr_file"
    status=$?
    set -e
    printf '%s\n' "$status" >"$status_file"
}

report_difference() {
    kind=$1
    gcc_file=$2
    minic_file=$3

    printf '%s\n' "FAIL external/tiny-aes-c: $kind differs" >&2
    diff -u "$gcc_file" "$minic_file" >&2 || true
    printf '%s\n' "--- MiniC assembly ---" >&2
    sed -n '1,320p' "$work/aes128-ecb.minic.s" >&2 || true
    printf '%s\n' "Artifacts retained in $work" >&2
    exit 1
}

if ! command -v "$riscv_cc" >/dev/null 2>&1; then
    printf '%s\n' "FAIL external/tiny-aes-c: missing RISC-V compiler $riscv_cc" >&2
    exit 1
fi
if ! command -v "$qemu" >/dev/null 2>&1; then
    printf '%s\n' "FAIL external/tiny-aes-c: missing QEMU executor $qemu" >&2
    exit 1
fi

fetch_and_verify aes.c 4481f7b24ec964019d38669842913fd571d28ba3
fetch_and_verify aes.h b29b6683549632676ec11c06eb86efd02964db57
fetch_and_verify unlicense.txt 68a49daad8ff7e35068f2b7a97d643aab440eaec

"$riscv_cc" \
    -std=c11 -O0 -static \
    -I"$work/upstream" \
    -DECB=1 -DCBC=0 -DCTR=0 \
    -DMULTIPLY_AS_A_FUNCTION=1 \
    "$harness" \
    -o "$work/aes128-ecb.gcc.elf"

"$riscv_cc" \
    -E -P -nostdinc -x c \
    -I"$root/tests/external/tiny-aes-c/include" \
    -I"$work/upstream" \
    -DECB=1 -DCBC=0 -DCTR=0 \
    -DMULTIPLY_AS_A_FUNCTION=1 \
    "$harness" \
    -o "$work/aes128-ecb.i"

if ! "$minic" -S \
    "$work/aes128-ecb.i" \
    -o "$work/aes128-ecb.minic.s" \
    >"$work/minic.stdout" 2>"$work/minic.stderr"; then
    printf '%s\n' \
        "FAIL external/tiny-aes-c: native-byte AES vector harness did not compile" >&2
    cat "$work/minic.stderr" >&2
    print_failure_context "$(sed -n '1p' "$work/minic.stderr")"
    exit 1
fi

if ! "$riscv_cc" -c \
    "$work/aes128-ecb.minic.s" \
    -o "$work/aes128-ecb.minic.o"; then
    printf '%s\n' \
        "FAIL external/tiny-aes-c: MiniC assembly did not assemble" >&2
    exit 1
fi
if test ! -s "$work/aes128-ecb.minic.o"; then
    printf '%s\n' \
        "FAIL external/tiny-aes-c: assembled object is empty" >&2
    exit 1
fi

"$riscv_cc" -static \
    "$work/aes128-ecb.minic.s" \
    -o "$work/aes128-ecb.minic.elf"

riscv_nm=${riscv_cc%gcc}nm
if command -v "$riscv_nm" >/dev/null 2>&1; then
    "$riscv_nm" "$work/aes128-ecb.minic.o" >"$work/aes128-ecb.minic.nm"
    grep -F " AES_ECB_encrypt" "$work/aes128-ecb.minic.nm" >/dev/null
    grep -F " AES_ECB_decrypt" "$work/aes128-ecb.minic.nm" >/dev/null
fi

run_elf \
    "$work/aes128-ecb.gcc.elf" \
    "$work/aes128-ecb.gcc.stdout" \
    "$work/aes128-ecb.gcc.stderr" \
    "$work/aes128-ecb.gcc.status"
run_elf \
    "$work/aes128-ecb.minic.elf" \
    "$work/aes128-ecb.minic.stdout" \
    "$work/aes128-ecb.minic.stderr" \
    "$work/aes128-ecb.minic.status"

if ! cmp -s \
    "$work/aes128-ecb.gcc.status" \
    "$work/aes128-ecb.minic.status"; then
    report_difference \
        "exit status" \
        "$work/aes128-ecb.gcc.status" \
        "$work/aes128-ecb.minic.status"
fi
if ! cmp -s \
    "$work/aes128-ecb.gcc.stdout" \
    "$work/aes128-ecb.minic.stdout"; then
    report_difference \
        "standard output" \
        "$work/aes128-ecb.gcc.stdout" \
        "$work/aes128-ecb.minic.stdout"
fi
if ! cmp -s \
    "$work/aes128-ecb.gcc.stderr" \
    "$work/aes128-ecb.minic.stderr"; then
    report_difference \
        "standard error" \
        "$work/aes128-ecb.gcc.stderr" \
        "$work/aes128-ecb.minic.stderr"
fi

status=$(cat "$work/aes128-ecb.minic.status")
if test "$status" -ne 0; then
    printf '%s\n' \
        "FAIL external/tiny-aes-c: AES vector harness exited $status" >&2
    exit 1
fi

object_bytes=$(wc -c <"$work/aes128-ecb.minic.o" | tr -d ' ')
stdout_bytes=$(wc -c <"$work/aes128-ecb.minic.stdout" | tr -d ' ')
stderr_bytes=$(wc -c <"$work/aes128-ecb.minic.stderr" | tr -d ' ')
printf '%s\n' \
    "PASS external/tiny-aes-c acceptance=aes128-ecb-vectors exit=$status stdout=$stdout_bytes stderr=$stderr_bytes object=$object_bytes"
