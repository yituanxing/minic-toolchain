#!/usr/bin/env bash
# Validate the actual MiniC-built compiler-rt in the host SysV AMD64 ABI.
# Independent driver is compiled by GCC; no LLVM/GNU replacement for .o.
set -Eeuo pipefail
if (( $# != 1 )); then echo "usage: $0 /path/to/libminic-rt.a" >&2; exit 64; fi
archive="$1"
test -s "$archive"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat >"$work/check.c" <<'SRC'
typedef __int128 I;
typedef unsigned __int128 U;
typedef unsigned long u64;
union Pair { U value; struct { u64 low, high; } pair; };
extern U __multi3(U,U);
extern I __divti3(I,I),__modti3(I,I);
extern U __udivti3(U,U),__umodti3(U,U);
extern int __cmpti2(I,I),__ucmpti2(U,U);
extern U __ashlti3(U,int),__lshrti3(U,int);
extern I __ashrti3(I,int);
extern float __addsf3(float,float),__subsf3(float,float);
extern float __mulsf3(float,float),__divsf3(float,float);
extern double __adddf3(double,double),__subdf3(double,double);
extern double __muldf3(double,double),__divdf3(double,double);
static volatile u64 zero;
static U pair(u64 high,u64 low) {
  union Pair v; v.pair.high=high; v.pair.low=low; return v.value;
}
static int equal(U value,u64 high,u64 low) {
  union Pair v; v.value=value;
  return v.pair.high==high && v.pair.low==low;
}
union F { float f; unsigned u; };
union D { double f; u64 u; };
static float f(unsigned x){ union F v; v.u=x; return v.f; }
static double d(u64 x){ union D v; v.u=x; return v.f; }
static unsigned fb(float x){ union F v; v.f=x;return v.u; }
static u64 db(double x){ union D v; v.f=x;return v.u; }
int main(void) {
 U big=pair(1,0);
 U one=pair(0,1);
 I minus=(I)pair(~(u64)0,~(u64)0);
 if (!equal(__multi3(big,one+one),2,0)) return 1;
 if (!equal((U)__divti3(minus*256,7),~(u64)0,(u64)-36)) return 2;
 if (!equal((U)__modti3(minus*256,7),~(u64)0,(u64)-4)) return 3;
 if (!equal(__udivti3(pair(16,0),big),0,16)) return 4;
 if (!equal(__umodti3(pair(16,3),big),0,3)) return 5;
 if (__cmpti2((I)-2,(I)3)!=0 || __cmpti2((I)3,(I)3)!=1 ||
     __cmpti2((I)4,(I)3)!=2) return 6;
 if (__ucmpti2(big,one)!=2 || __ucmpti2(one,big)!=0) return 7;
 if (!equal(__ashlti3(one,65),2,0)) return 8;
 if (!equal(__lshrti3(big,64),0,1)) return 9;
 if (!equal((U)__ashrti3((I)pair(~(u64)0,0),64),~(u64)0,~(u64)0)) return 10;
 if (fb(__addsf3(f(0x3f800000U),f(0x40000000U))) != 0x40400000U) return 11;
 if (fb(__subsf3(f(0x40000000U),f(0x3f000000U))) != 0x3fc00000U) return 12;
 if (fb(__mulsf3(f(0x40000000U),f(0x3f000000U))) != 0x3f800000U) return 13;
 if (fb(__divsf3(f(0x3f800000U),f(0x40000000U))) != 0x3f000000U) return 14;
 if (db(__adddf3(d(0x3ff0000000000000UL),d(0x4000000000000000UL))) != 0x4008000000000000UL) return 15;
 if (db(__subdf3(d(0x4000000000000000UL),d(0x3fe0000000000000UL))) != 0x3ff8000000000000UL) return 16;
 if (db(__muldf3(d(0x4000000000000000UL),d(0x3fe0000000000000UL))) != 0x3ff0000000000000UL) return 17;
 if (db(__divdf3(d(0x3ff0000000000000UL),d(0x4000000000000000UL))) != 0x3fe0000000000000UL) return 18;
 if (!equal(__multi3(big,pair(0,2)),2,0)) return 19;
 if (!equal(__udivti3(pair(0,100),pair(0,5)),0,20)) return 20;
 return (int)zero;
}
SRC
cc -O1 -fno-builtin -fno-lto -no-pie "$work/check.c" "$archive" -o "$work/check"
set +e
"$work/check"
rc=$?
set -e
if (( rc != 0 )); then
  echo "X86_RUNTIME_NATIVE=FAIL case=$rc" >&2
  exit 1
fi
echo "X86_RUNTIME_NATIVE=PASS cases=20 upstream_algorithms=1 minic_built_archive=1"
