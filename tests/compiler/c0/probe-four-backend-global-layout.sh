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
        nonzero_union)
            cat <<'SRC'
static const union Endian {
    int word;
    unsigned char low;
} nativeendian = {1};
int main(void) { return nativeendian.low == 1 ? 42 : 2; }
SRC
            ;;
        nested_nonzero)
            cat <<'SRC'
union Val { void *p; unsigned long bits; };
struct TValue { union Val value; unsigned char tag; };
struct Node { struct TValue tv; int key; union Val next; };
static const struct Node node = { {{0}, 5}, 3, {0} };
int main(void) {
    return (node.tv.value.p == 0 && node.tv.tag == 5 &&
            node.key == 3 && node.next.bits == 0) ? 42 : 2;
}
SRC
            ;;
        nested_record_array)
            cat <<'SRC'
struct Xform {
    unsigned char input[4];
    unsigned short mapping[3];
    unsigned int flags;
    unsigned long masks[2];
};
static const struct Xform aXformType[6] = {
    {{1,2,3,4}, {5,6,7}, 8, {9,10}},
    {{2,3,4,5}, {6,7,8}, 9, {10,11}},
    {{3,4,5,6}, {7,8,9}, 10, {11,12}},
    {{4,5,6,7}, {8,9,10}, 11, {12,13}},
    {{5,6,7,8}, {9,10,11}, 12, {13,14}},
    {{6,7,8,9}, {10,11,12}, 13, {14,15}}
};
int main(void) {
    return (aXformType[0].input[0] == 1 &&
            aXformType[1].mapping[1] == 7 &&
            aXformType[2].flags == 10 &&
            aXformType[3].masks[1] == 13 &&
            aXformType[4].input[3] == 8 &&
            aXformType[5].masks[0] == 14) ? 42 : 2;
}
SRC
            ;;
        bitfield_record_array)
            cat <<'SRC'
/* BusyBox ifconfig OptArray shape: relocation, adjacent 6:10 bitfields,
 * and an ordinary short. Distinguish bad bit packing from a correct 42. */
struct Option {
    const char *name;
    const unsigned int flags:6;
    const unsigned int arg_flags:10;
    const unsigned short selector;
};
static const struct Option options[3] = {
    {"metric", 37, 777, 0x1234},
    {"broadcast", 21, 523, 0xabcd},
    {0, 0, 0, 0}
};
int main(void) {
    const unsigned char *raw = (const unsigned char *)&options[0];
    return (sizeof(struct Option) == 16 &&
            options[0].name[0] == 'm' && options[1].name[0] == 'b' &&
            options[0].flags == 37 && options[0].arg_flags == 777 &&
            options[1].flags == 21 && options[1].arg_flags == 523 &&
            options[0].selector == 0x1234 &&
            options[1].selector == 0xabcd &&
            raw[8] == (unsigned char)(37 | ((777 & 3) << 6)) &&
            raw[9] == (unsigned char)(777 >> 2) &&
            options[2].name == 0) ? 42 : 1;
}
SRC
            ;;
        nested_record_array_reloc)
            cat <<'SRC'
static int item = 73;
struct Entry {
    unsigned short indices[2];
    int *ptr;
    unsigned char extra[3];
};
static const struct Entry table[2] = {
    {{1,2}, &item, {3,4,5}},
    {{6,7}, &item, {8,9,10}}
};
int main(void) {
    return (table[0].ptr == &item && table[1].ptr == &item &&
            *table[0].ptr == 73 && table[1].extra[2] == 10 &&
            table[0].indices[1] == 2) ? 42 : 3;
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
    for family in null_scalar null_array zero_union zero_record implicit_record nonzero_union nested_nonzero nested_record_array nested_record_array_reloc bitfield_record_array combined; do
        src="$w/$target.$family.i"
        write_case "$family" >"$src"
        "$gcc" -static -x c "$src" -o "$w/$target.$family.gnu"
        if ! check_exit_42 "$w/$target.$family.gnu" "$qemu"; then
            echo "FOUR_BACKEND_GLOBAL_LAYOUT=HARNESS_FAIL target=$target family=$family stage=gnu_qemu" >&2
            exit 1
        fi
        if ! MINIC_M1_TRACE=1 "$MINIC_CC" --target="$target-linux" -S "$src" -o "$w/$target.$family.s" >"$w/mini.out" 2>"$w/mini.err"; then
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
    echo "FOUR_BACKEND_GLOBAL_LAYOUT_ACCEPT=FAIL targets=2 families=11" >&2
    exit 1
fi
echo "FOUR_BACKEND_GLOBAL_LAYOUT_ACCEPT=PASS targets=2 families=11 GNU_QEMU_diff=1"
