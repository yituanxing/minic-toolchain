#!/bin/sh
# GNU computed-goto labels in a static-local relocation array.
# Independently compare MiniC-generated objects with GNU/QEMU on both ISAs.
set -eu
: "${MINIC_CC:?set MINIC_CC to the actual minic-cc}"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
cat >"$tmp/dispatch.i" <<'SRC'
static int dispatch(int n) {
    static void *const labels[] = {&&first, &&second, &&third};
    if (n < 0 || n > 2) return 0;
    goto *labels[n];
first:
    return 10;
second:
    return 20;
third:
    return 42;
}
int main(void) {
    return dispatch(0) == 10 && dispatch(1) == 20 &&
           dispatch(2) == 42 && dispatch(3) == 0 ? 42 : 1;
}
SRC
# Unlike static &&label arrays, this uses a live Core BLOCK_ADDRESS value.
cat >"$tmp/dynamic.i" <<'SRC'
static int dispatch_dynamic(int n) {
    void *where = &&miss;
    if (n == 1) where = &&hit;
    goto *where;
hit:
    return 42;
miss:
    return 7;
}
int main(void) {
    return dispatch_dynamic(1) == 42 && dispatch_dynamic(0) == 7 ? 42 : 1;
}
SRC
check_status() {
    binary=$1
    emulator=$2
    set +e
    timeout 45s "$emulator" "$binary" >"$binary.out" 2>"$binary.err"
    rc=$?
    set -e
    [ "$rc" -eq 42 ]
}
for target in ${MINIC_COMPUTED_GOTO_TARGET:-aarch64 loongarch64}; do
    case "$target" in
        aarch64) gcc=aarch64-linux-gnu-gcc; qemu=qemu-aarch64 ;;
        loongarch64) gcc=loongarch64-linux-gnu-gcc-13; qemu=qemu-loongarch64 ;;
    esac
    "$gcc" -static -x c "$tmp/dispatch.i" -o "$tmp/$target.gnu"
    if ! check_status "$tmp/$target.gnu" "$qemu"; then
        echo "FOUR_BACKEND_COMPUTED_GOTO=ORACLE_FAIL target=$target" >&2
        exit 1
    fi
    MINIC_M1_TRACE=1 "$MINIC_CC" --target="$target-linux" -S \
        "$tmp/dispatch.i" -o "$tmp/$target.s"
    "$gcc" -static -x assembler "$tmp/$target.s" -o "$tmp/$target.mini"
    if ! check_status "$tmp/$target.mini" "$qemu" ||
       ! cmp -s "$tmp/$target.gnu.out" "$tmp/$target.mini.out" ||
       ! cmp -s "$tmp/$target.gnu.err" "$tmp/$target.mini.err"; then
        echo "FOUR_BACKEND_COMPUTED_GOTO=FAIL target=$target stage=runtime" >&2
        exit 1
    fi
    "$gcc" -static -x c "$tmp/dynamic.i" -o "$tmp/$target.dynamic.gnu"
    if ! check_status "$tmp/$target.dynamic.gnu" "$qemu"; then
        echo "FOUR_BACKEND_COMPUTED_GOTO=ORACLE_FAIL target=$target fixture=dynamic" >&2
        exit 1
    fi
    MINIC_M1_TRACE=1 "$MINIC_CC" --target="$target-linux" -S \
        "$tmp/dynamic.i" -o "$tmp/$target.dynamic.s"
    "$gcc" -static -x assembler "$tmp/$target.dynamic.s" -o "$tmp/$target.dynamic.mini"
    if ! check_status "$tmp/$target.dynamic.mini" "$qemu" ||
       ! cmp -s "$tmp/$target.dynamic.gnu.out" "$tmp/$target.dynamic.mini.out" ||
       ! cmp -s "$tmp/$target.dynamic.gnu.err" "$tmp/$target.dynamic.mini.err"; then
        echo "FOUR_BACKEND_COMPUTED_GOTO=FAIL target=$target fixture=dynamic" >&2
        exit 1
    fi
    echo "FOUR_BACKEND_COMPUTED_GOTO=PASS target=$target static_label_array=1 dynamic_block_address=1 indirect_branch=1 GNU_QEMU_diff=1"
done
echo "FOUR_BACKEND_COMPUTED_GOTO_ACCEPT=PASS targets=2"
