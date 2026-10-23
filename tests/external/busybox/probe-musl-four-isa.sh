#!/usr/bin/env bash
# Original pinned BusyBox source and independent four-ISA musl static ABI gate.
set -Eeuo pipefail
target="$1"; cc="$2"; prefix="$3"; qemu="$4"
: "${MINIC_CC:?set MINIC_CC}"
repo=$(cd "$(dirname "$0")/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
case "$target" in
 x86_64) incl=/usr/include/x86_64-linux-gnu; triple=x86_64-linux-musl ;;
 riscv64) incl=/usr/riscv64-linux-gnu/include; triple=riscv64-linux-musl ;;
 aarch64) incl=/usr/aarch64-linux-gnu/include; triple=aarch64-linux-musl ;;
 loongarch64) incl=/usr/loongarch64-linux-gnu/include; triple=loongarch64-linux-musl ;;
 *) echo "MUSL_MATRIX=FAIL invalid_target=$target" >&2; exit 2 ;;
esac
mkdir -p "$work/build" "$work/sysroot/usr/include" "$work/busybox"
curl -fsSL --retry 4 --retry-delay 2 --connect-timeout 15 \
  https://musl.libc.org/releases/musl-1.2.5.tar.gz -o "$work/musl.tgz"
printf '%s  %s\n' \
  a9a118bbe84d8764da0ea0d28b3ab3fae8477fc7e4085d90102b8596fc7c75e4 \
  "$work/musl.tgz" | sha256sum -c --status
tar -xzf "$work/musl.tgz" -C "$work"
for d in linux asm asm-generic mtd; do
  if test -d "$incl/$d"; then cp -a "$incl/$d" "$work/sysroot/usr/include/"; fi
done
if test "$target" = x86_64; then
  for d in linux asm-generic mtd; do cp -a "/usr/include/$d" "$work/sysroot/usr/include/"; done
fi
(
  cd "$work/build"
  CC="$cc" AR="${prefix}ar" RANLIB="${prefix}ranlib" CFLAGS="-O2 -fno-pic -fno-pie" \
    "$work/musl-1.2.5/configure" --target="$triple" --disable-shared --prefix=/usr \
    >"$work/configure.log" 2>&1 || {
      echo "MUSL_MATRIX=FAIL target=$target stage=configure" >&2
      tail -n 35 "$work/configure.log" >&2
      exit 1
    }
  make -j4 lib/libc.a obj/crt/crt1.o obj/crt/crti.o obj/crt/crtn.o \
    >"$work/build.log" 2>&1 || {
      echo "MUSL_MATRIX=FAIL target=$target stage=build" >&2
      tail -n 35 "$work/build.log" >&2
      exit 1
    }
  make DESTDIR="$work/sysroot" install-headers >"$work/headers.log" 2>&1
)
test -s "$work/build/lib/libc.a"
test -s "$work/sysroot/usr/include/stdio.h"
builtin=$("$cc" -print-file-name=include)
libgcc=$("$cc" -print-libgcc-file-name)
headers=(-nostdinc -isystem "$work/sysroot/usr/include" -isystem "$builtin" \
  -I"$repo/tests/linker/fixtures/busybox-uapi")
cat >"$work/consumer.c" <<'SRC'
#include <stdio.h>
#include <stdlib.h>
int main(void) {
  puts("pinned-musl-1.2.5");
  return sizeof(size_t) == 8 ? 42 : 1;
}
SRC
"$cc" "${headers[@]}" -O0 -E -P "$work/consumer.c" -o "$work/consumer.i"
"$cc" "${headers[@]}" -O0 -fno-pic -fno-pie -c "$work/consumer.c" -o "$work/gcc.o"
"$MINIC_CC" "--target=$target-linux" -S "$work/consumer.i" -o "$work/mini.s"
"$cc" -c -x assembler "$work/mini.s" -o "$work/mini.o"
for impl in gcc mini; do
  "$cc" -nostdlib -static -no-pie -Wl,-e,_start \
    "$work/build/obj/crt/crt1.o" "$work/build/obj/crt/crti.o" "$work/$impl.o" \
    -Wl,--start-group "$work/build/lib/libc.a" "$libgcc" -Wl,--end-group \
    "$work/build/obj/crt/crtn.o" -o "$work/$impl"
done
set +e
"$qemu" "$work/gcc" >"$work/gcc.out" 2>"$work/gcc.err"; gcc_rc=$?
"$qemu" "$work/mini" >"$work/mini.out" 2>"$work/mini.err"; mini_rc=$?
set -e
if test "$gcc_rc" != 42 || test "$mini_rc" != 42 ||
   ! cmp -s "$work/gcc.out" "$work/mini.out" ||
   ! cmp -s "$work/gcc.err" "$work/mini.err"; then
  echo "MUSL_MATRIX=FAIL target=$target stage=GNU_MiniC_QEMU gcc=$gcc_rc minic=$mini_rc" >&2
  head -c 1200 "$work/gcc.err" "$work/mini.err" >&2 || true
  exit 1
fi
echo "MUSL_MATRIX_ABI=PASS target=$target libc=musl-1.2.5 CRT=musl static=1 GCC_MiniC_QEMU_byte_exact=1"
commit=1a64f6a20aaf6ea4dbba68bbfa8cc1ab7e5c57c4
curl -fsSL --retry 4 --retry-delay 2 --connect-timeout 15 \
  "https://github.com/mirror/busybox/archive/$commit.tar.gz" \
  -o "$work/busybox.tgz"
tar -xzf "$work/busybox.tgz" --strip-components=1 -C "$work/busybox"
sh "$repo/tests/external/busybox/check-pinned-1361-source.sh" "$work/busybox"
echo "MUSL_MATRIX_SOURCE=PASS target=$target version=BusyBox-1.36.1 commit=$commit"
