#!/usr/bin/env bash
# GNU GCC <-> MiniC bidirectional packed integer record LP64 ABI.
# LoongArch lib/vsprintf.i defines exactly this 8-byte packed bitfield layout.
set -Eeuo pipefail
target="$1";mini="$2";test -x "$mini"
case "$target" in
 aarch64) gnu=aarch64-linux-gnu-gcc; qemu=qemu-aarch64 ;;
 loongarch64) gnu=loongarch64-linux-gnu-gcc-13; qemu=qemu-loongarch64 ;;
 *) exit 2 ;;
esac
d=$(mktemp -d);trap 'rm -rf "$d"' EXIT
cat > "$d/mini.i" <<'SRC'
typedef unsigned long U;
struct PrintSpec {unsigned int type:8; signed int width:24; unsigned int flags:8; unsigned int base:8; signed int precision:16;} __attribute__((packed));
struct Packed13 {unsigned char tag; unsigned char bytes[12];} __attribute__((packed));
_Static_assert(sizeof(struct PrintSpec)==8,"8");
_Static_assert(sizeof(struct Packed13)==13,"13");
extern U gnu_value(struct PrintSpec);
extern U gnu_value13(struct Packed13);
extern U gnu_eight(U,U,U,U,U,U,U,U,struct Packed13);
extern int gnu_probe(void);
U mini_value(struct PrintSpec p) { return (U)p.type+(U)(p.width+100)+(U)p.flags+(U)p.base+(U)(p.precision+200); }
U mini_value13(struct Packed13 p) { return (U)p.tag+(U)p.bytes[0]+(U)p.bytes[11]; }
U mini_seven(U a,U b,U c,U d,U e,U f,U g,struct PrintSpec p) { return a+b+c+d+e+f+g+mini_value(p); }
int main(void) {
 struct PrintSpec p={.type=6,.width=-7,.flags=3,.base=10,.precision=-13};
 struct Packed13 q={.tag=9,.bytes={8,1,2,3,4,5,6,7,8,9,10,13}};
 if(mini_value(p)!=299 || mini_value13(q)!=30)return 1;
 if(gnu_value(p)!=299 || gnu_value13(q)!=30)return 2;
 if(mini_seven(1,2,3,4,5,6,7,p)!=327)return 3;
 if(gnu_eight(1,2,3,4,5,6,7,8,q)!=66)return 4;
 if(gnu_probe()!=42)return 5;
 return 42;
}
SRC
cat > "$d/gnu.c" <<'SRC'
typedef unsigned long U;
struct PrintSpec {unsigned int type:8; signed int width:24; unsigned int flags:8; unsigned int base:8; signed int precision:16;} __attribute__((packed));
struct Packed13 {unsigned char tag; unsigned char bytes[12];} __attribute__((packed));
extern U mini_value(struct PrintSpec);
extern U mini_value13(struct Packed13);
extern U mini_seven(U,U,U,U,U,U,U,struct PrintSpec);
U gnu_value(struct PrintSpec p) {return (U)p.type+(U)(p.width+100)+(U)p.flags+(U)p.base+(U)(p.precision+200);}
U gnu_value13(struct Packed13 p) {return (U)p.tag+(U)p.bytes[0]+(U)p.bytes[11];}
U gnu_eight(U a,U b,U c,U d,U e,U f,U g,U h,struct Packed13 p){return a+b+c+d+e+f+g+h+gnu_value13(p);}
int gnu_probe(void) {
 struct PrintSpec p={.type=6,.width=-7,.flags=3,.base=10,.precision=-13};
 struct Packed13 q={.tag=9,.bytes={8,1,2,3,4,5,6,7,8,9,10,13}};
 if(mini_value(p)!=299 || mini_value13(q)!=30)return 1;
 if(mini_seven(1,2,3,4,5,6,7,p)!=327)return 2;
 return 42;
}
SRC
"$mini" --target="$target-linux" -S "$d/mini.i" -o "$d/mini.s"
for fun in mini_value mini_value13 mini_seven main;do
 grep -Fq "$fun:" "$d/mini.s"
done
cat > "$d/unsafe.i" <<'SRC'
struct Unsafe {unsigned char prefix; unsigned long unaligned;} __attribute__((packed));
struct Unsafe value(struct Unsafe x){return x;}
SRC
if "$mini" --target="$target-linux" -S "$d/unsafe.i" -o "$d/unsafe.s" >/dev/null 2>&1;then
 echo 'Unaligned native LP64 integer member falsely classified GP-safe' >&2;exit 1
fi
cat > "$d/hfa.i" <<'SRC'
struct FP {float x;} __attribute__((packed));
struct FP value(struct FP x){return x;}
SRC
if "$mini" --target="$target-linux" -S "$d/hfa.i" -o "$d/hfa.s" >/dev/null 2>&1;then
 echo 'Packed float HFA falsely classified INTEGER' >&2;exit 1
fi
if [[ "${MINIC_SOURCE_ONLY:-0}" == 1 ]];then
 echo "LP64_PACKED_INTEGER_RECORD=PASS target=$target source=1 rejects=2"
 exit 0
fi
"$gnu" -static -O0 -c "$d/gnu.c" -o "$d/gnu.o"
"$gnu" -static -x assembler -c "$d/mini.s" -o "$d/mini.o"
"$gnu" -static "$d/mini.o" "$d/gnu.o" -o "$d/mixed.elf"
code=0; timeout 55s "$qemu" "$d/mixed.elf" || code=$?
if [[ "$code" != 42 ]];then
 echo "Mixed GNU<->MiniC packed ABI target=$target returned $code" >&2;exit 1
fi
echo "LP64_PACKED_INTEGER_RECORD=PASS target=$target GNU_to_MiniC=42 MiniC_to_GNU=42 packed_bitfields=8bytes raw_13byte=2GPR seventh_reg=1 eighth_stack=1 rejects=2"
