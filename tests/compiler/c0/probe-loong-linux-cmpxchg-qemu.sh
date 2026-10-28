#!/usr/bin/env bash
# Original Linux 6.6.143 LoongArch cmpxchg.h hardware LL/SC GNU oracle.
# Includes exact 32/64 and 8/16-bit masked-replacement original templates.
set -Eeuo pipefail
minic="$1";test -x "$minic"
d=$(mktemp -d);trap 'rm -rf "$d"' EXIT
cat > "$d/original.i" <<'SRC'
typedef unsigned int u32;
typedef unsigned long long u64;
static volatile u32 value32=5;
static volatile u64 value64=7;
static volatile u32 valueSmall=0x12345678;
u32 linux_cmpxchg32(u32 old,u32 newval) {
    u32 ret;
    volatile u32 *ptr=&value32;
    __asm__ __volatile__(
       "1:\t" "ll.w" "\t%0, %2\t\t# __cmpxchg_asm \n"
       "\tbne\t%0, %z3, 2f\t\t\t\n"
       "\tmove\t$t0, %z4\t\t\t\n"
       "\t" "sc.w" "\t$t0, %1\t\t\t\t\n"
       "\tbeqz\t$t0, 1b\t\t\t\t\n"
       "2:\t\t\t\t\t\t\n"
       "\tdbar 0x700\t\n"
       : "=&r" (ret), "=ZB" (*ptr)
       : "ZB" (*ptr), "Jr" (old), "Jr" (newval)
       : "t0", "memory");
    return ret;
}
u64 linux_cmpxchg64(u64 old,u64 newval) {
    u64 ret;
    volatile u64 *ptr=&value64;
    __asm__ __volatile__(
       "1:\t" "ll.d" "\t%0, %2\t\t# __cmpxchg_asm \n"
       "\tbne\t%0, %z3, 2f\t\t\t\n"
       "\tmove\t$t0, %z4\t\t\t\n"
       "\t" "sc.d" "\t$t0, %1\t\t\t\t\n"
       "\tbeqz\t$t0, 1b\t\t\t\t\n"
       "2:\t\t\t\t\t\t\n"
       "\tdbar 0x700\t\n"
       : "=&r" (ret), "=ZB" (*ptr)
       : "ZB" (*ptr), "Jr" (old), "Jr" (newval)
       : "t0", "memory");
    return ret;
}
u32 linux_cmpxchg_small(u32 old, u32 replacement) {
    u32 old32, temp, mask=0xffU;
    volatile u32 *ptr32=&valueSmall;
    asm volatile (
      "1:\tll.w\t\t%0, %3\t\t\n"
      "\tand\t\t%1, %0, %z4\t\n"
      "\tbne\t\t%1, %z5, 2f\t\n"
      "\tandn\t\t%1, %0, %z4\t\n"
      "\tor\t\t%1, %1, %z6\t\n"
      "\tsc.w\t\t%1, %2\t\t\n"
      "\tbeqz\t\t%1, 1b\t\t\n"
      "\tb\t\t3f\t\t\n"
      "2:\t\t\t\t\n"
      "\tdbar 0x700\t\n"
      "3:\t\t\t\t\n"
      : "=&r" (old32), "=&r" (temp), "=ZC" (*ptr32)
      : "ZC" (*ptr32), "Jr" (mask), "Jr" (old), "Jr" (replacement)
      : "memory");
    return old32 & mask;
}
int main(void) {
    if (linux_cmpxchg32(4,8)!=5||value32!=5) return 1;
    if (linux_cmpxchg32(5,8)!=5||value32!=8) return 2;
    if (linux_cmpxchg64(6,9)!=7||value64!=7) return 3;
    if (linux_cmpxchg64(7,9)!=7||value64!=9) return 4;
    if (linux_cmpxchg_small(0x77,0x9a)!=0x78||valueSmall!=0x12345678) return 5;
    if (linux_cmpxchg_small(0x78,0x9a)!=0x78||valueSmall!=0x1234569a) return 6;
    return 42;
}
SRC
"$minic" --target=loongarch64-linux -S "$d/original.i" -o "$d/minic.s"
test -s "$d/minic.s"
for opcode in 'll.w' 'll.d' 'sc.w' 'sc.d' 'andn' 'dbar 0x700'; do
  grep -Fq "$opcode" "$d/minic.s"
done
if [[ "${MINIC_SOURCE_ONLY:-0}" != 1 ]];then
  gcc=loongarch64-linux-gnu-gcc-13
  command -v "$gcc" >/dev/null
  command -v qemu-loongarch64 >/dev/null
  "$gcc" -static -x assembler "$d/minic.s" -o "$d/minic.elf"
  "$gcc" -static -x c "$d/original.i" -o "$d/gnu.elf"
  for provider in minic gnu;do
    code=0;timeout 45s qemu-loongarch64 "$d/$provider.elf" || code=$?
    if [[ "$code" != 42 ]];then
      echo "ORIGINAL_LOONG_CMPXCHG failed $provider code=$code" >&2;exit 1
    fi
  done
fi
for bad in bad-op bad-barrier;do
 if [[ "$bad" == bad-op ]];then
   sed 's/ll.w/ld.w/g' "$d/original.i" > "$d/$bad.i"
 else
   sed 's/dbar 0x700/dbar 0x701/g' "$d/original.i" > "$d/$bad.i"
 fi
 if "$minic" --target=loongarch64-linux -S "$d/$bad.i" -o "$d/$bad.s" >/dev/null 2>&1;then
   echo "unqualified LoongArch LL/SC source accepted $bad" >&2;exit 1
 fi
 test ! -e "$d/$bad.s"
done
echo 'ORIGINAL_LOONG_LLSC_CMPXCHG=PASS forms=3 GNU_QEMU=2 failure_and_success_paths=1 negatives=2'
