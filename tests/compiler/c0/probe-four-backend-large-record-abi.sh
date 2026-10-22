#!/bin/sh
# ABI round-trip on 144-byte struct (like BusyBox devfsd's struct stat).
set -eu
: "$MINIC_CC"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
cat >"$work/header.h" <<'SRC'
struct Large { unsigned long fields[18]; };
extern int from_gnu(struct Large, unsigned long);
extern int from_mini(struct Large, unsigned long);
SRC
cat >"$work/mini.c" <<'SRC'
#include "header.h"
int from_mini(struct Large a, unsigned long salt) {
    return a.fields[0] == 12345UL && a.fields[17] == 67890UL && salt == 23UL;
}
int main(void) {
    struct Large v = {0};
    v.fields[0] = 12345UL;
    v.fields[17] = 67890UL;
    return (from_gnu(v, 23UL) && from_mini(v, 23UL)) ? 42 : 1;
}
SRC
cat >"$work/gnu.c" <<'SRC'
#include "header.h"
int from_gnu(struct Large a, unsigned long salt) {
    return a.fields[0] == 12345UL && a.fields[17] == 67890UL && salt == 23UL;
}
SRC
for target in aarch64 loongarch64; do
    case "$target" in
      aarch64) cc=aarch64-linux-gnu-gcc; qemu=qemu-aarch64 ;;
      loongarch64) cc=loongarch64-linux-gnu-gcc-13; qemu=qemu-loongarch64 ;;
    esac
    "$cc" -static -I"$work" "$work/mini.c" "$work/gnu.c" -o "$work/$target.gnu"
    "$cc" -I"$work" -c "$work/gnu.c" -o "$work/$target.gnu.o"
    # MiniC consumes preprocessed translation units: GCC owns -I/-E only.
    "$cc" -E -P -I"$work" "$work/mini.c" -o "$work/$target.mini.i"
    if ! "$MINIC_CC" --target="$target-linux" -S "$work/$target.mini.i" \
             -o "$work/$target.s" 2>"$work/mini.err"; then
        echo "FOUR_BACKEND_LARGE_RECORD_ABI=FAIL target=$target stage=minic" >&2
        head -18 "$work/mini.err" >&2
        exit 1
    fi
    if ! "$cc" -static -x assembler "$work/$target.s" -x none \
             "$work/$target.gnu.o" -o "$work/$target.mini" \
             >"$work/as.out" 2>"$work/as.err"; then
        echo "FOUR_BACKEND_LARGE_RECORD_ABI=FAIL target=$target stage=GNU_AS" >&2
        head -18 "$work/as.err" >&2
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
        echo "FOUR_BACKEND_LARGE_RECORD_ABI=FAIL target=$target stage=GNU_QEMU gnu=$grc mini=$mrc" >&2
        exit 1
    fi
    echo "FOUR_BACKEND_LARGE_RECORD_ABI=PASS target=$target bytes=144 GNU_QEMU=1"
done
echo "FOUR_BACKEND_LARGE_RECORD_ABI_ACCEPT=PASS targets=2"
