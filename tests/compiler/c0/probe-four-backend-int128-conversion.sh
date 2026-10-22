#!/bin/sh
# GNU/QEMU differential: both signed and unsigned 64->128->64 conversion.
set -eu
: "$MINIC_CC"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
cat >"$work/main.c" <<'SRC'
extern unsigned long payload(void);
extern long signed_payload(void);
int main(void) {
    unsigned long u = payload();
    long n = signed_payload();
    unsigned long round_u = (unsigned long)((unsigned __int128)u);
    long round_s = (long)((__int128)n);
    return (round_u == u && round_s == n) ? 42 : 1;
}
SRC
cat >"$work/provider.c" <<'SRC'
unsigned long payload(void) { return 0xfedcba9876543210UL; }
long signed_payload(void) { return -77362348L; }
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
        echo "FOUR_BACKEND_INT128_CONVERSION=FAIL target=$target stage=minic" >&2
        sed -n '1,15p' "$work/mini.err" >&2
        exit 1
    fi
    if ! "$cc" -static -x assembler "$work/$target.s" -x none \
        "$work/$target.provider.o" -o "$work/$target.mini" \
        >"$work/as.out" 2>"$work/as.err"; then
        echo "FOUR_BACKEND_INT128_CONVERSION=FAIL target=$target stage=GNU_AS" >&2
        sed -n '1,15p' "$work/as.err" >&2
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
        echo "FOUR_BACKEND_INT128_CONVERSION=FAIL target=$target stage=GNU_QEMU gnu=$grc mini=$mrc" >&2
        exit 1
    fi
    echo "FOUR_BACKEND_INT128_CONVERSION=PASS target=$target GNU_QEMU_diff=1"
done
echo "FOUR_BACKEND_INT128_CONVERSION_ACCEPT=PASS targets=2"
