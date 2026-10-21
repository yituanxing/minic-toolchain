#!/bin/sh
# Cross-GNU/QEMU oracle: MiniC owns calling convention for outgoing stack
# arguments in 9-16 parameter calls. Do not substitute GNU-built functions.
set -eu
: "${MINIC_CC:?set MINIC_CC to built minic-cc}"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
# An .i input is already preprocessed: no C comments or directives are valid here.
# Boundary checks exercise seven GP registers followed by a two-eightbyte record.
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
struct MiniPair { long left; long right; };
extern long gnu_record_boundary(long,long,long,long,long,long,long,struct MiniPair);
long mini_outgoing_record_boundary(void) {
    struct MiniPair pair;
    pair.left = 23;
    pair.right = 19;
    return gnu_record_boundary(1,2,3,4,5,6,7,pair);
}
long mini_incoming_record_boundary(long a,long b,long c,long d,long e,long f,
                                  long g,struct MiniPair p) {
    return a==1 && b==2 && c==3 && d==4 && e==5 && f==6 && g==7 &&
           p.left==23 && p.right==19 ? 42 : 0;
}
long mini_incoming_float_record(float f,double d,struct MiniPair p) {
    return f==1.25f && d==2.5 && p.left==23 && p.right==19 ? 42 : 0;
}
long mini_incoming_after_record(struct MiniPair p,long marker) {
    return p.left==23 && p.right==19 && marker==7 ? 42 : 0;
}
SRC
cat >"$tmp/driver.c" <<'SRC'
extern long mini_outgoing_gp(void);
extern double mini_outgoing_fp(void);
extern double mini_incoming_fp9(double,double,double,double,double,double,double,double,double);
extern long mini_outgoing_mixed(void);
struct MiniPair { long left; long right; };
extern long mini_outgoing_record_boundary(void);
extern long mini_incoming_record_boundary(long,long,long,long,long,long,long,struct MiniPair);
extern long mini_incoming_float_record(float,double,struct MiniPair);
extern long mini_incoming_after_record(struct MiniPair,long);
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
long gnu_record_boundary(long a,long b,long c,long d,long e,long f,
                         long g,struct MiniPair p) {
    return a==1 && b==2 && c==3 && d==4 && e==5 && f==6 && g==7 &&
           p.left==23 && p.right==19 ? 42 : 0;
}
int main(void) {
    if (mini_outgoing_gp()!=42) return 1;
    if (mini_outgoing_fp()!=42.0) return 2;
    if (mini_incoming_fp9(1.25,2.25,3.25,4.25,5.25,6.25,7.25,8.25,9.25)!=42.0) return 4;
    if (mini_outgoing_mixed()!=42) return 3;
    if (mini_outgoing_record_boundary()!=42) return 5;
    struct MiniPair pair = {23,19};
    if (mini_incoming_record_boundary(1,2,3,4,5,6,7,pair)!=42) return 6;
    if (mini_incoming_float_record(1.25f,2.5,pair)!=42) return 7;
    if (mini_incoming_after_record(pair,7)!=42) return 8;
    return 0;
}
SRC
# LoongArch LP64D: even unsigned 32-bit stack arguments occupy a full
# 8-byte slot and must be sign-extended according to the ABI. The GNU
# assembly reader deliberately loads all 64 bits, so a 4-byte store with
# accidentally-correct low bits cannot pass this test.
cat >"$tmp/u32-stack.i" <<'SRC'
extern long gnu_u32_stack_raw(long,long,long,long,long,long,long,long,unsigned int);
long mini_outgoing_u32_stack(void) {
    return gnu_u32_stack_raw(1,2,3,4,5,6,7,8,0xffffffffU);
}
extern long gnu_s32_stack_raw(long,long,long,long,long,long,long,long,int);
long mini_outgoing_s32_stack(void) {
    return gnu_s32_stack_raw(1,2,3,4,5,6,7,8,-1);
}
SRC
cat >"$tmp/u32-stack-driver.c" <<'SRC'
extern long mini_outgoing_u32_stack(void);
extern long mini_outgoing_s32_stack(void);
__asm__(".text\n"
        ".globl gnu_u32_stack_raw\n"
        ".type gnu_u32_stack_raw,@function\n"
        "gnu_u32_stack_raw:\n"
        "  ld.d $a0, $sp, 0\n"
        "  ret\n"
        ".globl gnu_s32_stack_raw\n"
        ".type gnu_s32_stack_raw,@function\n"
        "gnu_s32_stack_raw:\n"
        "  ld.d $a0, $sp, 0\n"
        "  ret\n");
int main(void) {
    return mini_outgoing_u32_stack() == -1L &&
           mini_outgoing_s32_stack() == -1L ? 0 : 1;
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
    if [ "$target" = loongarch64 ]; then
        "$gcc" -O0 -static -x c "$tmp/u32-stack.i" \
            "$tmp/u32-stack-driver.c" -o "$tmp/u32-stack.gnu"
        timeout 45s "$qemu" "$tmp/u32-stack.gnu"
        "$MINIC_CC" --target="$target-linux" -S "$tmp/u32-stack.i" \
            -o "$tmp/u32-stack.s"
        "$gcc" -static -x assembler "$tmp/u32-stack.s" \
            -x c "$tmp/u32-stack-driver.c" -o "$tmp/u32-stack.mini"
        timeout 45s "$qemu" "$tmp/u32-stack.mini"
        echo "FOUR_BACKEND_OUTGOING_U32_STACK=PASS target=loongarch64 full_8byte_slot=1 unsigned32_sign_extended=1 signed32_sign_extended=1 GNU_QEMU_ABI=1"
    fi
    echo "FOUR_BACKEND_OUTGOING_ABI=PASS target=$target fixed_gp10=1 fixed_fp9=1 incoming_fp9=1 mixed_gp_fp11=1 record2_boundary_split=1 incoming_record_split=1 incoming_float_record=1 incoming_scalar_after_record=1 GNU_QEMU_differential=1"
done
echo "FOUR_BACKEND_OUTGOING_ABI_ACCEPT=PASS targets=2 independent_GNU_runtime=1"
