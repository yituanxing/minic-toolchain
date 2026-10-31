#!/usr/bin/env bash
# GNU psABI oracle: non-HFA records > 2xGRLEN travel by reference.
set -Eeuo pipefail
target="$1"
minic="$2"
test -x "$minic"
case "$target" in
  aarch64) gnu=aarch64-linux-gnu-gcc; emu=qemu-aarch64 ;;
  loongarch64) gnu=loongarch64-linux-gnu-gcc-13; emu=qemu-loongarch64 ;;
  *) echo "unknown LP64 target $target" >&2;exit 2 ;;
esac
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
cat > "$d/records.i" <<'SRC'
typedef unsigned long u64;
typedef struct { unsigned char a[17]; } R17;
typedef struct { u64 a,b,c; } R24;
typedef struct { u64 a,b,c,d; } R32;
typedef struct { u64 a,b,c,d,e; } R40;
__attribute__((noinline)) static u64 f17(R17 p) {
  p.a[0]=99; return p.a[1]+p.a[16];
}
__attribute__((noinline)) static u64 f24(const R24 p) {
  return p.b+p.c;
}
__attribute__((noinline)) static u64 f32(
    u64 a,u64 b,u64 c,u64 d,u64 e,u64 f,u64 g,u64 h,R32 p) {
  p.a=99; return a+b+c+d+e+f+g+h+p.b+p.c+p.d;
}
__attribute__((noinline)) static u64 f40(R40 p) {
  p.e=99; return p.a+p.b+p.c+p.d;
}
int main(void) {
  R17 a={{1,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,3}};
  R24 b={3,4,5}; R32 c={7,8,9,10};
  R40 d={11,12,13,14,15};
  if(f17(a)!=5||a.a[0]!=1)return 1;
  if(f24(b)!=9||b.a!=3)return 2;
  if(f32(1,2,3,4,5,6,7,8,c)!=63||c.a!=7)return 3;
  if(f40(d)!=50||d.e!=15)return 4;
  return 42;
}
SRC
"$minic" --target="$target-linux" -S "$d/records.i" -o "$d/minic.s"
test -s "$d/minic.s"
if [[ "$(printenv MINIC_SOURCE_ONLY || true)" == 1 ]];then
  echo "LP64_INDIRECT_RECORD=$target MiniC_SOURCE=PASS"
  exit 0
fi
"$gnu" -static -O0 -x c "$d/records.i" -o "$d/gnu.elf"
"$gnu" -static -x assembler "$d/minic.s" -o "$d/minic.elf"
for compiler in gnu minic;do
  result=0
  timeout 45s "$emu" "$d/$compiler.elf" || result=$?
  if [[ "$result" != 42 ]];then
    echo "incorrect $target $compiler non-HFA by-reference ABI: exit=$result" >&2
    exit 1
  fi
done
echo "LP64_INDIRECT_RECORD=$target GNU_GAS_LINK_QEMU=PASS sizes=17,24,32,40 mixed_GP_stack=1"
