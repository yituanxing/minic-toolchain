#!/bin/sh
# Complete pair preservation through GNU volatile memory and 128 >> 64.
set -eu
: "$MINIC_CC"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
cat >"$work/main.c" <<'SRC'
extern volatile unsigned __int128 input_pair;
extern volatile unsigned __int128 output_pair;
extern int check_output_pair(void);
int main(void) {
    unsigned __int128 value = input_pair;
    output_pair = value;
    unsigned long high = (unsigned long)(value >> 64);
    unsigned long low = (unsigned long)value;
    return (high == 0x123456789abcdef0UL &&
            low == 0xfedcba9876543210UL &&
            check_output_pair()) ? 42 : 1;
}
SRC
cat >"$work/provider.c" <<'SRC'
volatile unsigned __int128 input_pair =
    (((unsigned __int128)0x123456789abcdef0ULL) << 64) |
    (unsigned __int128)0xfedcba9876543210ULL;
volatile unsigned __int128 output_pair;
int check_output_pair(void) { return input_pair == output_pair; }
SRC
for target in aarch64 loongarch64; do
    case "$target" in
        aarch64) cc=aarch64-linux-gnu-gcc; qemu=qemu-aarch64 ;;
        loongarch64) cc=loongarch64-linux-gnu-gcc-13; qemu=qemu-loongarch64 ;;
    esac
    "$cc" -static "$work/main.c" "$work/provider.c" -o "$work/$target.gnu"
    "$cc" -c "$work/provider.c" -o "$work/$target.provider.o"
    if ! "$MINIC_CC" --target="$target-linux" -S "$work/main.c" \
        -o "$work/$target.s" >"$work/mini.out" 2>"$work/mini.err"; then
        echo "FOUR_BACKEND_INT128_MEMORY=FAIL target=$target stage=minic" >&2
        sed -n '1,18p' "$work/mini.err" >&2
        exit 1
    fi
    if ! "$cc" -static -x assembler "$work/$target.s" -x none \
        "$work/$target.provider.o" -o "$work/$target.mini" \
        >"$work/as.out" 2>"$work/as.err"; then
        echo "FOUR_BACKEND_INT128_MEMORY=FAIL target=$target stage=GNU_AS_LD" >&2
        sed -n '1,18p' "$work/as.err" >&2
        exit 1
    fi
    set +e
    timeout 45s "$qemu" "$work/$target.gnu" >"$work/$target.gnu.out" 2>"$work/$target.gnu.err"
    grc=$?
    timeout 45s "$qemu" "$work/$target.mini" >"$work/$target.mini.out" 2>"$work/$target.mini.err"
    mrc=$?
    set -e
    if [ "$grc" -ne 42 ] || [ "$mrc" -ne 42 ] ||
       ! cmp -s "$work/$target.gnu.out" "$work/$target.mini.out" ||
       ! cmp -s "$work/$target.gnu.err" "$work/$target.mini.err"; then
        echo "FOUR_BACKEND_INT128_MEMORY=FAIL target=$target stage=GNU_QEMU gnu=$grc mini=$mrc" >&2
        exit 1
    fi
    echo "FOUR_BACKEND_INT128_MEMORY=PASS target=$target volatile_load_store=1 shift64=1 GNU_QEMU_diff=1"
done
echo "FOUR_BACKEND_INT128_MEMORY_ACCEPT=PASS targets=2"
