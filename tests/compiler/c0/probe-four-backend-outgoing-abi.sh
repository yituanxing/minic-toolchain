#!/bin/sh
# Cross-GNU/QEMU oracle: MiniC owns calling convention for outgoing stack
# arguments in 9-16 parameter calls. Do not substitute GNU-built functions.
set -eu
: "${MINIC_CC:?set MINIC_CC to built minic-cc}"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
cat >"$tmp/overflow.i" <<'SRC'
extern long gnu_gp10(long,long,long,long,long,long,long,long,long,long);
extern double gnu_fp9(double,double,double,double,double,double,double,double,double);
extern long gnu_mix11(long,double,long,long,long,long,long,long,long,long,double);
long mini_outgoing_gp(void) {
    return gnu_gp10(1,2,3,4,5,6,7,8,9,10);
}
double mini_outgoing_fp(void) {
    return gnu_fp9(1.25,2.25,3.25,4.25,5.25,6.25,7.25,8.25,9.25);
}
double mini_incoming_fp9(double a,double b,double c,double d,double e,
                         double f,double g,double h,double i) {
    return (a==1.25 && b==2.25 && c==3.25 && d==4.25 && e==5.25 &&
            f==6.25 && g==7.25 && h==8.25 && i==9.25) ? 42.0 : 0.0;
}
long mini_outgoing_mixed(void) {
    return gnu_mix11(1,2.5,3,4,5,6,7,8,9,10,11.5);
}
SRC
cat >"$tmp/driver.c" <<'SRC'
extern long mini_outgoing_gp(void);
extern double mini_outgoing_fp(void);
extern double mini_incoming_fp9(double,double,double,double,double,double,double,double,double);
extern long mini_outgoing_mixed(void);
long gnu_gp10(long a,long b,long c,long d,long e,long f,long g,long h,long i,long j) {
    return a==1 && b==2 && c==3 && d==4 && e==5 && f==6 &&
           g==7 && h==8 && i==9 && j==10 ? 42 : 0;
}
double gnu_fp9(double a,double b,double c,double d,double e,double f,
               double g,double h,double i) {
    return a==1.25 && b==2.25 && c==3.25 && d==4.25 && e==5.25 &&
           f==6.25 && g==7.25 && h==8.25 && i==9.25 ? 42.0 : 0.0;
}
long gnu_mix11(long a,double b,long c,long d,long e,long f,
               long g,long h,long i,long j,double k) {
    return a==1 && b==2.5 && c==3 && d==4 && e==5 && f==6 &&
           g==7 && h==8 && i==9 && j==10 && k==11.5 ? 42 : 0;
}
int main(void) {
    if (mini_outgoing_gp()!=42) return 1;
    if (mini_outgoing_fp()!=42.0) return 2;
    if (mini_incoming_fp9(1.25,2.25,3.25,4.25,5.25,6.25,7.25,8.25,9.25)!=42.0) return 4;
    if (mini_outgoing_mixed()!=42) return 3;
    return 0;
}
SRC
for target in aarch64 loongarch64; do
    case "$target" in
        aarch64) gcc=aarch64-linux-gnu-gcc; qemu=qemu-aarch64 ;;
        loongarch64) gcc=loongarch64-linux-gnu-gcc-13; qemu=qemu-loongarch64 ;;
    esac
    "$gcc" -O0 -static -x c "$tmp/overflow.i" "$tmp/driver.c" \
        -o "$tmp/$target.gnu"
    timeout 45s "$qemu" "$tmp/$target.gnu" >"$tmp/$target.gnu.out" 2>"$tmp/$target.gnu.err"
    "$MINIC_CC" --target="$target-linux" -S "$tmp/overflow.i" \
        -o "$tmp/$target.s"
    "$gcc" -static -x assembler "$tmp/$target.s" -x c "$tmp/driver.c" \
        -o "$tmp/$target.mini"
    timeout 45s "$qemu" "$tmp/$target.mini" >"$tmp/$target.mini.out" 2>"$tmp/$target.mini.err"
    cmp "$tmp/$target.gnu.out" "$tmp/$target.mini.out"
    cmp "$tmp/$target.gnu.err" "$tmp/$target.mini.err"
    echo "FOUR_BACKEND_OUTGOING_ABI=PASS target=$target fixed_gp10=1 fixed_fp9=1 incoming_fp9=1 mixed_gp_fp11=1 GNU_QEMU_differential=1"
done
echo "FOUR_BACKEND_OUTGOING_ABI_ACCEPT=PASS targets=2 independent_GNU_runtime=1"
