#!/usr/bin/env bash
set -Eeuo pipefail

mini=${1:?MiniC compiler binary required}
test -x "$mini"
here=$(cd "$(dirname "$0")" && pwd)
source_file="$here/loongarch64_packed_hardware_asm_results.c"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

cc -std=gnu11 -Wall -Wextra -Werror -fsyntax-only "$source_file"
cc -E -P -x c "$source_file" -o "$work/source.i"
"$mini" --target=loongarch64-linux -S "$work/source.i" -o "$work/source.s"
grep -Fq 'csrxchg $t1, $t0, 0' "$work/source.s"
grep -Fq 'amadd_db.d $t2, $t0, $t1' "$work/source.s"

# LP64 has 64-bit long and long long, but native GNU CSR output requires
# the original C unsigned long rank, while CSR mask requires unsigned int.
# Likewise AMO signed long/long long is not interchangeable with unsigned.
for variant in csr_mask_rank csr_result_rank amo_signedness csr_operand_order; do
    case "$variant" in
        csr_mask_rank)
            sed 's/unsigned int mask)/unsigned long mask)/' \
                "$work/source.i" > "$work/mutated.i" ;;
        csr_result_rank)
            sed 's/unsigned long old;/unsigned long long old;/' \
                "$work/source.i" > "$work/mutated.i" ;;
        amo_signedness)
            sed 's/static long naturally_aligned_signed_mem;/static unsigned long naturally_aligned_signed_mem;/' \
                "$work/source.i" > "$work/mutated.i" ;;
        csr_operand_order)
            sed 's/csrxchg %0, %1, %2/csrxchg %0, %2, %1/' \
                "$work/source.i" > "$work/mutated.i" ;;
    esac
    if "$mini" --target=loongarch64-linux -S "$work/mutated.i" \
        -o "$work/mutated.s" >/dev/null 2>&1; then
        echo "LOONG_SCALAR_SOURCE_RANK=FAIL accepted $variant" >&2
        exit 1
    fi
    test ! -e "$work/mutated.s"
done
echo 'LOONG_SCALAR_SOURCE_RANK=PASS unsigned_long_output unsigned_int_mask signed_AMO operand_order GNU_C=PASS'
