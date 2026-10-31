#!/bin/sh
# Original UDF check_add_overflow(sizeof(...), le32_to_cpu(...), &alen).
# The exact 65-bit arithmetic must include unsigned carry and the i32
# destination range. A successful -S is checked for real carry/width ops.
set -eu
: "${MINIC_CC:?point MINIC_CC at the built compiler}"
src=tests/compiler/c0/loongarch64_linux_u64_u32_checked_add.c
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
gcc -E -P -nostdinc -x c "$src" -o "$work/original.i"
"$MINIC_CC" --target=loongarch64-linux -fPIE -S \
    "$work/original.i" -o "$work/loong.s"
test "$(grep -Fc '  sltu $t3, $t0, $t3' "$work/loong.s")" -eq 2
test "$(grep -Fc '  xor $t5, $t4, $t0' "$work/loong.s")" -eq 2
test "$(grep -Fc '  st.w $t4, $t2, 0' "$work/loong.s")" -eq 2
echo 'MINIC_LOONG_U64_U32_ADD=PASS order_both=2 high_carry=2 signed_i32_range=2 store=2'

cat >"$work/oracle.c" <<'SRC'
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
int udf_checked_add_u64_u32(unsigned long, unsigned int, int *);
int udf_checked_add_u32_u64(unsigned int, unsigned long, int *);
int main(void) {
    const unsigned long wide[] = {
        0UL, 1UL, INT_MAX - 1UL, INT_MAX, (unsigned long)INT_MAX + 1UL,
        UINT_MAX, ULONG_MAX - 1UL, ULONG_MAX
    };
    const unsigned int narrow[] = {
        0U, 1U, 2U, INT_MAX, (unsigned int)INT_MAX + 1U,
        UINT_MAX - 1U, UINT_MAX
    };
    unsigned long state = 0xabcdef123456789UL;
    size_t i, j, tested = 0U;
    for (i = 0U; i < sizeof(wide) / sizeof(wide[0]) + 4096U; ++i) {
        unsigned long a;
        if (i < sizeof(wide) / sizeof(wide[0])) a = wide[i];
        else { state ^= state << 13; state ^= state >> 7; state ^= state << 17; a = state; }
        for (j = 0U; j < sizeof(narrow) / sizeof(narrow[0]); ++j) {
            const unsigned int b = narrow[j];
            const unsigned __int128 sum =
                (unsigned __int128)a + (unsigned __int128)b;
            const int expected_flag = sum > (unsigned __int128)INT_MAX;
            const int expected_value = (int)(unsigned int)sum;
            int out0 = 0, out1 = 0;
            const int flag0 = udf_checked_add_u64_u32(a, b, &out0);
            const int flag1 = udf_checked_add_u32_u64(b, a, &out1);
            if (flag0 != expected_flag || flag1 != expected_flag ||
                out0 != expected_value || out1 != expected_value) {
                fprintf(stderr, "GNU overflow oracle mismatch i=%zu j=%zu\n", i, j);
                return 1;
            }
            ++tested;
        }
    }
    printf("MINIC_LOONG_U64_U32_GNU_ORACLE=PASS pairs=%zu orientations=2\n", tested);
    return 0;
}
SRC
gcc -std=gnu11 -O2 -Wall -Wextra -Werror \
    "$src" "$work/oracle.c" -o "$work/oracle"
"$work/oracle"

# Smaller source integer widths are not implicitly the same qualified
# u64+u32->i32 hardware path; preserve fail-closed target admission.
sed 's/int \*written/unsigned short *written/g' \
    "$work/original.i" >"$work/invalid.i"
if "$MINIC_CC" --target=loongarch64-linux -fPIE -S \
    "$work/invalid.i" -o "$work/invalid.s" 2>"$work/invalid.err"; then
    echo 'MINIC_LOONG_U64_U32_NEGATIVE=FAIL unsupported u16 result admitted' >&2
    exit 1
fi
test ! -e "$work/invalid.s"
echo 'MINIC_LOONG_U64_U32_NEGATIVE=PASS u16_destination=rejected'
