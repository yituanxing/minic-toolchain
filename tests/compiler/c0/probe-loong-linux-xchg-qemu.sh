#!/usr/bin/env bash
set -Eeuo pipefail
minic="$1";test -x "$minic"
d=$(mktemp -d);trap 'rm -rf "$d"' EXIT
cat > "$d/xchg.i" <<'SRC'
typedef unsigned int u32;
typedef unsigned long long u64;
static volatile u32 state32=5;
static volatile u64 state64=7;
u32 linux_xchg32(u32 v) {
  volatile u32 *ptr=&state32;
  u32 old;
  __asm__ __volatile__(
      "amswap_db.w %1, %z2, %0\n"
      : "+ZB" (*ptr), "=&r" (old) : "Jr" (v) : "memory");
  return old;
}
u64 linux_xchg64(u64 v) {
  volatile u64 *ptr=&state64;
  u64 old;
  __asm__ __volatile__(
      "amswap_db.d %1, %z2, %0\n"
      : "+ZB" (*ptr), "=&r" (old) : "Jr" (v) : "memory");
  return old;
}
int main(void){
  if(linux_xchg32(8)!=5 || state32!=8)return 1;
  if(linux_xchg64(9)!=7 || state64!=9)return 2;
  if(linux_xchg32(17)!=8 || state32!=17)return 3;
  if(linux_xchg64(23)!=9 || state64!=23)return 4;
  return 42;
}
SRC
"$minic" --target=loongarch64-linux -S "$d/xchg.i" -o "$d/mini.s"
test -s "$d/mini.s"
grep -Fq 'amswap_db.w' "$d/mini.s"
grep -Fq 'amswap_db.d' "$d/mini.s"
if [[ "${MINIC_SOURCE_ONLY:-0}" != 1 ]];then
    gcc=loongarch64-linux-gnu-gcc-13
    "$gcc" -static -O0 -x c "$d/xchg.i" -o "$d/gnu.elf"
    "$gcc" -static -x assembler "$d/mini.s" -o "$d/mini.elf"
    for compiler in gnu mini;do
      code=0
      timeout 45s qemu-loongarch64 "$d/$compiler.elf" || code=$?
      if [[ "$code" != 42 ]];then
        echo "LoongArch Linux amswap $compiler code=$code" >&2;exit 1
      fi
    done
fi
sed 's/amswap_db.w/amxor_db.w/' "$d/xchg.i" > "$d/unsafe-op.i"
sed 's/"+ZB"/"+m"/' "$d/xchg.i" > "$d/unsafe-constraint.i"
for bad in unsafe-op unsafe-constraint;do
  if "$minic" --target=loongarch64-linux -S "$d/$bad.i" \
      -o "$d/$bad.s" >/dev/null 2>&1;then
    echo "unsafe LoongArch Linux xchg contract accepted $bad" >&2;exit 1
  fi
  test ! -e "$d/$bad.s"
done
echo 'LOONG_LINUX_AMSWAP_DB_GNU=PASS widths=32/64 GNU_QEMU_42=2 original_volatile_memory=1 negatives=2'
