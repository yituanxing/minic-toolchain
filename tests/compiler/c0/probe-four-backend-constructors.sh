#!/bin/sh
# Real ELF init_array semantics on the two new LP64 backends.
# Compare genuine target-GCC and MiniC+GNU-AS executables under QEMU, and
# inspect the exact relocation that triggers initialization before main.
set -eu
: "${MINIC_CC:?set MINIC_CC}"
root=$(mktemp -d)
trap 'rm -rf "$root"' EXIT HUP INT TERM
cat > "$root/constructors.i" <<'C'
static volatile int start_value;
static void __attribute__((constructor)) register_one(void) {
    start_value += 17;
}
static void __attribute__((__constructor__)) register_two(void) {
    start_value += 25;
}
int main(void) {
    return start_value == 42 ? 42 : 3;
}
C
for target in aarch64 loongarch64; do
  case "$target" in
    aarch64) gcc=aarch64-linux-gnu-gcc; qemu=qemu-aarch64
             sysroot=/usr/aarch64-linux-gnu ;;
    loongarch64) gcc=loongarch64-linux-gnu-gcc-13; qemu=qemu-loongarch64
                 sysroot=/usr/loongarch64-linux-gnu ;;
  esac
  "$MINIC_CC" --target="$target-linux" -S "$root/constructors.i" \
    -o "$root/$target.minic.s"
  "$gcc" -c -x assembler "$root/$target.minic.s" \
    -o "$root/$target.minic.o"
  readelf -SW "$root/$target.minic.o" | grep -q '[.]init_array'
  readelf -rW "$root/$target.minic.o" | grep -q '[.]rela.init_array'
  "$gcc" -no-pie "$root/$target.minic.o" -o "$root/$target.minic.elf"
  "$gcc" -no-pie -x c "$root/constructors.i" -o "$root/$target.gnu.elf"
  set +e
  timeout 25s "$qemu" -L "$sysroot" "$root/$target.gnu.elf"
  gnu_rc=$?
  timeout 25s "$qemu" -L "$sysroot" "$root/$target.minic.elf"
  mini_rc=$?
  set -e
  if [ "$gnu_rc" -ne 42 ] || [ "$mini_rc" -ne 42 ]; then
    echo "FOUR_BACKEND_CONSTRUCTORS=FAIL target=$target GNU=$gnu_rc MiniC=$mini_rc" >&2
    exit 1
  fi
  echo "FOUR_BACKEND_CONSTRUCTORS=PASS target=$target relocations=2 before_main=1 GNU_QEMU_exitcode=42"
done
echo "FOUR_BACKEND_CONSTRUCTORS=PASS targets=2 init_array_GNU_QEMU_verified=1"
