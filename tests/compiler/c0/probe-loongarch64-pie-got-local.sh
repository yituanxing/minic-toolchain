#!/bin/sh
# A native CONFIG_RELOCATABLE LoongArch Linux kernel emits -fPIE. Verify that
# its TU-local symbols avoid GOT indirection and preemptible symbols use GOT.
# The shape gate runs without cross tools; GNU AS/LD/QEMU comparison is added
# when the cross compiler and emulator are available.
set -eu
: "${MINIC_CC:?point MINIC_CC at the built compiler}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
cat >"$work/pic.i" <<'SRC'
extern int external_value;
extern int external_add(int);
static int local_value = 17;
static int local_add(int x) { return x + 3; }
int *get_local_object(void) { return &local_value; }
int *get_external_object(void) { return &external_value; }
int (*get_local_function(void))(int) { return &local_add; }
int (*get_external_function(void))(int) { return &external_add; }
int loong_pie_entry(int x) {
    return *get_local_object() + *get_external_object() +
           get_local_function()(x) + get_external_function()(x);
}
SRC
for flag in -fPIE -fPIC; do
  "$MINIC_CC" --target=loongarch64-linux "$flag" -S \
    "$work/pic.i" -o "$work/minic.s"
  grep -Fq 'la.local $t0, local_value' "$work/minic.s"
  grep -Fq 'la.local $t0, local_add' "$work/minic.s"
  grep -Fq 'la.global $t0, external_value' "$work/minic.s"
  grep -Fq 'la.global $t0, external_add' "$work/minic.s"
  cp "$work/minic.s" "$work/${flag#-}.s"
done
cmp "$work/fPIE.s" "$work/fPIC.s"
echo 'MINIC_LOONGARCH64_PIE_SHAPE=PASS local_pcrel=2 external_got=2 PIE=1 PIC=1'

cross=${LOONGARCH64_CC:-loongarch64-linux-gnu-gcc-13}
qemu=${QEMU_LOONGARCH64:-qemu-loongarch64}
if ! command -v "$cross" >/dev/null 2>&1 ||
   ! command -v "$qemu" >/dev/null 2>&1; then
  echo 'MINIC_LOONGARCH64_PIE_RUNTIME=SKIP missing GNU cross compiler/QEMU'
  exit 0
fi
cat >"$work/main.c" <<'SRC'
extern int loong_pie_entry(int);
int external_value = 19;
int external_add(int x) { return x + 7; }
int main(void) { return loong_pie_entry(5) == (17+19+8+12) ? 0 : 3; }
SRC
"$cross" -c -fPIE -x c "$work/pic.i" -o "$work/gnu.o"
"$cross" -c -fPIE "$work/main.c" -o "$work/main.o"
"$cross" -c -x assembler "$work/fPIE.s" -o "$work/minic.o"
for provider in gnu minic; do
  "$cross" -fPIE -pie "$work/$provider.o" "$work/main.o" \
    -o "$work/$provider.elf"
  timeout 45s "$qemu" -L /usr/loongarch64-linux-gnu \
    "$work/$provider.elf" >"$work/$provider.out" 2>"$work/$provider.err"
done
cmp "$work/gnu.out" "$work/minic.out"
cmp "$work/gnu.err" "$work/minic.err"
echo 'MINIC_LOONGARCH64_PIE_RUNTIME=PASS GNU_AS=1 GNU_LD=1 QEMU_GNU_diff=1'
