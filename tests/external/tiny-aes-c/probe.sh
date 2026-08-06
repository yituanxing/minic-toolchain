#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
riscv_cc=${RISCV_CC:-riscv64-linux-gnu-gcc}
qemu=${QEMU_RISCV64:-qemu-riscv64}
work=${BUILD_DIR:-"$root/build/debug"}/tests/external/tiny-aes-c
harness="$root/tests/external/tiny-aes-c/aes128_ecb_vectors.c"
vendor="$root/tests/vendor/tiny-aes-c/upstream"
diagnostic_file="$work/diagnostic.txt"
upstream_commit=23856752fbd139da0b8ca6e471a13d5bcc99a08d

mkdir -p "$work"
rm -f "$diagnostic_file"

verify_vendor_file() {
    name=$1
    expected_blob=$2
    path="$vendor/$name"

    if test ! -f "$path"; then
        printf '%s\n' \
            "FAIL external/tiny-aes-c: missing vendored file $path" >&2
        exit 1
    fi
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

print_function_assembly() {
    symbol=$1

    printf '%s\n' "--- MiniC assembly: $symbol ---" >&2
    sed -n "/^${symbol}:/,/^\.size ${symbol},/p" \
        "$work/aes128-ecb.minic.s" >&2 || true
}

decode_minic_status() {
    status=$1

    case "$status" in
    0)
        printf '%s\n' "all AES-128 ECB vector checks passed"
        ;;
    17|18|19|20|21|22|23|24|25|26|27|28|29|30|31|32)
        printf '%s\n' "initial AddRoundKey mismatch byte=$((status - 16))"
        ;;
    33|34|35|36|37|38|39|40|41|42|43|44|45|46|47|48)
        printf '%s\n' "SubBytes mismatch byte=$((status - 32))"
        ;;
    49|50|51|52|53|54|55|56|57|58|59|60|61|62|63|64)
        printf '%s\n' "ShiftRows mismatch byte=$((status - 48))"
        ;;
    65|66|67|68|69|70|71|72|73|74|75|76|77|78|79|80)
        printf '%s\n' "MixColumns mismatch byte=$((status - 64))"
        ;;
    81|82|83|84|85|86|87|88|89|90|91|92|93|94|95|96)
        printf '%s\n' "round-one AddRoundKey mismatch byte=$((status - 80))"
        ;;
    97|98|99|100|101|102|103|104|105|106|107|108|109|110|111|112)
        printf '%s\n' "final encryption mismatch byte=$((status - 96))"
        ;;
    113|114|115|116|117|118|119|120|121|122|123|124|125|126|127|128)
        printf '%s\n' "final decryption mismatch byte=$((status - 112))"
        ;;
    129)
        printf '%s\n' "constant S-box lookup mismatch index=0x4d"
        ;;
    130)
        printf '%s\n' "dynamic S-box lookup mismatch state-byte=6"
        ;;
    *)
        printf '%s\n' "unclassified harness status=$status"
        ;;
    esac
}

write_runtime_diagnostic() {
    kind=$1
    gcc_status=$2
    minic_status=$3
    decoded=$(decode_minic_status "$minic_status")

    {
        printf 'kind=%s\n' "$kind"
        printf 'gcc_status=%s\n' "$gcc_status"
        printf 'minic_status=%s\n' "$minic_status"
        printf 'decoded=%s\n' "$decoded"
        printf 'branch_scope=AES-128-ECB-test-vector-execution\n'
        printf 'upstream_commit=%s\n' "$upstream_commit"
    } >"$diagnostic_file"

    printf '::error title=tiny-AES differential failure::gcc=%s minic=%s %s\n' \
        "$gcc_status" "$minic_status" "$decoded" >&2
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
    gcc_status=$(cat "$work/aes128-ecb.gcc.status")
    minic_status=$(cat "$work/aes128-ecb.minic.status")

    write_runtime_diagnostic "$kind" "$gcc_status" "$minic_status"
    printf '%s\n' \
        "FAIL external/tiny-aes-c: $kind differs gcc=$gcc_status minic=$minic_status" >&2
    diff -u "$gcc_file" "$minic_file" >&2 || true
    print_function_assembly KeyExpansion
    print_function_assembly AddRoundKey
    print_function_assembly SubBytes
    print_function_assembly ShiftRows
    print_function_assembly MixColumns
    print_function_assembly Cipher
    print_function_assembly AES_ECB_encrypt
    print_function_assembly first_mismatch
    print_function_assembly check_stage
    print_function_assembly main
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
if ! command -v git >/dev/null 2>&1; then
    printf '%s\n' "FAIL external/tiny-aes-c: missing git for vendor identity checks" >&2
    exit 1
fi

verify_vendor_file aes.c 4481f7b24ec964019d38669842913fd571d28ba3
verify_vendor_file aes.h b29b6683549632676ec11c06eb86efd02964db57
verify_vendor_file unlicense.txt 68a49daad8ff7e35068f2b7a97d643aab440eaec

"$riscv_cc" \
    -std=c11 -O0 -static \
    -I"$vendor" \
    -DECB=1 -DCBC=0 -DCTR=0 \
    -DMULTIPLY_AS_A_FUNCTION=1 \
    "$harness" \
    -o "$work/aes128-ecb.gcc.elf"

"$riscv_cc" \
    -E -P -nostdinc -x c \
    -I"$root/tests/external/tiny-aes-c/include" \
    -I"$vendor" \
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
    write_runtime_diagnostic \
        "nonzero matching status" \
        "$(cat "$work/aes128-ecb.gcc.status")" \
        "$status"
    printf '%s\n' \
        "FAIL external/tiny-aes-c: AES vector harness exited $status" >&2
    exit 1
fi

object_bytes=$(wc -c <"$work/aes128-ecb.minic.o" | tr -d ' ')
stdout_bytes=$(wc -c <"$work/aes128-ecb.minic.stdout" | tr -d ' ')
stderr_bytes=$(wc -c <"$work/aes128-ecb.minic.stderr" | tr -d ' ')
printf '%s\n' \
    "PASS external/tiny-aes-c acceptance=aes128-ecb-vectors exit=$status stdout=$stdout_bytes stderr=$stderr_bytes object=$object_bytes"
