#!/bin/sh
# C11 GNU relaxed atomics (memory_order=0): AArch64 / LoongArch64 full-size typed
# store+load, GNU assembly/link and QEMU result byte comparison.
# Does not certify acquire/release/SC or thread interleavings.
set -eu
: "${MINIC_CC:?set MINIC_CC to the actual minic-cc}"
w=$(mktemp -d)
trap 'rm -rf "$w"' EXIT HUP INT TERM
# The existing Core lowering rejects volatile float/double assignment
# at the target-address stage, before the native memory emitter is reached.
# Keep this oracle about supported narrow data accesses and relaxed atomics;
# the FP lvalue feature needs its own Core acceptance/test work.
cat >"$w/atomics.i" <<'SRC'
static unsigned char tiny;
static unsigned short half;
static unsigned int word;
static unsigned long wide;
static int chosen;
static int *global_ptr;
static volatile signed char narrow_signed;
static volatile unsigned short narrow_unsigned;
int main(void) {
    narrow_signed = -17;
    narrow_unsigned = 65001U;
    if (narrow_signed != -17 || narrow_unsigned != 65001U)
        return 2;
    __atomic_store_n(&tiny, (unsigned char)7, 0);
    __atomic_store_n(&half, (unsigned short)11, 0);
    __atomic_store_n(&word, 13U, 0);
    __atomic_store_n(&wide, 17UL, 0);
    __atomic_store_n(&global_ptr, &chosen, 0);
    if (__atomic_load_n(&tiny, 0) != 7 ||
        __atomic_load_n(&half, 0) != 11 ||
        __atomic_load_n(&word, 0) != 13 ||
        __atomic_load_n(&wide, 0) != 17 ||
        __atomic_load_n(&global_ptr, 0) != &chosen)
        return 1;
    return 42;
}
SRC
for target in aarch64 loongarch64; do
    case "$target" in
      aarch64) gcc=aarch64-linux-gnu-gcc; qemu=qemu-aarch64 ;;
      loongarch64) gcc=loongarch64-linux-gnu-gcc-13; qemu=qemu-loongarch64 ;;
    esac
    "$gcc" -static -x c "$w/atomics.i" -o "$w/$target.gnu"
    "$MINIC_CC" --target="$target-linux" -S "$w/atomics.i" \
        -o "$w/$target.s"
    "$gcc" -static -x assembler "$w/$target.s" -o "$w/$target.mini"
    for cc in gnu mini; do
        set +e
        timeout 45s "$qemu" "$w/$target.$cc" \
            >"$w/$target.$cc.out" 2>"$w/$target.$cc.err"
        rc=$?
        set -e
        if [ "$rc" -ne 42 ]; then
            echo "FOUR_BACKEND_RELAXED_ATOMICS=FAIL target=$target compiler=$cc status=$rc" >&2
            exit 1
        fi
    done
    cmp "$w/$target.gnu.out" "$w/$target.mini.out"
    cmp "$w/$target.gnu.err" "$w/$target.mini.err"
    echo "FOUR_BACKEND_RELAXED_ATOMICS=PASS target=$target widths=1,2,4,8 pointer=1 signed_unsigned=1 GNU_QEMU_diff=1"
done
echo "FOUR_BACKEND_RELAXED_ATOMICS_ACCEPT=PASS targets=2 orders=relaxed"
