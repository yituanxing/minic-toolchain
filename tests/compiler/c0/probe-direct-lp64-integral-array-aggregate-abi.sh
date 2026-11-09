#!/usr/bin/env bash
# Both LP64 ABIs use one/two integer eightbytes for natural-layout records
# whose fields include fixed integer/pointer arrays. Verify actual values in
# both directions against the *independently* compiled GNU C ABI.
set -Eeuo pipefail
: "${MINIC_CC:?exact minic-cc required}"
: "${MINIC_ARRAY_RECORD_TARGET:?target required}"
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
case "$MINIC_ARRAY_RECORD_TARGET" in
 aarch64) cc=aarch64-linux-gnu-gcc; emulator=qemu-aarch64;;
 loongarch64) cc=loongarch64-linux-gnu-gcc-13; emulator=qemu-loongarch64;;
 *) echo "LP64_ARRAY_RECORD=INVALID_TARGET" >&2;exit 2;;
esac
cat >"$d/api.i" <<'SRC'
typedef struct { unsigned char bytes[3]; } Byte3;
typedef struct { unsigned int words[3]; } Words12;
typedef struct { unsigned long values[2]; } Words16;
typedef struct { unsigned char b[2]; } Inner2;
typedef struct { Inner2 inner; unsigned int marker; } Nested8;
typedef struct {} Empty0;
typedef struct { void *lock; unsigned long flags; Empty0 cookie; } WithEmpty16;
__attribute__((noinline)) Byte3 ret3(unsigned char n) {
    Byte3 s;
    s.bytes[0] = n; s.bytes[1] = (unsigned char)(n+1);
    s.bytes[2] = (unsigned char)(n+2);
    return s;
}
__attribute__((noinline)) Words12 ret12(unsigned int n) {
    Words12 s;
    s.words[0]=n; s.words[1]=n+101U; s.words[2]=n+202U;
    return s;
}
__attribute__((noinline)) Words16 ret16(unsigned long n) {
    Words16 s;
    s.values[0]=n; s.values[1]=n+1000000UL;
    return s;
}
__attribute__((noinline)) Nested8 ret_nested(unsigned int n) {
    Nested8 s;
    s.inner.b[0]=(unsigned char)n;
    s.inner.b[1]=(unsigned char)(n+1);
    s.marker=n+7U;
    return s;
}
__attribute__((noinline)) int accept3(Byte3 s) {
    return s.bytes[0] == 37 && s.bytes[1] == 38 && s.bytes[2] == 39;
}
__attribute__((noinline)) int accept12(Words12 s) {
    return s.words[0]==40U && s.words[1]==141U && s.words[2]==242U;
}
__attribute__((noinline)) int accept16(Words16 s) {
    return s.values[0]==41UL && s.values[1]==1000041UL;
}
__attribute__((noinline)) int accept_nested(Nested8 s) {
    return s.inner.b[0]==42 && s.inner.b[1]==43 && s.marker==49;
}
__attribute__((noinline)) WithEmpty16 ret_empty(void *lock, unsigned long flags) {
    WithEmpty16 s; s.lock=lock; s.flags=flags; return s;
}
__attribute__((noinline)) int accept_empty(WithEmpty16 s) {
    return s.lock==(void *)0x1234UL && s.flags==77UL;
}
SRC
cat >"$d/caller.i" <<'SRC'
typedef struct { unsigned char bytes[3]; } Byte3;
typedef struct { unsigned int words[3]; } Words12;
typedef struct { unsigned long values[2]; } Words16;
typedef struct { unsigned char b[2]; } Inner2;
typedef struct { Inner2 inner; unsigned int marker; } Nested8;
typedef struct {} Empty0;
typedef struct { void *lock; unsigned long flags; Empty0 cookie; } WithEmpty16;
extern Byte3 ret3(unsigned char);
extern Words12 ret12(unsigned int);
extern Words16 ret16(unsigned long);
extern Nested8 ret_nested(unsigned int);
extern int accept3(Byte3);
extern int accept12(Words12);
extern int accept16(Words16);
extern int accept_nested(Nested8);
extern WithEmpty16 ret_empty(void *, unsigned long);
extern int accept_empty(WithEmpty16);
int main(void) {
    Byte3 a=ret3(37);
    Words12 b=ret12(40);
    Words16 c=ret16(41);
    Nested8 n=ret_nested(42);
    if (!accept3(a) || !accept12(b) || !accept16(c) ||
        !accept_nested(n) ||
        !accept_empty(ret_empty((void *)0x1234UL, 77UL))) return 1;
    return 42;
}
SRC
"$cc" -O0 -c -x c "$d/api.i" -o "$d/gnu-api.o"
"$cc" -O0 -c -x c "$d/caller.i" -o "$d/gnu-caller.o"
"$MINIC_CC" --target="$MINIC_ARRAY_RECORD_TARGET-linux" -S "$d/api.i" -o "$d/mini-api.s"
"$MINIC_CC" --target="$MINIC_ARRAY_RECORD_TARGET-linux" -S "$d/caller.i" -o "$d/mini-caller.s"
"$cc" -c -x assembler "$d/mini-api.s" -o "$d/mini-api.o"
"$cc" -c -x assembler "$d/mini-caller.s" -o "$d/mini-caller.o"
"$cc" -static "$d/gnu-caller.o" "$d/gnu-api.o" -o "$d/gnu"
"$cc" -static "$d/gnu-caller.o" "$d/mini-api.o" -o "$d/mini-api"
"$cc" -static "$d/mini-caller.o" "$d/gnu-api.o" -o "$d/mini-caller"
"$cc" -static "$d/mini-caller.o" "$d/mini-api.o" -o "$d/both-mini"
for variant in gnu mini-api mini-caller both-mini; do
    set +e
    timeout 50s "$emulator" "$d/$variant" >"$d/$variant.out" 2>"$d/$variant.err"
    rc=$?
    set -e
    if (( rc != 42 ));then
        echo "LP64_ARRAY_RECORD=FAIL target=$MINIC_ARRAY_RECORD_TARGET oracle=$variant exit=$rc" >&2
        exit 1
    fi
    cmp -s "$d/gnu.out" "$d/$variant.out"
    cmp -s "$d/gnu.err" "$d/$variant.err"
done
cat >"$d/invalid.i" <<'SRC'
typedef struct { double values[2]; } DoubleArray;
DoubleArray fp_hfa_array(double x) {
    DoubleArray t; t.values[0]=x; t.values[1]=x+1.0; return t;
}
SRC
if "$MINIC_CC" --target="$MINIC_ARRAY_RECORD_TARGET-linux" -S "$d/invalid.i" -o "$d/invalid.s" 2>"$d/invalid.err";then
    echo "LP64_ARRAY_RECORD=FAIL HFA_ARRAY_INCORRECTLY_CLASSIFIED" >&2;exit 1
fi
test ! -e "$d/invalid.s"
echo "LP64_ARRAY_RECORD=PASS target=$MINIC_ARRAY_RECORD_TARGET 3_8_12_16_byte_arrays=1 nested_records=1 zero_size_record_member=1 GNU_callers_and_callees=1 cross_abi_mixed=4 QEMU=1 HFA_array_rejected=1"
