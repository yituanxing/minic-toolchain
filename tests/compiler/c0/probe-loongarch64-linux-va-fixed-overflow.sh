#!/bin/sh
# GNU LoongArch64 LP64D: 8 fixed GARs followed contiguously by caller
# overflow slots. Linux drm_universal_plane_init has 9 named arguments;
# config_item_set_name can initialize more than one independent va cursor.
set -eu
: "${MINIC_CC:?point MINIC_CC at the built compiler}"
src=tests/compiler/c0/loongarch64_linux_va_fixed_overflow.c
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
gcc -E -P -nostdinc -x c "$src" -o "$work/original.i"
gcc -std=gnu11 -O0 -Wall -Wextra -Werror -Wno-unused-parameter \
    "$src" -o "$work/gnu-native"
"$work/gnu-native"
"$MINIC_CC" --target=loongarch64-linux -fPIE -S "$work/original.i" \
    -o "$work/loong.s"

# Inspect actual stack and va_start placement, rather than accepting a
# successful textual -S whose cursor still points at the wrong register.
# va9: caller first overflow slot consumed by named argument #9, so next
# anonymous argument starts at entry-SP+8. va10 begins at entry-SP+16.
# Both va_start operations with two named GP params start at saved a2.
check_cursor() {
    awk -v symbol="$1" -v gp="$2" -v required="$3" '
        $0 == symbol ":" { active=1 }
        active && /^  li.d \$t3, [0-9]+$/ && frame == 0 {
            frame=$3+0
        }
        active && /^  li.d \$t0, [0-9]+$/ && frame != 0 {
            offset=$3+0
            if (offset == frame-64+gp*8) count++
        }
        active && $0 ~ "^\\.size "symbol"," { active=0 }
        END { if (frame == 0 || count < required) exit 1 }
    ' "$work/loong.s"
}
check_cursor loong_va9 9 1
check_cursor loong_va10 10 1
check_cursor loong_va_two_starts 2 2
echo 'MINIC_LOONG_VA_FIXED_OVERFLOW=PASS named=9,10 gp_stack=8,16 multiple_va_start=2 GNU_native=1'

# All other backends retain their independent ABI and their own feature
# policy. RV32 intentionally uses non-PIC default; AAPCS64 has its own
# more restrictive independent GR/VR state and need not admit these forms.
for target in riscv32-linux riscv64-linux x86_64-linux; do
    case "$target" in
        riscv32-linux) pie=-fno-pie ;;
        *) pie=-fPIE ;;
    esac
    "$MINIC_CC" --target="$target" "$pie" -S "$work/original.i" \
        -o "$work/$target.s"
done
echo 'MINIC_LOONG_VA_OTHER_ISA=PASS RV32=1 RV64=1 x86_64=1 AArch=unchanged'

# Distinct negative: the final named parameter is an exact frontend GNU
# va_start contract; weakening the prototype check to get more Linux TUs
# must still reject the wrong last-named parameter.
sed 's/__builtin_va_start(ap, i)/__builtin_va_start(ap, h)/' \
    "$work/original.i" >"$work/invalid.i"
if "$MINIC_CC" --target=loongarch64-linux -fPIE -S \
    "$work/invalid.i" -o "$work/invalid.s" 2>"$work/invalid.err"; then
    echo 'MINIC_LOONG_VA_NEGATIVE=FAIL wrong-last-parameter admitted' >&2
    exit 1
fi
test ! -e "$work/invalid.s"
echo 'MINIC_LOONG_VA_NEGATIVE=PASS wrong_last_named_parameter=rejected'
