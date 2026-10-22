#!/bin/sh
# MiniC caller / real GNU libdiv() callee returns 8-byte div_t in GP ABI.
set -eu
: "$MINIC_CC"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
cat >"$work/div_consumer.c" <<'SRC'
typedef struct { int quot; int rem; } my_div_t;
extern my_div_t div(int numerator, int denominator);
int main(void) {
    int x = 987;
    my_div_t result = div(x, 10);
    if (sizeof(result) != 8 || result.quot != 98 || result.rem != 7)
        return 1;
    return 42;
}
SRC
for target in aarch64 loongarch64; do
    case "$target" in
      aarch64) gcc=aarch64-linux-gnu-gcc; qemu=qemu-aarch64 ;;
      loongarch64) gcc=loongarch64-linux-gnu-gcc-13; qemu=qemu-loongarch64 ;;
    esac
    "$gcc" -static "$work/div_consumer.c" -o "$work/$target.gnu"
    if ! "$MINIC_CC" --target="$target-linux" -S "$work/div_consumer.c" \
             -o "$work/$target.s" 2>"$work/minic.err"; then
        echo "FOUR_BACKEND_SMALL_RECORD_RETURN=FAIL target=$target stage=MiniC" >&2
        head -20 "$work/minic.err" >&2
        exit 1
    fi
    if ! "$gcc" -static -x assembler "$work/$target.s" -o "$work/$target.mini" \
           >"$work/gnu_as.out" 2>"$work/gnu_as.err"; then
        echo "FOUR_BACKEND_SMALL_RECORD_RETURN=FAIL target=$target stage=GNU_AS" >&2
        head -20 "$work/gnu_as.err" >&2
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
        echo "FOUR_BACKEND_SMALL_RECORD_RETURN=FAIL target=$target stage=GNU_QEMU gnu=$grc mini=$mrc" >&2
        exit 1
    fi
    echo "FOUR_BACKEND_SMALL_RECORD_RETURN=PASS target=$target GNU_QEMU_div_t=1"
done
echo "FOUR_BACKEND_SMALL_RECORD_RETURN_ACCEPT=PASS targets=2"
