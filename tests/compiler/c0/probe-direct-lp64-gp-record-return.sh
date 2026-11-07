#!/usr/bin/env bash
# Linux LP64 integer-class composite return ABI: 3, 8, 12, 16 bytes.
# Independent GNU-compiled caller checks actual AAPCS64/LP64D register ABI
# (and a GNU-built callee is linked with the MiniC caller in the second run).
set -Eeuo pipefail
: "${MINIC_CC:?exact compiler required}"
: "${MINIC_RECORD_RETURN_TARGET:?target required}"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
case "$MINIC_RECORD_RETURN_TARGET" in
 aarch64) gcc=aarch64-linux-gnu-gcc; qemu=qemu-aarch64;;
 loongarch64) gcc=loongarch64-linux-gnu-gcc-13; qemu=qemu-loongarch64;;
 *) echo "LP64_GP_RECORD_RETURN=INVALID_TARGET" >&2; exit 2;;
esac
cat >"$tmp/returns.i" <<'SRC'
typedef struct { unsigned char a, b, c; } Small3;
typedef struct { unsigned long a; } Single8;
typedef struct { unsigned int a, b, c; } Triple12;
typedef struct { unsigned long a, b; } Pair16;
__attribute__((noinline)) Small3 ret3(unsigned char n) {
    Small3 out = {n, (unsigned char)(n + 1), (unsigned char)(n + 2)};
    return out;
}
__attribute__((noinline)) Single8 ret8(unsigned long n) {
    Single8 out = {n ^ 0x5a5aUL};
    return out;
}
__attribute__((noinline)) Triple12 ret12(unsigned int n) {
    Triple12 out = {n, n + 101U, n + 202U};
    return out;
}
__attribute__((noinline)) Pair16 ret16(unsigned long n) {
    Pair16 out = {n, n * 11UL};
    return out;
}
SRC
cat >"$tmp/native_caller.c" <<'SRC'
typedef struct { unsigned char a, b, c; } Small3;
typedef struct { unsigned long a; } Single8;
typedef struct { unsigned int a, b, c; } Triple12;
typedef struct { unsigned long a, b; } Pair16;
extern Small3 ret3(unsigned char);
extern Single8 ret8(unsigned long);
extern Triple12 ret12(unsigned int);
extern Pair16 ret16(unsigned long);
int main(void) {
    Small3 x = ret3(27);
    Single8 y = ret8(0x117UL);
    Triple12 z = ret12(41);
    Pair16 w = ret16(13);
    if (x.a != 27 || x.b != 28 || x.c != 29) return 1;
    if (y.a != (0x117UL ^ 0x5a5aUL)) return 2;
    if (z.a != 41 || z.b != 142 || z.c != 243) return 3;
    if (w.a != 13 || w.b != 143) return 4;
    return 42;
}
SRC
"$gcc" -O0 -c -x c "$tmp/returns.i" -o "$tmp/gnu-return.o"
"$gcc" -O0 -c "$tmp/native_caller.c" -o "$tmp/native-caller.o"
"$MINIC_CC" --target="$MINIC_RECORD_RETURN_TARGET-linux" -S "$tmp/returns.i" -o "$tmp/mini-return.s"
"$gcc" -c -x assembler "$tmp/mini-return.s" -o "$tmp/mini-return.o"
"$gcc" -static "$tmp/native-caller.o" "$tmp/gnu-return.o" -o "$tmp/gnu.elf"
"$gcc" -static "$tmp/native-caller.o" "$tmp/mini-return.o" -o "$tmp/mini.elf"
set +e
timeout 50s "$qemu" "$tmp/gnu.elf" >"$tmp/gnu.out" 2>"$tmp/gnu.err"; rc_gnu=$?
timeout 50s "$qemu" "$tmp/mini.elf" >"$tmp/mini.out" 2>"$tmp/mini.err"; rc_mini=$?
set -e
if [[ $rc_gnu != 42 || $rc_mini != 42 ]] ||
   ! cmp -s "$tmp/gnu.out" "$tmp/mini.out" ||
   ! cmp -s "$tmp/gnu.err" "$tmp/mini.err";then
    echo "LP64_GP_RECORD_RETURN=FAIL target=$MINIC_RECORD_RETURN_TARGET caller=GNU original=$rc_gnu minic=$rc_mini" >&2
    exit 1
fi
# Inverse ABI oracle: the MiniC-built caller invokes independent GNU returns.
cat >"$tmp/mini_caller.i" <<'SRC'
typedef struct { unsigned char a, b, c; } Small3;
typedef struct { unsigned long a; } Single8;
typedef struct { unsigned int a, b, c; } Triple12;
typedef struct { unsigned long a, b; } Pair16;
extern Small3 ret3(unsigned char);
extern Single8 ret8(unsigned long);
extern Triple12 ret12(unsigned int);
extern Pair16 ret16(unsigned long);
int main(void) {
    Small3 x = ret3(27);
    Single8 y = ret8(0x117UL);
    Triple12 z = ret12(41);
    Pair16 w = ret16(13);
    if (x.a != 27 || x.b != 28 || x.c != 29) return 1;
    if (y.a != (0x117UL ^ 0x5a5aUL)) return 2;
    if (z.a != 41 || z.b != 142 || z.c != 243) return 3;
    if (w.a != 13 || w.b != 143) return 4;
    return 42;
}
SRC
"$MINIC_CC" --target="$MINIC_RECORD_RETURN_TARGET-linux" -S "$tmp/mini_caller.i" -o "$tmp/mini-caller.s"
"$gcc" -c -x assembler "$tmp/mini-caller.s" -o "$tmp/mini-caller.o"
"$gcc" -static "$tmp/mini-caller.o" "$tmp/gnu-return.o" -o "$tmp/inverse.elf"
set +e
timeout 50s "$qemu" "$tmp/inverse.elf" >"$tmp/inverse.out" 2>"$tmp/inverse.err"; rc_inverse=$?
set -e
if [[ $rc_inverse != 42 ]] ||
   ! cmp -s "$tmp/gnu.out" "$tmp/inverse.out" ||
   ! cmp -s "$tmp/gnu.err" "$tmp/inverse.err";then
    echo "LP64_GP_RECORD_RETURN=FAIL target=$MINIC_RECORD_RETURN_TARGET caller=MiniC callee=GNU rc=$rc_inverse" >&2; exit 1
fi
cat >"$tmp/bad.i" <<'SRC'
/* 3-double HFA remains outside the checked 1-2 field FPR family. */
struct HFA { double a,b,c; };
struct HFA hfa(double a) {
    struct HFA x = {a, a+1.0, a+2.0};
    return x;
}
SRC
if "$MINIC_CC" --target="$MINIC_RECORD_RETURN_TARGET-linux" -S "$tmp/bad.i" -o "$tmp/bad.s" 2>"$tmp/bad.err";then
  echo "LP64_GP_RECORD_RETURN=FAIL 3-member HFA return incorrectly admitted" >&2
  exit 1
fi
test ! -f "$tmp/bad.s"
echo "LP64_GP_RECORD_RETURN=PASS target=$MINIC_RECORD_RETURN_TARGET GNU_caller_and_GNU_callee=1 bytes=3,8,12,16 ABI_GP=1 FP_HFA_rejected=1"
