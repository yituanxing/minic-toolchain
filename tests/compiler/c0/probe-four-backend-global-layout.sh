#!/bin/sh
# Batch-isolate the original global-layout failure across both ISA backends.
# Every case uses independent GNU static/QEMU runtime behavior as its oracle;
# run all cases before rejecting so a single CI reports the failing family.
set -eu
: "${MINIC_CC:?set MINIC_CC to the actual minic-cc}"
w=$(mktemp -d)
trap 'rm -rf "$w"' EXIT HUP INT TERM
write_case() {
    case "$1" in
        null_scalar)
            cat <<'SRC'
static int *null_scalar = 0;
int main(void) { return null_scalar == 0 ? 42 : 1; }
SRC
            ;;
        null_array)
            cat <<'SRC'
static int *null_array[3] = {0,0,0};
int main(void) {
    return (null_array[0] == 0 && null_array[1] == 0 &&
            null_array[2] == 0) ? 42 : 1;
}
SRC
            ;;
        zero_union)
            cat <<'SRC'
union U { unsigned long bits; void *pointer; };
static union U empty_union = {0};
int main(void) { return empty_union.bits == 0 ? 42 : 2; }
SRC
            ;;
        zero_record)
            cat <<'SRC'
union U { unsigned long bits; void *pointer; };
struct R { int first; union U nested; int last; };
static struct R empty_record = {0};
int main(void) {
    return (empty_record.first == 0 && empty_record.nested.bits == 0 &&
            empty_record.last == 0) ? 42 : 2;
}
SRC
            ;;
        implicit_record)
            cat <<'SRC'
union U { unsigned long bits; void *pointer; };
struct R { int first; union U nested; int last; };
static struct R empty_record_implicit;
int main(void) { return empty_record_implicit.nested.bits == 0 ? 42 : 2; }
SRC
            ;;
        combined)
            cat <<'SRC'
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
            ;;
        *) echo "unknown case: $1" >&2; return 1 ;;
    esac
}
check_exit_42() {
    exe=$1
    emu=$2
    set +e
    timeout 45s "$emu" "$exe" >"$exe.out" 2>"$exe.err"
    code=$?
    set -e
    [ "$code" -eq 42 ]
}
failed=0
for target in aarch64 loongarch64; do
    case "$target" in
        aarch64) gcc=aarch64-linux-gnu-gcc; qemu=qemu-aarch64 ;;
        loongarch64) gcc=loongarch64-linux-gnu-gcc-13; qemu=qemu-loongarch64 ;;
    esac
    for family in null_scalar null_array zero_union zero_record implicit_record combined; do
        src="$w/$target.$family.i"
        write_case "$family" >"$src"
        "$gcc" -static -x c "$src" -o "$w/$target.$family.gnu"
        if ! check_exit_42 "$w/$target.$family.gnu" "$qemu"; then
            echo "FOUR_BACKEND_GLOBAL_LAYOUT=HARNESS_FAIL target=$target family=$family stage=gnu_qemu" >&2
            exit 1
        fi
        if ! "$MINIC_CC" --target="$target-linux" -S "$src" -o "$w/$target.$family.s" >"$w/mini.out" 2>"$w/mini.err"; then
            echo "FOUR_BACKEND_GLOBAL_LAYOUT=BLOCKED target=$target family=$family stage=minic"
            sed -n '1,8p' "$w/mini.err"
            failed=1
            continue
        fi
        if ! "$gcc" -static -x assembler "$w/$target.$family.s" -o "$w/$target.$family.mini" >"$w/mini.out" 2>"$w/mini.err"; then
            echo "FOUR_BACKEND_GLOBAL_LAYOUT=BLOCKED target=$target family=$family stage=gnu_as_link"
            sed -n '1,8p' "$w/mini.err"
            failed=1
            continue
        fi
        if ! check_exit_42 "$w/$target.$family.mini" "$qemu" ||
           ! cmp -s "$w/$target.$family.gnu.out" "$w/$target.$family.mini.out" ||
           ! cmp -s "$w/$target.$family.gnu.err" "$w/$target.$family.mini.err"; then
            echo "FOUR_BACKEND_GLOBAL_LAYOUT=FAIL target=$target family=$family stage=qemu_runtime"
            failed=1
            continue
        fi
        echo "FOUR_BACKEND_GLOBAL_LAYOUT=PASS target=$target family=$family GNU_QEMU_diff=1"
    done
done
if [ "$failed" -ne 0 ]; then
    echo "FOUR_BACKEND_GLOBAL_LAYOUT_ACCEPT=FAIL targets=2 families=6" >&2
    exit 1
fi
echo "FOUR_BACKEND_GLOBAL_LAYOUT_ACCEPT=PASS targets=2 families=6 GNU_QEMU_diff=1"
