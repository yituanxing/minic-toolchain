#!/usr/bin/env bash
# Execute the exact same Clang-compiled numerical driver against independent
# upstream Clang-built and actual MiniC-built compiler-rt archives. Cross-ABI
# i128 register-pair and 32/64-bit float transport must round-trip in QEMU.
# Freestanding static ELF: no target sysroot, C runtime or GNU cross compiler.
set -Eeuo pipefail
if (( $# != 3 )); then
    echo "usage: $0 {aarch64|loongarch64} /ref/libminic-rt.a /minic/libminic-rt.a" >&2
    exit 64
fi
target="$1"
reference="$2"
minic="$3"
test -s "$reference"
test -s "$minic"
case "$target" in
    aarch64)
        triple=aarch64-unknown-linux-gnu
        qemu=qemu-aarch64
        clang_flags=()
        ;;
    loongarch64)
        triple=loongarch64-unknown-linux-gnu
        qemu=qemu-loongarch64
        clang_flags=(-mabi=lp64d)
        ;;
    *) echo "unsupported target: $target" >&2; exit 64 ;;
esac
command -v clang >/dev/null
command -v ld.lld >/dev/null
command -v "$qemu" >/dev/null
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat >"$work/check.c" <<'SRC'
typedef __int128 I;
typedef unsigned __int128 U;
typedef unsigned long u64;
union Pair { U value; struct { u64 low,high; } part; };
union F { float f; unsigned bits; };
union D { double f; u64 bits; };
static volatile u64 opaque_zero;
extern void emit_failure(unsigned case_number, unsigned actual_bits);
extern U __multi3(U,U),__udivti3(U,U),__umodti3(U,U);
extern I __divti3(I,I),__modti3(I,I),__ashrti3(I,int);
extern int __cmpti2(I,I),__ucmpti2(U,U);
extern U __ashlti3(U,int),__lshrti3(U,int);
extern float __addsf3(float,float),__subsf3(float,float);
extern float __mulsf3(float,float),__divsf3(float,float);
extern double __adddf3(double,double),__subdf3(double,double);
extern double __muldf3(double,double),__divdf3(double,double);
static U pair(u64 hi,u64 lo) {
    union Pair p; p.part.high=hi; p.part.low=lo; return p.value;
}
static int equal(U v,u64 hi,u64 lo) {
    union Pair p; p.value=v; return p.part.high==hi && p.part.low==lo;
}
static float f(unsigned n) { union F v; v.bits=n; return v.f; }
static double d(u64 n) { union D v; v.bits=n; return v.f; }
static unsigned fb(float n) { union F v; v.f=n; return v.bits; }
static u64 db(double n) { union D v; v.f=n; return v.bits; }
int main(void) {
    U big=pair(1,0), one=pair(0,1);
    I neg=(I)pair(~(u64)0,~(u64)0);
    if (!equal(__multi3(big,one+one),2,0)) return 1;
    if (!equal((U)__divti3(neg*256,7),~(u64)0,(u64)-36)) return 2;
    if (!equal((U)__modti3(neg*256,7),~(u64)0,(u64)-4)) return 3;
    if (!equal(__udivti3(pair(16,0),big),0,16)) return 4;
    if (!equal(__umodti3(pair(16,3),big),0,3)) return 5;
    if (__cmpti2((I)-2,(I)3)!=0 || __cmpti2((I)3,(I)3)!=1 ||
        __cmpti2((I)4,(I)3)!=2) return 6;
    if (__ucmpti2(big,one)!=2 || __ucmpti2(one,big)!=0) return 7;
    if (!equal(__ashlti3(one,65),2,0)) return 8;
    if (!equal(__lshrti3(big,64),0,1)) return 9;
    if (!equal((U)__ashrti3((I)pair(~(u64)0,0),64),
               ~(u64)0,~(u64)0)) return 10;
    if (fb(__addsf3(f(0x3f800000U),f(0x40000000U)))!=0x40400000U) return 11;
    /* Distinguish a negative-operand __addsf3 bug from __subsf3's
     * sign-flip wrapper; report the actual IEEE payload on failure. */
    {
        unsigned got=fb(__addsf3(f(0x40000000U),f(0xbf000000U)));
        if (got!=0x3fc00000U) { emit_failure(21,got); return 21; }
        got=fb(__subsf3(f(0x40000000U),f(0x3f000000U)));
        if (got!=0x3fc00000U) { emit_failure(12,got); return 12; }
    }
    if (fb(__mulsf3(f(0x40000000U),f(0x3f000000U)))!=0x3f800000U) return 13;
    if (fb(__divsf3(f(0x3f800000U),f(0x40000000U)))!=0x3f000000U) return 14;
    if (db(__adddf3(d(0x3ff0000000000000UL),d(0x4000000000000000UL)))
           !=0x4008000000000000UL) return 15;
    if (db(__subdf3(d(0x4000000000000000UL),d(0x3fe0000000000000UL)))
           !=0x3ff8000000000000UL) return 16;
    if (db(__muldf3(d(0x4000000000000000UL),d(0x3fe0000000000000UL)))
           !=0x3ff0000000000000UL) return 17;
    if (db(__divdf3(d(0x3ff0000000000000UL),d(0x4000000000000000UL)))
           !=0x3fe0000000000000UL) return 18;
    if (!equal(__multi3(big,pair(0,2)),2,0)) return 19;
    if (!equal(__udivti3(pair(0,100),pair(0,5)),0,20)) return 20;
    return (int)opaque_zero;
}
SRC
cat >"$work/start.S" <<'SRC'
.text
.globl _start
_start:
#if defined(__aarch64__)
    bl main
    mov x8, #93
    svc #0
