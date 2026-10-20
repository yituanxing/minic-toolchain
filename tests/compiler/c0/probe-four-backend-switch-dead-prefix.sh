#!/bin/sh
# Independent GNU/MiniC runtime oracle for C switch dead prefixes.
set -eu
: "$MINIC_CC"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
cat >"$work/switch.c" <<'SRC'
static int switch_dead_prefix(int condition) {
    int score = 0;
    for (int i = 0; i < 3; ++i) {
        switch (condition) {
            score = 1000; /* Unreachable: no case label precedes it. */
            break;
            if (condition) score = -999;
            default:
                score += 14;
                break;
        }
    }
    return score;
}
int main(void) {
    return switch_dead_prefix(1) == 42 &&
           switch_dead_prefix(0) == 42 ? 0 : 3;
}
SRC
for target in aarch64 loongarch64; do
    case "$target" in
        aarch64) gcc=aarch64-linux-gnu-gcc; qemu=qemu-aarch64 ;;
        loongarch64) gcc=loongarch64-linux-gnu-gcc-13; qemu=qemu-loongarch64 ;;
    esac
    # MiniC's -S accepts the already GNU-preprocessed .i, not raw comments.
    # Feed exactly the SAME preprocessed bytes to the GNU reference.
    "$gcc" -E -P -std=gnu11 "$work/switch.c" -o "$work/$target.i"
    "$gcc" -O0 -std=gnu11 -static -x c "$work/$target.i" \
        -o "$work/$target.gnu"
    timeout 45s "$qemu" "$work/$target.gnu" \
        >"$work/$target.gnu.out" 2>"$work/$target.gnu.err"
    "$MINIC_CC" --target="$target-linux" -S "$work/$target.i" \
        -o "$work/$target.s"
    "$gcc" -static -x assembler "$work/$target.s" \
        -o "$work/$target.mini"
    timeout 45s "$qemu" "$work/$target.mini" \
        >"$work/$target.mini.out" 2>"$work/$target.mini.err"
    cmp "$work/$target.gnu.out" "$work/$target.mini.out"
    cmp "$work/$target.gnu.err" "$work/$target.mini.err"
    echo "FOUR_BACKEND_SWITCH_DEAD_PREFIX=PASS target=$target GNU_QEMU_byte_exact=1 normalized_for=1 switch_default=1"
done
