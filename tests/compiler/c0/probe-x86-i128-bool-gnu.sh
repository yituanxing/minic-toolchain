#!/usr/bin/env bash
# x86_64 MachineIR C __int128 -> _Bool / logical ! must observe all 128 bits.
# GNU GCC and MiniC compile the same source into distinct ELF64 objects; GNU
# AS/linker execute both on the host. Compare every result against C truth.
set -Eeuo pipefail
test "$#" -eq 1
cc=$1
test -x "$cc"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

cat > "$work/source.i" <<'SRC'
typedef unsigned __int128 U128;
typedef __int128 I128;
typedef _Bool Bool;

__attribute__((noinline)) Bool mini_return_u(U128 x) { return x; }
__attribute__((noinline)) Bool mini_return_s(I128 x) { return x; }
__attribute__((noinline)) Bool mini_assign(U128 x) {
    Bool result = x;
    return result;
}
__attribute__((noinline)) Bool mini_cast(U128 x) {
    return (Bool)x;
}
__attribute__((noinline)) int mini_if(U128 x) {
    if (x) return 17;
    return 29;
}
__attribute__((noinline)) int mini_ternary(U128 x) {
    return x ? 19 : 31;
}
__attribute__((noinline)) int mini_not(U128 x) {
    return !x;
}
__attribute__((noinline)) int mini_double_not(U128 x) {
    return !!x;
}
__attribute__((noinline)) Bool mini_mask(U128 x, U128 mask) {
    return x & mask;
}
SRC
sed 's/mini_/gnu_/g' "$work/source.i" > "$work/reference.c"

"$cc" --target=x86_64-linux -S "$work/source.i" -o "$work/minic.s"
test -s "$work/minic.s"
for fn in mini_return_u mini_return_s mini_assign mini_cast \
          mini_if mini_ternary mini_not mini_double_not mini_mask; do
    grep -Fq "$fn:" "$work/minic.s"
done
# Real selected x86 instructions must combine RDX (high) with RAX (low)
# before testing truth. This check is deliberately independent of GNU C.
grep -Eq '^[[:space:]]+orq[[:space:]]+%rdx,[[:space:]]*%rax' \
    "$work/minic.s" || {
    echo 'X86_I128_BOOL=FAIL missing physical upper-half truth OR' >&2
    exit 1
}
gcc -c -x assembler "$work/minic.s" -o "$work/minic.o"
gcc -std=gnu11 -O0 -fno-inline -fno-pic -fno-builtin \
    -c "$work/reference.c" -o "$work/gnu.o"
readelf -h "$work/minic.o" | grep -Eq 'Class:[[:space:]]+ELF64'
readelf -h "$work/gnu.o" | grep -Eq 'Class:[[:space:]]+ELF64'

cat > "$work/driver.c" <<'SRC'
typedef unsigned __int128 U128;
typedef __int128 I128;
typedef _Bool Bool;
extern Bool mini_return_u(U128), gnu_return_u(U128);
extern Bool mini_return_s(I128), gnu_return_s(I128);
extern Bool mini_assign(U128), gnu_assign(U128);
extern Bool mini_cast(U128), gnu_cast(U128);
extern int mini_if(U128), gnu_if(U128);
extern int mini_ternary(U128), gnu_ternary(U128);
extern int mini_not(U128), gnu_not(U128);
extern int mini_double_not(U128), gnu_double_not(U128);
extern Bool mini_mask(U128, U128), gnu_mask(U128, U128);
int main(void) {
    U128 values[] = {
        0, 1, 3, (U128)1 << 63, (U128)1 << 64,
        (U128)1 << 96, (U128)1 << 127,
        ((U128)1 << 127) | 1, ~(U128)0
    };
    int i;
    for (i = 0; i < 9; ++i) {
        U128 x = values[i];
        int truth = x != 0;
        if (mini_return_u(x) != gnu_return_u(x) ||
            mini_return_u(x) != truth) return 11;
        if (mini_return_s((I128)x) != gnu_return_s((I128)x) ||
            mini_return_s((I128)x) != truth) return 12;
        if (mini_assign(x) != gnu_assign(x) ||
            mini_assign(x) != truth) return 13;
        if (mini_cast(x) != gnu_cast(x) ||
            mini_cast(x) != truth) return 14;
        if (mini_if(x) != gnu_if(x) ||
            mini_if(x) != (truth ? 17 : 29)) return 15;
        if (mini_ternary(x) != gnu_ternary(x) ||
            mini_ternary(x) != (truth ? 19 : 31)) return 16;
        if (mini_not(x) != gnu_not(x) ||
            mini_not(x) != !truth) return 17;
        if (mini_double_not(x) != gnu_double_not(x) ||
            mini_double_not(x) != truth) return 18;
        if (mini_mask(x, (U128)1 << 64) !=
            gnu_mask(x, (U128)1 << 64) ||
            mini_mask(x, (U128)1 << 64) !=
            ((x & ((U128)1 << 64)) != 0)) return 19;
        if (mini_mask(x, (U128)1 << 127) !=
            gnu_mask(x, (U128)1 << 127) ||
            mini_mask(x, (U128)1 << 127) !=
            ((x & ((U128)1 << 127)) != 0)) return 20;
    }
    return 0;
}
SRC
gcc -std=gnu11 -O0 -fno-pic -no-pie \
    "$work/driver.c" "$work/minic.o" "$work/gnu.o" -o "$work/run"
status=0
"$work/run" || status=$?
if [ "$status" -ne 0 ]; then
    case "$status" in
        11) context=return_unsigned ;;
        12) context=return_signed ;;
        13) context=implicit_assignment ;;
        14) context=explicit_cast ;;
        15) context=direct_if ;;
        16) context=ternary_condition ;;
        17) context=logical_not ;;
        18) context=double_logical_not ;;
        19) context=and_mask_bit64 ;;
        20) context=and_mask_bit127 ;;
        *) context=unexpected_runtime_failure ;;
    esac
    echo "X86_I128_BOOL=FAIL context=$context guest_exit=$status" >&2
    exit 1
fi
echo 'X86_I128_BOOL=PASS x86_64=ELF64 GNU_GCC_GAS_LINK=1 MiniC_GAS=1 native_pairs=90 values=zero,low,bit63,bit64,bit96,bit127,highlow,multi conversions=return,assign,cast,if,ternary,logical,mask'
