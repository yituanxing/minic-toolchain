#!/usr/bin/env bash
# GNU AAPCS64 and LoongArch LP64D: natural C integer bit-field records
# occupy a real GP-class eightbyte; NEVER treat each bitfield as one ABI arg.
# Validate return and by-value parameter in all 4 GNU/MiniC link combinations.
set -Eeuo pipefail
: "${MINIC_CC:?set exact MiniC}"
: "${MINIC_BITFIELD_RECORD_TARGET:?set ISA}"
d=$(mktemp -d);trap 'rm -rf "$d"' EXIT
case "$MINIC_BITFIELD_RECORD_TARGET" in
 aarch64) cc=aarch64-linux-gnu-gcc; qemu=qemu-aarch64 ;;
 loongarch64) cc=loongarch64-linux-gnu-gcc-13; qemu=qemu-loongarch64 ;;
 *) echo "LP64_BITFIELD_RECORD=INVALID_TARGET" >&2;exit 2 ;;
esac
cat >"$d/api.i" <<'SRC'
typedef struct { unsigned int a:5, b:9, c:18; } Bit32;
typedef struct { unsigned long a:41, b:23; } Bit64;
typedef struct { unsigned int a:5, b:9, c:18; unsigned long tail; } Bit16;
__attribute__((noinline)) Bit32 make32(unsigned int n) {
    Bit32 r; r.a=n; r.b=n+113U; r.c=n+123456U;return r;
}
__attribute__((noinline)) Bit64 make64(unsigned long n) {
    Bit64 r; r.a=n; r.b=n+1234567UL;return r;
}
__attribute__((noinline)) Bit16 make16(unsigned int n) {
    Bit16 r;
    r.a=n; r.b=n+113U; r.c=n+123456U;
    r.tail=0x1122334455667788UL;
    return r;
}
__attribute__((noinline)) int check32(Bit32 r) {
    return r.a==7 && r.b==120 && r.c==123463;
}
__attribute__((noinline)) int check64(Bit64 r) {
    return r.a==47UL && r.b==1234614UL;
}
__attribute__((noinline)) int check16(Bit16 r) {
    return r.a==9 && r.b==122 && r.c==123465 &&
           r.tail==0x1122334455667788UL;
}
SRC
cat >"$d/caller.i" <<'SRC'
typedef struct { unsigned int a:5, b:9, c:18; } Bit32;
typedef struct { unsigned long a:41, b:23; } Bit64;
typedef struct { unsigned int a:5, b:9, c:18; unsigned long tail; } Bit16;
extern Bit32 make32(unsigned int);
extern Bit64 make64(unsigned long);
extern Bit16 make16(unsigned int);
extern int check32(Bit32);
extern int check64(Bit64);
extern int check16(Bit16);
int main(void) {
    Bit32 a=make32(7);
    Bit64 b=make64(47UL);
    Bit16 c=make16(9);
    if(!check32(a) || !check64(b) || !check16(c)) return 1;
    return 42;
}
SRC
"$cc" -O0 -c -x c "$d/api.i" -o "$d/gnu-api.o"
"$cc" -O0 -c -x c "$d/caller.i" -o "$d/gnu-caller.o"
"$MINIC_CC" --target="$MINIC_BITFIELD_RECORD_TARGET-linux" -S "$d/api.i" -o "$d/mini-api.s"
"$MINIC_CC" --target="$MINIC_BITFIELD_RECORD_TARGET-linux" -S "$d/caller.i" -o "$d/mini-caller.s"
"$cc" -c -x assembler "$d/mini-api.s" -o "$d/mini-api.o"
"$cc" -c -x assembler "$d/mini-caller.s" -o "$d/mini-caller.o"
"$cc" -static "$d/gnu-api.o" "$d/gnu-caller.o" -o "$d/original.gnu"
"$cc" -static "$d/mini-api.o" "$d/gnu-caller.o" -o "$d/gnu-caller.mini-api"
"$cc" -static "$d/gnu-api.o" "$d/mini-caller.o" -o "$d/mini-caller.gnu-api"
"$cc" -static "$d/mini-api.o" "$d/mini-caller.o" -o "$d/all.mini"
for name in original.gnu gnu-caller.mini-api mini-caller.gnu-api all.mini;do
    set +e
    timeout 50s "$qemu" "$d/$name" >"$d/$name.out" 2>"$d/$name.err"
    rc=$?
    set -e
    if (( rc!=42 ));then
        echo "LP64_BITFIELD_RECORD=FAIL target=$MINIC_BITFIELD_RECORD_TARGET variant=$name status=$rc" >&2;exit 1
    fi
    cmp -s "$d/original.gnu.out" "$d/$name.out"
    cmp -s "$d/original.gnu.err" "$d/$name.err"
done
cat >"$d/unsupported.i" <<'SRC'
typedef struct __attribute__((packed)) { unsigned int a:3,b:9,c:20; } Unsafe;
Unsafe unsupported(int a) { Unsafe r; r.a=(unsigned)a;return r; }
SRC
if "$MINIC_CC" --target="$MINIC_BITFIELD_RECORD_TARGET-linux" -S "$d/unsupported.i" -o "$d/unsupported.s" 2>"$d/unsupported.err"; then
    echo "LP64_BITFIELD_RECORD=FAIL packed_bitfield_incorrectly_admitted" >&2;exit 1
fi
test ! -e "$d/unsupported.s"
echo "LP64_BITFIELD_RECORD=PASS target=$MINIC_BITFIELD_RECORD_TARGET 4_8_16byte=1 native_QEMU=1 mixed_ABI=4 packed_rejected=1"
