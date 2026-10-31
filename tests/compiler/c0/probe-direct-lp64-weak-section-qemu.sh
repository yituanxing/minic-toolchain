#!/usr/bin/env bash
set -Eeuo pipefail
target=$1;mini=$2
case "$target" in
 aarch64) cross=aarch64-linux-gnu-gcc; qemu=qemu-aarch64 ;;
 loongarch64) cross=loongarch64-linux-gnu-gcc-13; qemu=qemu-loongarch64 ;;
 *) exit 2 ;;
esac
d=$(mktemp -d);trap 'rm -rf "$d"' EXIT
cat >"$d/weak.i" <<'SRC'
__attribute__((weak,section(".text.minic_custom")))
int portable_entry(int x){return x*3+7;}
__attribute__((section(".text.minic_sectioned")))
int portable_sectioned(int x){return x+111;}
__attribute__((weak))
int portable_hook(int x){return x+49;}
int portable_strong(int x){return portable_entry(x)+portable_sectioned(x)+portable_hook(x);}
SRC
"$mini" --target="$target-linux" -ffunction-sections -S "$d/weak.i" -o "$d/mini.s"
"$cross" -c -x assembler "$d/mini.s" -o "$d/mini.o"
"$cross" -O0 -ffunction-sections -c -x c "$d/weak.i" -o "$d/gnu.o"
for section in .text.minic_custom .text.minic_sectioned .text.portable_hook;do
 readelf -SW "$d/mini.o" | grep -Fq "$section"
 readelf -SW "$d/gnu.o" | grep -Fq "$section"
done
for sym in portable_entry portable_hook;do
 readelf -sW "$d/mini.o" | grep -E "[[:space:]]WEAK[[:space:]].*[[:space:]]$sym$" >/dev/null
 readelf -sW "$d/gnu.o" | grep -E "[[:space:]]WEAK[[:space:]].*[[:space:]]$sym$" >/dev/null
done
cat >"$d/main.c" <<'SRC'
extern int portable_strong(int);
int main(void){return portable_strong(13)==(13*3+7)+(13+111)+(13+49)?42:1;}
SRC
"$cross" -static -O0 "$d/main.c" "$d/mini.o" -o "$d/mini.elf"
"$cross" -static -O0 "$d/main.c" "$d/gnu.o" -o "$d/gnu.elf"
result=0;"$qemu" "$d/mini.elf" || result=$?
test "$result" -eq 42
result=0;"$qemu" "$d/gnu.elf" || result=$?
test "$result" -eq 42
echo "MINIC_LP64_WEAK_SECTION_GNU_QEMU=PASS target=$target weak=2 custom_sections=2"
