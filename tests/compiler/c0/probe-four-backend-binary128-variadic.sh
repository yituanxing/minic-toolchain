#!/bin/sh
# GNU/glibc/QEMU differential for binary128 unnamed variadic arguments.
# AArch64 uses Q registers; LoongArch LP64D uses aligned GP pairs.
set -eu
: "$MINIC_CC"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
cat >"$work/consumer.c" <<'SRC'
extern int snprintf(char *, unsigned long, const char *, ...);
extern int strcmp(const char *, const char *);
extern long double get_binary128(void);
static char rendered[128];
int main(void) {
    if (snprintf(rendered, sizeof rendered, "%.1Lf", get_binary128()) != 3)
        return 1;
    if (strcmp(rendered, "2.5") != 0)
        return 2;
    if (snprintf(rendered, sizeof rendered, "%d %d %d %d %d %.1Lf",
                 1, 2, 3, 4, 5, get_binary128()) != 13)
        return 3;
    if (strcmp(rendered, "1 2 3 4 5 2.5") != 0)
        return 4;
    return 42;
}
SRC
cat >"$work/provider.c" <<'SRC'
long double get_binary128(void) { return 2.5L; }
SRC
for target in aarch64 loongarch64; do
    case "$target" in
        aarch64) gcc=aarch64-linux-gnu-gcc; qemu=qemu-aarch64 ;;
        loongarch64) gcc=loongarch64-linux-gnu-gcc-13; qemu=qemu-loongarch64 ;;
    esac
    "$gcc" -static "$work/consumer.c" "$work/provider.c" -o "$work/$target.gnu"
    "$gcc" -c "$work/provider.c" -o "$work/$target.provider.o"
    if ! "$MINIC_CC" --target="$target-linux" -S "$work/consumer.c" \
             -o "$work/$target.s" >"$work/mini.out" 2>"$work/mini.err"; then
        echo "FOUR_BACKEND_BINARY128_VARIADIC=FAIL target=$target stage=minic" >&2
        sed -n '1,14p' "$work/mini.err" >&2
        exit 1
    fi
    if ! "$gcc" -static -x assembler "$work/$target.s" -x none \
             "$work/$target.provider.o" -o "$work/$target.mini" \
             >"$work/as.out" 2>"$work/as.err"; then
        echo "FOUR_BACKEND_BINARY128_VARIADIC=FAIL target=$target stage=gnu_as_ld" >&2
        sed -n '1,14p' "$work/as.err" >&2
        exit 1
    fi
    set +e
    timeout 45s "$qemu" "$work/$target.gnu" >"$work/$target.gnu.out" 2>"$work/$target.gnu.err"
    gnu_rc=$?
    timeout 45s "$qemu" "$work/$target.mini" >"$work/$target.mini.out" 2>"$work/$target.mini.err"
    mini_rc=$?
    set -e
    if [ "$gnu_rc" -ne 42 ] || [ "$mini_rc" -ne 42 ] ||
       ! cmp -s "$work/$target.gnu.out" "$work/$target.mini.out" ||
       ! cmp -s "$work/$target.gnu.err" "$work/$target.mini.err"; then
        echo "FOUR_BACKEND_BINARY128_VARIADIC=FAIL target=$target stage=qemu_diff gnu=$gnu_rc mini=$mini_rc" >&2
        exit 1
    fi
    echo "FOUR_BACKEND_BINARY128_VARIADIC=PASS target=$target GNU_glibc_snprintf=1 QEMU_byte_exact=1 register_and_stack=1"
done
echo "FOUR_BACKEND_BINARY128_VARIADIC_ACCEPT=PASS targets=2 GNU_QEMU_diff=1"
