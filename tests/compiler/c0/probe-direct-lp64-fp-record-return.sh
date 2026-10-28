#!/usr/bin/env bash
# Native PSABI: homogeneous float/double 1 and 2-field record returns.
# ABI tested both directions against an independent GNU cross-compiler caller.
set -Eeuo pipefail
: "${MINIC_CC:?expected MiniC binary}"
: "${MINIC_FP_RETURN_TARGET:?expected target ISA}"
d=$(mktemp -d);trap 'rm -rf "$d"' EXIT
case "$MINIC_FP_RETURN_TARGET" in
 aarch64) cc=aarch64-linux-gnu-gcc; qemu=qemu-aarch64;;
 loongarch64) cc=loongarch64-linux-gnu-gcc-13; qemu=qemu-loongarch64;;
 *) echo "LP64_FP_RECORD_RETURN=INVALID_TARGET" >&2;exit 2;;
esac
cat >"$d/returns.i" <<'SRC'
typedef struct { float x; } F1;
typedef struct { float x,y; } F2;
typedef struct { double x; } D1;
typedef struct { double x,y; } D2;
__attribute__((noinline)) F1 retf1(float a) {
    F1 r; r.x=a; return r;
}
__attribute__((noinline)) F2 retf2(float a,float b) {
    F2 r; r.x=a; r.y=b; return r;
}
__attribute__((noinline)) D1 retd1(double a) {
    D1 r; r.x=a; return r;
}
__attribute__((noinline)) D2 retd2(double a,double b) {
    D2 r; r.x=a; r.y=b; return r;
}
SRC
cat >"$d/gnu_caller.c" <<'SRC'
typedef struct { float x; } F1;
typedef struct { float x,y; } F2;
typedef struct { double x; } D1;
typedef struct { double x,y; } D2;
extern F1 retf1(float);
extern F2 retf2(float,float);
extern D1 retd1(double);
extern D2 retd2(double,double);
int main(void) {
    F1 a=retf1(3.5f); F2 b=retf2(7.5f,11.25f);
    D1 c=retd1(19.5); D2 d=retd2(23.5,37.25);
    if(a.x!=3.5f || b.x!=7.5f || b.y!=11.25f) return 1;
    if(c.x!=19.5 || d.x!=23.5 || d.y!=37.25) return 2;
    return 42;
}
SRC
cp "$d/gnu_caller.c" "$d/mini_caller.i"
"$cc" -O0 -c -x c "$d/returns.i" -o "$d/returns.gnu.o"
"$cc" -O0 -c "$d/gnu_caller.c" -o "$d/caller.gnu.o"
"$MINIC_CC" --target="$MINIC_FP_RETURN_TARGET-linux" -S "$d/returns.i" -o "$d/returns.mini.s"
"$MINIC_CC" --target="$MINIC_FP_RETURN_TARGET-linux" -S "$d/mini_caller.i" -o "$d/caller.mini.s"
"$cc" -c -x assembler "$d/returns.mini.s" -o "$d/returns.mini.o"
"$cc" -c -x assembler "$d/caller.mini.s" -o "$d/caller.mini.o"
"$cc" -static "$d/caller.gnu.o" "$d/returns.gnu.o" -o "$d/all.gnu"
"$cc" -static "$d/caller.gnu.o" "$d/returns.mini.o" -o "$d/gnu.caller"
"$cc" -static "$d/caller.mini.o" "$d/returns.gnu.o" -o "$d/mini.caller"
for variant in all.gnu gnu.caller mini.caller; do
    set +e
    timeout 45s "$qemu" "$d/$variant" >"$d/$variant.out" 2>"$d/$variant.err"
    rc=$?
    set -e
    if (( rc != 42 ));then
        echo "LP64_FP_RECORD_RETURN=FAIL target=$MINIC_FP_RETURN_TARGET variant=$variant exit=$rc" >&2
        exit 1
    fi
done
cmp -s "$d/all.gnu.out" "$d/gnu.caller.out"
cmp -s "$d/all.gnu.err" "$d/gnu.caller.err"
cmp -s "$d/all.gnu.out" "$d/mini.caller.out"
cmp -s "$d/all.gnu.err" "$d/mini.caller.err"
cat >"$d/hfa-guard.i" <<'SRC'
struct Huge { long double a[4]; };
struct Huge unsupported(struct Huge a) { return a; }
SRC
if "$MINIC_CC" --target="$MINIC_FP_RETURN_TARGET-linux" -S "$d/hfa-guard.i" -o "$d/hfa-guard.s" 2>"$d/hfa-guard.err";then
  echo "LP64_FP_RECORD_RETURN=FAIL target=$MINIC_FP_RETURN_TARGET long-double HFA admitted as scalar" >&2;exit 1
fi
test ! -e "$d/hfa-guard.s"
echo "LP64_FP_RECORD_RETURN=PASS target=$MINIC_FP_RETURN_TARGET 1_and_2_FPR_fields=1 GNU_caller_and_GNU_callee=1 QEMU=1 binary128_HFA_rejected=1"
