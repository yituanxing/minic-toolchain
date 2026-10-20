#!/bin/sh
# Independent GNU/QEMU oracle for nullable scalar/array pointers and
# zero-initialized nested aggregate/union globals on new 64-bit ISAs.
set -eu
: "${MINIC_CC:?set MINIC_CC to the actual minic-cc}"
w=$(mktemp -d)
trap 'rm -rf "$w"' EXIT HUP INT TERM
cat >"$w/global-layout.i" <<'SRC'
union U { unsigned long bits; void *pointer; };
struct R { int first; union U nested; int last; };
static int *null_scalar = 0;
static int *null_array[3] = {0,0,0};
static struct R empty_record = {0};
static union U empty_union = {0};
static struct R empty_record_implicit;
int main(void) {
    if (null_scalar != 0 || null_array[0] != 0 ||
        null_array[1] != 0 || null_array[2] != 0)
        return 1;
    if (empty_record.first != 0 || empty_record.nested.bits != 0 ||
        empty_record.last != 0 || empty_union.bits != 0 ||
        empty_record_implicit.nested.bits != 0)
        return 2;
    return 42;
}
SRC
check_exit_42() {
    exe=$1
    emu=$2
    set +e
    timeout 45s "$emu" "$exe" >"$exe.out" 2>"$exe.err"
    code=$?
    set -e
    if [ "$code" -ne 42 ]; then
        echo "FOUR_BACKEND_GLOBAL_LAYOUT=FAIL binary=$exe code=$code" >&2
        sed -n '1,20p' "$exe.err" >&2
        exit 1
    fi
}
for target in aarch64 loongarch64; do
    case "$target" in
        aarch64) gcc=aarch64-linux-gnu-gcc; qemu=qemu-aarch64 ;;
        loongarch64) gcc=loongarch64-linux-gnu-gcc-13; qemu=qemu-loongarch64 ;;
    esac
    "$gcc" -static -x c "$w/global-layout.i" -o "$w/$target.gnu"
    check_exit_42 "$w/$target.gnu" "$qemu"
    "$MINIC_CC" --target="$target-linux" -S "$w/global-layout.i" -o "$w/$target.s"
    "$gcc" -static -x assembler "$w/$target.s" -o "$w/$target.mini"
    check_exit_42 "$w/$target.mini" "$qemu"
    cmp "$w/$target.gnu.out" "$w/$target.mini.out"
    cmp "$w/$target.gnu.err" "$w/$target.mini.err"
    echo "FOUR_BACKEND_GLOBAL_LAYOUT=PASS target=$target nullable_pointer=1 pointer_array=1 zero_struct_union=1 GNU_QEMU_diff=1"
done
echo "FOUR_BACKEND_GLOBAL_LAYOUT_ACCEPT=PASS targets=2 original_source=1"
