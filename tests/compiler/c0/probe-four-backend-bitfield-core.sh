#!/bin/sh
# MiniC-only differential: C bit-field reads, compound RMW and prefix/postfix
# values must agree with the target GNU C compiler on the same initial bits.
# No C comments/directives in the .i input: it is already preprocessed.
set -eu
: "${MINIC_CC:?set MINIC_CC to built minic-cc}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

cat >"$work/bitfield.i" <<'SRC'
struct MiniBF { unsigned int tag : 3; int signed_value : 5; unsigned int count : 4; };
int mini_read_signed(const struct MiniBF *p) { return p->signed_value; }
int mini_compound_signed(struct MiniBF *p) { return p->signed_value += 2; }
unsigned int mini_compound_unsigned(struct MiniBF *p) { return p->count ^= 5U; }
int mini_pre_signed(struct MiniBF *p) { return ++p->signed_value; }
int mini_post_signed(struct MiniBF *p) { return p->signed_value--; }
unsigned int mini_pre_unsigned(struct MiniBF *p) { return ++p->count; }
int mini_volatile_read(volatile struct MiniBF *p) { return p->signed_value; }
int mini_assign_signed(struct MiniBF *p) { return p->signed_value = -7; }
unsigned int mini_assign_unsigned(struct MiniBF *p) { return p->count = 9U; }
unsigned int mini_assign_volatile(volatile struct MiniBF *p) { return p->count = 3U; }
SRC
sed 's/mini_/gnu_/g' "$work/bitfield.i" >"$work/gnu.c"
cat >"$work/driver.c" <<'SRC'
#include <string.h>
struct MiniBF { unsigned int tag : 3; int signed_value : 5; unsigned int count : 4; };
extern int mini_read_signed(const struct MiniBF *);
extern int gnu_read_signed(const struct MiniBF *);
extern int mini_compound_signed(struct MiniBF *);
extern int gnu_compound_signed(struct MiniBF *);
extern unsigned int mini_compound_unsigned(struct MiniBF *);
extern unsigned int gnu_compound_unsigned(struct MiniBF *);
extern int mini_pre_signed(struct MiniBF *);
extern int gnu_pre_signed(struct MiniBF *);
extern int mini_post_signed(struct MiniBF *);
extern int gnu_post_signed(struct MiniBF *);
extern unsigned int mini_pre_unsigned(struct MiniBF *);
extern unsigned int gnu_pre_unsigned(struct MiniBF *);
extern int mini_volatile_read(volatile struct MiniBF *);
extern int gnu_volatile_read(volatile struct MiniBF *);
extern int mini_assign_signed(struct MiniBF *);
extern int gnu_assign_signed(struct MiniBF *);
extern unsigned int mini_assign_unsigned(struct MiniBF *);
extern unsigned int gnu_assign_unsigned(struct MiniBF *);
extern unsigned int mini_assign_volatile(volatile struct MiniBF *);
extern unsigned int gnu_assign_volatile(volatile struct MiniBF *);
int main(void) {
    struct MiniBF mini, gnu;
    memset(&mini, 0, sizeof(mini));
    memset(&gnu, 0, sizeof(gnu));
    mini.tag = gnu.tag = 2U;
    mini.signed_value = gnu.signed_value = -3;
    mini.count = gnu.count = 7U;
    if (mini_read_signed(&mini) != gnu_read_signed(&gnu)) return 1;
    if (mini_compound_signed(&mini) != gnu_compound_signed(&gnu)) return 2;
    if (memcmp(&mini, &gnu, sizeof(mini)) != 0) return 3;
    if (mini_compound_unsigned(&mini) != gnu_compound_unsigned(&gnu)) return 4;
    if (memcmp(&mini, &gnu, sizeof(mini)) != 0) return 5;
    if (mini_pre_signed(&mini) != gnu_pre_signed(&gnu)) return 6;
    if (mini_post_signed(&mini) != gnu_post_signed(&gnu)) return 7;
    if (mini_pre_unsigned(&mini) != gnu_pre_unsigned(&gnu)) return 8;
    if (mini_volatile_read(&mini) != gnu_volatile_read(&gnu)) return 9;
    if (memcmp(&mini, &gnu, sizeof(mini)) != 0) return 10;
    if (mini_assign_signed(&mini) != gnu_assign_signed(&gnu)) return 11;
    if (memcmp(&mini, &gnu, sizeof(mini)) != 0) return 12;
    if (mini_assign_unsigned(&mini) != gnu_assign_unsigned(&gnu)) return 13;
    if (memcmp(&mini, &gnu, sizeof(mini)) != 0) return 14;
    if (mini_assign_volatile(&mini) != gnu_assign_volatile(&gnu)) return 15;
    if (memcmp(&mini, &gnu, sizeof(mini)) != 0) return 16;
    return 0;
}
SRC
for target in aarch64 loongarch64; do
    case "$target" in
        aarch64) gcc=aarch64-linux-gnu-gcc; qemu=qemu-aarch64 ;;
        loongarch64) gcc=loongarch64-linux-gnu-gcc-13; qemu=qemu-loongarch64 ;;
    esac
    "$MINIC_CC" --target="$target-linux" -S "$work/bitfield.i" -o "$work/$target.mini.s"
    "$gcc" -O0 -static -x assembler "$work/$target.mini.s" \
        -x c "$work/gnu.c" "$work/driver.c" -o "$work/$target.mini"
    timeout 45s "$qemu" "$work/$target.mini" >"$work/$target.mini.out" 2>"$work/$target.mini.err"
    "$gcc" -O0 -static -x c "$work/bitfield.i" "$work/gnu.c" "$work/driver.c" \
        -o "$work/$target.gnu"
    timeout 45s "$qemu" "$work/$target.gnu" >"$work/$target.gnu.out" 2>"$work/$target.gnu.err"
    cmp "$work/$target.gnu.out" "$work/$target.mini.out"
    cmp "$work/$target.gnu.err" "$work/$target.mini.err"
    echo "FOUR_BACKEND_BITFIELD_CORE=PASS target=$target simple_signed=1 simple_unsigned=1 simple_volatile=1 signed_read=1 signed_rmw=1 unsigned_rmw=1 prefix=1 postfix=1 volatile=1 GNU_QEMU_differential=1"
done
echo "FOUR_BACKEND_BITFIELD_CORE_ACCEPT=PASS targets=2 GNU_runtime_oracle=1"