.globl emit_failure
emit_failure:
    sub sp, sp, #16
    str w0, [sp]
    str w1, [sp, #4]
    mov x0, #2
    mov x1, sp
    mov x2, #8
    mov x8, #64
    svc #0
    add sp, sp, #16
    ret
#elif defined(__loongarch64)
    bl main
    li.w $a7, 93
    syscall 0
.globl emit_failure
emit_failure:
    addi.d $sp, $sp, -16
    st.w $a0, $sp, 0
    st.w $a1, $sp, 4
    li.w $a0, 2
    move $a1, $sp
    li.w $a2, 8
    li.w $a7, 64
    syscall 0
    addi.d $sp, $sp, 16
    ret
#else
#error unsupported architecture
#endif
SRC
clang --target="$triple" "${clang_flags[@]}" -O1 -fno-builtin -fno-stack-protector \
    -ffreestanding -fno-pie -fno-pic -c "$work/check.c" -o "$work/check.o"
clang --target="$triple" "${clang_flags[@]}" -c "$work/start.S" -o "$work/start.o"
for mode in reference minic; do
    if [[ "$mode" == reference ]]; then archive="$reference"; else archive="$minic"; fi
    clang --target="$triple" "${clang_flags[@]}" -fuse-ld=lld -static -nostdlib \
        -fno-pie -no-pie -Wl,-e,_start \
        "$work/start.o" "$work/check.o" "$archive" -o "$work/$mode.elf"
    set +e
    timeout 45s "$qemu" "$work/$mode.elf" >"$work/$mode.out" 2>"$work/$mode.err"
    result=$?
    set -e
    if (( result != 0 )); then
        echo "LP64_RUNTIME_QEMU=FAIL target=$target mode=$mode case=$result" >&2
        if [[ -s "$work/$mode.err" ]]; then
            echo "LP64_RUNTIME_DIAGNOSTIC=case_number,actual_IEEE754_bits" >&2
            od -An -tx4 "$work/$mode.err" >&2
        fi
        exit 1
    fi
    echo "LP64_RUNTIME_QEMU=PASS target=$target mode=$mode cases=20 type=upstream_runtime"
done
