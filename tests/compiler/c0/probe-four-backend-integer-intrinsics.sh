#!/bin/sh
# Cross-GNU oracle: checked 32/64-bit arithmetic and CLZ/CTZ primitives.
set -eu
: "${MINIC_CC:?set MINIC_CC to the built minic-cc}"
w=$(mktemp -d)
trap 'rm -rf "$w"' EXIT HUP INT TERM
cat >"$w/ints.i" <<'SRC'
static int checks(void) {
    int s32 = 0;
    unsigned int u32 = 0;
    long s64 = 0;
    unsigned long u64 = 0;
    if (!__builtin_add_overflow(2147483647, 1, &s32) ||
        s32 != (-2147483647-1)) return 1;
    if (!__builtin_sub_overflow((-2147483647-1), 1, &s32) ||
        s32 != 2147483647) return 2;
    if (!__builtin_mul_overflow(50000, 50000, &s32) ||
        s32 != (int)2500000000U) return 3;
    if (!__builtin_add_overflow(4294967295U, 1U, &u32) ||
        u32 != 0U) return 4;
    if (!__builtin_sub_overflow(0U, 1U, &u32) ||
        u32 != 4294967295U) return 5;
    if (!__builtin_mul_overflow(65536U, 65536U, &u32) ||
        u32 != 0U) return 6;
    if (!__builtin_add_overflow(9223372036854775807L, 1L, &s64) ||
        s64 != (-9223372036854775807L-1L)) return 7;
    if (!__builtin_sub_overflow((-9223372036854775807L-1L), 1L, &s64) ||
        s64 != 9223372036854775807L) return 8;
    if (!__builtin_mul_overflow(9223372036854775807L, 2L, &s64) ||
        s64 != -2L) return 9;
    if (!__builtin_add_overflow(18446744073709551615UL, 1UL, &u64) ||
        u64 != 0UL) return 10;
    if (!__builtin_sub_overflow(0UL, 1UL, &u64) ||
        u64 != 18446744073709551615UL) return 11;
    if (!__builtin_mul_overflow(18446744073709551615UL, 2UL, &u64) ||
        u64 != 18446744073709551614UL) return 12;
    if (__builtin_add_overflow(20, 22, &s32) || s32 != 42) return 13;
    if (__builtin_mul_overflow(3UL, 7UL, &u64) || u64 != 21UL) return 14;
    if (__builtin_clz(1U) != 31 || __builtin_clz(0x80000000U) != 0 ||
        __builtin_ctz(2U) != 1 || __builtin_ctz(16U) != 4)
        return 15;
    if (__builtin_clzll(1UL) != 63 ||
        __builtin_clzll(0x8000000000000000UL) != 0 ||
        __builtin_ctzll(8UL) != 3 ||
        __builtin_ctzll(0x100000000UL) != 32)
        return 16;
    /* Generic GNU overflow uses infinite precision, including a 65th bit
     * when an unsigned LP64 operand meets a signed 32-bit operand. */
    {
        unsigned long large = 0x80000000UL;
        int small = -1;
        if (__builtin_add_overflow(large, small, &s32) ||
            s32 != 2147483647) return 17;
        large = ~0UL;
        small = 1;
        if (!__builtin_add_overflow(large, small, &s32) ||
            s32 != 0) return 18;
        large = 0UL;
        small = -1;
        if (__builtin_add_overflow(small, large, &s32) ||
            s32 != -1) return 19;
        large = 0x80000001UL;
        if (!__builtin_add_overflow(small, large, &s32) ||
            s32 != (-2147483647-1)) return 20;
    }
    return 0;
}
int main(void) { return checks() == 0 ? 42 : 1; }
SRC
for target in aarch64 loongarch64; do
  case "$target" in
    aarch64) gcc=aarch64-linux-gnu-gcc; qemu=qemu-aarch64 ;;
    loongarch64) gcc=loongarch64-linux-gnu-gcc-13; qemu=qemu-loongarch64 ;;
  esac
  "$gcc" -static -x c "$w/ints.i" -o "$w/$target.gnu"
  MINIC_M1_TRACE=1 "$MINIC_CC" --target="$target-linux" \
       -S "$w/ints.i" -o "$w/$target.s"
  "$gcc" -static -x assembler "$w/$target.s" -o "$w/$target.mini"
  for cc in gnu mini; do
      set +e
      timeout 45s "$qemu" "$w/$target.$cc" >"$w/$target.$cc.out" 2>"$w/$target.$cc.err"
      rc=$?
      set -e
      if [ "$rc" -ne 42 ]; then
          echo "INTEGER_INTRINSICS=FAIL target=$target compiler=$cc stage=QEMU status=$rc" >&2
          sed -n '1,12p' "$w/$target.$cc.err" >&2
          exit 1
      fi
  done
  cmp "$w/$target.gnu.out" "$w/$target.mini.out"
  cmp "$w/$target.gnu.err" "$w/$target.mini.err"
  echo "INTEGER_INTRINSICS=PASS target=$target signed=1 unsigned=1 widths=32,64 add=1 sub=1 mul=1 mixed_add=1 clz=1 ctz=1 GNU_QEMU=1"
done
echo "INTEGER_INTRINSICS_ACCEPT=PASS targets=2"
